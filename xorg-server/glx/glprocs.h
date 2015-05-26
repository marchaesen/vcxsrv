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
    "glMultiDrawArraysIndirect\0"
    "glMultiDrawElementsIndirect\0"
    "glGetProgramInterfaceiv\0"
    "glGetProgramResourceIndex\0"
    "glGetProgramResourceLocation\0"
    "glGetProgramResourceLocationIndex\0"
    "glGetProgramResourceName\0"
    "glGetProgramResourceiv\0"
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
    "glUnmapNamedBuffer\0"
    "glVertexArrayAttribBinding\0"
    "glVertexArrayAttribFormat\0"
    "glVertexArrayAttribIFormat\0"
    "glVertexArrayAttribLFormat\0"
    "glVertexArrayBindingDivisor\0"
    "glVertexArrayElementBuffer\0"
    "glVertexArrayVertexBuffer\0"
    "glVertexArrayVertexBuffers\0"
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
    "glBindFramebufferEXT\0"
    "glBindRenderbufferEXT\0"
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
    "glMinSampleShadingARB\0"
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
    "glBlendEquationSeparatei\0"
    "glBlendEquationIndexedAMD\0"
    "glBlendEquationi\0"
    "glBlendFuncSeparateIndexedAMD\0"
    "glBlendFuncSeparatei\0"
    "glBlendFuncIndexedAMD\0"
    "glBlendFunci\0"
    "glGetQueryObjecti64vEXT\0"
    "glGetQueryObjectui64vEXT\0"
    "glClearDepthfOES\0"
    "glDepthRangefOES\0"
    "glGetProgramBinaryOES\0"
    "glProgramBinaryOES\0"
    "glProgramParameteriARB\0"
    "glProgramParameteriEXT\0"
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
    "glProgramUniform1fEXT\0"
    "glProgramUniform1fvEXT\0"
    "glProgramUniform1iEXT\0"
    "glProgramUniform1ivEXT\0"
    "glProgramUniform1uiEXT\0"
    "glProgramUniform1uivEXT\0"
    "glProgramUniform2fEXT\0"
    "glProgramUniform2fvEXT\0"
    "glProgramUniform2iEXT\0"
    "glProgramUniform2ivEXT\0"
    "glProgramUniform2uiEXT\0"
    "glProgramUniform2uivEXT\0"
    "glProgramUniform3fEXT\0"
    "glProgramUniform3fvEXT\0"
    "glProgramUniform3iEXT\0"
    "glProgramUniform3ivEXT\0"
    "glProgramUniform3uiEXT\0"
    "glProgramUniform3uivEXT\0"
    "glProgramUniform4fEXT\0"
    "glProgramUniform4fvEXT\0"
    "glProgramUniform4iEXT\0"
    "glProgramUniform4ivEXT\0"
    "glProgramUniform4uiEXT\0"
    "glProgramUniform4uivEXT\0"
    "glProgramUniformMatrix2fvEXT\0"
    "glProgramUniformMatrix2x3fvEXT\0"
    "glProgramUniformMatrix2x4fvEXT\0"
    "glProgramUniformMatrix3fvEXT\0"
    "glProgramUniformMatrix3x2fvEXT\0"
    "glProgramUniformMatrix3x4fvEXT\0"
    "glProgramUniformMatrix4fvEXT\0"
    "glProgramUniformMatrix4x2fvEXT\0"
    "glProgramUniformMatrix4x3fvEXT\0"
    "glUseProgramStagesEXT\0"
    "glValidateProgramPipelineEXT\0"
    "glDebugMessageCallbackARB\0"
    "glDebugMessageControlARB\0"
    "glDebugMessageInsertARB\0"
    "glGetDebugMessageLogARB\0"
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
    "glTextureBarrier\0"
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
#define gl_dispatch_stub_731 mgl_dispatch_stub_731
#define gl_dispatch_stub_732 mgl_dispatch_stub_732
#define gl_dispatch_stub_733 mgl_dispatch_stub_733
#define gl_dispatch_stub_856 mgl_dispatch_stub_856
#define gl_dispatch_stub_1003 mgl_dispatch_stub_1003
#define gl_dispatch_stub_1004 mgl_dispatch_stub_1004
#define gl_dispatch_stub_1005 mgl_dispatch_stub_1005
#define gl_dispatch_stub_1006 mgl_dispatch_stub_1006
#define gl_dispatch_stub_1007 mgl_dispatch_stub_1007
#define gl_dispatch_stub_1008 mgl_dispatch_stub_1008
#define gl_dispatch_stub_1009 mgl_dispatch_stub_1009
#define gl_dispatch_stub_1010 mgl_dispatch_stub_1010
#define gl_dispatch_stub_1012 mgl_dispatch_stub_1012
#define gl_dispatch_stub_1013 mgl_dispatch_stub_1013
#define gl_dispatch_stub_1014 mgl_dispatch_stub_1014
#define gl_dispatch_stub_1021 mgl_dispatch_stub_1021
#define gl_dispatch_stub_1022 mgl_dispatch_stub_1022
#define gl_dispatch_stub_1023 mgl_dispatch_stub_1023
#define gl_dispatch_stub_1024 mgl_dispatch_stub_1024
#define gl_dispatch_stub_1025 mgl_dispatch_stub_1025
#define gl_dispatch_stub_1026 mgl_dispatch_stub_1026
#define gl_dispatch_stub_1027 mgl_dispatch_stub_1027
#define gl_dispatch_stub_1028 mgl_dispatch_stub_1028
#define gl_dispatch_stub_1029 mgl_dispatch_stub_1029
#define gl_dispatch_stub_1031 mgl_dispatch_stub_1031
#define gl_dispatch_stub_1032 mgl_dispatch_stub_1032
#define gl_dispatch_stub_1033 mgl_dispatch_stub_1033
#define gl_dispatch_stub_1034 mgl_dispatch_stub_1034
#define gl_dispatch_stub_1035 mgl_dispatch_stub_1035
#define gl_dispatch_stub_1036 mgl_dispatch_stub_1036
#define gl_dispatch_stub_1037 mgl_dispatch_stub_1037
#define gl_dispatch_stub_1038 mgl_dispatch_stub_1038
#define gl_dispatch_stub_1039 mgl_dispatch_stub_1039
#define gl_dispatch_stub_1040 mgl_dispatch_stub_1040
#define gl_dispatch_stub_1041 mgl_dispatch_stub_1041
#define gl_dispatch_stub_1042 mgl_dispatch_stub_1042
#define gl_dispatch_stub_1043 mgl_dispatch_stub_1043
#define gl_dispatch_stub_1044 mgl_dispatch_stub_1044
#define gl_dispatch_stub_1045 mgl_dispatch_stub_1045
#define gl_dispatch_stub_1046 mgl_dispatch_stub_1046
#define gl_dispatch_stub_1047 mgl_dispatch_stub_1047
#define gl_dispatch_stub_1048 mgl_dispatch_stub_1048
#define gl_dispatch_stub_1049 mgl_dispatch_stub_1049
#define gl_dispatch_stub_1050 mgl_dispatch_stub_1050
#define gl_dispatch_stub_1051 mgl_dispatch_stub_1051
#define gl_dispatch_stub_1052 mgl_dispatch_stub_1052
#define gl_dispatch_stub_1053 mgl_dispatch_stub_1053
#define gl_dispatch_stub_1054 mgl_dispatch_stub_1054
#define gl_dispatch_stub_1055 mgl_dispatch_stub_1055
#define gl_dispatch_stub_1056 mgl_dispatch_stub_1056
#define gl_dispatch_stub_1057 mgl_dispatch_stub_1057
#define gl_dispatch_stub_1058 mgl_dispatch_stub_1058
#define gl_dispatch_stub_1059 mgl_dispatch_stub_1059
#define gl_dispatch_stub_1060 mgl_dispatch_stub_1060
#define gl_dispatch_stub_1061 mgl_dispatch_stub_1061
#define gl_dispatch_stub_1062 mgl_dispatch_stub_1062
#define gl_dispatch_stub_1063 mgl_dispatch_stub_1063
#define gl_dispatch_stub_1064 mgl_dispatch_stub_1064
#define gl_dispatch_stub_1065 mgl_dispatch_stub_1065
#define gl_dispatch_stub_1066 mgl_dispatch_stub_1066
#define gl_dispatch_stub_1067 mgl_dispatch_stub_1067
#define gl_dispatch_stub_1068 mgl_dispatch_stub_1068
#define gl_dispatch_stub_1069 mgl_dispatch_stub_1069
#define gl_dispatch_stub_1070 mgl_dispatch_stub_1070
#define gl_dispatch_stub_1071 mgl_dispatch_stub_1071
#define gl_dispatch_stub_1072 mgl_dispatch_stub_1072
#define gl_dispatch_stub_1073 mgl_dispatch_stub_1073
#define gl_dispatch_stub_1074 mgl_dispatch_stub_1074
#define gl_dispatch_stub_1075 mgl_dispatch_stub_1075
#define gl_dispatch_stub_1076 mgl_dispatch_stub_1076
#define gl_dispatch_stub_1077 mgl_dispatch_stub_1077
#define gl_dispatch_stub_1078 mgl_dispatch_stub_1078
#define gl_dispatch_stub_1079 mgl_dispatch_stub_1079
#define gl_dispatch_stub_1080 mgl_dispatch_stub_1080
#define gl_dispatch_stub_1082 mgl_dispatch_stub_1082
#define gl_dispatch_stub_1083 mgl_dispatch_stub_1083
#define gl_dispatch_stub_1108 mgl_dispatch_stub_1108
#define gl_dispatch_stub_1109 mgl_dispatch_stub_1109
#define gl_dispatch_stub_1183 mgl_dispatch_stub_1183
#define gl_dispatch_stub_1184 mgl_dispatch_stub_1184
#define gl_dispatch_stub_1185 mgl_dispatch_stub_1185
#define gl_dispatch_stub_1193 mgl_dispatch_stub_1193
#define gl_dispatch_stub_1194 mgl_dispatch_stub_1194
#define gl_dispatch_stub_1195 mgl_dispatch_stub_1195
#define gl_dispatch_stub_1196 mgl_dispatch_stub_1196
#define gl_dispatch_stub_1199 mgl_dispatch_stub_1199
#define gl_dispatch_stub_1200 mgl_dispatch_stub_1200
#define gl_dispatch_stub_1246 mgl_dispatch_stub_1246
#define gl_dispatch_stub_1247 mgl_dispatch_stub_1247
#define gl_dispatch_stub_1248 mgl_dispatch_stub_1248
#define gl_dispatch_stub_1249 mgl_dispatch_stub_1249
#define gl_dispatch_stub_1250 mgl_dispatch_stub_1250
#define gl_dispatch_stub_1251 mgl_dispatch_stub_1251
#define gl_dispatch_stub_1252 mgl_dispatch_stub_1252
#define gl_dispatch_stub_1253 mgl_dispatch_stub_1253
#define gl_dispatch_stub_1254 mgl_dispatch_stub_1254
#define gl_dispatch_stub_1255 mgl_dispatch_stub_1255
#define gl_dispatch_stub_1257 mgl_dispatch_stub_1257
#define gl_dispatch_stub_1258 mgl_dispatch_stub_1258
#define gl_dispatch_stub_1259 mgl_dispatch_stub_1259
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
void GLAPIENTRY gl_dispatch_stub_731(GLuint id, GLenum pname, GLint64 * params);
void GLAPIENTRY gl_dispatch_stub_732(GLuint id, GLenum pname, GLuint64 * params);
void GLAPIENTRY gl_dispatch_stub_733(GLuint id, GLenum target);
void GLAPIENTRY gl_dispatch_stub_856(GLenum target, GLenum internalformat, GLenum pname, GLsizei bufSize, GLint * params);
void GLAPIENTRY gl_dispatch_stub_1003(GLfloat x, GLfloat y, GLfloat z, GLfloat width, GLfloat height);
void GLAPIENTRY gl_dispatch_stub_1004(const GLfloat * coords);
void GLAPIENTRY gl_dispatch_stub_1005(GLint x, GLint y, GLint z, GLint width, GLint height);
void GLAPIENTRY gl_dispatch_stub_1006(const GLint * coords);
void GLAPIENTRY gl_dispatch_stub_1007(GLshort x, GLshort y, GLshort z, GLshort width, GLshort height);
void GLAPIENTRY gl_dispatch_stub_1008(const GLshort * coords);
void GLAPIENTRY gl_dispatch_stub_1009(GLfixed x, GLfixed y, GLfixed z, GLfixed width, GLfixed height);
void GLAPIENTRY gl_dispatch_stub_1010(const GLfixed * coords);
GLbitfield GLAPIENTRY gl_dispatch_stub_1012(GLfixed * mantissa, GLint * exponent);
void GLAPIENTRY gl_dispatch_stub_1013(GLclampf value, GLboolean invert);
void GLAPIENTRY gl_dispatch_stub_1014(GLenum pattern);
void GLAPIENTRY gl_dispatch_stub_1021(GLenum target, GLsizei numAttachments, const GLenum * attachments);
void GLAPIENTRY gl_dispatch_stub_1022(GLuint pipeline, GLuint program);
void GLAPIENTRY gl_dispatch_stub_1023(GLuint pipeline);
GLuint GLAPIENTRY gl_dispatch_stub_1024(GLenum type, GLsizei count, const GLchar * const * strings);
void GLAPIENTRY gl_dispatch_stub_1025(GLsizei n, const GLuint * pipelines);
void GLAPIENTRY gl_dispatch_stub_1026(GLsizei n, GLuint * pipelines);
void GLAPIENTRY gl_dispatch_stub_1027(GLuint pipeline, GLsizei bufSize, GLsizei * length, GLchar * infoLog);
void GLAPIENTRY gl_dispatch_stub_1028(GLuint pipeline, GLenum pname, GLint * params);
GLboolean GLAPIENTRY gl_dispatch_stub_1029(GLuint pipeline);
void GLAPIENTRY gl_dispatch_stub_1031(GLuint program, GLint location, GLdouble x);
void GLAPIENTRY gl_dispatch_stub_1032(GLuint program, GLint location, GLsizei count, const GLdouble * value);
void GLAPIENTRY gl_dispatch_stub_1033(GLuint program, GLint location, GLfloat x);
void GLAPIENTRY gl_dispatch_stub_1034(GLuint program, GLint location, GLsizei count, const GLfloat * value);
void GLAPIENTRY gl_dispatch_stub_1035(GLuint program, GLint location, GLint x);
void GLAPIENTRY gl_dispatch_stub_1036(GLuint program, GLint location, GLsizei count, const GLint * value);
void GLAPIENTRY gl_dispatch_stub_1037(GLuint program, GLint location, GLuint x);
void GLAPIENTRY gl_dispatch_stub_1038(GLuint program, GLint location, GLsizei count, const GLuint * value);
void GLAPIENTRY gl_dispatch_stub_1039(GLuint program, GLint location, GLdouble x, GLdouble y);
void GLAPIENTRY gl_dispatch_stub_1040(GLuint program, GLint location, GLsizei count, const GLdouble * value);
void GLAPIENTRY gl_dispatch_stub_1041(GLuint program, GLint location, GLfloat x, GLfloat y);
void GLAPIENTRY gl_dispatch_stub_1042(GLuint program, GLint location, GLsizei count, const GLfloat * value);
void GLAPIENTRY gl_dispatch_stub_1043(GLuint program, GLint location, GLint x, GLint y);
void GLAPIENTRY gl_dispatch_stub_1044(GLuint program, GLint location, GLsizei count, const GLint * value);
void GLAPIENTRY gl_dispatch_stub_1045(GLuint program, GLint location, GLuint x, GLuint y);
void GLAPIENTRY gl_dispatch_stub_1046(GLuint program, GLint location, GLsizei count, const GLuint * value);
void GLAPIENTRY gl_dispatch_stub_1047(GLuint program, GLint location, GLdouble x, GLdouble y, GLdouble z);
void GLAPIENTRY gl_dispatch_stub_1048(GLuint program, GLint location, GLsizei count, const GLdouble * value);
void GLAPIENTRY gl_dispatch_stub_1049(GLuint program, GLint location, GLfloat x, GLfloat y, GLfloat z);
void GLAPIENTRY gl_dispatch_stub_1050(GLuint program, GLint location, GLsizei count, const GLfloat * value);
void GLAPIENTRY gl_dispatch_stub_1051(GLuint program, GLint location, GLint x, GLint y, GLint z);
void GLAPIENTRY gl_dispatch_stub_1052(GLuint program, GLint location, GLsizei count, const GLint * value);
void GLAPIENTRY gl_dispatch_stub_1053(GLuint program, GLint location, GLuint x, GLuint y, GLuint z);
void GLAPIENTRY gl_dispatch_stub_1054(GLuint program, GLint location, GLsizei count, const GLuint * value);
void GLAPIENTRY gl_dispatch_stub_1055(GLuint program, GLint location, GLdouble x, GLdouble y, GLdouble z, GLdouble w);
void GLAPIENTRY gl_dispatch_stub_1056(GLuint program, GLint location, GLsizei count, const GLdouble * value);
void GLAPIENTRY gl_dispatch_stub_1057(GLuint program, GLint location, GLfloat x, GLfloat y, GLfloat z, GLfloat w);
void GLAPIENTRY gl_dispatch_stub_1058(GLuint program, GLint location, GLsizei count, const GLfloat * value);
void GLAPIENTRY gl_dispatch_stub_1059(GLuint program, GLint location, GLint x, GLint y, GLint z, GLint w);
void GLAPIENTRY gl_dispatch_stub_1060(GLuint program, GLint location, GLsizei count, const GLint * value);
void GLAPIENTRY gl_dispatch_stub_1061(GLuint program, GLint location, GLuint x, GLuint y, GLuint z, GLuint w);
void GLAPIENTRY gl_dispatch_stub_1062(GLuint program, GLint location, GLsizei count, const GLuint * value);
void GLAPIENTRY gl_dispatch_stub_1063(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLdouble * value);
void GLAPIENTRY gl_dispatch_stub_1064(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat * value);
void GLAPIENTRY gl_dispatch_stub_1065(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLdouble * value);
void GLAPIENTRY gl_dispatch_stub_1066(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat * value);
void GLAPIENTRY gl_dispatch_stub_1067(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLdouble * value);
void GLAPIENTRY gl_dispatch_stub_1068(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat * value);
void GLAPIENTRY gl_dispatch_stub_1069(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLdouble * value);
void GLAPIENTRY gl_dispatch_stub_1070(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat * value);
void GLAPIENTRY gl_dispatch_stub_1071(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLdouble * value);
void GLAPIENTRY gl_dispatch_stub_1072(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat * value);
void GLAPIENTRY gl_dispatch_stub_1073(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLdouble * value);
void GLAPIENTRY gl_dispatch_stub_1074(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat * value);
void GLAPIENTRY gl_dispatch_stub_1075(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLdouble * value);
void GLAPIENTRY gl_dispatch_stub_1076(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat * value);
void GLAPIENTRY gl_dispatch_stub_1077(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLdouble * value);
void GLAPIENTRY gl_dispatch_stub_1078(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat * value);
void GLAPIENTRY gl_dispatch_stub_1079(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLdouble * value);
void GLAPIENTRY gl_dispatch_stub_1080(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat * value);
void GLAPIENTRY gl_dispatch_stub_1082(GLuint pipeline, GLbitfield stages, GLuint program);
void GLAPIENTRY gl_dispatch_stub_1083(GLuint pipeline);
void GLAPIENTRY gl_dispatch_stub_1108(const GLenum * mode, const GLint * first, const GLsizei * count, GLsizei primcount, GLint modestride);
void GLAPIENTRY gl_dispatch_stub_1109(const GLenum * mode, const GLsizei * count, GLenum type, const GLvoid * const * indices, GLsizei primcount, GLint modestride);
void GLAPIENTRY gl_dispatch_stub_1183(GLenum face);
void GLAPIENTRY gl_dispatch_stub_1184(GLuint array);
void GLAPIENTRY gl_dispatch_stub_1185(GLsizei n, GLuint * arrays);
void GLAPIENTRY gl_dispatch_stub_1193(GLenum coord, GLenum pname, GLfixed * params);
void GLAPIENTRY gl_dispatch_stub_1194(GLenum coord, GLenum pname, GLint param);
void GLAPIENTRY gl_dispatch_stub_1195(GLenum coord, GLenum pname, const GLfixed * params);
void GLAPIENTRY gl_dispatch_stub_1196(GLclampd zmin, GLclampd zmax);
void GLAPIENTRY gl_dispatch_stub_1199(GLenum target, GLenum pname, GLint param);
void GLAPIENTRY gl_dispatch_stub_1200(GLenum target, GLintptr offset, GLsizeiptr size);
void GLAPIENTRY gl_dispatch_stub_1246(GLuint queryHandle);
void GLAPIENTRY gl_dispatch_stub_1247(GLuint queryId, GLuint * queryHandle);
void GLAPIENTRY gl_dispatch_stub_1248(GLuint queryHandle);
void GLAPIENTRY gl_dispatch_stub_1249(GLuint queryHandle);
void GLAPIENTRY gl_dispatch_stub_1250(GLuint * queryId);
void GLAPIENTRY gl_dispatch_stub_1251(GLuint queryId, GLuint * nextQueryId);
void GLAPIENTRY gl_dispatch_stub_1252(GLuint queryId, GLuint counterId, GLuint counterNameLength, GLchar * counterName, GLuint counterDescLength, GLchar * counterDesc, GLuint * counterOffset, GLuint * counterDataSize, GLuint * counterTypeEnum, GLuint * counterDataTypeEnum, GLuint64 * rawCounterMaxValue);
void GLAPIENTRY gl_dispatch_stub_1253(GLuint queryHandle, GLuint flags, GLsizei dataSize, GLvoid * data, GLuint * bytesWritten);
void GLAPIENTRY gl_dispatch_stub_1254(GLchar * queryName, GLuint * queryId);
void GLAPIENTRY gl_dispatch_stub_1255(GLuint queryId, GLuint queryNameLength, GLchar * queryName, GLuint * dataSize, GLuint * noCounters, GLuint * noInstances, GLuint * capsMask);
void GLAPIENTRY gl_dispatch_stub_1257(GLenum frontfunc, GLenum backfunc, GLint ref, GLuint mask);
void GLAPIENTRY gl_dispatch_stub_1258(GLenum target, GLuint index, GLsizei count, const GLfloat * params);
void GLAPIENTRY gl_dispatch_stub_1259(GLenum target, GLuint index, GLsizei count, const GLfloat * params);
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
    NAME_FUNC_OFFSET( 9460, glBindProgramARB, glBindProgramARB, NULL, 621),
    NAME_FUNC_OFFSET( 9477, glDeleteProgramsARB, glDeleteProgramsARB, NULL, 622),
    NAME_FUNC_OFFSET( 9497, glGenProgramsARB, glGenProgramsARB, NULL, 623),
    NAME_FUNC_OFFSET( 9514, glGetProgramEnvParameterdvARB, glGetProgramEnvParameterdvARB, NULL, 624),
    NAME_FUNC_OFFSET( 9544, glGetProgramEnvParameterfvARB, glGetProgramEnvParameterfvARB, NULL, 625),
    NAME_FUNC_OFFSET( 9574, glGetProgramLocalParameterdvARB, glGetProgramLocalParameterdvARB, NULL, 626),
    NAME_FUNC_OFFSET( 9606, glGetProgramLocalParameterfvARB, glGetProgramLocalParameterfvARB, NULL, 627),
    NAME_FUNC_OFFSET( 9638, glGetProgramStringARB, glGetProgramStringARB, NULL, 628),
    NAME_FUNC_OFFSET( 9660, glGetProgramivARB, glGetProgramivARB, NULL, 629),
    NAME_FUNC_OFFSET( 9678, glIsProgramARB, glIsProgramARB, NULL, 630),
    NAME_FUNC_OFFSET( 9693, glProgramEnvParameter4dARB, glProgramEnvParameter4dARB, NULL, 631),
    NAME_FUNC_OFFSET( 9720, glProgramEnvParameter4dvARB, glProgramEnvParameter4dvARB, NULL, 632),
    NAME_FUNC_OFFSET( 9748, glProgramEnvParameter4fARB, glProgramEnvParameter4fARB, NULL, 633),
    NAME_FUNC_OFFSET( 9775, glProgramEnvParameter4fvARB, glProgramEnvParameter4fvARB, NULL, 634),
    NAME_FUNC_OFFSET( 9803, glProgramLocalParameter4dARB, glProgramLocalParameter4dARB, NULL, 635),
    NAME_FUNC_OFFSET( 9832, glProgramLocalParameter4dvARB, glProgramLocalParameter4dvARB, NULL, 636),
    NAME_FUNC_OFFSET( 9862, glProgramLocalParameter4fARB, glProgramLocalParameter4fARB, NULL, 637),
    NAME_FUNC_OFFSET( 9891, glProgramLocalParameter4fvARB, glProgramLocalParameter4fvARB, NULL, 638),
    NAME_FUNC_OFFSET( 9921, glProgramStringARB, glProgramStringARB, NULL, 639),
    NAME_FUNC_OFFSET( 9940, glVertexAttrib1fARB, glVertexAttrib1fARB, NULL, 640),
    NAME_FUNC_OFFSET( 9960, glVertexAttrib1fvARB, glVertexAttrib1fvARB, NULL, 641),
    NAME_FUNC_OFFSET( 9981, glVertexAttrib2fARB, glVertexAttrib2fARB, NULL, 642),
    NAME_FUNC_OFFSET(10001, glVertexAttrib2fvARB, glVertexAttrib2fvARB, NULL, 643),
    NAME_FUNC_OFFSET(10022, glVertexAttrib3fARB, glVertexAttrib3fARB, NULL, 644),
    NAME_FUNC_OFFSET(10042, glVertexAttrib3fvARB, glVertexAttrib3fvARB, NULL, 645),
    NAME_FUNC_OFFSET(10063, glVertexAttrib4fARB, glVertexAttrib4fARB, NULL, 646),
    NAME_FUNC_OFFSET(10083, glVertexAttrib4fvARB, glVertexAttrib4fvARB, NULL, 647),
    NAME_FUNC_OFFSET(10104, glAttachObjectARB, glAttachObjectARB, NULL, 648),
    NAME_FUNC_OFFSET(10122, glCreateProgramObjectARB, glCreateProgramObjectARB, NULL, 649),
    NAME_FUNC_OFFSET(10147, glCreateShaderObjectARB, glCreateShaderObjectARB, NULL, 650),
    NAME_FUNC_OFFSET(10171, glDeleteObjectARB, glDeleteObjectARB, NULL, 651),
    NAME_FUNC_OFFSET(10189, glDetachObjectARB, glDetachObjectARB, NULL, 652),
    NAME_FUNC_OFFSET(10207, glGetAttachedObjectsARB, glGetAttachedObjectsARB, NULL, 653),
    NAME_FUNC_OFFSET(10231, glGetHandleARB, glGetHandleARB, NULL, 654),
    NAME_FUNC_OFFSET(10246, glGetInfoLogARB, glGetInfoLogARB, NULL, 655),
    NAME_FUNC_OFFSET(10262, glGetObjectParameterfvARB, glGetObjectParameterfvARB, NULL, 656),
    NAME_FUNC_OFFSET(10288, glGetObjectParameterivARB, glGetObjectParameterivARB, NULL, 657),
    NAME_FUNC_OFFSET(10314, glDrawArraysInstancedARB, glDrawArraysInstancedARB, NULL, 658),
    NAME_FUNC_OFFSET(10339, glDrawElementsInstancedARB, glDrawElementsInstancedARB, NULL, 659),
    NAME_FUNC_OFFSET(10366, glBindFramebuffer, glBindFramebuffer, NULL, 660),
    NAME_FUNC_OFFSET(10384, glBindRenderbuffer, glBindRenderbuffer, NULL, 661),
    NAME_FUNC_OFFSET(10403, glBlitFramebuffer, glBlitFramebuffer, NULL, 662),
    NAME_FUNC_OFFSET(10421, glCheckFramebufferStatus, glCheckFramebufferStatus, NULL, 663),
    NAME_FUNC_OFFSET(10446, glDeleteFramebuffers, glDeleteFramebuffers, NULL, 664),
    NAME_FUNC_OFFSET(10467, glDeleteRenderbuffers, glDeleteRenderbuffers, NULL, 665),
    NAME_FUNC_OFFSET(10489, glFramebufferRenderbuffer, glFramebufferRenderbuffer, NULL, 666),
    NAME_FUNC_OFFSET(10515, glFramebufferTexture1D, glFramebufferTexture1D, NULL, 667),
    NAME_FUNC_OFFSET(10538, glFramebufferTexture2D, glFramebufferTexture2D, NULL, 668),
    NAME_FUNC_OFFSET(10561, glFramebufferTexture3D, glFramebufferTexture3D, NULL, 669),
    NAME_FUNC_OFFSET(10584, glFramebufferTextureLayer, glFramebufferTextureLayer, NULL, 670),
    NAME_FUNC_OFFSET(10610, glGenFramebuffers, glGenFramebuffers, NULL, 671),
    NAME_FUNC_OFFSET(10628, glGenRenderbuffers, glGenRenderbuffers, NULL, 672),
    NAME_FUNC_OFFSET(10647, glGenerateMipmap, glGenerateMipmap, NULL, 673),
    NAME_FUNC_OFFSET(10664, glGetFramebufferAttachmentParameteriv, glGetFramebufferAttachmentParameteriv, NULL, 674),
    NAME_FUNC_OFFSET(10702, glGetRenderbufferParameteriv, glGetRenderbufferParameteriv, NULL, 675),
    NAME_FUNC_OFFSET(10731, glIsFramebuffer, glIsFramebuffer, NULL, 676),
    NAME_FUNC_OFFSET(10747, glIsRenderbuffer, glIsRenderbuffer, NULL, 677),
    NAME_FUNC_OFFSET(10764, glRenderbufferStorage, glRenderbufferStorage, NULL, 678),
    NAME_FUNC_OFFSET(10786, glRenderbufferStorageMultisample, glRenderbufferStorageMultisample, NULL, 679),
    NAME_FUNC_OFFSET(10819, glFramebufferTextureFaceARB, glFramebufferTextureFaceARB, NULL, 680),
    NAME_FUNC_OFFSET(10847, glFlushMappedBufferRange, glFlushMappedBufferRange, NULL, 681),
    NAME_FUNC_OFFSET(10872, glMapBufferRange, glMapBufferRange, NULL, 682),
    NAME_FUNC_OFFSET(10889, glBindVertexArray, glBindVertexArray, NULL, 683),
    NAME_FUNC_OFFSET(10907, glDeleteVertexArrays, glDeleteVertexArrays, NULL, 684),
    NAME_FUNC_OFFSET(10928, glGenVertexArrays, glGenVertexArrays, NULL, 685),
    NAME_FUNC_OFFSET(10946, glIsVertexArray, glIsVertexArray, NULL, 686),
    NAME_FUNC_OFFSET(10962, glGetActiveUniformBlockName, glGetActiveUniformBlockName, NULL, 687),
    NAME_FUNC_OFFSET(10990, glGetActiveUniformBlockiv, glGetActiveUniformBlockiv, NULL, 688),
    NAME_FUNC_OFFSET(11016, glGetActiveUniformName, glGetActiveUniformName, NULL, 689),
    NAME_FUNC_OFFSET(11039, glGetActiveUniformsiv, glGetActiveUniformsiv, NULL, 690),
    NAME_FUNC_OFFSET(11061, glGetUniformBlockIndex, glGetUniformBlockIndex, NULL, 691),
    NAME_FUNC_OFFSET(11084, glGetUniformIndices, glGetUniformIndices, NULL, 692),
    NAME_FUNC_OFFSET(11104, glUniformBlockBinding, glUniformBlockBinding, NULL, 693),
    NAME_FUNC_OFFSET(11126, glCopyBufferSubData, glCopyBufferSubData, NULL, 694),
    NAME_FUNC_OFFSET(11146, glClientWaitSync, glClientWaitSync, NULL, 695),
    NAME_FUNC_OFFSET(11163, glDeleteSync, glDeleteSync, NULL, 696),
    NAME_FUNC_OFFSET(11176, glFenceSync, glFenceSync, NULL, 697),
    NAME_FUNC_OFFSET(11188, glGetInteger64v, glGetInteger64v, NULL, 698),
    NAME_FUNC_OFFSET(11204, glGetSynciv, glGetSynciv, NULL, 699),
    NAME_FUNC_OFFSET(11216, glIsSync, glIsSync, NULL, 700),
    NAME_FUNC_OFFSET(11225, glWaitSync, glWaitSync, NULL, 701),
    NAME_FUNC_OFFSET(11236, glDrawElementsBaseVertex, glDrawElementsBaseVertex, NULL, 702),
    NAME_FUNC_OFFSET(11261, glDrawElementsInstancedBaseVertex, glDrawElementsInstancedBaseVertex, NULL, 703),
    NAME_FUNC_OFFSET(11295, glDrawRangeElementsBaseVertex, glDrawRangeElementsBaseVertex, NULL, 704),
    NAME_FUNC_OFFSET(11325, glMultiDrawElementsBaseVertex, glMultiDrawElementsBaseVertex, NULL, 705),
    NAME_FUNC_OFFSET(11355, glProvokingVertex, glProvokingVertex, NULL, 706),
    NAME_FUNC_OFFSET(11373, glGetMultisamplefv, glGetMultisamplefv, NULL, 707),
    NAME_FUNC_OFFSET(11392, glSampleMaski, glSampleMaski, NULL, 708),
    NAME_FUNC_OFFSET(11406, glTexImage2DMultisample, glTexImage2DMultisample, NULL, 709),
    NAME_FUNC_OFFSET(11430, glTexImage3DMultisample, glTexImage3DMultisample, NULL, 710),
    NAME_FUNC_OFFSET(11454, glBlendEquationSeparateiARB, glBlendEquationSeparateiARB, NULL, 711),
    NAME_FUNC_OFFSET(11482, glBlendEquationiARB, glBlendEquationiARB, NULL, 712),
    NAME_FUNC_OFFSET(11502, glBlendFuncSeparateiARB, glBlendFuncSeparateiARB, NULL, 713),
    NAME_FUNC_OFFSET(11526, glBlendFunciARB, glBlendFunciARB, NULL, 714),
    NAME_FUNC_OFFSET(11542, glBindFragDataLocationIndexed, glBindFragDataLocationIndexed, NULL, 715),
    NAME_FUNC_OFFSET(11572, glGetFragDataIndex, glGetFragDataIndex, NULL, 716),
    NAME_FUNC_OFFSET(11591, glBindSampler, glBindSampler, NULL, 717),
    NAME_FUNC_OFFSET(11605, glDeleteSamplers, glDeleteSamplers, NULL, 718),
    NAME_FUNC_OFFSET(11622, glGenSamplers, glGenSamplers, NULL, 719),
    NAME_FUNC_OFFSET(11636, glGetSamplerParameterIiv, glGetSamplerParameterIiv, NULL, 720),
    NAME_FUNC_OFFSET(11661, glGetSamplerParameterIuiv, glGetSamplerParameterIuiv, NULL, 721),
    NAME_FUNC_OFFSET(11687, glGetSamplerParameterfv, glGetSamplerParameterfv, NULL, 722),
    NAME_FUNC_OFFSET(11711, glGetSamplerParameteriv, glGetSamplerParameteriv, NULL, 723),
    NAME_FUNC_OFFSET(11735, glIsSampler, glIsSampler, NULL, 724),
    NAME_FUNC_OFFSET(11747, glSamplerParameterIiv, glSamplerParameterIiv, NULL, 725),
    NAME_FUNC_OFFSET(11769, glSamplerParameterIuiv, glSamplerParameterIuiv, NULL, 726),
    NAME_FUNC_OFFSET(11792, glSamplerParameterf, glSamplerParameterf, NULL, 727),
    NAME_FUNC_OFFSET(11812, glSamplerParameterfv, glSamplerParameterfv, NULL, 728),
    NAME_FUNC_OFFSET(11833, glSamplerParameteri, glSamplerParameteri, NULL, 729),
    NAME_FUNC_OFFSET(11853, glSamplerParameteriv, glSamplerParameteriv, NULL, 730),
    NAME_FUNC_OFFSET(11874, gl_dispatch_stub_731, gl_dispatch_stub_731, NULL, 731),
    NAME_FUNC_OFFSET(11895, gl_dispatch_stub_732, gl_dispatch_stub_732, NULL, 732),
    NAME_FUNC_OFFSET(11917, gl_dispatch_stub_733, gl_dispatch_stub_733, NULL, 733),
    NAME_FUNC_OFFSET(11932, glColorP3ui, glColorP3ui, NULL, 734),
    NAME_FUNC_OFFSET(11944, glColorP3uiv, glColorP3uiv, NULL, 735),
    NAME_FUNC_OFFSET(11957, glColorP4ui, glColorP4ui, NULL, 736),
    NAME_FUNC_OFFSET(11969, glColorP4uiv, glColorP4uiv, NULL, 737),
    NAME_FUNC_OFFSET(11982, glMultiTexCoordP1ui, glMultiTexCoordP1ui, NULL, 738),
    NAME_FUNC_OFFSET(12002, glMultiTexCoordP1uiv, glMultiTexCoordP1uiv, NULL, 739),
    NAME_FUNC_OFFSET(12023, glMultiTexCoordP2ui, glMultiTexCoordP2ui, NULL, 740),
    NAME_FUNC_OFFSET(12043, glMultiTexCoordP2uiv, glMultiTexCoordP2uiv, NULL, 741),
    NAME_FUNC_OFFSET(12064, glMultiTexCoordP3ui, glMultiTexCoordP3ui, NULL, 742),
    NAME_FUNC_OFFSET(12084, glMultiTexCoordP3uiv, glMultiTexCoordP3uiv, NULL, 743),
    NAME_FUNC_OFFSET(12105, glMultiTexCoordP4ui, glMultiTexCoordP4ui, NULL, 744),
    NAME_FUNC_OFFSET(12125, glMultiTexCoordP4uiv, glMultiTexCoordP4uiv, NULL, 745),
    NAME_FUNC_OFFSET(12146, glNormalP3ui, glNormalP3ui, NULL, 746),
    NAME_FUNC_OFFSET(12159, glNormalP3uiv, glNormalP3uiv, NULL, 747),
    NAME_FUNC_OFFSET(12173, glSecondaryColorP3ui, glSecondaryColorP3ui, NULL, 748),
    NAME_FUNC_OFFSET(12194, glSecondaryColorP3uiv, glSecondaryColorP3uiv, NULL, 749),
    NAME_FUNC_OFFSET(12216, glTexCoordP1ui, glTexCoordP1ui, NULL, 750),
    NAME_FUNC_OFFSET(12231, glTexCoordP1uiv, glTexCoordP1uiv, NULL, 751),
    NAME_FUNC_OFFSET(12247, glTexCoordP2ui, glTexCoordP2ui, NULL, 752),
    NAME_FUNC_OFFSET(12262, glTexCoordP2uiv, glTexCoordP2uiv, NULL, 753),
    NAME_FUNC_OFFSET(12278, glTexCoordP3ui, glTexCoordP3ui, NULL, 754),
    NAME_FUNC_OFFSET(12293, glTexCoordP3uiv, glTexCoordP3uiv, NULL, 755),
    NAME_FUNC_OFFSET(12309, glTexCoordP4ui, glTexCoordP4ui, NULL, 756),
    NAME_FUNC_OFFSET(12324, glTexCoordP4uiv, glTexCoordP4uiv, NULL, 757),
    NAME_FUNC_OFFSET(12340, glVertexAttribP1ui, glVertexAttribP1ui, NULL, 758),
    NAME_FUNC_OFFSET(12359, glVertexAttribP1uiv, glVertexAttribP1uiv, NULL, 759),
    NAME_FUNC_OFFSET(12379, glVertexAttribP2ui, glVertexAttribP2ui, NULL, 760),
    NAME_FUNC_OFFSET(12398, glVertexAttribP2uiv, glVertexAttribP2uiv, NULL, 761),
    NAME_FUNC_OFFSET(12418, glVertexAttribP3ui, glVertexAttribP3ui, NULL, 762),
    NAME_FUNC_OFFSET(12437, glVertexAttribP3uiv, glVertexAttribP3uiv, NULL, 763),
    NAME_FUNC_OFFSET(12457, glVertexAttribP4ui, glVertexAttribP4ui, NULL, 764),
    NAME_FUNC_OFFSET(12476, glVertexAttribP4uiv, glVertexAttribP4uiv, NULL, 765),
    NAME_FUNC_OFFSET(12496, glVertexP2ui, glVertexP2ui, NULL, 766),
    NAME_FUNC_OFFSET(12509, glVertexP2uiv, glVertexP2uiv, NULL, 767),
    NAME_FUNC_OFFSET(12523, glVertexP3ui, glVertexP3ui, NULL, 768),
    NAME_FUNC_OFFSET(12536, glVertexP3uiv, glVertexP3uiv, NULL, 769),
    NAME_FUNC_OFFSET(12550, glVertexP4ui, glVertexP4ui, NULL, 770),
    NAME_FUNC_OFFSET(12563, glVertexP4uiv, glVertexP4uiv, NULL, 771),
    NAME_FUNC_OFFSET(12577, glDrawArraysIndirect, glDrawArraysIndirect, NULL, 772),
    NAME_FUNC_OFFSET(12598, glDrawElementsIndirect, glDrawElementsIndirect, NULL, 773),
    NAME_FUNC_OFFSET(12621, glGetUniformdv, glGetUniformdv, NULL, 774),
    NAME_FUNC_OFFSET(12636, glUniform1d, glUniform1d, NULL, 775),
    NAME_FUNC_OFFSET(12648, glUniform1dv, glUniform1dv, NULL, 776),
    NAME_FUNC_OFFSET(12661, glUniform2d, glUniform2d, NULL, 777),
    NAME_FUNC_OFFSET(12673, glUniform2dv, glUniform2dv, NULL, 778),
    NAME_FUNC_OFFSET(12686, glUniform3d, glUniform3d, NULL, 779),
    NAME_FUNC_OFFSET(12698, glUniform3dv, glUniform3dv, NULL, 780),
    NAME_FUNC_OFFSET(12711, glUniform4d, glUniform4d, NULL, 781),
    NAME_FUNC_OFFSET(12723, glUniform4dv, glUniform4dv, NULL, 782),
    NAME_FUNC_OFFSET(12736, glUniformMatrix2dv, glUniformMatrix2dv, NULL, 783),
    NAME_FUNC_OFFSET(12755, glUniformMatrix2x3dv, glUniformMatrix2x3dv, NULL, 784),
    NAME_FUNC_OFFSET(12776, glUniformMatrix2x4dv, glUniformMatrix2x4dv, NULL, 785),
    NAME_FUNC_OFFSET(12797, glUniformMatrix3dv, glUniformMatrix3dv, NULL, 786),
    NAME_FUNC_OFFSET(12816, glUniformMatrix3x2dv, glUniformMatrix3x2dv, NULL, 787),
    NAME_FUNC_OFFSET(12837, glUniformMatrix3x4dv, glUniformMatrix3x4dv, NULL, 788),
    NAME_FUNC_OFFSET(12858, glUniformMatrix4dv, glUniformMatrix4dv, NULL, 789),
    NAME_FUNC_OFFSET(12877, glUniformMatrix4x2dv, glUniformMatrix4x2dv, NULL, 790),
    NAME_FUNC_OFFSET(12898, glUniformMatrix4x3dv, glUniformMatrix4x3dv, NULL, 791),
    NAME_FUNC_OFFSET(12919, glBindTransformFeedback, glBindTransformFeedback, NULL, 792),
    NAME_FUNC_OFFSET(12943, glDeleteTransformFeedbacks, glDeleteTransformFeedbacks, NULL, 793),
    NAME_FUNC_OFFSET(12970, glDrawTransformFeedback, glDrawTransformFeedback, NULL, 794),
    NAME_FUNC_OFFSET(12994, glGenTransformFeedbacks, glGenTransformFeedbacks, NULL, 795),
    NAME_FUNC_OFFSET(13018, glIsTransformFeedback, glIsTransformFeedback, NULL, 796),
    NAME_FUNC_OFFSET(13040, glPauseTransformFeedback, glPauseTransformFeedback, NULL, 797),
    NAME_FUNC_OFFSET(13065, glResumeTransformFeedback, glResumeTransformFeedback, NULL, 798),
    NAME_FUNC_OFFSET(13091, glBeginQueryIndexed, glBeginQueryIndexed, NULL, 799),
    NAME_FUNC_OFFSET(13111, glDrawTransformFeedbackStream, glDrawTransformFeedbackStream, NULL, 800),
    NAME_FUNC_OFFSET(13141, glEndQueryIndexed, glEndQueryIndexed, NULL, 801),
    NAME_FUNC_OFFSET(13159, glGetQueryIndexediv, glGetQueryIndexediv, NULL, 802),
    NAME_FUNC_OFFSET(13179, glClearDepthf, glClearDepthf, NULL, 803),
    NAME_FUNC_OFFSET(13193, glDepthRangef, glDepthRangef, NULL, 804),
    NAME_FUNC_OFFSET(13207, glGetShaderPrecisionFormat, glGetShaderPrecisionFormat, NULL, 805),
    NAME_FUNC_OFFSET(13234, glReleaseShaderCompiler, glReleaseShaderCompiler, NULL, 806),
    NAME_FUNC_OFFSET(13258, glShaderBinary, glShaderBinary, NULL, 807),
    NAME_FUNC_OFFSET(13273, glGetProgramBinary, glGetProgramBinary, NULL, 808),
    NAME_FUNC_OFFSET(13292, glProgramBinary, glProgramBinary, NULL, 809),
    NAME_FUNC_OFFSET(13308, glProgramParameteri, glProgramParameteri, NULL, 810),
    NAME_FUNC_OFFSET(13328, glGetVertexAttribLdv, glGetVertexAttribLdv, NULL, 811),
    NAME_FUNC_OFFSET(13349, glVertexAttribL1d, glVertexAttribL1d, NULL, 812),
    NAME_FUNC_OFFSET(13367, glVertexAttribL1dv, glVertexAttribL1dv, NULL, 813),
    NAME_FUNC_OFFSET(13386, glVertexAttribL2d, glVertexAttribL2d, NULL, 814),
    NAME_FUNC_OFFSET(13404, glVertexAttribL2dv, glVertexAttribL2dv, NULL, 815),
    NAME_FUNC_OFFSET(13423, glVertexAttribL3d, glVertexAttribL3d, NULL, 816),
    NAME_FUNC_OFFSET(13441, glVertexAttribL3dv, glVertexAttribL3dv, NULL, 817),
    NAME_FUNC_OFFSET(13460, glVertexAttribL4d, glVertexAttribL4d, NULL, 818),
    NAME_FUNC_OFFSET(13478, glVertexAttribL4dv, glVertexAttribL4dv, NULL, 819),
    NAME_FUNC_OFFSET(13497, glVertexAttribLPointer, glVertexAttribLPointer, NULL, 820),
    NAME_FUNC_OFFSET(13520, glDepthRangeArrayv, glDepthRangeArrayv, NULL, 821),
    NAME_FUNC_OFFSET(13539, glDepthRangeIndexed, glDepthRangeIndexed, NULL, 822),
    NAME_FUNC_OFFSET(13559, glGetDoublei_v, glGetDoublei_v, NULL, 823),
    NAME_FUNC_OFFSET(13574, glGetFloati_v, glGetFloati_v, NULL, 824),
    NAME_FUNC_OFFSET(13588, glScissorArrayv, glScissorArrayv, NULL, 825),
    NAME_FUNC_OFFSET(13604, glScissorIndexed, glScissorIndexed, NULL, 826),
    NAME_FUNC_OFFSET(13621, glScissorIndexedv, glScissorIndexedv, NULL, 827),
    NAME_FUNC_OFFSET(13639, glViewportArrayv, glViewportArrayv, NULL, 828),
    NAME_FUNC_OFFSET(13656, glViewportIndexedf, glViewportIndexedf, NULL, 829),
    NAME_FUNC_OFFSET(13675, glViewportIndexedfv, glViewportIndexedfv, NULL, 830),
    NAME_FUNC_OFFSET(13695, glGetGraphicsResetStatusARB, glGetGraphicsResetStatusARB, NULL, 831),
    NAME_FUNC_OFFSET(13723, glGetnColorTableARB, glGetnColorTableARB, NULL, 832),
    NAME_FUNC_OFFSET(13743, glGetnCompressedTexImageARB, glGetnCompressedTexImageARB, NULL, 833),
    NAME_FUNC_OFFSET(13771, glGetnConvolutionFilterARB, glGetnConvolutionFilterARB, NULL, 834),
    NAME_FUNC_OFFSET(13798, glGetnHistogramARB, glGetnHistogramARB, NULL, 835),
    NAME_FUNC_OFFSET(13817, glGetnMapdvARB, glGetnMapdvARB, NULL, 836),
    NAME_FUNC_OFFSET(13832, glGetnMapfvARB, glGetnMapfvARB, NULL, 837),
    NAME_FUNC_OFFSET(13847, glGetnMapivARB, glGetnMapivARB, NULL, 838),
    NAME_FUNC_OFFSET(13862, glGetnMinmaxARB, glGetnMinmaxARB, NULL, 839),
    NAME_FUNC_OFFSET(13878, glGetnPixelMapfvARB, glGetnPixelMapfvARB, NULL, 840),
    NAME_FUNC_OFFSET(13898, glGetnPixelMapuivARB, glGetnPixelMapuivARB, NULL, 841),
    NAME_FUNC_OFFSET(13919, glGetnPixelMapusvARB, glGetnPixelMapusvARB, NULL, 842),
    NAME_FUNC_OFFSET(13940, glGetnPolygonStippleARB, glGetnPolygonStippleARB, NULL, 843),
    NAME_FUNC_OFFSET(13964, glGetnSeparableFilterARB, glGetnSeparableFilterARB, NULL, 844),
    NAME_FUNC_OFFSET(13989, glGetnTexImageARB, glGetnTexImageARB, NULL, 845),
    NAME_FUNC_OFFSET(14007, glGetnUniformdvARB, glGetnUniformdvARB, NULL, 846),
    NAME_FUNC_OFFSET(14026, glGetnUniformfvARB, glGetnUniformfvARB, NULL, 847),
    NAME_FUNC_OFFSET(14045, glGetnUniformivARB, glGetnUniformivARB, NULL, 848),
    NAME_FUNC_OFFSET(14064, glGetnUniformuivARB, glGetnUniformuivARB, NULL, 849),
    NAME_FUNC_OFFSET(14084, glReadnPixelsARB, glReadnPixelsARB, NULL, 850),
    NAME_FUNC_OFFSET(14101, glDrawArraysInstancedBaseInstance, glDrawArraysInstancedBaseInstance, NULL, 851),
    NAME_FUNC_OFFSET(14135, glDrawElementsInstancedBaseInstance, glDrawElementsInstancedBaseInstance, NULL, 852),
    NAME_FUNC_OFFSET(14171, glDrawElementsInstancedBaseVertexBaseInstance, glDrawElementsInstancedBaseVertexBaseInstance, NULL, 853),
    NAME_FUNC_OFFSET(14217, glDrawTransformFeedbackInstanced, glDrawTransformFeedbackInstanced, NULL, 854),
    NAME_FUNC_OFFSET(14250, glDrawTransformFeedbackStreamInstanced, glDrawTransformFeedbackStreamInstanced, NULL, 855),
    NAME_FUNC_OFFSET(14289, gl_dispatch_stub_856, gl_dispatch_stub_856, NULL, 856),
    NAME_FUNC_OFFSET(14311, glGetActiveAtomicCounterBufferiv, glGetActiveAtomicCounterBufferiv, NULL, 857),
    NAME_FUNC_OFFSET(14344, glBindImageTexture, glBindImageTexture, NULL, 858),
    NAME_FUNC_OFFSET(14363, glMemoryBarrier, glMemoryBarrier, NULL, 859),
    NAME_FUNC_OFFSET(14379, glTexStorage1D, glTexStorage1D, NULL, 860),
    NAME_FUNC_OFFSET(14394, glTexStorage2D, glTexStorage2D, NULL, 861),
    NAME_FUNC_OFFSET(14409, glTexStorage3D, glTexStorage3D, NULL, 862),
    NAME_FUNC_OFFSET(14424, glTextureStorage1DEXT, glTextureStorage1DEXT, NULL, 863),
    NAME_FUNC_OFFSET(14446, glTextureStorage2DEXT, glTextureStorage2DEXT, NULL, 864),
    NAME_FUNC_OFFSET(14468, glTextureStorage3DEXT, glTextureStorage3DEXT, NULL, 865),
    NAME_FUNC_OFFSET(14490, glClearBufferData, glClearBufferData, NULL, 866),
    NAME_FUNC_OFFSET(14508, glClearBufferSubData, glClearBufferSubData, NULL, 867),
    NAME_FUNC_OFFSET(14529, glDispatchCompute, glDispatchCompute, NULL, 868),
    NAME_FUNC_OFFSET(14547, glDispatchComputeIndirect, glDispatchComputeIndirect, NULL, 869),
    NAME_FUNC_OFFSET(14573, glCopyImageSubData, glCopyImageSubData, NULL, 870),
    NAME_FUNC_OFFSET(14592, glTextureView, glTextureView, NULL, 871),
    NAME_FUNC_OFFSET(14606, glBindVertexBuffer, glBindVertexBuffer, NULL, 872),
    NAME_FUNC_OFFSET(14625, glVertexAttribBinding, glVertexAttribBinding, NULL, 873),
    NAME_FUNC_OFFSET(14647, glVertexAttribFormat, glVertexAttribFormat, NULL, 874),
    NAME_FUNC_OFFSET(14668, glVertexAttribIFormat, glVertexAttribIFormat, NULL, 875),
    NAME_FUNC_OFFSET(14690, glVertexAttribLFormat, glVertexAttribLFormat, NULL, 876),
    NAME_FUNC_OFFSET(14712, glVertexBindingDivisor, glVertexBindingDivisor, NULL, 877),
    NAME_FUNC_OFFSET(14735, glMultiDrawArraysIndirect, glMultiDrawArraysIndirect, NULL, 878),
    NAME_FUNC_OFFSET(14761, glMultiDrawElementsIndirect, glMultiDrawElementsIndirect, NULL, 879),
    NAME_FUNC_OFFSET(14789, glGetProgramInterfaceiv, glGetProgramInterfaceiv, NULL, 880),
    NAME_FUNC_OFFSET(14813, glGetProgramResourceIndex, glGetProgramResourceIndex, NULL, 881),
    NAME_FUNC_OFFSET(14839, glGetProgramResourceLocation, glGetProgramResourceLocation, NULL, 882),
    NAME_FUNC_OFFSET(14868, glGetProgramResourceLocationIndex, glGetProgramResourceLocationIndex, NULL, 883),
    NAME_FUNC_OFFSET(14902, glGetProgramResourceName, glGetProgramResourceName, NULL, 884),
    NAME_FUNC_OFFSET(14927, glGetProgramResourceiv, glGetProgramResourceiv, NULL, 885),
    NAME_FUNC_OFFSET(14950, glTexBufferRange, glTexBufferRange, NULL, 886),
    NAME_FUNC_OFFSET(14967, glTexStorage2DMultisample, glTexStorage2DMultisample, NULL, 887),
    NAME_FUNC_OFFSET(14993, glTexStorage3DMultisample, glTexStorage3DMultisample, NULL, 888),
    NAME_FUNC_OFFSET(15019, glBufferStorage, glBufferStorage, NULL, 889),
    NAME_FUNC_OFFSET(15035, glClearTexImage, glClearTexImage, NULL, 890),
    NAME_FUNC_OFFSET(15051, glClearTexSubImage, glClearTexSubImage, NULL, 891),
    NAME_FUNC_OFFSET(15070, glBindBuffersBase, glBindBuffersBase, NULL, 892),
    NAME_FUNC_OFFSET(15088, glBindBuffersRange, glBindBuffersRange, NULL, 893),
    NAME_FUNC_OFFSET(15107, glBindImageTextures, glBindImageTextures, NULL, 894),
    NAME_FUNC_OFFSET(15127, glBindSamplers, glBindSamplers, NULL, 895),
    NAME_FUNC_OFFSET(15142, glBindTextures, glBindTextures, NULL, 896),
    NAME_FUNC_OFFSET(15157, glBindVertexBuffers, glBindVertexBuffers, NULL, 897),
    NAME_FUNC_OFFSET(15177, glClipControl, glClipControl, NULL, 898),
    NAME_FUNC_OFFSET(15191, glBindTextureUnit, glBindTextureUnit, NULL, 899),
    NAME_FUNC_OFFSET(15209, glBlitNamedFramebuffer, glBlitNamedFramebuffer, NULL, 900),
    NAME_FUNC_OFFSET(15232, glCheckNamedFramebufferStatus, glCheckNamedFramebufferStatus, NULL, 901),
    NAME_FUNC_OFFSET(15262, glClearNamedBufferData, glClearNamedBufferData, NULL, 902),
    NAME_FUNC_OFFSET(15285, glClearNamedBufferSubData, glClearNamedBufferSubData, NULL, 903),
    NAME_FUNC_OFFSET(15311, glClearNamedFramebufferfi, glClearNamedFramebufferfi, NULL, 904),
    NAME_FUNC_OFFSET(15337, glClearNamedFramebufferfv, glClearNamedFramebufferfv, NULL, 905),
    NAME_FUNC_OFFSET(15363, glClearNamedFramebufferiv, glClearNamedFramebufferiv, NULL, 906),
    NAME_FUNC_OFFSET(15389, glClearNamedFramebufferuiv, glClearNamedFramebufferuiv, NULL, 907),
    NAME_FUNC_OFFSET(15416, glCompressedTextureSubImage1D, glCompressedTextureSubImage1D, NULL, 908),
    NAME_FUNC_OFFSET(15446, glCompressedTextureSubImage2D, glCompressedTextureSubImage2D, NULL, 909),
    NAME_FUNC_OFFSET(15476, glCompressedTextureSubImage3D, glCompressedTextureSubImage3D, NULL, 910),
    NAME_FUNC_OFFSET(15506, glCopyNamedBufferSubData, glCopyNamedBufferSubData, NULL, 911),
    NAME_FUNC_OFFSET(15531, glCopyTextureSubImage1D, glCopyTextureSubImage1D, NULL, 912),
    NAME_FUNC_OFFSET(15555, glCopyTextureSubImage2D, glCopyTextureSubImage2D, NULL, 913),
    NAME_FUNC_OFFSET(15579, glCopyTextureSubImage3D, glCopyTextureSubImage3D, NULL, 914),
    NAME_FUNC_OFFSET(15603, glCreateBuffers, glCreateBuffers, NULL, 915),
    NAME_FUNC_OFFSET(15619, glCreateFramebuffers, glCreateFramebuffers, NULL, 916),
    NAME_FUNC_OFFSET(15640, glCreateProgramPipelines, glCreateProgramPipelines, NULL, 917),
    NAME_FUNC_OFFSET(15665, glCreateQueries, glCreateQueries, NULL, 918),
    NAME_FUNC_OFFSET(15681, glCreateRenderbuffers, glCreateRenderbuffers, NULL, 919),
    NAME_FUNC_OFFSET(15703, glCreateSamplers, glCreateSamplers, NULL, 920),
    NAME_FUNC_OFFSET(15720, glCreateTextures, glCreateTextures, NULL, 921),
    NAME_FUNC_OFFSET(15737, glCreateTransformFeedbacks, glCreateTransformFeedbacks, NULL, 922),
    NAME_FUNC_OFFSET(15764, glCreateVertexArrays, glCreateVertexArrays, NULL, 923),
    NAME_FUNC_OFFSET(15785, glDisableVertexArrayAttrib, glDisableVertexArrayAttrib, NULL, 924),
    NAME_FUNC_OFFSET(15812, glEnableVertexArrayAttrib, glEnableVertexArrayAttrib, NULL, 925),
    NAME_FUNC_OFFSET(15838, glFlushMappedNamedBufferRange, glFlushMappedNamedBufferRange, NULL, 926),
    NAME_FUNC_OFFSET(15868, glGenerateTextureMipmap, glGenerateTextureMipmap, NULL, 927),
    NAME_FUNC_OFFSET(15892, glGetCompressedTextureImage, glGetCompressedTextureImage, NULL, 928),
    NAME_FUNC_OFFSET(15920, glGetNamedBufferParameteri64v, glGetNamedBufferParameteri64v, NULL, 929),
    NAME_FUNC_OFFSET(15950, glGetNamedBufferParameteriv, glGetNamedBufferParameteriv, NULL, 930),
    NAME_FUNC_OFFSET(15978, glGetNamedBufferPointerv, glGetNamedBufferPointerv, NULL, 931),
    NAME_FUNC_OFFSET(16003, glGetNamedBufferSubData, glGetNamedBufferSubData, NULL, 932),
    NAME_FUNC_OFFSET(16027, glGetNamedFramebufferAttachmentParameteriv, glGetNamedFramebufferAttachmentParameteriv, NULL, 933),
    NAME_FUNC_OFFSET(16070, glGetNamedFramebufferParameteriv, glGetNamedFramebufferParameteriv, NULL, 934),
    NAME_FUNC_OFFSET(16103, glGetNamedRenderbufferParameteriv, glGetNamedRenderbufferParameteriv, NULL, 935),
    NAME_FUNC_OFFSET(16137, glGetQueryBufferObjecti64v, glGetQueryBufferObjecti64v, NULL, 936),
    NAME_FUNC_OFFSET(16164, glGetQueryBufferObjectiv, glGetQueryBufferObjectiv, NULL, 937),
    NAME_FUNC_OFFSET(16189, glGetQueryBufferObjectui64v, glGetQueryBufferObjectui64v, NULL, 938),
    NAME_FUNC_OFFSET(16217, glGetQueryBufferObjectuiv, glGetQueryBufferObjectuiv, NULL, 939),
    NAME_FUNC_OFFSET(16243, glGetTextureImage, glGetTextureImage, NULL, 940),
    NAME_FUNC_OFFSET(16261, glGetTextureLevelParameterfv, glGetTextureLevelParameterfv, NULL, 941),
    NAME_FUNC_OFFSET(16290, glGetTextureLevelParameteriv, glGetTextureLevelParameteriv, NULL, 942),
    NAME_FUNC_OFFSET(16319, glGetTextureParameterIiv, glGetTextureParameterIiv, NULL, 943),
    NAME_FUNC_OFFSET(16344, glGetTextureParameterIuiv, glGetTextureParameterIuiv, NULL, 944),
    NAME_FUNC_OFFSET(16370, glGetTextureParameterfv, glGetTextureParameterfv, NULL, 945),
    NAME_FUNC_OFFSET(16394, glGetTextureParameteriv, glGetTextureParameteriv, NULL, 946),
    NAME_FUNC_OFFSET(16418, glGetTransformFeedbacki64_v, glGetTransformFeedbacki64_v, NULL, 947),
    NAME_FUNC_OFFSET(16446, glGetTransformFeedbacki_v, glGetTransformFeedbacki_v, NULL, 948),
    NAME_FUNC_OFFSET(16472, glGetTransformFeedbackiv, glGetTransformFeedbackiv, NULL, 949),
    NAME_FUNC_OFFSET(16497, glGetVertexArrayIndexed64iv, glGetVertexArrayIndexed64iv, NULL, 950),
    NAME_FUNC_OFFSET(16525, glGetVertexArrayIndexediv, glGetVertexArrayIndexediv, NULL, 951),
    NAME_FUNC_OFFSET(16551, glGetVertexArrayiv, glGetVertexArrayiv, NULL, 952),
    NAME_FUNC_OFFSET(16570, glInvalidateNamedFramebufferData, glInvalidateNamedFramebufferData, NULL, 953),
    NAME_FUNC_OFFSET(16603, glInvalidateNamedFramebufferSubData, glInvalidateNamedFramebufferSubData, NULL, 954),
    NAME_FUNC_OFFSET(16639, glMapNamedBuffer, glMapNamedBuffer, NULL, 955),
    NAME_FUNC_OFFSET(16656, glMapNamedBufferRange, glMapNamedBufferRange, NULL, 956),
    NAME_FUNC_OFFSET(16678, glNamedBufferData, glNamedBufferData, NULL, 957),
    NAME_FUNC_OFFSET(16696, glNamedBufferStorage, glNamedBufferStorage, NULL, 958),
    NAME_FUNC_OFFSET(16717, glNamedBufferSubData, glNamedBufferSubData, NULL, 959),
    NAME_FUNC_OFFSET(16738, glNamedFramebufferDrawBuffer, glNamedFramebufferDrawBuffer, NULL, 960),
    NAME_FUNC_OFFSET(16767, glNamedFramebufferDrawBuffers, glNamedFramebufferDrawBuffers, NULL, 961),
    NAME_FUNC_OFFSET(16797, glNamedFramebufferParameteri, glNamedFramebufferParameteri, NULL, 962),
    NAME_FUNC_OFFSET(16826, glNamedFramebufferReadBuffer, glNamedFramebufferReadBuffer, NULL, 963),
    NAME_FUNC_OFFSET(16855, glNamedFramebufferRenderbuffer, glNamedFramebufferRenderbuffer, NULL, 964),
    NAME_FUNC_OFFSET(16886, glNamedFramebufferTexture, glNamedFramebufferTexture, NULL, 965),
    NAME_FUNC_OFFSET(16912, glNamedFramebufferTextureLayer, glNamedFramebufferTextureLayer, NULL, 966),
    NAME_FUNC_OFFSET(16943, glNamedRenderbufferStorage, glNamedRenderbufferStorage, NULL, 967),
    NAME_FUNC_OFFSET(16970, glNamedRenderbufferStorageMultisample, glNamedRenderbufferStorageMultisample, NULL, 968),
    NAME_FUNC_OFFSET(17008, glTextureBuffer, glTextureBuffer, NULL, 969),
    NAME_FUNC_OFFSET(17024, glTextureBufferRange, glTextureBufferRange, NULL, 970),
    NAME_FUNC_OFFSET(17045, glTextureParameterIiv, glTextureParameterIiv, NULL, 971),
    NAME_FUNC_OFFSET(17067, glTextureParameterIuiv, glTextureParameterIuiv, NULL, 972),
    NAME_FUNC_OFFSET(17090, glTextureParameterf, glTextureParameterf, NULL, 973),
    NAME_FUNC_OFFSET(17110, glTextureParameterfv, glTextureParameterfv, NULL, 974),
    NAME_FUNC_OFFSET(17131, glTextureParameteri, glTextureParameteri, NULL, 975),
    NAME_FUNC_OFFSET(17151, glTextureParameteriv, glTextureParameteriv, NULL, 976),
    NAME_FUNC_OFFSET(17172, glTextureStorage1D, glTextureStorage1D, NULL, 977),
    NAME_FUNC_OFFSET(17191, glTextureStorage2D, glTextureStorage2D, NULL, 978),
    NAME_FUNC_OFFSET(17210, glTextureStorage2DMultisample, glTextureStorage2DMultisample, NULL, 979),
    NAME_FUNC_OFFSET(17240, glTextureStorage3D, glTextureStorage3D, NULL, 980),
    NAME_FUNC_OFFSET(17259, glTextureStorage3DMultisample, glTextureStorage3DMultisample, NULL, 981),
    NAME_FUNC_OFFSET(17289, glTextureSubImage1D, glTextureSubImage1D, NULL, 982),
    NAME_FUNC_OFFSET(17309, glTextureSubImage2D, glTextureSubImage2D, NULL, 983),
    NAME_FUNC_OFFSET(17329, glTextureSubImage3D, glTextureSubImage3D, NULL, 984),
    NAME_FUNC_OFFSET(17349, glTransformFeedbackBufferBase, glTransformFeedbackBufferBase, NULL, 985),
    NAME_FUNC_OFFSET(17379, glTransformFeedbackBufferRange, glTransformFeedbackBufferRange, NULL, 986),
    NAME_FUNC_OFFSET(17410, glUnmapNamedBuffer, glUnmapNamedBuffer, NULL, 987),
    NAME_FUNC_OFFSET(17429, glVertexArrayAttribBinding, glVertexArrayAttribBinding, NULL, 988),
    NAME_FUNC_OFFSET(17456, glVertexArrayAttribFormat, glVertexArrayAttribFormat, NULL, 989),
    NAME_FUNC_OFFSET(17482, glVertexArrayAttribIFormat, glVertexArrayAttribIFormat, NULL, 990),
    NAME_FUNC_OFFSET(17509, glVertexArrayAttribLFormat, glVertexArrayAttribLFormat, NULL, 991),
    NAME_FUNC_OFFSET(17536, glVertexArrayBindingDivisor, glVertexArrayBindingDivisor, NULL, 992),
    NAME_FUNC_OFFSET(17564, glVertexArrayElementBuffer, glVertexArrayElementBuffer, NULL, 993),
    NAME_FUNC_OFFSET(17591, glVertexArrayVertexBuffer, glVertexArrayVertexBuffer, NULL, 994),
    NAME_FUNC_OFFSET(17617, glVertexArrayVertexBuffers, glVertexArrayVertexBuffers, NULL, 995),
    NAME_FUNC_OFFSET(17644, glInvalidateBufferData, glInvalidateBufferData, NULL, 996),
    NAME_FUNC_OFFSET(17667, glInvalidateBufferSubData, glInvalidateBufferSubData, NULL, 997),
    NAME_FUNC_OFFSET(17693, glInvalidateFramebuffer, glInvalidateFramebuffer, NULL, 998),
    NAME_FUNC_OFFSET(17717, glInvalidateSubFramebuffer, glInvalidateSubFramebuffer, NULL, 999),
    NAME_FUNC_OFFSET(17744, glInvalidateTexImage, glInvalidateTexImage, NULL, 1000),
    NAME_FUNC_OFFSET(17765, glInvalidateTexSubImage, glInvalidateTexSubImage, NULL, 1001),
    NAME_FUNC_OFFSET(17789, glPolygonOffsetEXT, glPolygonOffsetEXT, NULL, 1002),
    NAME_FUNC_OFFSET(17808, gl_dispatch_stub_1003, gl_dispatch_stub_1003, NULL, 1003),
    NAME_FUNC_OFFSET(17822, gl_dispatch_stub_1004, gl_dispatch_stub_1004, NULL, 1004),
    NAME_FUNC_OFFSET(17837, gl_dispatch_stub_1005, gl_dispatch_stub_1005, NULL, 1005),
    NAME_FUNC_OFFSET(17851, gl_dispatch_stub_1006, gl_dispatch_stub_1006, NULL, 1006),
    NAME_FUNC_OFFSET(17866, gl_dispatch_stub_1007, gl_dispatch_stub_1007, NULL, 1007),
    NAME_FUNC_OFFSET(17880, gl_dispatch_stub_1008, gl_dispatch_stub_1008, NULL, 1008),
    NAME_FUNC_OFFSET(17895, gl_dispatch_stub_1009, gl_dispatch_stub_1009, NULL, 1009),
    NAME_FUNC_OFFSET(17909, gl_dispatch_stub_1010, gl_dispatch_stub_1010, NULL, 1010),
    NAME_FUNC_OFFSET(17924, glPointSizePointerOES, glPointSizePointerOES, NULL, 1011),
    NAME_FUNC_OFFSET(17946, gl_dispatch_stub_1012, gl_dispatch_stub_1012, NULL, 1012),
    NAME_FUNC_OFFSET(17964, gl_dispatch_stub_1013, gl_dispatch_stub_1013, NULL, 1013),
    NAME_FUNC_OFFSET(17981, gl_dispatch_stub_1014, gl_dispatch_stub_1014, NULL, 1014),
    NAME_FUNC_OFFSET(18001, glColorPointerEXT, glColorPointerEXT, NULL, 1015),
    NAME_FUNC_OFFSET(18019, glEdgeFlagPointerEXT, glEdgeFlagPointerEXT, NULL, 1016),
    NAME_FUNC_OFFSET(18040, glIndexPointerEXT, glIndexPointerEXT, NULL, 1017),
    NAME_FUNC_OFFSET(18058, glNormalPointerEXT, glNormalPointerEXT, NULL, 1018),
    NAME_FUNC_OFFSET(18077, glTexCoordPointerEXT, glTexCoordPointerEXT, NULL, 1019),
    NAME_FUNC_OFFSET(18098, glVertexPointerEXT, glVertexPointerEXT, NULL, 1020),
    NAME_FUNC_OFFSET(18117, gl_dispatch_stub_1021, gl_dispatch_stub_1021, NULL, 1021),
    NAME_FUNC_OFFSET(18141, gl_dispatch_stub_1022, gl_dispatch_stub_1022, NULL, 1022),
    NAME_FUNC_OFFSET(18163, gl_dispatch_stub_1023, gl_dispatch_stub_1023, NULL, 1023),
    NAME_FUNC_OFFSET(18185, gl_dispatch_stub_1024, gl_dispatch_stub_1024, NULL, 1024),
    NAME_FUNC_OFFSET(18208, gl_dispatch_stub_1025, gl_dispatch_stub_1025, NULL, 1025),
    NAME_FUNC_OFFSET(18233, gl_dispatch_stub_1026, gl_dispatch_stub_1026, NULL, 1026),
    NAME_FUNC_OFFSET(18255, gl_dispatch_stub_1027, gl_dispatch_stub_1027, NULL, 1027),
    NAME_FUNC_OFFSET(18283, gl_dispatch_stub_1028, gl_dispatch_stub_1028, NULL, 1028),
    NAME_FUNC_OFFSET(18306, gl_dispatch_stub_1029, gl_dispatch_stub_1029, NULL, 1029),
    NAME_FUNC_OFFSET(18326, glLockArraysEXT, glLockArraysEXT, NULL, 1030),
    NAME_FUNC_OFFSET(18342, gl_dispatch_stub_1031, gl_dispatch_stub_1031, NULL, 1031),
    NAME_FUNC_OFFSET(18361, gl_dispatch_stub_1032, gl_dispatch_stub_1032, NULL, 1032),
    NAME_FUNC_OFFSET(18381, gl_dispatch_stub_1033, gl_dispatch_stub_1033, NULL, 1033),
    NAME_FUNC_OFFSET(18400, gl_dispatch_stub_1034, gl_dispatch_stub_1034, NULL, 1034),
    NAME_FUNC_OFFSET(18420, gl_dispatch_stub_1035, gl_dispatch_stub_1035, NULL, 1035),
    NAME_FUNC_OFFSET(18439, gl_dispatch_stub_1036, gl_dispatch_stub_1036, NULL, 1036),
    NAME_FUNC_OFFSET(18459, gl_dispatch_stub_1037, gl_dispatch_stub_1037, NULL, 1037),
    NAME_FUNC_OFFSET(18479, gl_dispatch_stub_1038, gl_dispatch_stub_1038, NULL, 1038),
    NAME_FUNC_OFFSET(18500, gl_dispatch_stub_1039, gl_dispatch_stub_1039, NULL, 1039),
    NAME_FUNC_OFFSET(18519, gl_dispatch_stub_1040, gl_dispatch_stub_1040, NULL, 1040),
    NAME_FUNC_OFFSET(18539, gl_dispatch_stub_1041, gl_dispatch_stub_1041, NULL, 1041),
    NAME_FUNC_OFFSET(18558, gl_dispatch_stub_1042, gl_dispatch_stub_1042, NULL, 1042),
    NAME_FUNC_OFFSET(18578, gl_dispatch_stub_1043, gl_dispatch_stub_1043, NULL, 1043),
    NAME_FUNC_OFFSET(18597, gl_dispatch_stub_1044, gl_dispatch_stub_1044, NULL, 1044),
    NAME_FUNC_OFFSET(18617, gl_dispatch_stub_1045, gl_dispatch_stub_1045, NULL, 1045),
    NAME_FUNC_OFFSET(18637, gl_dispatch_stub_1046, gl_dispatch_stub_1046, NULL, 1046),
    NAME_FUNC_OFFSET(18658, gl_dispatch_stub_1047, gl_dispatch_stub_1047, NULL, 1047),
    NAME_FUNC_OFFSET(18677, gl_dispatch_stub_1048, gl_dispatch_stub_1048, NULL, 1048),
    NAME_FUNC_OFFSET(18697, gl_dispatch_stub_1049, gl_dispatch_stub_1049, NULL, 1049),
    NAME_FUNC_OFFSET(18716, gl_dispatch_stub_1050, gl_dispatch_stub_1050, NULL, 1050),
    NAME_FUNC_OFFSET(18736, gl_dispatch_stub_1051, gl_dispatch_stub_1051, NULL, 1051),
    NAME_FUNC_OFFSET(18755, gl_dispatch_stub_1052, gl_dispatch_stub_1052, NULL, 1052),
    NAME_FUNC_OFFSET(18775, gl_dispatch_stub_1053, gl_dispatch_stub_1053, NULL, 1053),
    NAME_FUNC_OFFSET(18795, gl_dispatch_stub_1054, gl_dispatch_stub_1054, NULL, 1054),
    NAME_FUNC_OFFSET(18816, gl_dispatch_stub_1055, gl_dispatch_stub_1055, NULL, 1055),
    NAME_FUNC_OFFSET(18835, gl_dispatch_stub_1056, gl_dispatch_stub_1056, NULL, 1056),
    NAME_FUNC_OFFSET(18855, gl_dispatch_stub_1057, gl_dispatch_stub_1057, NULL, 1057),
    NAME_FUNC_OFFSET(18874, gl_dispatch_stub_1058, gl_dispatch_stub_1058, NULL, 1058),
    NAME_FUNC_OFFSET(18894, gl_dispatch_stub_1059, gl_dispatch_stub_1059, NULL, 1059),
    NAME_FUNC_OFFSET(18913, gl_dispatch_stub_1060, gl_dispatch_stub_1060, NULL, 1060),
    NAME_FUNC_OFFSET(18933, gl_dispatch_stub_1061, gl_dispatch_stub_1061, NULL, 1061),
    NAME_FUNC_OFFSET(18953, gl_dispatch_stub_1062, gl_dispatch_stub_1062, NULL, 1062),
    NAME_FUNC_OFFSET(18974, gl_dispatch_stub_1063, gl_dispatch_stub_1063, NULL, 1063),
    NAME_FUNC_OFFSET(19000, gl_dispatch_stub_1064, gl_dispatch_stub_1064, NULL, 1064),
    NAME_FUNC_OFFSET(19026, gl_dispatch_stub_1065, gl_dispatch_stub_1065, NULL, 1065),
    NAME_FUNC_OFFSET(19054, gl_dispatch_stub_1066, gl_dispatch_stub_1066, NULL, 1066),
    NAME_FUNC_OFFSET(19082, gl_dispatch_stub_1067, gl_dispatch_stub_1067, NULL, 1067),
    NAME_FUNC_OFFSET(19110, gl_dispatch_stub_1068, gl_dispatch_stub_1068, NULL, 1068),
    NAME_FUNC_OFFSET(19138, gl_dispatch_stub_1069, gl_dispatch_stub_1069, NULL, 1069),
    NAME_FUNC_OFFSET(19164, gl_dispatch_stub_1070, gl_dispatch_stub_1070, NULL, 1070),
    NAME_FUNC_OFFSET(19190, gl_dispatch_stub_1071, gl_dispatch_stub_1071, NULL, 1071),
    NAME_FUNC_OFFSET(19218, gl_dispatch_stub_1072, gl_dispatch_stub_1072, NULL, 1072),
    NAME_FUNC_OFFSET(19246, gl_dispatch_stub_1073, gl_dispatch_stub_1073, NULL, 1073),
    NAME_FUNC_OFFSET(19274, gl_dispatch_stub_1074, gl_dispatch_stub_1074, NULL, 1074),
    NAME_FUNC_OFFSET(19302, gl_dispatch_stub_1075, gl_dispatch_stub_1075, NULL, 1075),
    NAME_FUNC_OFFSET(19328, gl_dispatch_stub_1076, gl_dispatch_stub_1076, NULL, 1076),
    NAME_FUNC_OFFSET(19354, gl_dispatch_stub_1077, gl_dispatch_stub_1077, NULL, 1077),
    NAME_FUNC_OFFSET(19382, gl_dispatch_stub_1078, gl_dispatch_stub_1078, NULL, 1078),
    NAME_FUNC_OFFSET(19410, gl_dispatch_stub_1079, gl_dispatch_stub_1079, NULL, 1079),
    NAME_FUNC_OFFSET(19438, gl_dispatch_stub_1080, gl_dispatch_stub_1080, NULL, 1080),
    NAME_FUNC_OFFSET(19466, glUnlockArraysEXT, glUnlockArraysEXT, NULL, 1081),
    NAME_FUNC_OFFSET(19484, gl_dispatch_stub_1082, gl_dispatch_stub_1082, NULL, 1082),
    NAME_FUNC_OFFSET(19503, gl_dispatch_stub_1083, gl_dispatch_stub_1083, NULL, 1083),
    NAME_FUNC_OFFSET(19529, glDebugMessageCallback, glDebugMessageCallback, NULL, 1084),
    NAME_FUNC_OFFSET(19552, glDebugMessageControl, glDebugMessageControl, NULL, 1085),
    NAME_FUNC_OFFSET(19574, glDebugMessageInsert, glDebugMessageInsert, NULL, 1086),
    NAME_FUNC_OFFSET(19595, glGetDebugMessageLog, glGetDebugMessageLog, NULL, 1087),
    NAME_FUNC_OFFSET(19616, glGetObjectLabel, glGetObjectLabel, NULL, 1088),
    NAME_FUNC_OFFSET(19633, glGetObjectPtrLabel, glGetObjectPtrLabel, NULL, 1089),
    NAME_FUNC_OFFSET(19653, glObjectLabel, glObjectLabel, NULL, 1090),
    NAME_FUNC_OFFSET(19667, glObjectPtrLabel, glObjectPtrLabel, NULL, 1091),
    NAME_FUNC_OFFSET(19684, glPopDebugGroup, glPopDebugGroup, NULL, 1092),
    NAME_FUNC_OFFSET(19700, glPushDebugGroup, glPushDebugGroup, NULL, 1093),
    NAME_FUNC_OFFSET(19717, glSecondaryColor3fEXT, glSecondaryColor3fEXT, NULL, 1094),
    NAME_FUNC_OFFSET(19739, glSecondaryColor3fvEXT, glSecondaryColor3fvEXT, NULL, 1095),
    NAME_FUNC_OFFSET(19762, glMultiDrawElementsEXT, glMultiDrawElementsEXT, NULL, 1096),
    NAME_FUNC_OFFSET(19785, glFogCoordfEXT, glFogCoordfEXT, NULL, 1097),
    NAME_FUNC_OFFSET(19800, glFogCoordfvEXT, glFogCoordfvEXT, NULL, 1098),
    NAME_FUNC_OFFSET(19816, glResizeBuffersMESA, glResizeBuffersMESA, NULL, 1099),
    NAME_FUNC_OFFSET(19836, glWindowPos4dMESA, glWindowPos4dMESA, NULL, 1100),
    NAME_FUNC_OFFSET(19854, glWindowPos4dvMESA, glWindowPos4dvMESA, NULL, 1101),
    NAME_FUNC_OFFSET(19873, glWindowPos4fMESA, glWindowPos4fMESA, NULL, 1102),
    NAME_FUNC_OFFSET(19891, glWindowPos4fvMESA, glWindowPos4fvMESA, NULL, 1103),
    NAME_FUNC_OFFSET(19910, glWindowPos4iMESA, glWindowPos4iMESA, NULL, 1104),
    NAME_FUNC_OFFSET(19928, glWindowPos4ivMESA, glWindowPos4ivMESA, NULL, 1105),
    NAME_FUNC_OFFSET(19947, glWindowPos4sMESA, glWindowPos4sMESA, NULL, 1106),
    NAME_FUNC_OFFSET(19965, glWindowPos4svMESA, glWindowPos4svMESA, NULL, 1107),
    NAME_FUNC_OFFSET(19984, gl_dispatch_stub_1108, gl_dispatch_stub_1108, NULL, 1108),
    NAME_FUNC_OFFSET(20009, gl_dispatch_stub_1109, gl_dispatch_stub_1109, NULL, 1109),
    NAME_FUNC_OFFSET(20036, glAreProgramsResidentNV, glAreProgramsResidentNV, NULL, 1110),
    NAME_FUNC_OFFSET(20060, glExecuteProgramNV, glExecuteProgramNV, NULL, 1111),
    NAME_FUNC_OFFSET(20079, glGetProgramParameterdvNV, glGetProgramParameterdvNV, NULL, 1112),
    NAME_FUNC_OFFSET(20105, glGetProgramParameterfvNV, glGetProgramParameterfvNV, NULL, 1113),
    NAME_FUNC_OFFSET(20131, glGetProgramStringNV, glGetProgramStringNV, NULL, 1114),
    NAME_FUNC_OFFSET(20152, glGetProgramivNV, glGetProgramivNV, NULL, 1115),
    NAME_FUNC_OFFSET(20169, glGetTrackMatrixivNV, glGetTrackMatrixivNV, NULL, 1116),
    NAME_FUNC_OFFSET(20190, glGetVertexAttribdvNV, glGetVertexAttribdvNV, NULL, 1117),
    NAME_FUNC_OFFSET(20212, glGetVertexAttribfvNV, glGetVertexAttribfvNV, NULL, 1118),
    NAME_FUNC_OFFSET(20234, glGetVertexAttribivNV, glGetVertexAttribivNV, NULL, 1119),
    NAME_FUNC_OFFSET(20256, glLoadProgramNV, glLoadProgramNV, NULL, 1120),
    NAME_FUNC_OFFSET(20272, glProgramParameters4dvNV, glProgramParameters4dvNV, NULL, 1121),
    NAME_FUNC_OFFSET(20297, glProgramParameters4fvNV, glProgramParameters4fvNV, NULL, 1122),
    NAME_FUNC_OFFSET(20322, glRequestResidentProgramsNV, glRequestResidentProgramsNV, NULL, 1123),
    NAME_FUNC_OFFSET(20350, glTrackMatrixNV, glTrackMatrixNV, NULL, 1124),
    NAME_FUNC_OFFSET(20366, glVertexAttrib1dNV, glVertexAttrib1dNV, NULL, 1125),
    NAME_FUNC_OFFSET(20385, glVertexAttrib1dvNV, glVertexAttrib1dvNV, NULL, 1126),
    NAME_FUNC_OFFSET(20405, glVertexAttrib1fNV, glVertexAttrib1fNV, NULL, 1127),
    NAME_FUNC_OFFSET(20424, glVertexAttrib1fvNV, glVertexAttrib1fvNV, NULL, 1128),
    NAME_FUNC_OFFSET(20444, glVertexAttrib1sNV, glVertexAttrib1sNV, NULL, 1129),
    NAME_FUNC_OFFSET(20463, glVertexAttrib1svNV, glVertexAttrib1svNV, NULL, 1130),
    NAME_FUNC_OFFSET(20483, glVertexAttrib2dNV, glVertexAttrib2dNV, NULL, 1131),
    NAME_FUNC_OFFSET(20502, glVertexAttrib2dvNV, glVertexAttrib2dvNV, NULL, 1132),
    NAME_FUNC_OFFSET(20522, glVertexAttrib2fNV, glVertexAttrib2fNV, NULL, 1133),
    NAME_FUNC_OFFSET(20541, glVertexAttrib2fvNV, glVertexAttrib2fvNV, NULL, 1134),
    NAME_FUNC_OFFSET(20561, glVertexAttrib2sNV, glVertexAttrib2sNV, NULL, 1135),
    NAME_FUNC_OFFSET(20580, glVertexAttrib2svNV, glVertexAttrib2svNV, NULL, 1136),
    NAME_FUNC_OFFSET(20600, glVertexAttrib3dNV, glVertexAttrib3dNV, NULL, 1137),
    NAME_FUNC_OFFSET(20619, glVertexAttrib3dvNV, glVertexAttrib3dvNV, NULL, 1138),
    NAME_FUNC_OFFSET(20639, glVertexAttrib3fNV, glVertexAttrib3fNV, NULL, 1139),
    NAME_FUNC_OFFSET(20658, glVertexAttrib3fvNV, glVertexAttrib3fvNV, NULL, 1140),
    NAME_FUNC_OFFSET(20678, glVertexAttrib3sNV, glVertexAttrib3sNV, NULL, 1141),
    NAME_FUNC_OFFSET(20697, glVertexAttrib3svNV, glVertexAttrib3svNV, NULL, 1142),
    NAME_FUNC_OFFSET(20717, glVertexAttrib4dNV, glVertexAttrib4dNV, NULL, 1143),
    NAME_FUNC_OFFSET(20736, glVertexAttrib4dvNV, glVertexAttrib4dvNV, NULL, 1144),
    NAME_FUNC_OFFSET(20756, glVertexAttrib4fNV, glVertexAttrib4fNV, NULL, 1145),
    NAME_FUNC_OFFSET(20775, glVertexAttrib4fvNV, glVertexAttrib4fvNV, NULL, 1146),
    NAME_FUNC_OFFSET(20795, glVertexAttrib4sNV, glVertexAttrib4sNV, NULL, 1147),
    NAME_FUNC_OFFSET(20814, glVertexAttrib4svNV, glVertexAttrib4svNV, NULL, 1148),
    NAME_FUNC_OFFSET(20834, glVertexAttrib4ubNV, glVertexAttrib4ubNV, NULL, 1149),
    NAME_FUNC_OFFSET(20854, glVertexAttrib4ubvNV, glVertexAttrib4ubvNV, NULL, 1150),
    NAME_FUNC_OFFSET(20875, glVertexAttribPointerNV, glVertexAttribPointerNV, NULL, 1151),
    NAME_FUNC_OFFSET(20899, glVertexAttribs1dvNV, glVertexAttribs1dvNV, NULL, 1152),
    NAME_FUNC_OFFSET(20920, glVertexAttribs1fvNV, glVertexAttribs1fvNV, NULL, 1153),
    NAME_FUNC_OFFSET(20941, glVertexAttribs1svNV, glVertexAttribs1svNV, NULL, 1154),
    NAME_FUNC_OFFSET(20962, glVertexAttribs2dvNV, glVertexAttribs2dvNV, NULL, 1155),
    NAME_FUNC_OFFSET(20983, glVertexAttribs2fvNV, glVertexAttribs2fvNV, NULL, 1156),
    NAME_FUNC_OFFSET(21004, glVertexAttribs2svNV, glVertexAttribs2svNV, NULL, 1157),
    NAME_FUNC_OFFSET(21025, glVertexAttribs3dvNV, glVertexAttribs3dvNV, NULL, 1158),
    NAME_FUNC_OFFSET(21046, glVertexAttribs3fvNV, glVertexAttribs3fvNV, NULL, 1159),
    NAME_FUNC_OFFSET(21067, glVertexAttribs3svNV, glVertexAttribs3svNV, NULL, 1160),
    NAME_FUNC_OFFSET(21088, glVertexAttribs4dvNV, glVertexAttribs4dvNV, NULL, 1161),
    NAME_FUNC_OFFSET(21109, glVertexAttribs4fvNV, glVertexAttribs4fvNV, NULL, 1162),
    NAME_FUNC_OFFSET(21130, glVertexAttribs4svNV, glVertexAttribs4svNV, NULL, 1163),
    NAME_FUNC_OFFSET(21151, glVertexAttribs4ubvNV, glVertexAttribs4ubvNV, NULL, 1164),
    NAME_FUNC_OFFSET(21173, glGetTexBumpParameterfvATI, glGetTexBumpParameterfvATI, NULL, 1165),
    NAME_FUNC_OFFSET(21200, glGetTexBumpParameterivATI, glGetTexBumpParameterivATI, NULL, 1166),
    NAME_FUNC_OFFSET(21227, glTexBumpParameterfvATI, glTexBumpParameterfvATI, NULL, 1167),
    NAME_FUNC_OFFSET(21251, glTexBumpParameterivATI, glTexBumpParameterivATI, NULL, 1168),
    NAME_FUNC_OFFSET(21275, glAlphaFragmentOp1ATI, glAlphaFragmentOp1ATI, NULL, 1169),
    NAME_FUNC_OFFSET(21297, glAlphaFragmentOp2ATI, glAlphaFragmentOp2ATI, NULL, 1170),
    NAME_FUNC_OFFSET(21319, glAlphaFragmentOp3ATI, glAlphaFragmentOp3ATI, NULL, 1171),
    NAME_FUNC_OFFSET(21341, glBeginFragmentShaderATI, glBeginFragmentShaderATI, NULL, 1172),
    NAME_FUNC_OFFSET(21366, glBindFragmentShaderATI, glBindFragmentShaderATI, NULL, 1173),
    NAME_FUNC_OFFSET(21390, glColorFragmentOp1ATI, glColorFragmentOp1ATI, NULL, 1174),
    NAME_FUNC_OFFSET(21412, glColorFragmentOp2ATI, glColorFragmentOp2ATI, NULL, 1175),
    NAME_FUNC_OFFSET(21434, glColorFragmentOp3ATI, glColorFragmentOp3ATI, NULL, 1176),
    NAME_FUNC_OFFSET(21456, glDeleteFragmentShaderATI, glDeleteFragmentShaderATI, NULL, 1177),
    NAME_FUNC_OFFSET(21482, glEndFragmentShaderATI, glEndFragmentShaderATI, NULL, 1178),
    NAME_FUNC_OFFSET(21505, glGenFragmentShadersATI, glGenFragmentShadersATI, NULL, 1179),
    NAME_FUNC_OFFSET(21529, glPassTexCoordATI, glPassTexCoordATI, NULL, 1180),
    NAME_FUNC_OFFSET(21547, glSampleMapATI, glSampleMapATI, NULL, 1181),
    NAME_FUNC_OFFSET(21562, glSetFragmentShaderConstantATI, glSetFragmentShaderConstantATI, NULL, 1182),
    NAME_FUNC_OFFSET(21593, gl_dispatch_stub_1183, gl_dispatch_stub_1183, NULL, 1183),
    NAME_FUNC_OFFSET(21616, gl_dispatch_stub_1184, gl_dispatch_stub_1184, NULL, 1184),
    NAME_FUNC_OFFSET(21639, gl_dispatch_stub_1185, gl_dispatch_stub_1185, NULL, 1185),
    NAME_FUNC_OFFSET(21662, glGetProgramNamedParameterdvNV, glGetProgramNamedParameterdvNV, NULL, 1186),
    NAME_FUNC_OFFSET(21693, glGetProgramNamedParameterfvNV, glGetProgramNamedParameterfvNV, NULL, 1187),
    NAME_FUNC_OFFSET(21724, glProgramNamedParameter4dNV, glProgramNamedParameter4dNV, NULL, 1188),
    NAME_FUNC_OFFSET(21752, glProgramNamedParameter4dvNV, glProgramNamedParameter4dvNV, NULL, 1189),
    NAME_FUNC_OFFSET(21781, glProgramNamedParameter4fNV, glProgramNamedParameter4fNV, NULL, 1190),
    NAME_FUNC_OFFSET(21809, glProgramNamedParameter4fvNV, glProgramNamedParameter4fvNV, NULL, 1191),
    NAME_FUNC_OFFSET(21838, glPrimitiveRestartNV, glPrimitiveRestartNV, NULL, 1192),
    NAME_FUNC_OFFSET(21859, gl_dispatch_stub_1193, gl_dispatch_stub_1193, NULL, 1193),
    NAME_FUNC_OFFSET(21876, gl_dispatch_stub_1194, gl_dispatch_stub_1194, NULL, 1194),
    NAME_FUNC_OFFSET(21889, gl_dispatch_stub_1195, gl_dispatch_stub_1195, NULL, 1195),
    NAME_FUNC_OFFSET(21903, gl_dispatch_stub_1196, gl_dispatch_stub_1196, NULL, 1196),
    NAME_FUNC_OFFSET(21920, glBindFramebufferEXT, glBindFramebufferEXT, NULL, 1197),
    NAME_FUNC_OFFSET(21941, glBindRenderbufferEXT, glBindRenderbufferEXT, NULL, 1198),
    NAME_FUNC_OFFSET(21963, gl_dispatch_stub_1199, gl_dispatch_stub_1199, NULL, 1199),
    NAME_FUNC_OFFSET(21987, gl_dispatch_stub_1200, gl_dispatch_stub_1200, NULL, 1200),
    NAME_FUNC_OFFSET(22017, glVertexAttribI1iEXT, glVertexAttribI1iEXT, NULL, 1201),
    NAME_FUNC_OFFSET(22038, glVertexAttribI1uiEXT, glVertexAttribI1uiEXT, NULL, 1202),
    NAME_FUNC_OFFSET(22060, glVertexAttribI2iEXT, glVertexAttribI2iEXT, NULL, 1203),
    NAME_FUNC_OFFSET(22081, glVertexAttribI2ivEXT, glVertexAttribI2ivEXT, NULL, 1204),
    NAME_FUNC_OFFSET(22103, glVertexAttribI2uiEXT, glVertexAttribI2uiEXT, NULL, 1205),
    NAME_FUNC_OFFSET(22125, glVertexAttribI2uivEXT, glVertexAttribI2uivEXT, NULL, 1206),
    NAME_FUNC_OFFSET(22148, glVertexAttribI3iEXT, glVertexAttribI3iEXT, NULL, 1207),
    NAME_FUNC_OFFSET(22169, glVertexAttribI3ivEXT, glVertexAttribI3ivEXT, NULL, 1208),
    NAME_FUNC_OFFSET(22191, glVertexAttribI3uiEXT, glVertexAttribI3uiEXT, NULL, 1209),
    NAME_FUNC_OFFSET(22213, glVertexAttribI3uivEXT, glVertexAttribI3uivEXT, NULL, 1210),
    NAME_FUNC_OFFSET(22236, glVertexAttribI4iEXT, glVertexAttribI4iEXT, NULL, 1211),
    NAME_FUNC_OFFSET(22257, glVertexAttribI4ivEXT, glVertexAttribI4ivEXT, NULL, 1212),
    NAME_FUNC_OFFSET(22279, glVertexAttribI4uiEXT, glVertexAttribI4uiEXT, NULL, 1213),
    NAME_FUNC_OFFSET(22301, glVertexAttribI4uivEXT, glVertexAttribI4uivEXT, NULL, 1214),
    NAME_FUNC_OFFSET(22324, glClearColorIiEXT, glClearColorIiEXT, NULL, 1215),
    NAME_FUNC_OFFSET(22342, glClearColorIuiEXT, glClearColorIuiEXT, NULL, 1216),
    NAME_FUNC_OFFSET(22361, glBindBufferOffsetEXT, glBindBufferOffsetEXT, NULL, 1217),
    NAME_FUNC_OFFSET(22383, glBeginPerfMonitorAMD, glBeginPerfMonitorAMD, NULL, 1218),
    NAME_FUNC_OFFSET(22405, glDeletePerfMonitorsAMD, glDeletePerfMonitorsAMD, NULL, 1219),
    NAME_FUNC_OFFSET(22429, glEndPerfMonitorAMD, glEndPerfMonitorAMD, NULL, 1220),
    NAME_FUNC_OFFSET(22449, glGenPerfMonitorsAMD, glGenPerfMonitorsAMD, NULL, 1221),
    NAME_FUNC_OFFSET(22470, glGetPerfMonitorCounterDataAMD, glGetPerfMonitorCounterDataAMD, NULL, 1222),
    NAME_FUNC_OFFSET(22501, glGetPerfMonitorCounterInfoAMD, glGetPerfMonitorCounterInfoAMD, NULL, 1223),
    NAME_FUNC_OFFSET(22532, glGetPerfMonitorCounterStringAMD, glGetPerfMonitorCounterStringAMD, NULL, 1224),
    NAME_FUNC_OFFSET(22565, glGetPerfMonitorCountersAMD, glGetPerfMonitorCountersAMD, NULL, 1225),
    NAME_FUNC_OFFSET(22593, glGetPerfMonitorGroupStringAMD, glGetPerfMonitorGroupStringAMD, NULL, 1226),
    NAME_FUNC_OFFSET(22624, glGetPerfMonitorGroupsAMD, glGetPerfMonitorGroupsAMD, NULL, 1227),
    NAME_FUNC_OFFSET(22650, glSelectPerfMonitorCountersAMD, glSelectPerfMonitorCountersAMD, NULL, 1228),
    NAME_FUNC_OFFSET(22681, glGetObjectParameterivAPPLE, glGetObjectParameterivAPPLE, NULL, 1229),
    NAME_FUNC_OFFSET(22709, glObjectPurgeableAPPLE, glObjectPurgeableAPPLE, NULL, 1230),
    NAME_FUNC_OFFSET(22732, glObjectUnpurgeableAPPLE, glObjectUnpurgeableAPPLE, NULL, 1231),
    NAME_FUNC_OFFSET(22757, glActiveProgramEXT, glActiveProgramEXT, NULL, 1232),
    NAME_FUNC_OFFSET(22776, glCreateShaderProgramEXT, glCreateShaderProgramEXT, NULL, 1233),
    NAME_FUNC_OFFSET(22801, glUseShaderProgramEXT, glUseShaderProgramEXT, NULL, 1234),
    NAME_FUNC_OFFSET(22823, glTextureBarrierNV, glTextureBarrierNV, NULL, 1235),
    NAME_FUNC_OFFSET(22842, glVDPAUFiniNV, glVDPAUFiniNV, NULL, 1236),
    NAME_FUNC_OFFSET(22856, glVDPAUGetSurfaceivNV, glVDPAUGetSurfaceivNV, NULL, 1237),
    NAME_FUNC_OFFSET(22878, glVDPAUInitNV, glVDPAUInitNV, NULL, 1238),
    NAME_FUNC_OFFSET(22892, glVDPAUIsSurfaceNV, glVDPAUIsSurfaceNV, NULL, 1239),
    NAME_FUNC_OFFSET(22911, glVDPAUMapSurfacesNV, glVDPAUMapSurfacesNV, NULL, 1240),
    NAME_FUNC_OFFSET(22932, glVDPAURegisterOutputSurfaceNV, glVDPAURegisterOutputSurfaceNV, NULL, 1241),
    NAME_FUNC_OFFSET(22963, glVDPAURegisterVideoSurfaceNV, glVDPAURegisterVideoSurfaceNV, NULL, 1242),
    NAME_FUNC_OFFSET(22993, glVDPAUSurfaceAccessNV, glVDPAUSurfaceAccessNV, NULL, 1243),
    NAME_FUNC_OFFSET(23016, glVDPAUUnmapSurfacesNV, glVDPAUUnmapSurfacesNV, NULL, 1244),
    NAME_FUNC_OFFSET(23039, glVDPAUUnregisterSurfaceNV, glVDPAUUnregisterSurfaceNV, NULL, 1245),
    NAME_FUNC_OFFSET(23066, gl_dispatch_stub_1246, gl_dispatch_stub_1246, NULL, 1246),
    NAME_FUNC_OFFSET(23088, gl_dispatch_stub_1247, gl_dispatch_stub_1247, NULL, 1247),
    NAME_FUNC_OFFSET(23111, gl_dispatch_stub_1248, gl_dispatch_stub_1248, NULL, 1248),
    NAME_FUNC_OFFSET(23134, gl_dispatch_stub_1249, gl_dispatch_stub_1249, NULL, 1249),
    NAME_FUNC_OFFSET(23154, gl_dispatch_stub_1250, gl_dispatch_stub_1250, NULL, 1250),
    NAME_FUNC_OFFSET(23181, gl_dispatch_stub_1251, gl_dispatch_stub_1251, NULL, 1251),
    NAME_FUNC_OFFSET(23207, gl_dispatch_stub_1252, gl_dispatch_stub_1252, NULL, 1252),
    NAME_FUNC_OFFSET(23233, gl_dispatch_stub_1253, gl_dispatch_stub_1253, NULL, 1253),
    NAME_FUNC_OFFSET(23257, gl_dispatch_stub_1254, gl_dispatch_stub_1254, NULL, 1254),
    NAME_FUNC_OFFSET(23285, gl_dispatch_stub_1255, gl_dispatch_stub_1255, NULL, 1255),
    NAME_FUNC_OFFSET(23309, glPolygonOffsetClampEXT, glPolygonOffsetClampEXT, NULL, 1256),
    NAME_FUNC_OFFSET(23333, gl_dispatch_stub_1257, gl_dispatch_stub_1257, NULL, 1257),
    NAME_FUNC_OFFSET(23358, gl_dispatch_stub_1258, gl_dispatch_stub_1258, NULL, 1258),
    NAME_FUNC_OFFSET(23387, gl_dispatch_stub_1259, gl_dispatch_stub_1259, NULL, 1259),
    NAME_FUNC_OFFSET(23418, glEGLImageTargetRenderbufferStorageOES, glEGLImageTargetRenderbufferStorageOES, NULL, 1260),
    NAME_FUNC_OFFSET(23457, glEGLImageTargetTexture2DOES, glEGLImageTargetTexture2DOES, NULL, 1261),
    NAME_FUNC_OFFSET(23486, glAlphaFuncx, glAlphaFuncx, NULL, 1262),
    NAME_FUNC_OFFSET(23499, glClearColorx, glClearColorx, NULL, 1263),
    NAME_FUNC_OFFSET(23513, glClearDepthx, glClearDepthx, NULL, 1264),
    NAME_FUNC_OFFSET(23527, glColor4x, glColor4x, NULL, 1265),
    NAME_FUNC_OFFSET(23537, glDepthRangex, glDepthRangex, NULL, 1266),
    NAME_FUNC_OFFSET(23551, glFogx, glFogx, NULL, 1267),
    NAME_FUNC_OFFSET(23558, glFogxv, glFogxv, NULL, 1268),
    NAME_FUNC_OFFSET(23566, glFrustumf, glFrustumf, NULL, 1269),
    NAME_FUNC_OFFSET(23577, glFrustumx, glFrustumx, NULL, 1270),
    NAME_FUNC_OFFSET(23588, glLightModelx, glLightModelx, NULL, 1271),
    NAME_FUNC_OFFSET(23602, glLightModelxv, glLightModelxv, NULL, 1272),
    NAME_FUNC_OFFSET(23617, glLightx, glLightx, NULL, 1273),
    NAME_FUNC_OFFSET(23626, glLightxv, glLightxv, NULL, 1274),
    NAME_FUNC_OFFSET(23636, glLineWidthx, glLineWidthx, NULL, 1275),
    NAME_FUNC_OFFSET(23649, glLoadMatrixx, glLoadMatrixx, NULL, 1276),
    NAME_FUNC_OFFSET(23663, glMaterialx, glMaterialx, NULL, 1277),
    NAME_FUNC_OFFSET(23675, glMaterialxv, glMaterialxv, NULL, 1278),
    NAME_FUNC_OFFSET(23688, glMultMatrixx, glMultMatrixx, NULL, 1279),
    NAME_FUNC_OFFSET(23702, glMultiTexCoord4x, glMultiTexCoord4x, NULL, 1280),
    NAME_FUNC_OFFSET(23720, glNormal3x, glNormal3x, NULL, 1281),
    NAME_FUNC_OFFSET(23731, glOrthof, glOrthof, NULL, 1282),
    NAME_FUNC_OFFSET(23740, glOrthox, glOrthox, NULL, 1283),
    NAME_FUNC_OFFSET(23749, glPointSizex, glPointSizex, NULL, 1284),
    NAME_FUNC_OFFSET(23762, glPolygonOffsetx, glPolygonOffsetx, NULL, 1285),
    NAME_FUNC_OFFSET(23779, glRotatex, glRotatex, NULL, 1286),
    NAME_FUNC_OFFSET(23789, glSampleCoveragex, glSampleCoveragex, NULL, 1287),
    NAME_FUNC_OFFSET(23807, glScalex, glScalex, NULL, 1288),
    NAME_FUNC_OFFSET(23816, glTexEnvx, glTexEnvx, NULL, 1289),
    NAME_FUNC_OFFSET(23826, glTexEnvxv, glTexEnvxv, NULL, 1290),
    NAME_FUNC_OFFSET(23837, glTexParameterx, glTexParameterx, NULL, 1291),
    NAME_FUNC_OFFSET(23853, glTranslatex, glTranslatex, NULL, 1292),
    NAME_FUNC_OFFSET(23866, glClipPlanef, glClipPlanef, NULL, 1293),
    NAME_FUNC_OFFSET(23879, glClipPlanex, glClipPlanex, NULL, 1294),
    NAME_FUNC_OFFSET(23892, glGetClipPlanef, glGetClipPlanef, NULL, 1295),
    NAME_FUNC_OFFSET(23908, glGetClipPlanex, glGetClipPlanex, NULL, 1296),
    NAME_FUNC_OFFSET(23924, glGetFixedv, glGetFixedv, NULL, 1297),
    NAME_FUNC_OFFSET(23936, glGetLightxv, glGetLightxv, NULL, 1298),
    NAME_FUNC_OFFSET(23949, glGetMaterialxv, glGetMaterialxv, NULL, 1299),
    NAME_FUNC_OFFSET(23965, glGetTexEnvxv, glGetTexEnvxv, NULL, 1300),
    NAME_FUNC_OFFSET(23979, glGetTexParameterxv, glGetTexParameterxv, NULL, 1301),
    NAME_FUNC_OFFSET(23999, glPointParameterx, glPointParameterx, NULL, 1302),
    NAME_FUNC_OFFSET(24017, glPointParameterxv, glPointParameterxv, NULL, 1303),
    NAME_FUNC_OFFSET(24036, glTexParameterxv, glTexParameterxv, NULL, 1304),
    NAME_FUNC_OFFSET(24053, glTexGenf, glTexGenf, NULL, 190),
    NAME_FUNC_OFFSET(24066, glTexGenfv, glTexGenfv, NULL, 191),
    NAME_FUNC_OFFSET(24080, glTexGeni, glTexGeni, NULL, 192),
    NAME_FUNC_OFFSET(24093, glTexGeniv, glTexGeniv, NULL, 193),
    NAME_FUNC_OFFSET(24107, glReadBuffer, glReadBuffer, NULL, 254),
    NAME_FUNC_OFFSET(24122, glGetTexGenfv, glGetTexGenfv, NULL, 279),
    NAME_FUNC_OFFSET(24139, glGetTexGeniv, glGetTexGeniv, NULL, 280),
    NAME_FUNC_OFFSET(24156, glArrayElement, glArrayElement, NULL, 306),
    NAME_FUNC_OFFSET(24174, glBindTexture, glBindTexture, NULL, 307),
    NAME_FUNC_OFFSET(24191, glDrawArrays, glDrawArrays, NULL, 310),
    NAME_FUNC_OFFSET(24207, glAreTexturesResident, glAreTexturesResidentEXT, glAreTexturesResidentEXT, 322),
    NAME_FUNC_OFFSET(24232, glCopyTexImage1D, glCopyTexImage1D, NULL, 323),
    NAME_FUNC_OFFSET(24252, glCopyTexImage2D, glCopyTexImage2D, NULL, 324),
    NAME_FUNC_OFFSET(24272, glCopyTexSubImage1D, glCopyTexSubImage1D, NULL, 325),
    NAME_FUNC_OFFSET(24295, glCopyTexSubImage2D, glCopyTexSubImage2D, NULL, 326),
    NAME_FUNC_OFFSET(24318, glDeleteTextures, glDeleteTexturesEXT, glDeleteTexturesEXT, 327),
    NAME_FUNC_OFFSET(24338, glGenTextures, glGenTexturesEXT, glGenTexturesEXT, 328),
    NAME_FUNC_OFFSET(24355, glGetPointerv, glGetPointerv, NULL, 329),
    NAME_FUNC_OFFSET(24372, glIsTexture, glIsTextureEXT, glIsTextureEXT, 330),
    NAME_FUNC_OFFSET(24387, glPrioritizeTextures, glPrioritizeTextures, NULL, 331),
    NAME_FUNC_OFFSET(24411, glTexSubImage1D, glTexSubImage1D, NULL, 332),
    NAME_FUNC_OFFSET(24430, glTexSubImage2D, glTexSubImage2D, NULL, 333),
    NAME_FUNC_OFFSET(24449, glBlendColor, glBlendColor, NULL, 336),
    NAME_FUNC_OFFSET(24465, glBlendEquation, glBlendEquation, NULL, 337),
    NAME_FUNC_OFFSET(24484, glBlendEquation, glBlendEquation, NULL, 337),
    NAME_FUNC_OFFSET(24503, glDrawRangeElements, glDrawRangeElements, NULL, 338),
    NAME_FUNC_OFFSET(24526, glColorTable, glColorTable, NULL, 339),
    NAME_FUNC_OFFSET(24542, glColorTable, glColorTable, NULL, 339),
    NAME_FUNC_OFFSET(24558, glColorTableParameterfv, glColorTableParameterfv, NULL, 340),
    NAME_FUNC_OFFSET(24585, glColorTableParameteriv, glColorTableParameteriv, NULL, 341),
    NAME_FUNC_OFFSET(24612, glCopyColorTable, glCopyColorTable, NULL, 342),
    NAME_FUNC_OFFSET(24632, glGetColorTable, glGetColorTableEXT, glGetColorTableEXT, 343),
    NAME_FUNC_OFFSET(24651, glGetColorTable, glGetColorTableEXT, glGetColorTableEXT, 343),
    NAME_FUNC_OFFSET(24670, glGetColorTableParameterfv, glGetColorTableParameterfvEXT, glGetColorTableParameterfvEXT, 344),
    NAME_FUNC_OFFSET(24700, glGetColorTableParameterfv, glGetColorTableParameterfvEXT, glGetColorTableParameterfvEXT, 344),
    NAME_FUNC_OFFSET(24730, glGetColorTableParameteriv, glGetColorTableParameterivEXT, glGetColorTableParameterivEXT, 345),
    NAME_FUNC_OFFSET(24760, glGetColorTableParameteriv, glGetColorTableParameterivEXT, glGetColorTableParameterivEXT, 345),
    NAME_FUNC_OFFSET(24790, glColorSubTable, glColorSubTable, NULL, 346),
    NAME_FUNC_OFFSET(24809, glCopyColorSubTable, glCopyColorSubTable, NULL, 347),
    NAME_FUNC_OFFSET(24832, glConvolutionFilter1D, glConvolutionFilter1D, NULL, 348),
    NAME_FUNC_OFFSET(24857, glConvolutionFilter2D, glConvolutionFilter2D, NULL, 349),
    NAME_FUNC_OFFSET(24882, glConvolutionParameterf, glConvolutionParameterf, NULL, 350),
    NAME_FUNC_OFFSET(24909, glConvolutionParameterfv, glConvolutionParameterfv, NULL, 351),
    NAME_FUNC_OFFSET(24937, glConvolutionParameteri, glConvolutionParameteri, NULL, 352),
    NAME_FUNC_OFFSET(24964, glConvolutionParameteriv, glConvolutionParameteriv, NULL, 353),
    NAME_FUNC_OFFSET(24992, glCopyConvolutionFilter1D, glCopyConvolutionFilter1D, NULL, 354),
    NAME_FUNC_OFFSET(25021, glCopyConvolutionFilter2D, glCopyConvolutionFilter2D, NULL, 355),
    NAME_FUNC_OFFSET(25050, glGetConvolutionFilter, gl_dispatch_stub_356, gl_dispatch_stub_356, 356),
    NAME_FUNC_OFFSET(25076, glGetConvolutionParameterfv, gl_dispatch_stub_357, gl_dispatch_stub_357, 357),
    NAME_FUNC_OFFSET(25107, glGetConvolutionParameteriv, gl_dispatch_stub_358, gl_dispatch_stub_358, 358),
    NAME_FUNC_OFFSET(25138, glGetSeparableFilter, gl_dispatch_stub_359, gl_dispatch_stub_359, 359),
    NAME_FUNC_OFFSET(25162, glSeparableFilter2D, glSeparableFilter2D, NULL, 360),
    NAME_FUNC_OFFSET(25185, glGetHistogram, gl_dispatch_stub_361, gl_dispatch_stub_361, 361),
    NAME_FUNC_OFFSET(25203, glGetHistogramParameterfv, gl_dispatch_stub_362, gl_dispatch_stub_362, 362),
    NAME_FUNC_OFFSET(25232, glGetHistogramParameteriv, gl_dispatch_stub_363, gl_dispatch_stub_363, 363),
    NAME_FUNC_OFFSET(25261, glGetMinmax, gl_dispatch_stub_364, gl_dispatch_stub_364, 364),
    NAME_FUNC_OFFSET(25276, glGetMinmaxParameterfv, gl_dispatch_stub_365, gl_dispatch_stub_365, 365),
    NAME_FUNC_OFFSET(25302, glGetMinmaxParameteriv, gl_dispatch_stub_366, gl_dispatch_stub_366, 366),
    NAME_FUNC_OFFSET(25328, glHistogram, glHistogram, NULL, 367),
    NAME_FUNC_OFFSET(25343, glMinmax, glMinmax, NULL, 368),
    NAME_FUNC_OFFSET(25355, glResetHistogram, glResetHistogram, NULL, 369),
    NAME_FUNC_OFFSET(25375, glResetMinmax, glResetMinmax, NULL, 370),
    NAME_FUNC_OFFSET(25392, glTexImage3D, glTexImage3D, NULL, 371),
    NAME_FUNC_OFFSET(25408, glTexImage3D, glTexImage3D, NULL, 371),
    NAME_FUNC_OFFSET(25424, glTexSubImage3D, glTexSubImage3D, NULL, 372),
    NAME_FUNC_OFFSET(25443, glTexSubImage3D, glTexSubImage3D, NULL, 372),
    NAME_FUNC_OFFSET(25462, glCopyTexSubImage3D, glCopyTexSubImage3D, NULL, 373),
    NAME_FUNC_OFFSET(25485, glCopyTexSubImage3D, glCopyTexSubImage3D, NULL, 373),
    NAME_FUNC_OFFSET(25508, glActiveTexture, glActiveTexture, NULL, 374),
    NAME_FUNC_OFFSET(25527, glClientActiveTexture, glClientActiveTexture, NULL, 375),
    NAME_FUNC_OFFSET(25552, glMultiTexCoord1d, glMultiTexCoord1d, NULL, 376),
    NAME_FUNC_OFFSET(25573, glMultiTexCoord1dv, glMultiTexCoord1dv, NULL, 377),
    NAME_FUNC_OFFSET(25595, glMultiTexCoord1fARB, glMultiTexCoord1fARB, NULL, 378),
    NAME_FUNC_OFFSET(25613, glMultiTexCoord1fvARB, glMultiTexCoord1fvARB, NULL, 379),
    NAME_FUNC_OFFSET(25632, glMultiTexCoord1i, glMultiTexCoord1i, NULL, 380),
    NAME_FUNC_OFFSET(25653, glMultiTexCoord1iv, glMultiTexCoord1iv, NULL, 381),
    NAME_FUNC_OFFSET(25675, glMultiTexCoord1s, glMultiTexCoord1s, NULL, 382),
    NAME_FUNC_OFFSET(25696, glMultiTexCoord1sv, glMultiTexCoord1sv, NULL, 383),
    NAME_FUNC_OFFSET(25718, glMultiTexCoord2d, glMultiTexCoord2d, NULL, 384),
    NAME_FUNC_OFFSET(25739, glMultiTexCoord2dv, glMultiTexCoord2dv, NULL, 385),
    NAME_FUNC_OFFSET(25761, glMultiTexCoord2fARB, glMultiTexCoord2fARB, NULL, 386),
    NAME_FUNC_OFFSET(25779, glMultiTexCoord2fvARB, glMultiTexCoord2fvARB, NULL, 387),
    NAME_FUNC_OFFSET(25798, glMultiTexCoord2i, glMultiTexCoord2i, NULL, 388),
    NAME_FUNC_OFFSET(25819, glMultiTexCoord2iv, glMultiTexCoord2iv, NULL, 389),
    NAME_FUNC_OFFSET(25841, glMultiTexCoord2s, glMultiTexCoord2s, NULL, 390),
    NAME_FUNC_OFFSET(25862, glMultiTexCoord2sv, glMultiTexCoord2sv, NULL, 391),
    NAME_FUNC_OFFSET(25884, glMultiTexCoord3d, glMultiTexCoord3d, NULL, 392),
    NAME_FUNC_OFFSET(25905, glMultiTexCoord3dv, glMultiTexCoord3dv, NULL, 393),
    NAME_FUNC_OFFSET(25927, glMultiTexCoord3fARB, glMultiTexCoord3fARB, NULL, 394),
    NAME_FUNC_OFFSET(25945, glMultiTexCoord3fvARB, glMultiTexCoord3fvARB, NULL, 395),
    NAME_FUNC_OFFSET(25964, glMultiTexCoord3i, glMultiTexCoord3i, NULL, 396),
    NAME_FUNC_OFFSET(25985, glMultiTexCoord3iv, glMultiTexCoord3iv, NULL, 397),
    NAME_FUNC_OFFSET(26007, glMultiTexCoord3s, glMultiTexCoord3s, NULL, 398),
    NAME_FUNC_OFFSET(26028, glMultiTexCoord3sv, glMultiTexCoord3sv, NULL, 399),
    NAME_FUNC_OFFSET(26050, glMultiTexCoord4d, glMultiTexCoord4d, NULL, 400),
    NAME_FUNC_OFFSET(26071, glMultiTexCoord4dv, glMultiTexCoord4dv, NULL, 401),
    NAME_FUNC_OFFSET(26093, glMultiTexCoord4fARB, glMultiTexCoord4fARB, NULL, 402),
    NAME_FUNC_OFFSET(26111, glMultiTexCoord4fvARB, glMultiTexCoord4fvARB, NULL, 403),
    NAME_FUNC_OFFSET(26130, glMultiTexCoord4i, glMultiTexCoord4i, NULL, 404),
    NAME_FUNC_OFFSET(26151, glMultiTexCoord4iv, glMultiTexCoord4iv, NULL, 405),
    NAME_FUNC_OFFSET(26173, glMultiTexCoord4s, glMultiTexCoord4s, NULL, 406),
    NAME_FUNC_OFFSET(26194, glMultiTexCoord4sv, glMultiTexCoord4sv, NULL, 407),
    NAME_FUNC_OFFSET(26216, glCompressedTexImage1D, glCompressedTexImage1D, NULL, 408),
    NAME_FUNC_OFFSET(26242, glCompressedTexImage2D, glCompressedTexImage2D, NULL, 409),
    NAME_FUNC_OFFSET(26268, glCompressedTexImage3D, glCompressedTexImage3D, NULL, 410),
    NAME_FUNC_OFFSET(26294, glCompressedTexImage3D, glCompressedTexImage3D, NULL, 410),
    NAME_FUNC_OFFSET(26320, glCompressedTexSubImage1D, glCompressedTexSubImage1D, NULL, 411),
    NAME_FUNC_OFFSET(26349, glCompressedTexSubImage2D, glCompressedTexSubImage2D, NULL, 412),
    NAME_FUNC_OFFSET(26378, glCompressedTexSubImage3D, glCompressedTexSubImage3D, NULL, 413),
    NAME_FUNC_OFFSET(26407, glCompressedTexSubImage3D, glCompressedTexSubImage3D, NULL, 413),
    NAME_FUNC_OFFSET(26436, glGetCompressedTexImage, glGetCompressedTexImage, NULL, 414),
    NAME_FUNC_OFFSET(26463, glLoadTransposeMatrixd, glLoadTransposeMatrixd, NULL, 415),
    NAME_FUNC_OFFSET(26489, glLoadTransposeMatrixf, glLoadTransposeMatrixf, NULL, 416),
    NAME_FUNC_OFFSET(26515, glMultTransposeMatrixd, glMultTransposeMatrixd, NULL, 417),
    NAME_FUNC_OFFSET(26541, glMultTransposeMatrixf, glMultTransposeMatrixf, NULL, 418),
    NAME_FUNC_OFFSET(26567, glSampleCoverage, glSampleCoverage, NULL, 419),
    NAME_FUNC_OFFSET(26587, glBlendFuncSeparate, glBlendFuncSeparate, NULL, 420),
    NAME_FUNC_OFFSET(26610, glBlendFuncSeparate, glBlendFuncSeparate, NULL, 420),
    NAME_FUNC_OFFSET(26634, glBlendFuncSeparate, glBlendFuncSeparate, NULL, 420),
    NAME_FUNC_OFFSET(26657, glFogCoordPointer, glFogCoordPointer, NULL, 421),
    NAME_FUNC_OFFSET(26678, glFogCoordd, glFogCoordd, NULL, 422),
    NAME_FUNC_OFFSET(26693, glFogCoorddv, glFogCoorddv, NULL, 423),
    NAME_FUNC_OFFSET(26709, glMultiDrawArrays, glMultiDrawArrays, NULL, 424),
    NAME_FUNC_OFFSET(26730, glPointParameterf, glPointParameterf, NULL, 425),
    NAME_FUNC_OFFSET(26751, glPointParameterf, glPointParameterf, NULL, 425),
    NAME_FUNC_OFFSET(26772, glPointParameterf, glPointParameterf, NULL, 425),
    NAME_FUNC_OFFSET(26794, glPointParameterfv, glPointParameterfv, NULL, 426),
    NAME_FUNC_OFFSET(26816, glPointParameterfv, glPointParameterfv, NULL, 426),
    NAME_FUNC_OFFSET(26838, glPointParameterfv, glPointParameterfv, NULL, 426),
    NAME_FUNC_OFFSET(26861, glPointParameteri, glPointParameteri, NULL, 427),
    NAME_FUNC_OFFSET(26881, glPointParameteriv, glPointParameteriv, NULL, 428),
    NAME_FUNC_OFFSET(26902, glSecondaryColor3b, glSecondaryColor3b, NULL, 429),
    NAME_FUNC_OFFSET(26924, glSecondaryColor3bv, glSecondaryColor3bv, NULL, 430),
    NAME_FUNC_OFFSET(26947, glSecondaryColor3d, glSecondaryColor3d, NULL, 431),
    NAME_FUNC_OFFSET(26969, glSecondaryColor3dv, glSecondaryColor3dv, NULL, 432),
    NAME_FUNC_OFFSET(26992, glSecondaryColor3i, glSecondaryColor3i, NULL, 433),
    NAME_FUNC_OFFSET(27014, glSecondaryColor3iv, glSecondaryColor3iv, NULL, 434),
    NAME_FUNC_OFFSET(27037, glSecondaryColor3s, glSecondaryColor3s, NULL, 435),
    NAME_FUNC_OFFSET(27059, glSecondaryColor3sv, glSecondaryColor3sv, NULL, 436),
    NAME_FUNC_OFFSET(27082, glSecondaryColor3ub, glSecondaryColor3ub, NULL, 437),
    NAME_FUNC_OFFSET(27105, glSecondaryColor3ubv, glSecondaryColor3ubv, NULL, 438),
    NAME_FUNC_OFFSET(27129, glSecondaryColor3ui, glSecondaryColor3ui, NULL, 439),
    NAME_FUNC_OFFSET(27152, glSecondaryColor3uiv, glSecondaryColor3uiv, NULL, 440),
    NAME_FUNC_OFFSET(27176, glSecondaryColor3us, glSecondaryColor3us, NULL, 441),
    NAME_FUNC_OFFSET(27199, glSecondaryColor3usv, glSecondaryColor3usv, NULL, 442),
    NAME_FUNC_OFFSET(27223, glSecondaryColorPointer, glSecondaryColorPointer, NULL, 443),
    NAME_FUNC_OFFSET(27250, glWindowPos2d, glWindowPos2d, NULL, 444),
    NAME_FUNC_OFFSET(27267, glWindowPos2d, glWindowPos2d, NULL, 444),
    NAME_FUNC_OFFSET(27285, glWindowPos2dv, glWindowPos2dv, NULL, 445),
    NAME_FUNC_OFFSET(27303, glWindowPos2dv, glWindowPos2dv, NULL, 445),
    NAME_FUNC_OFFSET(27322, glWindowPos2f, glWindowPos2f, NULL, 446),
    NAME_FUNC_OFFSET(27339, glWindowPos2f, glWindowPos2f, NULL, 446),
    NAME_FUNC_OFFSET(27357, glWindowPos2fv, glWindowPos2fv, NULL, 447),
    NAME_FUNC_OFFSET(27375, glWindowPos2fv, glWindowPos2fv, NULL, 447),
    NAME_FUNC_OFFSET(27394, glWindowPos2i, glWindowPos2i, NULL, 448),
    NAME_FUNC_OFFSET(27411, glWindowPos2i, glWindowPos2i, NULL, 448),
    NAME_FUNC_OFFSET(27429, glWindowPos2iv, glWindowPos2iv, NULL, 449),
    NAME_FUNC_OFFSET(27447, glWindowPos2iv, glWindowPos2iv, NULL, 449),
    NAME_FUNC_OFFSET(27466, glWindowPos2s, glWindowPos2s, NULL, 450),
    NAME_FUNC_OFFSET(27483, glWindowPos2s, glWindowPos2s, NULL, 450),
    NAME_FUNC_OFFSET(27501, glWindowPos2sv, glWindowPos2sv, NULL, 451),
    NAME_FUNC_OFFSET(27519, glWindowPos2sv, glWindowPos2sv, NULL, 451),
    NAME_FUNC_OFFSET(27538, glWindowPos3d, glWindowPos3d, NULL, 452),
    NAME_FUNC_OFFSET(27555, glWindowPos3d, glWindowPos3d, NULL, 452),
    NAME_FUNC_OFFSET(27573, glWindowPos3dv, glWindowPos3dv, NULL, 453),
    NAME_FUNC_OFFSET(27591, glWindowPos3dv, glWindowPos3dv, NULL, 453),
    NAME_FUNC_OFFSET(27610, glWindowPos3f, glWindowPos3f, NULL, 454),
    NAME_FUNC_OFFSET(27627, glWindowPos3f, glWindowPos3f, NULL, 454),
    NAME_FUNC_OFFSET(27645, glWindowPos3fv, glWindowPos3fv, NULL, 455),
    NAME_FUNC_OFFSET(27663, glWindowPos3fv, glWindowPos3fv, NULL, 455),
    NAME_FUNC_OFFSET(27682, glWindowPos3i, glWindowPos3i, NULL, 456),
    NAME_FUNC_OFFSET(27699, glWindowPos3i, glWindowPos3i, NULL, 456),
    NAME_FUNC_OFFSET(27717, glWindowPos3iv, glWindowPos3iv, NULL, 457),
    NAME_FUNC_OFFSET(27735, glWindowPos3iv, glWindowPos3iv, NULL, 457),
    NAME_FUNC_OFFSET(27754, glWindowPos3s, glWindowPos3s, NULL, 458),
    NAME_FUNC_OFFSET(27771, glWindowPos3s, glWindowPos3s, NULL, 458),
    NAME_FUNC_OFFSET(27789, glWindowPos3sv, glWindowPos3sv, NULL, 459),
    NAME_FUNC_OFFSET(27807, glWindowPos3sv, glWindowPos3sv, NULL, 459),
    NAME_FUNC_OFFSET(27826, glBeginQuery, glBeginQuery, NULL, 460),
    NAME_FUNC_OFFSET(27842, glBindBuffer, glBindBuffer, NULL, 461),
    NAME_FUNC_OFFSET(27858, glBufferData, glBufferData, NULL, 462),
    NAME_FUNC_OFFSET(27874, glBufferSubData, glBufferSubData, NULL, 463),
    NAME_FUNC_OFFSET(27893, glDeleteBuffers, glDeleteBuffers, NULL, 464),
    NAME_FUNC_OFFSET(27912, glDeleteQueries, glDeleteQueries, NULL, 465),
    NAME_FUNC_OFFSET(27931, glEndQuery, glEndQuery, NULL, 466),
    NAME_FUNC_OFFSET(27945, glGenBuffers, glGenBuffers, NULL, 467),
    NAME_FUNC_OFFSET(27961, glGenQueries, glGenQueries, NULL, 468),
    NAME_FUNC_OFFSET(27977, glGetBufferParameteriv, glGetBufferParameteriv, NULL, 469),
    NAME_FUNC_OFFSET(28003, glGetBufferPointerv, glGetBufferPointerv, NULL, 470),
    NAME_FUNC_OFFSET(28026, glGetBufferPointerv, glGetBufferPointerv, NULL, 470),
    NAME_FUNC_OFFSET(28049, glGetBufferSubData, glGetBufferSubData, NULL, 471),
    NAME_FUNC_OFFSET(28071, glGetQueryObjectiv, glGetQueryObjectiv, NULL, 472),
    NAME_FUNC_OFFSET(28093, glGetQueryObjectuiv, glGetQueryObjectuiv, NULL, 473),
    NAME_FUNC_OFFSET(28116, glGetQueryiv, glGetQueryiv, NULL, 474),
    NAME_FUNC_OFFSET(28132, glIsBuffer, glIsBuffer, NULL, 475),
    NAME_FUNC_OFFSET(28146, glIsQuery, glIsQuery, NULL, 476),
    NAME_FUNC_OFFSET(28159, glMapBuffer, glMapBuffer, NULL, 477),
    NAME_FUNC_OFFSET(28174, glMapBuffer, glMapBuffer, NULL, 477),
    NAME_FUNC_OFFSET(28189, glUnmapBuffer, glUnmapBuffer, NULL, 478),
    NAME_FUNC_OFFSET(28206, glUnmapBuffer, glUnmapBuffer, NULL, 478),
    NAME_FUNC_OFFSET(28223, glBindAttribLocation, glBindAttribLocation, NULL, 480),
    NAME_FUNC_OFFSET(28247, glBlendEquationSeparate, glBlendEquationSeparate, NULL, 481),
    NAME_FUNC_OFFSET(28274, glBlendEquationSeparate, glBlendEquationSeparate, NULL, 481),
    NAME_FUNC_OFFSET(28301, glBlendEquationSeparate, glBlendEquationSeparate, NULL, 481),
    NAME_FUNC_OFFSET(28328, glCompileShader, glCompileShader, NULL, 482),
    NAME_FUNC_OFFSET(28347, glDisableVertexAttribArray, glDisableVertexAttribArray, NULL, 488),
    NAME_FUNC_OFFSET(28377, glDrawBuffers, glDrawBuffers, NULL, 489),
    NAME_FUNC_OFFSET(28394, glDrawBuffers, glDrawBuffers, NULL, 489),
    NAME_FUNC_OFFSET(28411, glDrawBuffers, glDrawBuffers, NULL, 489),
    NAME_FUNC_OFFSET(28427, glDrawBuffers, glDrawBuffers, NULL, 489),
    NAME_FUNC_OFFSET(28444, glEnableVertexAttribArray, glEnableVertexAttribArray, NULL, 490),
    NAME_FUNC_OFFSET(28473, glGetActiveAttrib, glGetActiveAttrib, NULL, 491),
    NAME_FUNC_OFFSET(28494, glGetActiveUniform, glGetActiveUniform, NULL, 492),
    NAME_FUNC_OFFSET(28516, glGetAttribLocation, glGetAttribLocation, NULL, 494),
    NAME_FUNC_OFFSET(28539, glGetShaderSource, glGetShaderSource, NULL, 498),
    NAME_FUNC_OFFSET(28560, glGetUniformLocation, glGetUniformLocation, NULL, 500),
    NAME_FUNC_OFFSET(28584, glGetUniformfv, glGetUniformfv, NULL, 501),
    NAME_FUNC_OFFSET(28602, glGetUniformiv, glGetUniformiv, NULL, 502),
    NAME_FUNC_OFFSET(28620, glGetVertexAttribPointerv, glGetVertexAttribPointerv, NULL, 503),
    NAME_FUNC_OFFSET(28649, glGetVertexAttribPointerv, glGetVertexAttribPointerv, NULL, 503),
    NAME_FUNC_OFFSET(28677, glGetVertexAttribdv, glGetVertexAttribdv, NULL, 504),
    NAME_FUNC_OFFSET(28700, glGetVertexAttribfv, glGetVertexAttribfv, NULL, 505),
    NAME_FUNC_OFFSET(28723, glGetVertexAttribiv, glGetVertexAttribiv, NULL, 506),
    NAME_FUNC_OFFSET(28746, glLinkProgram, glLinkProgram, NULL, 509),
    NAME_FUNC_OFFSET(28763, glShaderSource, glShaderSource, NULL, 510),
    NAME_FUNC_OFFSET(28781, glStencilOpSeparate, glStencilOpSeparate, NULL, 513),
    NAME_FUNC_OFFSET(28804, glUniform1f, glUniform1f, NULL, 514),
    NAME_FUNC_OFFSET(28819, glUniform1fv, glUniform1fv, NULL, 515),
    NAME_FUNC_OFFSET(28835, glUniform1i, glUniform1i, NULL, 516),
    NAME_FUNC_OFFSET(28850, glUniform1iv, glUniform1iv, NULL, 517),
    NAME_FUNC_OFFSET(28866, glUniform2f, glUniform2f, NULL, 518),
    NAME_FUNC_OFFSET(28881, glUniform2fv, glUniform2fv, NULL, 519),
    NAME_FUNC_OFFSET(28897, glUniform2i, glUniform2i, NULL, 520),
    NAME_FUNC_OFFSET(28912, glUniform2iv, glUniform2iv, NULL, 521),
    NAME_FUNC_OFFSET(28928, glUniform3f, glUniform3f, NULL, 522),
    NAME_FUNC_OFFSET(28943, glUniform3fv, glUniform3fv, NULL, 523),
    NAME_FUNC_OFFSET(28959, glUniform3i, glUniform3i, NULL, 524),
    NAME_FUNC_OFFSET(28974, glUniform3iv, glUniform3iv, NULL, 525),
    NAME_FUNC_OFFSET(28990, glUniform4f, glUniform4f, NULL, 526),
    NAME_FUNC_OFFSET(29005, glUniform4fv, glUniform4fv, NULL, 527),
    NAME_FUNC_OFFSET(29021, glUniform4i, glUniform4i, NULL, 528),
    NAME_FUNC_OFFSET(29036, glUniform4iv, glUniform4iv, NULL, 529),
    NAME_FUNC_OFFSET(29052, glUniformMatrix2fv, glUniformMatrix2fv, NULL, 530),
    NAME_FUNC_OFFSET(29074, glUniformMatrix3fv, glUniformMatrix3fv, NULL, 531),
    NAME_FUNC_OFFSET(29096, glUniformMatrix4fv, glUniformMatrix4fv, NULL, 532),
    NAME_FUNC_OFFSET(29118, glUseProgram, glUseProgram, NULL, 533),
    NAME_FUNC_OFFSET(29140, glValidateProgram, glValidateProgram, NULL, 534),
    NAME_FUNC_OFFSET(29161, glVertexAttrib1d, glVertexAttrib1d, NULL, 535),
    NAME_FUNC_OFFSET(29181, glVertexAttrib1dv, glVertexAttrib1dv, NULL, 536),
    NAME_FUNC_OFFSET(29202, glVertexAttrib1s, glVertexAttrib1s, NULL, 537),
    NAME_FUNC_OFFSET(29222, glVertexAttrib1sv, glVertexAttrib1sv, NULL, 538),
    NAME_FUNC_OFFSET(29243, glVertexAttrib2d, glVertexAttrib2d, NULL, 539),
    NAME_FUNC_OFFSET(29263, glVertexAttrib2dv, glVertexAttrib2dv, NULL, 540),
    NAME_FUNC_OFFSET(29284, glVertexAttrib2s, glVertexAttrib2s, NULL, 541),
    NAME_FUNC_OFFSET(29304, glVertexAttrib2sv, glVertexAttrib2sv, NULL, 542),
    NAME_FUNC_OFFSET(29325, glVertexAttrib3d, glVertexAttrib3d, NULL, 543),
    NAME_FUNC_OFFSET(29345, glVertexAttrib3dv, glVertexAttrib3dv, NULL, 544),
    NAME_FUNC_OFFSET(29366, glVertexAttrib3s, glVertexAttrib3s, NULL, 545),
    NAME_FUNC_OFFSET(29386, glVertexAttrib3sv, glVertexAttrib3sv, NULL, 546),
    NAME_FUNC_OFFSET(29407, glVertexAttrib4Nbv, glVertexAttrib4Nbv, NULL, 547),
    NAME_FUNC_OFFSET(29429, glVertexAttrib4Niv, glVertexAttrib4Niv, NULL, 548),
    NAME_FUNC_OFFSET(29451, glVertexAttrib4Nsv, glVertexAttrib4Nsv, NULL, 549),
    NAME_FUNC_OFFSET(29473, glVertexAttrib4Nub, glVertexAttrib4Nub, NULL, 550),
    NAME_FUNC_OFFSET(29495, glVertexAttrib4Nubv, glVertexAttrib4Nubv, NULL, 551),
    NAME_FUNC_OFFSET(29518, glVertexAttrib4Nuiv, glVertexAttrib4Nuiv, NULL, 552),
    NAME_FUNC_OFFSET(29541, glVertexAttrib4Nusv, glVertexAttrib4Nusv, NULL, 553),
    NAME_FUNC_OFFSET(29564, glVertexAttrib4bv, glVertexAttrib4bv, NULL, 554),
    NAME_FUNC_OFFSET(29585, glVertexAttrib4d, glVertexAttrib4d, NULL, 555),
    NAME_FUNC_OFFSET(29605, glVertexAttrib4dv, glVertexAttrib4dv, NULL, 556),
    NAME_FUNC_OFFSET(29626, glVertexAttrib4iv, glVertexAttrib4iv, NULL, 557),
    NAME_FUNC_OFFSET(29647, glVertexAttrib4s, glVertexAttrib4s, NULL, 558),
    NAME_FUNC_OFFSET(29667, glVertexAttrib4sv, glVertexAttrib4sv, NULL, 559),
    NAME_FUNC_OFFSET(29688, glVertexAttrib4ubv, glVertexAttrib4ubv, NULL, 560),
    NAME_FUNC_OFFSET(29710, glVertexAttrib4uiv, glVertexAttrib4uiv, NULL, 561),
    NAME_FUNC_OFFSET(29732, glVertexAttrib4usv, glVertexAttrib4usv, NULL, 562),
    NAME_FUNC_OFFSET(29754, glVertexAttribPointer, glVertexAttribPointer, NULL, 563),
    NAME_FUNC_OFFSET(29779, glBeginConditionalRender, glBeginConditionalRender, NULL, 570),
    NAME_FUNC_OFFSET(29806, glBeginTransformFeedback, glBeginTransformFeedback, NULL, 571),
    NAME_FUNC_OFFSET(29834, glBindBufferBase, glBindBufferBase, NULL, 572),
    NAME_FUNC_OFFSET(29854, glBindBufferRange, glBindBufferRange, NULL, 573),
    NAME_FUNC_OFFSET(29875, glBindFragDataLocation, glBindFragDataLocation, NULL, 574),
    NAME_FUNC_OFFSET(29901, glClampColor, glClampColor, NULL, 575),
    NAME_FUNC_OFFSET(29917, glColorMaski, glColorMaski, NULL, 580),
    NAME_FUNC_OFFSET(29939, glDisablei, glDisablei, NULL, 581),
    NAME_FUNC_OFFSET(29959, glEnablei, glEnablei, NULL, 582),
    NAME_FUNC_OFFSET(29978, glEndConditionalRender, glEndConditionalRender, NULL, 583),
    NAME_FUNC_OFFSET(30003, glEndTransformFeedback, glEndTransformFeedback, NULL, 584),
    NAME_FUNC_OFFSET(30029, glGetBooleani_v, glGetBooleani_v, NULL, 585),
    NAME_FUNC_OFFSET(30053, glGetFragDataLocation, glGetFragDataLocation, NULL, 586),
    NAME_FUNC_OFFSET(30078, glGetIntegeri_v, glGetIntegeri_v, NULL, 587),
    NAME_FUNC_OFFSET(30102, glGetTexParameterIiv, glGetTexParameterIiv, NULL, 589),
    NAME_FUNC_OFFSET(30126, glGetTexParameterIuiv, glGetTexParameterIuiv, NULL, 590),
    NAME_FUNC_OFFSET(30151, glGetTransformFeedbackVarying, glGetTransformFeedbackVarying, NULL, 591),
    NAME_FUNC_OFFSET(30184, glGetUniformuiv, glGetUniformuiv, NULL, 592),
    NAME_FUNC_OFFSET(30203, glGetVertexAttribIiv, glGetVertexAttribIiv, NULL, 593),
    NAME_FUNC_OFFSET(30227, glGetVertexAttribIuiv, glGetVertexAttribIuiv, NULL, 594),
    NAME_FUNC_OFFSET(30252, glIsEnabledi, glIsEnabledi, NULL, 595),
    NAME_FUNC_OFFSET(30274, glTexParameterIiv, glTexParameterIiv, NULL, 596),
    NAME_FUNC_OFFSET(30295, glTexParameterIuiv, glTexParameterIuiv, NULL, 597),
    NAME_FUNC_OFFSET(30317, glTransformFeedbackVaryings, glTransformFeedbackVaryings, NULL, 598),
    NAME_FUNC_OFFSET(30348, glUniform1ui, glUniform1ui, NULL, 599),
    NAME_FUNC_OFFSET(30364, glUniform1uiv, glUniform1uiv, NULL, 600),
    NAME_FUNC_OFFSET(30381, glUniform2ui, glUniform2ui, NULL, 601),
    NAME_FUNC_OFFSET(30397, glUniform2uiv, glUniform2uiv, NULL, 602),
    NAME_FUNC_OFFSET(30414, glUniform3ui, glUniform3ui, NULL, 603),
    NAME_FUNC_OFFSET(30430, glUniform3uiv, glUniform3uiv, NULL, 604),
    NAME_FUNC_OFFSET(30447, glUniform4ui, glUniform4ui, NULL, 605),
    NAME_FUNC_OFFSET(30463, glUniform4uiv, glUniform4uiv, NULL, 606),
    NAME_FUNC_OFFSET(30480, glVertexAttribI1iv, glVertexAttribI1iv, NULL, 607),
    NAME_FUNC_OFFSET(30502, glVertexAttribI1uiv, glVertexAttribI1uiv, NULL, 608),
    NAME_FUNC_OFFSET(30525, glVertexAttribI4bv, glVertexAttribI4bv, NULL, 609),
    NAME_FUNC_OFFSET(30547, glVertexAttribI4sv, glVertexAttribI4sv, NULL, 610),
    NAME_FUNC_OFFSET(30569, glVertexAttribI4ubv, glVertexAttribI4ubv, NULL, 611),
    NAME_FUNC_OFFSET(30592, glVertexAttribI4usv, glVertexAttribI4usv, NULL, 612),
    NAME_FUNC_OFFSET(30615, glVertexAttribIPointer, glVertexAttribIPointer, NULL, 613),
    NAME_FUNC_OFFSET(30641, glPrimitiveRestartIndex, glPrimitiveRestartIndex, NULL, 614),
    NAME_FUNC_OFFSET(30667, glTexBuffer, glTexBuffer, NULL, 615),
    NAME_FUNC_OFFSET(30682, glFramebufferTexture, glFramebufferTexture, NULL, 616),
    NAME_FUNC_OFFSET(30706, glVertexAttribDivisor, glVertexAttribDivisor, NULL, 619),
    NAME_FUNC_OFFSET(30731, glMinSampleShading, glMinSampleShading, NULL, 620),
    NAME_FUNC_OFFSET(30753, glBindProgramARB, glBindProgramARB, NULL, 621),
    NAME_FUNC_OFFSET(30769, glDeleteProgramsARB, glDeleteProgramsARB, NULL, 622),
    NAME_FUNC_OFFSET(30788, glGenProgramsARB, glGenProgramsARB, NULL, 623),
    NAME_FUNC_OFFSET(30804, glIsProgramARB, glIsProgramARB, NULL, 630),
    NAME_FUNC_OFFSET(30818, glProgramEnvParameter4dARB, glProgramEnvParameter4dARB, NULL, 631),
    NAME_FUNC_OFFSET(30841, glProgramEnvParameter4dvARB, glProgramEnvParameter4dvARB, NULL, 632),
    NAME_FUNC_OFFSET(30865, glProgramEnvParameter4fARB, glProgramEnvParameter4fARB, NULL, 633),
    NAME_FUNC_OFFSET(30888, glProgramEnvParameter4fvARB, glProgramEnvParameter4fvARB, NULL, 634),
    NAME_FUNC_OFFSET(30912, glVertexAttrib1fARB, glVertexAttrib1fARB, NULL, 640),
    NAME_FUNC_OFFSET(30929, glVertexAttrib1fvARB, glVertexAttrib1fvARB, NULL, 641),
    NAME_FUNC_OFFSET(30947, glVertexAttrib2fARB, glVertexAttrib2fARB, NULL, 642),
    NAME_FUNC_OFFSET(30964, glVertexAttrib2fvARB, glVertexAttrib2fvARB, NULL, 643),
    NAME_FUNC_OFFSET(30982, glVertexAttrib3fARB, glVertexAttrib3fARB, NULL, 644),
    NAME_FUNC_OFFSET(30999, glVertexAttrib3fvARB, glVertexAttrib3fvARB, NULL, 645),
    NAME_FUNC_OFFSET(31017, glVertexAttrib4fARB, glVertexAttrib4fARB, NULL, 646),
    NAME_FUNC_OFFSET(31034, glVertexAttrib4fvARB, glVertexAttrib4fvARB, NULL, 647),
    NAME_FUNC_OFFSET(31052, glDrawArraysInstancedARB, glDrawArraysInstancedARB, NULL, 658),
    NAME_FUNC_OFFSET(31077, glDrawArraysInstancedARB, glDrawArraysInstancedARB, NULL, 658),
    NAME_FUNC_OFFSET(31099, glDrawElementsInstancedARB, glDrawElementsInstancedARB, NULL, 659),
    NAME_FUNC_OFFSET(31126, glDrawElementsInstancedARB, glDrawElementsInstancedARB, NULL, 659),
    NAME_FUNC_OFFSET(31150, glBindFramebuffer, glBindFramebuffer, NULL, 660),
    NAME_FUNC_OFFSET(31171, glBindRenderbuffer, glBindRenderbuffer, NULL, 661),
    NAME_FUNC_OFFSET(31193, glBlitFramebuffer, glBlitFramebuffer, NULL, 662),
    NAME_FUNC_OFFSET(31214, glCheckFramebufferStatus, glCheckFramebufferStatus, NULL, 663),
    NAME_FUNC_OFFSET(31242, glCheckFramebufferStatus, glCheckFramebufferStatus, NULL, 663),
    NAME_FUNC_OFFSET(31270, glDeleteFramebuffers, glDeleteFramebuffers, NULL, 664),
    NAME_FUNC_OFFSET(31294, glDeleteFramebuffers, glDeleteFramebuffers, NULL, 664),
    NAME_FUNC_OFFSET(31318, glDeleteRenderbuffers, glDeleteRenderbuffers, NULL, 665),
    NAME_FUNC_OFFSET(31343, glDeleteRenderbuffers, glDeleteRenderbuffers, NULL, 665),
    NAME_FUNC_OFFSET(31368, glFramebufferRenderbuffer, glFramebufferRenderbuffer, NULL, 666),
    NAME_FUNC_OFFSET(31397, glFramebufferRenderbuffer, glFramebufferRenderbuffer, NULL, 666),
    NAME_FUNC_OFFSET(31426, glFramebufferTexture1D, glFramebufferTexture1D, NULL, 667),
    NAME_FUNC_OFFSET(31452, glFramebufferTexture2D, glFramebufferTexture2D, NULL, 668),
    NAME_FUNC_OFFSET(31478, glFramebufferTexture2D, glFramebufferTexture2D, NULL, 668),
    NAME_FUNC_OFFSET(31504, glFramebufferTexture3D, glFramebufferTexture3D, NULL, 669),
    NAME_FUNC_OFFSET(31530, glFramebufferTexture3D, glFramebufferTexture3D, NULL, 669),
    NAME_FUNC_OFFSET(31556, glFramebufferTextureLayer, glFramebufferTextureLayer, NULL, 670),
    NAME_FUNC_OFFSET(31585, glFramebufferTextureLayer, glFramebufferTextureLayer, NULL, 670),
    NAME_FUNC_OFFSET(31614, glGenFramebuffers, glGenFramebuffers, NULL, 671),
    NAME_FUNC_OFFSET(31635, glGenFramebuffers, glGenFramebuffers, NULL, 671),
    NAME_FUNC_OFFSET(31656, glGenRenderbuffers, glGenRenderbuffers, NULL, 672),
    NAME_FUNC_OFFSET(31678, glGenRenderbuffers, glGenRenderbuffers, NULL, 672),
    NAME_FUNC_OFFSET(31700, glGenerateMipmap, glGenerateMipmap, NULL, 673),
    NAME_FUNC_OFFSET(31720, glGenerateMipmap, glGenerateMipmap, NULL, 673),
    NAME_FUNC_OFFSET(31740, glGetFramebufferAttachmentParameteriv, glGetFramebufferAttachmentParameteriv, NULL, 674),
    NAME_FUNC_OFFSET(31781, glGetFramebufferAttachmentParameteriv, glGetFramebufferAttachmentParameteriv, NULL, 674),
    NAME_FUNC_OFFSET(31822, glGetRenderbufferParameteriv, glGetRenderbufferParameteriv, NULL, 675),
    NAME_FUNC_OFFSET(31854, glGetRenderbufferParameteriv, glGetRenderbufferParameteriv, NULL, 675),
    NAME_FUNC_OFFSET(31886, glIsFramebuffer, glIsFramebuffer, NULL, 676),
    NAME_FUNC_OFFSET(31905, glIsFramebuffer, glIsFramebuffer, NULL, 676),
    NAME_FUNC_OFFSET(31924, glIsRenderbuffer, glIsRenderbuffer, NULL, 677),
    NAME_FUNC_OFFSET(31944, glIsRenderbuffer, glIsRenderbuffer, NULL, 677),
    NAME_FUNC_OFFSET(31964, glRenderbufferStorage, glRenderbufferStorage, NULL, 678),
    NAME_FUNC_OFFSET(31989, glRenderbufferStorage, glRenderbufferStorage, NULL, 678),
    NAME_FUNC_OFFSET(32014, glRenderbufferStorageMultisample, glRenderbufferStorageMultisample, NULL, 679),
    NAME_FUNC_OFFSET(32050, glFlushMappedBufferRange, glFlushMappedBufferRange, NULL, 681),
    NAME_FUNC_OFFSET(32078, glMapBufferRange, glMapBufferRange, NULL, 682),
    NAME_FUNC_OFFSET(32098, glBindVertexArray, glBindVertexArray, NULL, 683),
    NAME_FUNC_OFFSET(32119, glDeleteVertexArrays, glDeleteVertexArrays, NULL, 684),
    NAME_FUNC_OFFSET(32145, glDeleteVertexArrays, glDeleteVertexArrays, NULL, 684),
    NAME_FUNC_OFFSET(32169, glGenVertexArrays, glGenVertexArrays, NULL, 685),
    NAME_FUNC_OFFSET(32190, glIsVertexArray, glIsVertexArray, NULL, 686),
    NAME_FUNC_OFFSET(32211, glIsVertexArray, glIsVertexArray, NULL, 686),
    NAME_FUNC_OFFSET(32230, glProvokingVertex, glProvokingVertex, NULL, 706),
    NAME_FUNC_OFFSET(32251, glBlendEquationSeparateiARB, glBlendEquationSeparateiARB, NULL, 711),
    NAME_FUNC_OFFSET(32285, glBlendEquationSeparateiARB, glBlendEquationSeparateiARB, NULL, 711),
    NAME_FUNC_OFFSET(32310, glBlendEquationiARB, glBlendEquationiARB, NULL, 712),
    NAME_FUNC_OFFSET(32336, glBlendEquationiARB, glBlendEquationiARB, NULL, 712),
    NAME_FUNC_OFFSET(32353, glBlendFuncSeparateiARB, glBlendFuncSeparateiARB, NULL, 713),
    NAME_FUNC_OFFSET(32383, glBlendFuncSeparateiARB, glBlendFuncSeparateiARB, NULL, 713),
    NAME_FUNC_OFFSET(32404, glBlendFunciARB, glBlendFunciARB, NULL, 714),
    NAME_FUNC_OFFSET(32426, glBlendFunciARB, glBlendFunciARB, NULL, 714),
    NAME_FUNC_OFFSET(32439, gl_dispatch_stub_731, gl_dispatch_stub_731, NULL, 731),
    NAME_FUNC_OFFSET(32463, gl_dispatch_stub_732, gl_dispatch_stub_732, NULL, 732),
    NAME_FUNC_OFFSET(32488, glClearDepthf, glClearDepthf, NULL, 803),
    NAME_FUNC_OFFSET(32505, glDepthRangef, glDepthRangef, NULL, 804),
    NAME_FUNC_OFFSET(32522, glGetProgramBinary, glGetProgramBinary, NULL, 808),
    NAME_FUNC_OFFSET(32544, glProgramBinary, glProgramBinary, NULL, 809),
    NAME_FUNC_OFFSET(32563, glProgramParameteri, glProgramParameteri, NULL, 810),
    NAME_FUNC_OFFSET(32586, glProgramParameteri, glProgramParameteri, NULL, 810),
    NAME_FUNC_OFFSET(32609, gl_dispatch_stub_1013, gl_dispatch_stub_1013, NULL, 1013),
    NAME_FUNC_OFFSET(32625, gl_dispatch_stub_1014, gl_dispatch_stub_1014, NULL, 1014),
    NAME_FUNC_OFFSET(32644, gl_dispatch_stub_1022, gl_dispatch_stub_1022, NULL, 1022),
    NAME_FUNC_OFFSET(32669, gl_dispatch_stub_1023, gl_dispatch_stub_1023, NULL, 1023),
    NAME_FUNC_OFFSET(32694, gl_dispatch_stub_1024, gl_dispatch_stub_1024, NULL, 1024),
    NAME_FUNC_OFFSET(32720, gl_dispatch_stub_1025, gl_dispatch_stub_1025, NULL, 1025),
    NAME_FUNC_OFFSET(32748, gl_dispatch_stub_1026, gl_dispatch_stub_1026, NULL, 1026),
    NAME_FUNC_OFFSET(32773, gl_dispatch_stub_1027, gl_dispatch_stub_1027, NULL, 1027),
    NAME_FUNC_OFFSET(32804, gl_dispatch_stub_1028, gl_dispatch_stub_1028, NULL, 1028),
    NAME_FUNC_OFFSET(32830, gl_dispatch_stub_1029, gl_dispatch_stub_1029, NULL, 1029),
    NAME_FUNC_OFFSET(32853, gl_dispatch_stub_1033, gl_dispatch_stub_1033, NULL, 1033),
    NAME_FUNC_OFFSET(32875, gl_dispatch_stub_1034, gl_dispatch_stub_1034, NULL, 1034),
    NAME_FUNC_OFFSET(32898, gl_dispatch_stub_1035, gl_dispatch_stub_1035, NULL, 1035),
    NAME_FUNC_OFFSET(32920, gl_dispatch_stub_1036, gl_dispatch_stub_1036, NULL, 1036),
    NAME_FUNC_OFFSET(32943, gl_dispatch_stub_1037, gl_dispatch_stub_1037, NULL, 1037),
    NAME_FUNC_OFFSET(32966, gl_dispatch_stub_1038, gl_dispatch_stub_1038, NULL, 1038),
    NAME_FUNC_OFFSET(32990, gl_dispatch_stub_1041, gl_dispatch_stub_1041, NULL, 1041),
    NAME_FUNC_OFFSET(33012, gl_dispatch_stub_1042, gl_dispatch_stub_1042, NULL, 1042),
    NAME_FUNC_OFFSET(33035, gl_dispatch_stub_1043, gl_dispatch_stub_1043, NULL, 1043),
    NAME_FUNC_OFFSET(33057, gl_dispatch_stub_1044, gl_dispatch_stub_1044, NULL, 1044),
    NAME_FUNC_OFFSET(33080, gl_dispatch_stub_1045, gl_dispatch_stub_1045, NULL, 1045),
    NAME_FUNC_OFFSET(33103, gl_dispatch_stub_1046, gl_dispatch_stub_1046, NULL, 1046),
    NAME_FUNC_OFFSET(33127, gl_dispatch_stub_1049, gl_dispatch_stub_1049, NULL, 1049),
    NAME_FUNC_OFFSET(33149, gl_dispatch_stub_1050, gl_dispatch_stub_1050, NULL, 1050),
    NAME_FUNC_OFFSET(33172, gl_dispatch_stub_1051, gl_dispatch_stub_1051, NULL, 1051),
    NAME_FUNC_OFFSET(33194, gl_dispatch_stub_1052, gl_dispatch_stub_1052, NULL, 1052),
    NAME_FUNC_OFFSET(33217, gl_dispatch_stub_1053, gl_dispatch_stub_1053, NULL, 1053),
    NAME_FUNC_OFFSET(33240, gl_dispatch_stub_1054, gl_dispatch_stub_1054, NULL, 1054),
    NAME_FUNC_OFFSET(33264, gl_dispatch_stub_1057, gl_dispatch_stub_1057, NULL, 1057),
    NAME_FUNC_OFFSET(33286, gl_dispatch_stub_1058, gl_dispatch_stub_1058, NULL, 1058),
    NAME_FUNC_OFFSET(33309, gl_dispatch_stub_1059, gl_dispatch_stub_1059, NULL, 1059),
    NAME_FUNC_OFFSET(33331, gl_dispatch_stub_1060, gl_dispatch_stub_1060, NULL, 1060),
    NAME_FUNC_OFFSET(33354, gl_dispatch_stub_1061, gl_dispatch_stub_1061, NULL, 1061),
    NAME_FUNC_OFFSET(33377, gl_dispatch_stub_1062, gl_dispatch_stub_1062, NULL, 1062),
    NAME_FUNC_OFFSET(33401, gl_dispatch_stub_1064, gl_dispatch_stub_1064, NULL, 1064),
    NAME_FUNC_OFFSET(33430, gl_dispatch_stub_1066, gl_dispatch_stub_1066, NULL, 1066),
    NAME_FUNC_OFFSET(33461, gl_dispatch_stub_1068, gl_dispatch_stub_1068, NULL, 1068),
    NAME_FUNC_OFFSET(33492, gl_dispatch_stub_1070, gl_dispatch_stub_1070, NULL, 1070),
    NAME_FUNC_OFFSET(33521, gl_dispatch_stub_1072, gl_dispatch_stub_1072, NULL, 1072),
    NAME_FUNC_OFFSET(33552, gl_dispatch_stub_1074, gl_dispatch_stub_1074, NULL, 1074),
    NAME_FUNC_OFFSET(33583, gl_dispatch_stub_1076, gl_dispatch_stub_1076, NULL, 1076),
    NAME_FUNC_OFFSET(33612, gl_dispatch_stub_1078, gl_dispatch_stub_1078, NULL, 1078),
    NAME_FUNC_OFFSET(33643, gl_dispatch_stub_1080, gl_dispatch_stub_1080, NULL, 1080),
    NAME_FUNC_OFFSET(33674, gl_dispatch_stub_1082, gl_dispatch_stub_1082, NULL, 1082),
    NAME_FUNC_OFFSET(33696, gl_dispatch_stub_1083, gl_dispatch_stub_1083, NULL, 1083),
    NAME_FUNC_OFFSET(33725, glDebugMessageCallback, glDebugMessageCallback, NULL, 1084),
    NAME_FUNC_OFFSET(33751, glDebugMessageControl, glDebugMessageControl, NULL, 1085),
    NAME_FUNC_OFFSET(33776, glDebugMessageInsert, glDebugMessageInsert, NULL, 1086),
    NAME_FUNC_OFFSET(33800, glGetDebugMessageLog, glGetDebugMessageLog, NULL, 1087),
    NAME_FUNC_OFFSET(33824, glSecondaryColor3fEXT, glSecondaryColor3fEXT, NULL, 1094),
    NAME_FUNC_OFFSET(33843, glSecondaryColor3fvEXT, glSecondaryColor3fvEXT, NULL, 1095),
    NAME_FUNC_OFFSET(33863, glMultiDrawElementsEXT, glMultiDrawElementsEXT, NULL, 1096),
    NAME_FUNC_OFFSET(33883, glFogCoordfEXT, glFogCoordfEXT, NULL, 1097),
    NAME_FUNC_OFFSET(33895, glFogCoordfvEXT, glFogCoordfvEXT, NULL, 1098),
    NAME_FUNC_OFFSET(33908, glVertexAttribI1iEXT, glVertexAttribI1iEXT, NULL, 1201),
    NAME_FUNC_OFFSET(33926, glVertexAttribI1uiEXT, glVertexAttribI1uiEXT, NULL, 1202),
    NAME_FUNC_OFFSET(33945, glVertexAttribI2iEXT, glVertexAttribI2iEXT, NULL, 1203),
    NAME_FUNC_OFFSET(33963, glVertexAttribI2ivEXT, glVertexAttribI2ivEXT, NULL, 1204),
    NAME_FUNC_OFFSET(33982, glVertexAttribI2uiEXT, glVertexAttribI2uiEXT, NULL, 1205),
    NAME_FUNC_OFFSET(34001, glVertexAttribI2uivEXT, glVertexAttribI2uivEXT, NULL, 1206),
    NAME_FUNC_OFFSET(34021, glVertexAttribI3iEXT, glVertexAttribI3iEXT, NULL, 1207),
    NAME_FUNC_OFFSET(34039, glVertexAttribI3ivEXT, glVertexAttribI3ivEXT, NULL, 1208),
    NAME_FUNC_OFFSET(34058, glVertexAttribI3uiEXT, glVertexAttribI3uiEXT, NULL, 1209),
    NAME_FUNC_OFFSET(34077, glVertexAttribI3uivEXT, glVertexAttribI3uivEXT, NULL, 1210),
    NAME_FUNC_OFFSET(34097, glVertexAttribI4iEXT, glVertexAttribI4iEXT, NULL, 1211),
    NAME_FUNC_OFFSET(34115, glVertexAttribI4ivEXT, glVertexAttribI4ivEXT, NULL, 1212),
    NAME_FUNC_OFFSET(34134, glVertexAttribI4uiEXT, glVertexAttribI4uiEXT, NULL, 1213),
    NAME_FUNC_OFFSET(34153, glVertexAttribI4uivEXT, glVertexAttribI4uivEXT, NULL, 1214),
    NAME_FUNC_OFFSET(34173, glTextureBarrierNV, glTextureBarrierNV, NULL, 1235),
    NAME_FUNC_OFFSET(34190, glAlphaFuncx, glAlphaFuncx, NULL, 1262),
    NAME_FUNC_OFFSET(34206, glClearColorx, glClearColorx, NULL, 1263),
    NAME_FUNC_OFFSET(34223, glClearDepthx, glClearDepthx, NULL, 1264),
    NAME_FUNC_OFFSET(34240, glColor4x, glColor4x, NULL, 1265),
    NAME_FUNC_OFFSET(34253, glDepthRangex, glDepthRangex, NULL, 1266),
    NAME_FUNC_OFFSET(34270, glFogx, glFogx, NULL, 1267),
    NAME_FUNC_OFFSET(34280, glFogxv, glFogxv, NULL, 1268),
    NAME_FUNC_OFFSET(34291, glFrustumf, glFrustumf, NULL, 1269),
    NAME_FUNC_OFFSET(34305, glFrustumx, glFrustumx, NULL, 1270),
    NAME_FUNC_OFFSET(34319, glLightModelx, glLightModelx, NULL, 1271),
    NAME_FUNC_OFFSET(34336, glLightModelxv, glLightModelxv, NULL, 1272),
    NAME_FUNC_OFFSET(34354, glLightx, glLightx, NULL, 1273),
    NAME_FUNC_OFFSET(34366, glLightxv, glLightxv, NULL, 1274),
    NAME_FUNC_OFFSET(34379, glLineWidthx, glLineWidthx, NULL, 1275),
    NAME_FUNC_OFFSET(34395, glLoadMatrixx, glLoadMatrixx, NULL, 1276),
    NAME_FUNC_OFFSET(34412, glMaterialx, glMaterialx, NULL, 1277),
    NAME_FUNC_OFFSET(34427, glMaterialxv, glMaterialxv, NULL, 1278),
    NAME_FUNC_OFFSET(34443, glMultMatrixx, glMultMatrixx, NULL, 1279),
    NAME_FUNC_OFFSET(34460, glMultiTexCoord4x, glMultiTexCoord4x, NULL, 1280),
    NAME_FUNC_OFFSET(34481, glNormal3x, glNormal3x, NULL, 1281),
    NAME_FUNC_OFFSET(34495, glOrthof, glOrthof, NULL, 1282),
    NAME_FUNC_OFFSET(34507, glOrthox, glOrthox, NULL, 1283),
    NAME_FUNC_OFFSET(34519, glPointSizex, glPointSizex, NULL, 1284),
    NAME_FUNC_OFFSET(34535, glPolygonOffsetx, glPolygonOffsetx, NULL, 1285),
    NAME_FUNC_OFFSET(34555, glRotatex, glRotatex, NULL, 1286),
    NAME_FUNC_OFFSET(34568, glSampleCoveragex, glSampleCoveragex, NULL, 1287),
    NAME_FUNC_OFFSET(34589, glScalex, glScalex, NULL, 1288),
    NAME_FUNC_OFFSET(34601, glTexEnvx, glTexEnvx, NULL, 1289),
    NAME_FUNC_OFFSET(34614, glTexEnvxv, glTexEnvxv, NULL, 1290),
    NAME_FUNC_OFFSET(34628, glTexParameterx, glTexParameterx, NULL, 1291),
    NAME_FUNC_OFFSET(34647, glTranslatex, glTranslatex, NULL, 1292),
    NAME_FUNC_OFFSET(34663, glClipPlanef, glClipPlanef, NULL, 1293),
    NAME_FUNC_OFFSET(34679, glClipPlanex, glClipPlanex, NULL, 1294),
    NAME_FUNC_OFFSET(34695, glGetClipPlanef, glGetClipPlanef, NULL, 1295),
    NAME_FUNC_OFFSET(34714, glGetClipPlanex, glGetClipPlanex, NULL, 1296),
    NAME_FUNC_OFFSET(34733, glGetFixedv, glGetFixedv, NULL, 1297),
    NAME_FUNC_OFFSET(34748, glGetLightxv, glGetLightxv, NULL, 1298),
    NAME_FUNC_OFFSET(34764, glGetMaterialxv, glGetMaterialxv, NULL, 1299),
    NAME_FUNC_OFFSET(34783, glGetTexEnvxv, glGetTexEnvxv, NULL, 1300),
    NAME_FUNC_OFFSET(34800, glGetTexParameterxv, glGetTexParameterxv, NULL, 1301),
    NAME_FUNC_OFFSET(34823, glPointParameterx, glPointParameterx, NULL, 1302),
    NAME_FUNC_OFFSET(34844, glPointParameterxv, glPointParameterxv, NULL, 1303),
    NAME_FUNC_OFFSET(34866, glTexParameterxv, glTexParameterxv, NULL, 1304),
    NAME_FUNC_OFFSET(-1, NULL, NULL, NULL, 0)
};

#undef NAME_FUNC_OFFSET
