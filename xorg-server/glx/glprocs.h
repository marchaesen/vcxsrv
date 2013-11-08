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
    "glGetActiveAtomicCounterBufferiv\0"
    "glTexStorage1D\0"
    "glTexStorage2D\0"
    "glTexStorage3D\0"
    "glTextureStorage1DEXT\0"
    "glTextureStorage2DEXT\0"
    "glTextureStorage3DEXT\0"
    "glTexBufferRange\0"
    "glTexStorage2DMultisample\0"
    "glTexStorage3DMultisample\0"
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
#define gl_dispatch_stub_731 mgl_dispatch_stub_731
#define gl_dispatch_stub_732 mgl_dispatch_stub_732
#define gl_dispatch_stub_733 mgl_dispatch_stub_733
#define gl_dispatch_stub_820 mgl_dispatch_stub_820
#define gl_dispatch_stub_838 mgl_dispatch_stub_838
#define gl_dispatch_stub_839 mgl_dispatch_stub_839
#define gl_dispatch_stub_840 mgl_dispatch_stub_840
#define gl_dispatch_stub_841 mgl_dispatch_stub_841
#define gl_dispatch_stub_842 mgl_dispatch_stub_842
#define gl_dispatch_stub_843 mgl_dispatch_stub_843
#define gl_dispatch_stub_844 mgl_dispatch_stub_844
#define gl_dispatch_stub_845 mgl_dispatch_stub_845
#define gl_dispatch_stub_846 mgl_dispatch_stub_846
#define gl_dispatch_stub_847 mgl_dispatch_stub_847
#define gl_dispatch_stub_848 mgl_dispatch_stub_848
#define gl_dispatch_stub_849 mgl_dispatch_stub_849
#define gl_dispatch_stub_856 mgl_dispatch_stub_856
#define gl_dispatch_stub_883 mgl_dispatch_stub_883
#define gl_dispatch_stub_884 mgl_dispatch_stub_884
#define gl_dispatch_stub_958 mgl_dispatch_stub_958
#define gl_dispatch_stub_959 mgl_dispatch_stub_959
#define gl_dispatch_stub_960 mgl_dispatch_stub_960
#define gl_dispatch_stub_968 mgl_dispatch_stub_968
#define gl_dispatch_stub_969 mgl_dispatch_stub_969
#define gl_dispatch_stub_970 mgl_dispatch_stub_970
#define gl_dispatch_stub_971 mgl_dispatch_stub_971
#define gl_dispatch_stub_974 mgl_dispatch_stub_974
#define gl_dispatch_stub_975 mgl_dispatch_stub_975
#define gl_dispatch_stub_1021 mgl_dispatch_stub_1021
#define gl_dispatch_stub_1022 mgl_dispatch_stub_1022
#define gl_dispatch_stub_1023 mgl_dispatch_stub_1023
#define gl_dispatch_stub_1026 mgl_dispatch_stub_1026
#define gl_dispatch_stub_1027 mgl_dispatch_stub_1027
#define gl_dispatch_stub_1028 mgl_dispatch_stub_1028
#define gl_dispatch_stub_1029 mgl_dispatch_stub_1029
#define gl_dispatch_stub_1030 mgl_dispatch_stub_1030
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
void GLAPIENTRY gl_dispatch_stub_820(GLenum target, GLenum internalformat, GLenum pname, GLsizei bufSize, GLint * params);
void GLAPIENTRY gl_dispatch_stub_838(GLfloat x, GLfloat y, GLfloat z, GLfloat width, GLfloat height);
void GLAPIENTRY gl_dispatch_stub_839(const GLfloat * coords);
void GLAPIENTRY gl_dispatch_stub_840(GLint x, GLint y, GLint z, GLint width, GLint height);
void GLAPIENTRY gl_dispatch_stub_841(const GLint * coords);
void GLAPIENTRY gl_dispatch_stub_842(GLshort x, GLshort y, GLshort z, GLshort width, GLshort height);
void GLAPIENTRY gl_dispatch_stub_843(const GLshort * coords);
void GLAPIENTRY gl_dispatch_stub_844(GLfixed x, GLfixed y, GLfixed z, GLfixed width, GLfixed height);
void GLAPIENTRY gl_dispatch_stub_845(const GLfixed * coords);
void GLAPIENTRY gl_dispatch_stub_846(GLenum type, GLsizei stride, const GLvoid * pointer);
GLbitfield GLAPIENTRY gl_dispatch_stub_847(GLfixed * mantissa, GLint * exponent);
void GLAPIENTRY gl_dispatch_stub_848(GLclampf value, GLboolean invert);
void GLAPIENTRY gl_dispatch_stub_849(GLenum pattern);
void GLAPIENTRY gl_dispatch_stub_856(GLenum target, GLsizei numAttachments, const GLenum * attachments);
void GLAPIENTRY gl_dispatch_stub_883(const GLenum * mode, const GLint * first, const GLsizei * count, GLsizei primcount, GLint modestride);
void GLAPIENTRY gl_dispatch_stub_884(const GLenum * mode, const GLsizei * count, GLenum type, const GLvoid * const * indices, GLsizei primcount, GLint modestride);
void GLAPIENTRY gl_dispatch_stub_958(GLenum face);
void GLAPIENTRY gl_dispatch_stub_959(GLuint array);
void GLAPIENTRY gl_dispatch_stub_960(GLsizei n, GLuint * arrays);
void GLAPIENTRY gl_dispatch_stub_968(GLenum coord, GLenum pname, GLfixed * params);
void GLAPIENTRY gl_dispatch_stub_969(GLenum coord, GLenum pname, GLint param);
void GLAPIENTRY gl_dispatch_stub_970(GLenum coord, GLenum pname, const GLfixed * params);
void GLAPIENTRY gl_dispatch_stub_971(GLclampd zmin, GLclampd zmax);
void GLAPIENTRY gl_dispatch_stub_974(GLenum target, GLenum pname, GLint param);
void GLAPIENTRY gl_dispatch_stub_975(GLenum target, GLintptr offset, GLsizeiptr size);
void GLAPIENTRY gl_dispatch_stub_1021(GLenum frontfunc, GLenum backfunc, GLint ref, GLuint mask);
void GLAPIENTRY gl_dispatch_stub_1022(GLenum target, GLuint index, GLsizei count, const GLfloat * params);
void GLAPIENTRY gl_dispatch_stub_1023(GLenum target, GLuint index, GLsizei count, const GLfloat * params);
void GLAPIENTRY gl_dispatch_stub_1026(GLenum func, GLclampx ref);
void GLAPIENTRY gl_dispatch_stub_1027(GLclampx red, GLclampx green, GLclampx blue, GLclampx alpha);
void GLAPIENTRY gl_dispatch_stub_1028(GLclampx depth);
void GLAPIENTRY gl_dispatch_stub_1029(GLfixed red, GLfixed green, GLfixed blue, GLfixed alpha);
void GLAPIENTRY gl_dispatch_stub_1030(GLclampx zNear, GLclampx zFar);
void GLAPIENTRY gl_dispatch_stub_1031(GLenum pname, GLfixed param);
void GLAPIENTRY gl_dispatch_stub_1032(GLenum pname, const GLfixed * params);
void GLAPIENTRY gl_dispatch_stub_1033(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top, GLfloat zNear, GLfloat zFar);
void GLAPIENTRY gl_dispatch_stub_1034(GLfixed left, GLfixed right, GLfixed bottom, GLfixed top, GLfixed zNear, GLfixed zFar);
void GLAPIENTRY gl_dispatch_stub_1035(GLenum pname, GLfixed param);
void GLAPIENTRY gl_dispatch_stub_1036(GLenum pname, const GLfixed * params);
void GLAPIENTRY gl_dispatch_stub_1037(GLenum light, GLenum pname, GLfixed param);
void GLAPIENTRY gl_dispatch_stub_1038(GLenum light, GLenum pname, const GLfixed * params);
void GLAPIENTRY gl_dispatch_stub_1039(GLfixed width);
void GLAPIENTRY gl_dispatch_stub_1040(const GLfixed * m);
void GLAPIENTRY gl_dispatch_stub_1041(GLenum face, GLenum pname, GLfixed param);
void GLAPIENTRY gl_dispatch_stub_1042(GLenum face, GLenum pname, const GLfixed * params);
void GLAPIENTRY gl_dispatch_stub_1043(const GLfixed * m);
void GLAPIENTRY gl_dispatch_stub_1044(GLenum target, GLfixed s, GLfixed t, GLfixed r, GLfixed q);
void GLAPIENTRY gl_dispatch_stub_1045(GLfixed nx, GLfixed ny, GLfixed nz);
void GLAPIENTRY gl_dispatch_stub_1046(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top, GLfloat zNear, GLfloat zFar);
void GLAPIENTRY gl_dispatch_stub_1047(GLfixed left, GLfixed right, GLfixed bottom, GLfixed top, GLfixed zNear, GLfixed zFar);
void GLAPIENTRY gl_dispatch_stub_1048(GLfixed size);
void GLAPIENTRY gl_dispatch_stub_1049(GLfixed factor, GLfixed units);
void GLAPIENTRY gl_dispatch_stub_1050(GLfixed angle, GLfixed x, GLfixed y, GLfixed z);
void GLAPIENTRY gl_dispatch_stub_1051(GLclampx value, GLboolean invert);
void GLAPIENTRY gl_dispatch_stub_1052(GLfixed x, GLfixed y, GLfixed z);
void GLAPIENTRY gl_dispatch_stub_1053(GLenum target, GLenum pname, GLfixed param);
void GLAPIENTRY gl_dispatch_stub_1054(GLenum target, GLenum pname, const GLfixed * params);
void GLAPIENTRY gl_dispatch_stub_1055(GLenum target, GLenum pname, GLfixed param);
void GLAPIENTRY gl_dispatch_stub_1056(GLfixed x, GLfixed y, GLfixed z);
void GLAPIENTRY gl_dispatch_stub_1057(GLenum plane, const GLfloat * equation);
void GLAPIENTRY gl_dispatch_stub_1058(GLenum plane, const GLfixed * equation);
void GLAPIENTRY gl_dispatch_stub_1059(GLenum plane, GLfloat * equation);
void GLAPIENTRY gl_dispatch_stub_1060(GLenum plane, GLfixed * equation);
void GLAPIENTRY gl_dispatch_stub_1061(GLenum pname, GLfixed * params);
void GLAPIENTRY gl_dispatch_stub_1062(GLenum light, GLenum pname, GLfixed * params);
void GLAPIENTRY gl_dispatch_stub_1063(GLenum face, GLenum pname, GLfixed * params);
void GLAPIENTRY gl_dispatch_stub_1064(GLenum target, GLenum pname, GLfixed * params);
void GLAPIENTRY gl_dispatch_stub_1065(GLenum target, GLenum pname, GLfixed * params);
void GLAPIENTRY gl_dispatch_stub_1066(GLenum pname, GLfixed param);
void GLAPIENTRY gl_dispatch_stub_1067(GLenum pname, const GLfixed * params);
void GLAPIENTRY gl_dispatch_stub_1068(GLenum target, GLenum pname, const GLfixed * params);
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
    NAME_FUNC_OFFSET(12577, glBindTransformFeedback, glBindTransformFeedback, NULL, 772),
    NAME_FUNC_OFFSET(12601, glDeleteTransformFeedbacks, glDeleteTransformFeedbacks, NULL, 773),
    NAME_FUNC_OFFSET(12628, glDrawTransformFeedback, glDrawTransformFeedback, NULL, 774),
    NAME_FUNC_OFFSET(12652, glGenTransformFeedbacks, glGenTransformFeedbacks, NULL, 775),
    NAME_FUNC_OFFSET(12676, glIsTransformFeedback, glIsTransformFeedback, NULL, 776),
    NAME_FUNC_OFFSET(12698, glPauseTransformFeedback, glPauseTransformFeedback, NULL, 777),
    NAME_FUNC_OFFSET(12723, glResumeTransformFeedback, glResumeTransformFeedback, NULL, 778),
    NAME_FUNC_OFFSET(12749, glBeginQueryIndexed, glBeginQueryIndexed, NULL, 779),
    NAME_FUNC_OFFSET(12769, glDrawTransformFeedbackStream, glDrawTransformFeedbackStream, NULL, 780),
    NAME_FUNC_OFFSET(12799, glEndQueryIndexed, glEndQueryIndexed, NULL, 781),
    NAME_FUNC_OFFSET(12817, glGetQueryIndexediv, glGetQueryIndexediv, NULL, 782),
    NAME_FUNC_OFFSET(12837, glClearDepthf, glClearDepthf, NULL, 783),
    NAME_FUNC_OFFSET(12851, glDepthRangef, glDepthRangef, NULL, 784),
    NAME_FUNC_OFFSET(12865, glGetShaderPrecisionFormat, glGetShaderPrecisionFormat, NULL, 785),
    NAME_FUNC_OFFSET(12892, glReleaseShaderCompiler, glReleaseShaderCompiler, NULL, 786),
    NAME_FUNC_OFFSET(12916, glShaderBinary, glShaderBinary, NULL, 787),
    NAME_FUNC_OFFSET(12931, glGetProgramBinary, glGetProgramBinary, NULL, 788),
    NAME_FUNC_OFFSET(12950, glProgramBinary, glProgramBinary, NULL, 789),
    NAME_FUNC_OFFSET(12966, glProgramParameteri, glProgramParameteri, NULL, 790),
    NAME_FUNC_OFFSET(12986, glDebugMessageCallbackARB, glDebugMessageCallbackARB, NULL, 791),
    NAME_FUNC_OFFSET(13012, glDebugMessageControlARB, glDebugMessageControlARB, NULL, 792),
    NAME_FUNC_OFFSET(13037, glDebugMessageInsertARB, glDebugMessageInsertARB, NULL, 793),
    NAME_FUNC_OFFSET(13061, glGetDebugMessageLogARB, glGetDebugMessageLogARB, NULL, 794),
    NAME_FUNC_OFFSET(13085, glGetGraphicsResetStatusARB, glGetGraphicsResetStatusARB, NULL, 795),
    NAME_FUNC_OFFSET(13113, glGetnColorTableARB, glGetnColorTableARB, NULL, 796),
    NAME_FUNC_OFFSET(13133, glGetnCompressedTexImageARB, glGetnCompressedTexImageARB, NULL, 797),
    NAME_FUNC_OFFSET(13161, glGetnConvolutionFilterARB, glGetnConvolutionFilterARB, NULL, 798),
    NAME_FUNC_OFFSET(13188, glGetnHistogramARB, glGetnHistogramARB, NULL, 799),
    NAME_FUNC_OFFSET(13207, glGetnMapdvARB, glGetnMapdvARB, NULL, 800),
    NAME_FUNC_OFFSET(13222, glGetnMapfvARB, glGetnMapfvARB, NULL, 801),
    NAME_FUNC_OFFSET(13237, glGetnMapivARB, glGetnMapivARB, NULL, 802),
    NAME_FUNC_OFFSET(13252, glGetnMinmaxARB, glGetnMinmaxARB, NULL, 803),
    NAME_FUNC_OFFSET(13268, glGetnPixelMapfvARB, glGetnPixelMapfvARB, NULL, 804),
    NAME_FUNC_OFFSET(13288, glGetnPixelMapuivARB, glGetnPixelMapuivARB, NULL, 805),
    NAME_FUNC_OFFSET(13309, glGetnPixelMapusvARB, glGetnPixelMapusvARB, NULL, 806),
    NAME_FUNC_OFFSET(13330, glGetnPolygonStippleARB, glGetnPolygonStippleARB, NULL, 807),
    NAME_FUNC_OFFSET(13354, glGetnSeparableFilterARB, glGetnSeparableFilterARB, NULL, 808),
    NAME_FUNC_OFFSET(13379, glGetnTexImageARB, glGetnTexImageARB, NULL, 809),
    NAME_FUNC_OFFSET(13397, glGetnUniformdvARB, glGetnUniformdvARB, NULL, 810),
    NAME_FUNC_OFFSET(13416, glGetnUniformfvARB, glGetnUniformfvARB, NULL, 811),
    NAME_FUNC_OFFSET(13435, glGetnUniformivARB, glGetnUniformivARB, NULL, 812),
    NAME_FUNC_OFFSET(13454, glGetnUniformuivARB, glGetnUniformuivARB, NULL, 813),
    NAME_FUNC_OFFSET(13474, glReadnPixelsARB, glReadnPixelsARB, NULL, 814),
    NAME_FUNC_OFFSET(13491, glDrawArraysInstancedBaseInstance, glDrawArraysInstancedBaseInstance, NULL, 815),
    NAME_FUNC_OFFSET(13525, glDrawElementsInstancedBaseInstance, glDrawElementsInstancedBaseInstance, NULL, 816),
    NAME_FUNC_OFFSET(13561, glDrawElementsInstancedBaseVertexBaseInstance, glDrawElementsInstancedBaseVertexBaseInstance, NULL, 817),
    NAME_FUNC_OFFSET(13607, glDrawTransformFeedbackInstanced, glDrawTransformFeedbackInstanced, NULL, 818),
    NAME_FUNC_OFFSET(13640, glDrawTransformFeedbackStreamInstanced, glDrawTransformFeedbackStreamInstanced, NULL, 819),
    NAME_FUNC_OFFSET(13679, gl_dispatch_stub_820, gl_dispatch_stub_820, NULL, 820),
    NAME_FUNC_OFFSET(13701, glGetActiveAtomicCounterBufferiv, glGetActiveAtomicCounterBufferiv, NULL, 821),
    NAME_FUNC_OFFSET(13734, glTexStorage1D, glTexStorage1D, NULL, 822),
    NAME_FUNC_OFFSET(13749, glTexStorage2D, glTexStorage2D, NULL, 823),
    NAME_FUNC_OFFSET(13764, glTexStorage3D, glTexStorage3D, NULL, 824),
    NAME_FUNC_OFFSET(13779, glTextureStorage1DEXT, glTextureStorage1DEXT, NULL, 825),
    NAME_FUNC_OFFSET(13801, glTextureStorage2DEXT, glTextureStorage2DEXT, NULL, 826),
    NAME_FUNC_OFFSET(13823, glTextureStorage3DEXT, glTextureStorage3DEXT, NULL, 827),
    NAME_FUNC_OFFSET(13845, glTexBufferRange, glTexBufferRange, NULL, 828),
    NAME_FUNC_OFFSET(13862, glTexStorage2DMultisample, glTexStorage2DMultisample, NULL, 829),
    NAME_FUNC_OFFSET(13888, glTexStorage3DMultisample, glTexStorage3DMultisample, NULL, 830),
    NAME_FUNC_OFFSET(13914, glInvalidateBufferData, glInvalidateBufferData, NULL, 831),
    NAME_FUNC_OFFSET(13937, glInvalidateBufferSubData, glInvalidateBufferSubData, NULL, 832),
    NAME_FUNC_OFFSET(13963, glInvalidateFramebuffer, glInvalidateFramebuffer, NULL, 833),
    NAME_FUNC_OFFSET(13987, glInvalidateSubFramebuffer, glInvalidateSubFramebuffer, NULL, 834),
    NAME_FUNC_OFFSET(14014, glInvalidateTexImage, glInvalidateTexImage, NULL, 835),
    NAME_FUNC_OFFSET(14035, glInvalidateTexSubImage, glInvalidateTexSubImage, NULL, 836),
    NAME_FUNC_OFFSET(14059, glPolygonOffsetEXT, glPolygonOffsetEXT, NULL, 837),
    NAME_FUNC_OFFSET(14078, gl_dispatch_stub_838, gl_dispatch_stub_838, NULL, 838),
    NAME_FUNC_OFFSET(14092, gl_dispatch_stub_839, gl_dispatch_stub_839, NULL, 839),
    NAME_FUNC_OFFSET(14107, gl_dispatch_stub_840, gl_dispatch_stub_840, NULL, 840),
    NAME_FUNC_OFFSET(14121, gl_dispatch_stub_841, gl_dispatch_stub_841, NULL, 841),
    NAME_FUNC_OFFSET(14136, gl_dispatch_stub_842, gl_dispatch_stub_842, NULL, 842),
    NAME_FUNC_OFFSET(14150, gl_dispatch_stub_843, gl_dispatch_stub_843, NULL, 843),
    NAME_FUNC_OFFSET(14165, gl_dispatch_stub_844, gl_dispatch_stub_844, NULL, 844),
    NAME_FUNC_OFFSET(14179, gl_dispatch_stub_845, gl_dispatch_stub_845, NULL, 845),
    NAME_FUNC_OFFSET(14194, gl_dispatch_stub_846, gl_dispatch_stub_846, NULL, 846),
    NAME_FUNC_OFFSET(14216, gl_dispatch_stub_847, gl_dispatch_stub_847, NULL, 847),
    NAME_FUNC_OFFSET(14234, gl_dispatch_stub_848, gl_dispatch_stub_848, NULL, 848),
    NAME_FUNC_OFFSET(14251, gl_dispatch_stub_849, gl_dispatch_stub_849, NULL, 849),
    NAME_FUNC_OFFSET(14271, glColorPointerEXT, glColorPointerEXT, NULL, 850),
    NAME_FUNC_OFFSET(14289, glEdgeFlagPointerEXT, glEdgeFlagPointerEXT, NULL, 851),
    NAME_FUNC_OFFSET(14310, glIndexPointerEXT, glIndexPointerEXT, NULL, 852),
    NAME_FUNC_OFFSET(14328, glNormalPointerEXT, glNormalPointerEXT, NULL, 853),
    NAME_FUNC_OFFSET(14347, glTexCoordPointerEXT, glTexCoordPointerEXT, NULL, 854),
    NAME_FUNC_OFFSET(14368, glVertexPointerEXT, glVertexPointerEXT, NULL, 855),
    NAME_FUNC_OFFSET(14387, gl_dispatch_stub_856, gl_dispatch_stub_856, NULL, 856),
    NAME_FUNC_OFFSET(14411, glLockArraysEXT, glLockArraysEXT, NULL, 857),
    NAME_FUNC_OFFSET(14427, glUnlockArraysEXT, glUnlockArraysEXT, NULL, 858),
    NAME_FUNC_OFFSET(14445, glDebugMessageCallback, glDebugMessageCallback, NULL, 859),
    NAME_FUNC_OFFSET(14468, glDebugMessageControl, glDebugMessageControl, NULL, 860),
    NAME_FUNC_OFFSET(14490, glDebugMessageInsert, glDebugMessageInsert, NULL, 861),
    NAME_FUNC_OFFSET(14511, glGetDebugMessageLog, glGetDebugMessageLog, NULL, 862),
    NAME_FUNC_OFFSET(14532, glGetObjectLabel, glGetObjectLabel, NULL, 863),
    NAME_FUNC_OFFSET(14549, glGetObjectPtrLabel, glGetObjectPtrLabel, NULL, 864),
    NAME_FUNC_OFFSET(14569, glObjectLabel, glObjectLabel, NULL, 865),
    NAME_FUNC_OFFSET(14583, glObjectPtrLabel, glObjectPtrLabel, NULL, 866),
    NAME_FUNC_OFFSET(14600, glPopDebugGroup, glPopDebugGroup, NULL, 867),
    NAME_FUNC_OFFSET(14616, glPushDebugGroup, glPushDebugGroup, NULL, 868),
    NAME_FUNC_OFFSET(14633, glSecondaryColor3fEXT, glSecondaryColor3fEXT, NULL, 869),
    NAME_FUNC_OFFSET(14655, glSecondaryColor3fvEXT, glSecondaryColor3fvEXT, NULL, 870),
    NAME_FUNC_OFFSET(14678, glMultiDrawElementsEXT, glMultiDrawElementsEXT, NULL, 871),
    NAME_FUNC_OFFSET(14701, glFogCoordfEXT, glFogCoordfEXT, NULL, 872),
    NAME_FUNC_OFFSET(14716, glFogCoordfvEXT, glFogCoordfvEXT, NULL, 873),
    NAME_FUNC_OFFSET(14732, glResizeBuffersMESA, glResizeBuffersMESA, NULL, 874),
    NAME_FUNC_OFFSET(14752, glWindowPos4dMESA, glWindowPos4dMESA, NULL, 875),
    NAME_FUNC_OFFSET(14770, glWindowPos4dvMESA, glWindowPos4dvMESA, NULL, 876),
    NAME_FUNC_OFFSET(14789, glWindowPos4fMESA, glWindowPos4fMESA, NULL, 877),
    NAME_FUNC_OFFSET(14807, glWindowPos4fvMESA, glWindowPos4fvMESA, NULL, 878),
    NAME_FUNC_OFFSET(14826, glWindowPos4iMESA, glWindowPos4iMESA, NULL, 879),
    NAME_FUNC_OFFSET(14844, glWindowPos4ivMESA, glWindowPos4ivMESA, NULL, 880),
    NAME_FUNC_OFFSET(14863, glWindowPos4sMESA, glWindowPos4sMESA, NULL, 881),
    NAME_FUNC_OFFSET(14881, glWindowPos4svMESA, glWindowPos4svMESA, NULL, 882),
    NAME_FUNC_OFFSET(14900, gl_dispatch_stub_883, gl_dispatch_stub_883, NULL, 883),
    NAME_FUNC_OFFSET(14925, gl_dispatch_stub_884, gl_dispatch_stub_884, NULL, 884),
    NAME_FUNC_OFFSET(14952, glAreProgramsResidentNV, glAreProgramsResidentNV, NULL, 885),
    NAME_FUNC_OFFSET(14976, glExecuteProgramNV, glExecuteProgramNV, NULL, 886),
    NAME_FUNC_OFFSET(14995, glGetProgramParameterdvNV, glGetProgramParameterdvNV, NULL, 887),
    NAME_FUNC_OFFSET(15021, glGetProgramParameterfvNV, glGetProgramParameterfvNV, NULL, 888),
    NAME_FUNC_OFFSET(15047, glGetProgramStringNV, glGetProgramStringNV, NULL, 889),
    NAME_FUNC_OFFSET(15068, glGetProgramivNV, glGetProgramivNV, NULL, 890),
    NAME_FUNC_OFFSET(15085, glGetTrackMatrixivNV, glGetTrackMatrixivNV, NULL, 891),
    NAME_FUNC_OFFSET(15106, glGetVertexAttribdvNV, glGetVertexAttribdvNV, NULL, 892),
    NAME_FUNC_OFFSET(15128, glGetVertexAttribfvNV, glGetVertexAttribfvNV, NULL, 893),
    NAME_FUNC_OFFSET(15150, glGetVertexAttribivNV, glGetVertexAttribivNV, NULL, 894),
    NAME_FUNC_OFFSET(15172, glLoadProgramNV, glLoadProgramNV, NULL, 895),
    NAME_FUNC_OFFSET(15188, glProgramParameters4dvNV, glProgramParameters4dvNV, NULL, 896),
    NAME_FUNC_OFFSET(15213, glProgramParameters4fvNV, glProgramParameters4fvNV, NULL, 897),
    NAME_FUNC_OFFSET(15238, glRequestResidentProgramsNV, glRequestResidentProgramsNV, NULL, 898),
    NAME_FUNC_OFFSET(15266, glTrackMatrixNV, glTrackMatrixNV, NULL, 899),
    NAME_FUNC_OFFSET(15282, glVertexAttrib1dNV, glVertexAttrib1dNV, NULL, 900),
    NAME_FUNC_OFFSET(15301, glVertexAttrib1dvNV, glVertexAttrib1dvNV, NULL, 901),
    NAME_FUNC_OFFSET(15321, glVertexAttrib1fNV, glVertexAttrib1fNV, NULL, 902),
    NAME_FUNC_OFFSET(15340, glVertexAttrib1fvNV, glVertexAttrib1fvNV, NULL, 903),
    NAME_FUNC_OFFSET(15360, glVertexAttrib1sNV, glVertexAttrib1sNV, NULL, 904),
    NAME_FUNC_OFFSET(15379, glVertexAttrib1svNV, glVertexAttrib1svNV, NULL, 905),
    NAME_FUNC_OFFSET(15399, glVertexAttrib2dNV, glVertexAttrib2dNV, NULL, 906),
    NAME_FUNC_OFFSET(15418, glVertexAttrib2dvNV, glVertexAttrib2dvNV, NULL, 907),
    NAME_FUNC_OFFSET(15438, glVertexAttrib2fNV, glVertexAttrib2fNV, NULL, 908),
    NAME_FUNC_OFFSET(15457, glVertexAttrib2fvNV, glVertexAttrib2fvNV, NULL, 909),
    NAME_FUNC_OFFSET(15477, glVertexAttrib2sNV, glVertexAttrib2sNV, NULL, 910),
    NAME_FUNC_OFFSET(15496, glVertexAttrib2svNV, glVertexAttrib2svNV, NULL, 911),
    NAME_FUNC_OFFSET(15516, glVertexAttrib3dNV, glVertexAttrib3dNV, NULL, 912),
    NAME_FUNC_OFFSET(15535, glVertexAttrib3dvNV, glVertexAttrib3dvNV, NULL, 913),
    NAME_FUNC_OFFSET(15555, glVertexAttrib3fNV, glVertexAttrib3fNV, NULL, 914),
    NAME_FUNC_OFFSET(15574, glVertexAttrib3fvNV, glVertexAttrib3fvNV, NULL, 915),
    NAME_FUNC_OFFSET(15594, glVertexAttrib3sNV, glVertexAttrib3sNV, NULL, 916),
    NAME_FUNC_OFFSET(15613, glVertexAttrib3svNV, glVertexAttrib3svNV, NULL, 917),
    NAME_FUNC_OFFSET(15633, glVertexAttrib4dNV, glVertexAttrib4dNV, NULL, 918),
    NAME_FUNC_OFFSET(15652, glVertexAttrib4dvNV, glVertexAttrib4dvNV, NULL, 919),
    NAME_FUNC_OFFSET(15672, glVertexAttrib4fNV, glVertexAttrib4fNV, NULL, 920),
    NAME_FUNC_OFFSET(15691, glVertexAttrib4fvNV, glVertexAttrib4fvNV, NULL, 921),
    NAME_FUNC_OFFSET(15711, glVertexAttrib4sNV, glVertexAttrib4sNV, NULL, 922),
    NAME_FUNC_OFFSET(15730, glVertexAttrib4svNV, glVertexAttrib4svNV, NULL, 923),
    NAME_FUNC_OFFSET(15750, glVertexAttrib4ubNV, glVertexAttrib4ubNV, NULL, 924),
    NAME_FUNC_OFFSET(15770, glVertexAttrib4ubvNV, glVertexAttrib4ubvNV, NULL, 925),
    NAME_FUNC_OFFSET(15791, glVertexAttribPointerNV, glVertexAttribPointerNV, NULL, 926),
    NAME_FUNC_OFFSET(15815, glVertexAttribs1dvNV, glVertexAttribs1dvNV, NULL, 927),
    NAME_FUNC_OFFSET(15836, glVertexAttribs1fvNV, glVertexAttribs1fvNV, NULL, 928),
    NAME_FUNC_OFFSET(15857, glVertexAttribs1svNV, glVertexAttribs1svNV, NULL, 929),
    NAME_FUNC_OFFSET(15878, glVertexAttribs2dvNV, glVertexAttribs2dvNV, NULL, 930),
    NAME_FUNC_OFFSET(15899, glVertexAttribs2fvNV, glVertexAttribs2fvNV, NULL, 931),
    NAME_FUNC_OFFSET(15920, glVertexAttribs2svNV, glVertexAttribs2svNV, NULL, 932),
    NAME_FUNC_OFFSET(15941, glVertexAttribs3dvNV, glVertexAttribs3dvNV, NULL, 933),
    NAME_FUNC_OFFSET(15962, glVertexAttribs3fvNV, glVertexAttribs3fvNV, NULL, 934),
    NAME_FUNC_OFFSET(15983, glVertexAttribs3svNV, glVertexAttribs3svNV, NULL, 935),
    NAME_FUNC_OFFSET(16004, glVertexAttribs4dvNV, glVertexAttribs4dvNV, NULL, 936),
    NAME_FUNC_OFFSET(16025, glVertexAttribs4fvNV, glVertexAttribs4fvNV, NULL, 937),
    NAME_FUNC_OFFSET(16046, glVertexAttribs4svNV, glVertexAttribs4svNV, NULL, 938),
    NAME_FUNC_OFFSET(16067, glVertexAttribs4ubvNV, glVertexAttribs4ubvNV, NULL, 939),
    NAME_FUNC_OFFSET(16089, glGetTexBumpParameterfvATI, glGetTexBumpParameterfvATI, NULL, 940),
    NAME_FUNC_OFFSET(16116, glGetTexBumpParameterivATI, glGetTexBumpParameterivATI, NULL, 941),
    NAME_FUNC_OFFSET(16143, glTexBumpParameterfvATI, glTexBumpParameterfvATI, NULL, 942),
    NAME_FUNC_OFFSET(16167, glTexBumpParameterivATI, glTexBumpParameterivATI, NULL, 943),
    NAME_FUNC_OFFSET(16191, glAlphaFragmentOp1ATI, glAlphaFragmentOp1ATI, NULL, 944),
    NAME_FUNC_OFFSET(16213, glAlphaFragmentOp2ATI, glAlphaFragmentOp2ATI, NULL, 945),
    NAME_FUNC_OFFSET(16235, glAlphaFragmentOp3ATI, glAlphaFragmentOp3ATI, NULL, 946),
    NAME_FUNC_OFFSET(16257, glBeginFragmentShaderATI, glBeginFragmentShaderATI, NULL, 947),
    NAME_FUNC_OFFSET(16282, glBindFragmentShaderATI, glBindFragmentShaderATI, NULL, 948),
    NAME_FUNC_OFFSET(16306, glColorFragmentOp1ATI, glColorFragmentOp1ATI, NULL, 949),
    NAME_FUNC_OFFSET(16328, glColorFragmentOp2ATI, glColorFragmentOp2ATI, NULL, 950),
    NAME_FUNC_OFFSET(16350, glColorFragmentOp3ATI, glColorFragmentOp3ATI, NULL, 951),
    NAME_FUNC_OFFSET(16372, glDeleteFragmentShaderATI, glDeleteFragmentShaderATI, NULL, 952),
    NAME_FUNC_OFFSET(16398, glEndFragmentShaderATI, glEndFragmentShaderATI, NULL, 953),
    NAME_FUNC_OFFSET(16421, glGenFragmentShadersATI, glGenFragmentShadersATI, NULL, 954),
    NAME_FUNC_OFFSET(16445, glPassTexCoordATI, glPassTexCoordATI, NULL, 955),
    NAME_FUNC_OFFSET(16463, glSampleMapATI, glSampleMapATI, NULL, 956),
    NAME_FUNC_OFFSET(16478, glSetFragmentShaderConstantATI, glSetFragmentShaderConstantATI, NULL, 957),
    NAME_FUNC_OFFSET(16509, gl_dispatch_stub_958, gl_dispatch_stub_958, NULL, 958),
    NAME_FUNC_OFFSET(16532, gl_dispatch_stub_959, gl_dispatch_stub_959, NULL, 959),
    NAME_FUNC_OFFSET(16555, gl_dispatch_stub_960, gl_dispatch_stub_960, NULL, 960),
    NAME_FUNC_OFFSET(16578, glGetProgramNamedParameterdvNV, glGetProgramNamedParameterdvNV, NULL, 961),
    NAME_FUNC_OFFSET(16609, glGetProgramNamedParameterfvNV, glGetProgramNamedParameterfvNV, NULL, 962),
    NAME_FUNC_OFFSET(16640, glProgramNamedParameter4dNV, glProgramNamedParameter4dNV, NULL, 963),
    NAME_FUNC_OFFSET(16668, glProgramNamedParameter4dvNV, glProgramNamedParameter4dvNV, NULL, 964),
    NAME_FUNC_OFFSET(16697, glProgramNamedParameter4fNV, glProgramNamedParameter4fNV, NULL, 965),
    NAME_FUNC_OFFSET(16725, glProgramNamedParameter4fvNV, glProgramNamedParameter4fvNV, NULL, 966),
    NAME_FUNC_OFFSET(16754, glPrimitiveRestartNV, glPrimitiveRestartNV, NULL, 967),
    NAME_FUNC_OFFSET(16775, gl_dispatch_stub_968, gl_dispatch_stub_968, NULL, 968),
    NAME_FUNC_OFFSET(16792, gl_dispatch_stub_969, gl_dispatch_stub_969, NULL, 969),
    NAME_FUNC_OFFSET(16805, gl_dispatch_stub_970, gl_dispatch_stub_970, NULL, 970),
    NAME_FUNC_OFFSET(16819, gl_dispatch_stub_971, gl_dispatch_stub_971, NULL, 971),
    NAME_FUNC_OFFSET(16836, glBindFramebufferEXT, glBindFramebufferEXT, NULL, 972),
    NAME_FUNC_OFFSET(16857, glBindRenderbufferEXT, glBindRenderbufferEXT, NULL, 973),
    NAME_FUNC_OFFSET(16879, gl_dispatch_stub_974, gl_dispatch_stub_974, NULL, 974),
    NAME_FUNC_OFFSET(16903, gl_dispatch_stub_975, gl_dispatch_stub_975, NULL, 975),
    NAME_FUNC_OFFSET(16933, glVertexAttribI1iEXT, glVertexAttribI1iEXT, NULL, 976),
    NAME_FUNC_OFFSET(16954, glVertexAttribI1uiEXT, glVertexAttribI1uiEXT, NULL, 977),
    NAME_FUNC_OFFSET(16976, glVertexAttribI2iEXT, glVertexAttribI2iEXT, NULL, 978),
    NAME_FUNC_OFFSET(16997, glVertexAttribI2ivEXT, glVertexAttribI2ivEXT, NULL, 979),
    NAME_FUNC_OFFSET(17019, glVertexAttribI2uiEXT, glVertexAttribI2uiEXT, NULL, 980),
    NAME_FUNC_OFFSET(17041, glVertexAttribI2uivEXT, glVertexAttribI2uivEXT, NULL, 981),
    NAME_FUNC_OFFSET(17064, glVertexAttribI3iEXT, glVertexAttribI3iEXT, NULL, 982),
    NAME_FUNC_OFFSET(17085, glVertexAttribI3ivEXT, glVertexAttribI3ivEXT, NULL, 983),
    NAME_FUNC_OFFSET(17107, glVertexAttribI3uiEXT, glVertexAttribI3uiEXT, NULL, 984),
    NAME_FUNC_OFFSET(17129, glVertexAttribI3uivEXT, glVertexAttribI3uivEXT, NULL, 985),
    NAME_FUNC_OFFSET(17152, glVertexAttribI4iEXT, glVertexAttribI4iEXT, NULL, 986),
    NAME_FUNC_OFFSET(17173, glVertexAttribI4ivEXT, glVertexAttribI4ivEXT, NULL, 987),
    NAME_FUNC_OFFSET(17195, glVertexAttribI4uiEXT, glVertexAttribI4uiEXT, NULL, 988),
    NAME_FUNC_OFFSET(17217, glVertexAttribI4uivEXT, glVertexAttribI4uivEXT, NULL, 989),
    NAME_FUNC_OFFSET(17240, glClearColorIiEXT, glClearColorIiEXT, NULL, 990),
    NAME_FUNC_OFFSET(17258, glClearColorIuiEXT, glClearColorIuiEXT, NULL, 991),
    NAME_FUNC_OFFSET(17277, glBindBufferOffsetEXT, glBindBufferOffsetEXT, NULL, 992),
    NAME_FUNC_OFFSET(17299, glBeginPerfMonitorAMD, glBeginPerfMonitorAMD, NULL, 993),
    NAME_FUNC_OFFSET(17321, glDeletePerfMonitorsAMD, glDeletePerfMonitorsAMD, NULL, 994),
    NAME_FUNC_OFFSET(17345, glEndPerfMonitorAMD, glEndPerfMonitorAMD, NULL, 995),
    NAME_FUNC_OFFSET(17365, glGenPerfMonitorsAMD, glGenPerfMonitorsAMD, NULL, 996),
    NAME_FUNC_OFFSET(17386, glGetPerfMonitorCounterDataAMD, glGetPerfMonitorCounterDataAMD, NULL, 997),
    NAME_FUNC_OFFSET(17417, glGetPerfMonitorCounterInfoAMD, glGetPerfMonitorCounterInfoAMD, NULL, 998),
    NAME_FUNC_OFFSET(17448, glGetPerfMonitorCounterStringAMD, glGetPerfMonitorCounterStringAMD, NULL, 999),
    NAME_FUNC_OFFSET(17481, glGetPerfMonitorCountersAMD, glGetPerfMonitorCountersAMD, NULL, 1000),
    NAME_FUNC_OFFSET(17509, glGetPerfMonitorGroupStringAMD, glGetPerfMonitorGroupStringAMD, NULL, 1001),
    NAME_FUNC_OFFSET(17540, glGetPerfMonitorGroupsAMD, glGetPerfMonitorGroupsAMD, NULL, 1002),
    NAME_FUNC_OFFSET(17566, glSelectPerfMonitorCountersAMD, glSelectPerfMonitorCountersAMD, NULL, 1003),
    NAME_FUNC_OFFSET(17597, glGetObjectParameterivAPPLE, glGetObjectParameterivAPPLE, NULL, 1004),
    NAME_FUNC_OFFSET(17625, glObjectPurgeableAPPLE, glObjectPurgeableAPPLE, NULL, 1005),
    NAME_FUNC_OFFSET(17648, glObjectUnpurgeableAPPLE, glObjectUnpurgeableAPPLE, NULL, 1006),
    NAME_FUNC_OFFSET(17673, glActiveProgramEXT, glActiveProgramEXT, NULL, 1007),
    NAME_FUNC_OFFSET(17692, glCreateShaderProgramEXT, glCreateShaderProgramEXT, NULL, 1008),
    NAME_FUNC_OFFSET(17717, glUseShaderProgramEXT, glUseShaderProgramEXT, NULL, 1009),
    NAME_FUNC_OFFSET(17739, glTextureBarrierNV, glTextureBarrierNV, NULL, 1010),
    NAME_FUNC_OFFSET(17758, glVDPAUFiniNV, glVDPAUFiniNV, NULL, 1011),
    NAME_FUNC_OFFSET(17772, glVDPAUGetSurfaceivNV, glVDPAUGetSurfaceivNV, NULL, 1012),
    NAME_FUNC_OFFSET(17794, glVDPAUInitNV, glVDPAUInitNV, NULL, 1013),
    NAME_FUNC_OFFSET(17808, glVDPAUIsSurfaceNV, glVDPAUIsSurfaceNV, NULL, 1014),
    NAME_FUNC_OFFSET(17827, glVDPAUMapSurfacesNV, glVDPAUMapSurfacesNV, NULL, 1015),
    NAME_FUNC_OFFSET(17848, glVDPAURegisterOutputSurfaceNV, glVDPAURegisterOutputSurfaceNV, NULL, 1016),
    NAME_FUNC_OFFSET(17879, glVDPAURegisterVideoSurfaceNV, glVDPAURegisterVideoSurfaceNV, NULL, 1017),
    NAME_FUNC_OFFSET(17909, glVDPAUSurfaceAccessNV, glVDPAUSurfaceAccessNV, NULL, 1018),
    NAME_FUNC_OFFSET(17932, glVDPAUUnmapSurfacesNV, glVDPAUUnmapSurfacesNV, NULL, 1019),
    NAME_FUNC_OFFSET(17955, glVDPAUUnregisterSurfaceNV, glVDPAUUnregisterSurfaceNV, NULL, 1020),
    NAME_FUNC_OFFSET(17982, gl_dispatch_stub_1021, gl_dispatch_stub_1021, NULL, 1021),
    NAME_FUNC_OFFSET(18007, gl_dispatch_stub_1022, gl_dispatch_stub_1022, NULL, 1022),
    NAME_FUNC_OFFSET(18036, gl_dispatch_stub_1023, gl_dispatch_stub_1023, NULL, 1023),
    NAME_FUNC_OFFSET(18067, glEGLImageTargetRenderbufferStorageOES, glEGLImageTargetRenderbufferStorageOES, NULL, 1024),
    NAME_FUNC_OFFSET(18106, glEGLImageTargetTexture2DOES, glEGLImageTargetTexture2DOES, NULL, 1025),
    NAME_FUNC_OFFSET(18135, gl_dispatch_stub_1026, gl_dispatch_stub_1026, NULL, 1026),
    NAME_FUNC_OFFSET(18148, gl_dispatch_stub_1027, gl_dispatch_stub_1027, NULL, 1027),
    NAME_FUNC_OFFSET(18162, gl_dispatch_stub_1028, gl_dispatch_stub_1028, NULL, 1028),
    NAME_FUNC_OFFSET(18176, gl_dispatch_stub_1029, gl_dispatch_stub_1029, NULL, 1029),
    NAME_FUNC_OFFSET(18186, gl_dispatch_stub_1030, gl_dispatch_stub_1030, NULL, 1030),
    NAME_FUNC_OFFSET(18200, gl_dispatch_stub_1031, gl_dispatch_stub_1031, NULL, 1031),
    NAME_FUNC_OFFSET(18207, gl_dispatch_stub_1032, gl_dispatch_stub_1032, NULL, 1032),
    NAME_FUNC_OFFSET(18215, gl_dispatch_stub_1033, gl_dispatch_stub_1033, NULL, 1033),
    NAME_FUNC_OFFSET(18226, gl_dispatch_stub_1034, gl_dispatch_stub_1034, NULL, 1034),
    NAME_FUNC_OFFSET(18237, gl_dispatch_stub_1035, gl_dispatch_stub_1035, NULL, 1035),
    NAME_FUNC_OFFSET(18251, gl_dispatch_stub_1036, gl_dispatch_stub_1036, NULL, 1036),
    NAME_FUNC_OFFSET(18266, gl_dispatch_stub_1037, gl_dispatch_stub_1037, NULL, 1037),
    NAME_FUNC_OFFSET(18275, gl_dispatch_stub_1038, gl_dispatch_stub_1038, NULL, 1038),
    NAME_FUNC_OFFSET(18285, gl_dispatch_stub_1039, gl_dispatch_stub_1039, NULL, 1039),
    NAME_FUNC_OFFSET(18298, gl_dispatch_stub_1040, gl_dispatch_stub_1040, NULL, 1040),
    NAME_FUNC_OFFSET(18312, gl_dispatch_stub_1041, gl_dispatch_stub_1041, NULL, 1041),
    NAME_FUNC_OFFSET(18324, gl_dispatch_stub_1042, gl_dispatch_stub_1042, NULL, 1042),
    NAME_FUNC_OFFSET(18337, gl_dispatch_stub_1043, gl_dispatch_stub_1043, NULL, 1043),
    NAME_FUNC_OFFSET(18351, gl_dispatch_stub_1044, gl_dispatch_stub_1044, NULL, 1044),
    NAME_FUNC_OFFSET(18369, gl_dispatch_stub_1045, gl_dispatch_stub_1045, NULL, 1045),
    NAME_FUNC_OFFSET(18380, gl_dispatch_stub_1046, gl_dispatch_stub_1046, NULL, 1046),
    NAME_FUNC_OFFSET(18389, gl_dispatch_stub_1047, gl_dispatch_stub_1047, NULL, 1047),
    NAME_FUNC_OFFSET(18398, gl_dispatch_stub_1048, gl_dispatch_stub_1048, NULL, 1048),
    NAME_FUNC_OFFSET(18411, gl_dispatch_stub_1049, gl_dispatch_stub_1049, NULL, 1049),
    NAME_FUNC_OFFSET(18428, gl_dispatch_stub_1050, gl_dispatch_stub_1050, NULL, 1050),
    NAME_FUNC_OFFSET(18438, gl_dispatch_stub_1051, gl_dispatch_stub_1051, NULL, 1051),
    NAME_FUNC_OFFSET(18456, gl_dispatch_stub_1052, gl_dispatch_stub_1052, NULL, 1052),
    NAME_FUNC_OFFSET(18465, gl_dispatch_stub_1053, gl_dispatch_stub_1053, NULL, 1053),
    NAME_FUNC_OFFSET(18475, gl_dispatch_stub_1054, gl_dispatch_stub_1054, NULL, 1054),
    NAME_FUNC_OFFSET(18486, gl_dispatch_stub_1055, gl_dispatch_stub_1055, NULL, 1055),
    NAME_FUNC_OFFSET(18502, gl_dispatch_stub_1056, gl_dispatch_stub_1056, NULL, 1056),
    NAME_FUNC_OFFSET(18515, gl_dispatch_stub_1057, gl_dispatch_stub_1057, NULL, 1057),
    NAME_FUNC_OFFSET(18528, gl_dispatch_stub_1058, gl_dispatch_stub_1058, NULL, 1058),
    NAME_FUNC_OFFSET(18541, gl_dispatch_stub_1059, gl_dispatch_stub_1059, NULL, 1059),
    NAME_FUNC_OFFSET(18557, gl_dispatch_stub_1060, gl_dispatch_stub_1060, NULL, 1060),
    NAME_FUNC_OFFSET(18573, gl_dispatch_stub_1061, gl_dispatch_stub_1061, NULL, 1061),
    NAME_FUNC_OFFSET(18585, gl_dispatch_stub_1062, gl_dispatch_stub_1062, NULL, 1062),
    NAME_FUNC_OFFSET(18598, gl_dispatch_stub_1063, gl_dispatch_stub_1063, NULL, 1063),
    NAME_FUNC_OFFSET(18614, gl_dispatch_stub_1064, gl_dispatch_stub_1064, NULL, 1064),
    NAME_FUNC_OFFSET(18628, gl_dispatch_stub_1065, gl_dispatch_stub_1065, NULL, 1065),
    NAME_FUNC_OFFSET(18648, gl_dispatch_stub_1066, gl_dispatch_stub_1066, NULL, 1066),
    NAME_FUNC_OFFSET(18666, gl_dispatch_stub_1067, gl_dispatch_stub_1067, NULL, 1067),
    NAME_FUNC_OFFSET(18685, gl_dispatch_stub_1068, gl_dispatch_stub_1068, NULL, 1068),
    NAME_FUNC_OFFSET(18702, glTexGenf, glTexGenf, NULL, 190),
    NAME_FUNC_OFFSET(18715, glTexGenfv, glTexGenfv, NULL, 191),
    NAME_FUNC_OFFSET(18729, glTexGeni, glTexGeni, NULL, 192),
    NAME_FUNC_OFFSET(18742, glTexGeniv, glTexGeniv, NULL, 193),
    NAME_FUNC_OFFSET(18756, glReadBuffer, glReadBuffer, NULL, 254),
    NAME_FUNC_OFFSET(18771, glGetTexGenfv, glGetTexGenfv, NULL, 279),
    NAME_FUNC_OFFSET(18788, glGetTexGeniv, glGetTexGeniv, NULL, 280),
    NAME_FUNC_OFFSET(18805, glArrayElement, glArrayElement, NULL, 306),
    NAME_FUNC_OFFSET(18823, glBindTexture, glBindTexture, NULL, 307),
    NAME_FUNC_OFFSET(18840, glDrawArrays, glDrawArrays, NULL, 310),
    NAME_FUNC_OFFSET(18856, glAreTexturesResident, glAreTexturesResidentEXT, glAreTexturesResidentEXT, 322),
    NAME_FUNC_OFFSET(18881, glCopyTexImage1D, glCopyTexImage1D, NULL, 323),
    NAME_FUNC_OFFSET(18901, glCopyTexImage2D, glCopyTexImage2D, NULL, 324),
    NAME_FUNC_OFFSET(18921, glCopyTexSubImage1D, glCopyTexSubImage1D, NULL, 325),
    NAME_FUNC_OFFSET(18944, glCopyTexSubImage2D, glCopyTexSubImage2D, NULL, 326),
    NAME_FUNC_OFFSET(18967, glDeleteTextures, glDeleteTexturesEXT, glDeleteTexturesEXT, 327),
    NAME_FUNC_OFFSET(18987, glGenTextures, glGenTexturesEXT, glGenTexturesEXT, 328),
    NAME_FUNC_OFFSET(19004, glGetPointerv, glGetPointerv, NULL, 329),
    NAME_FUNC_OFFSET(19021, glIsTexture, glIsTextureEXT, glIsTextureEXT, 330),
    NAME_FUNC_OFFSET(19036, glPrioritizeTextures, glPrioritizeTextures, NULL, 331),
    NAME_FUNC_OFFSET(19060, glTexSubImage1D, glTexSubImage1D, NULL, 332),
    NAME_FUNC_OFFSET(19079, glTexSubImage2D, glTexSubImage2D, NULL, 333),
    NAME_FUNC_OFFSET(19098, glBlendColor, glBlendColor, NULL, 336),
    NAME_FUNC_OFFSET(19114, glBlendEquation, glBlendEquation, NULL, 337),
    NAME_FUNC_OFFSET(19133, glBlendEquation, glBlendEquation, NULL, 337),
    NAME_FUNC_OFFSET(19152, glDrawRangeElements, glDrawRangeElements, NULL, 338),
    NAME_FUNC_OFFSET(19175, glColorTable, glColorTable, NULL, 339),
    NAME_FUNC_OFFSET(19191, glColorTable, glColorTable, NULL, 339),
    NAME_FUNC_OFFSET(19207, glColorTableParameterfv, glColorTableParameterfv, NULL, 340),
    NAME_FUNC_OFFSET(19234, glColorTableParameteriv, glColorTableParameteriv, NULL, 341),
    NAME_FUNC_OFFSET(19261, glCopyColorTable, glCopyColorTable, NULL, 342),
    NAME_FUNC_OFFSET(19281, glGetColorTable, glGetColorTableEXT, glGetColorTableEXT, 343),
    NAME_FUNC_OFFSET(19300, glGetColorTable, glGetColorTableEXT, glGetColorTableEXT, 343),
    NAME_FUNC_OFFSET(19319, glGetColorTableParameterfv, glGetColorTableParameterfvEXT, glGetColorTableParameterfvEXT, 344),
    NAME_FUNC_OFFSET(19349, glGetColorTableParameterfv, glGetColorTableParameterfvEXT, glGetColorTableParameterfvEXT, 344),
    NAME_FUNC_OFFSET(19379, glGetColorTableParameteriv, glGetColorTableParameterivEXT, glGetColorTableParameterivEXT, 345),
    NAME_FUNC_OFFSET(19409, glGetColorTableParameteriv, glGetColorTableParameterivEXT, glGetColorTableParameterivEXT, 345),
    NAME_FUNC_OFFSET(19439, glColorSubTable, glColorSubTable, NULL, 346),
    NAME_FUNC_OFFSET(19458, glCopyColorSubTable, glCopyColorSubTable, NULL, 347),
    NAME_FUNC_OFFSET(19481, glConvolutionFilter1D, glConvolutionFilter1D, NULL, 348),
    NAME_FUNC_OFFSET(19506, glConvolutionFilter2D, glConvolutionFilter2D, NULL, 349),
    NAME_FUNC_OFFSET(19531, glConvolutionParameterf, glConvolutionParameterf, NULL, 350),
    NAME_FUNC_OFFSET(19558, glConvolutionParameterfv, glConvolutionParameterfv, NULL, 351),
    NAME_FUNC_OFFSET(19586, glConvolutionParameteri, glConvolutionParameteri, NULL, 352),
    NAME_FUNC_OFFSET(19613, glConvolutionParameteriv, glConvolutionParameteriv, NULL, 353),
    NAME_FUNC_OFFSET(19641, glCopyConvolutionFilter1D, glCopyConvolutionFilter1D, NULL, 354),
    NAME_FUNC_OFFSET(19670, glCopyConvolutionFilter2D, glCopyConvolutionFilter2D, NULL, 355),
    NAME_FUNC_OFFSET(19699, glGetConvolutionFilter, gl_dispatch_stub_356, gl_dispatch_stub_356, 356),
    NAME_FUNC_OFFSET(19725, glGetConvolutionParameterfv, gl_dispatch_stub_357, gl_dispatch_stub_357, 357),
    NAME_FUNC_OFFSET(19756, glGetConvolutionParameteriv, gl_dispatch_stub_358, gl_dispatch_stub_358, 358),
    NAME_FUNC_OFFSET(19787, glGetSeparableFilter, gl_dispatch_stub_359, gl_dispatch_stub_359, 359),
    NAME_FUNC_OFFSET(19811, glSeparableFilter2D, glSeparableFilter2D, NULL, 360),
    NAME_FUNC_OFFSET(19834, glGetHistogram, gl_dispatch_stub_361, gl_dispatch_stub_361, 361),
    NAME_FUNC_OFFSET(19852, glGetHistogramParameterfv, gl_dispatch_stub_362, gl_dispatch_stub_362, 362),
    NAME_FUNC_OFFSET(19881, glGetHistogramParameteriv, gl_dispatch_stub_363, gl_dispatch_stub_363, 363),
    NAME_FUNC_OFFSET(19910, glGetMinmax, gl_dispatch_stub_364, gl_dispatch_stub_364, 364),
    NAME_FUNC_OFFSET(19925, glGetMinmaxParameterfv, gl_dispatch_stub_365, gl_dispatch_stub_365, 365),
    NAME_FUNC_OFFSET(19951, glGetMinmaxParameteriv, gl_dispatch_stub_366, gl_dispatch_stub_366, 366),
    NAME_FUNC_OFFSET(19977, glHistogram, glHistogram, NULL, 367),
    NAME_FUNC_OFFSET(19992, glMinmax, glMinmax, NULL, 368),
    NAME_FUNC_OFFSET(20004, glResetHistogram, glResetHistogram, NULL, 369),
    NAME_FUNC_OFFSET(20024, glResetMinmax, glResetMinmax, NULL, 370),
    NAME_FUNC_OFFSET(20041, glTexImage3D, glTexImage3D, NULL, 371),
    NAME_FUNC_OFFSET(20057, glTexImage3D, glTexImage3D, NULL, 371),
    NAME_FUNC_OFFSET(20073, glTexSubImage3D, glTexSubImage3D, NULL, 372),
    NAME_FUNC_OFFSET(20092, glTexSubImage3D, glTexSubImage3D, NULL, 372),
    NAME_FUNC_OFFSET(20111, glCopyTexSubImage3D, glCopyTexSubImage3D, NULL, 373),
    NAME_FUNC_OFFSET(20134, glCopyTexSubImage3D, glCopyTexSubImage3D, NULL, 373),
    NAME_FUNC_OFFSET(20157, glActiveTexture, glActiveTexture, NULL, 374),
    NAME_FUNC_OFFSET(20176, glClientActiveTexture, glClientActiveTexture, NULL, 375),
    NAME_FUNC_OFFSET(20201, glMultiTexCoord1d, glMultiTexCoord1d, NULL, 376),
    NAME_FUNC_OFFSET(20222, glMultiTexCoord1dv, glMultiTexCoord1dv, NULL, 377),
    NAME_FUNC_OFFSET(20244, glMultiTexCoord1fARB, glMultiTexCoord1fARB, NULL, 378),
    NAME_FUNC_OFFSET(20262, glMultiTexCoord1fvARB, glMultiTexCoord1fvARB, NULL, 379),
    NAME_FUNC_OFFSET(20281, glMultiTexCoord1i, glMultiTexCoord1i, NULL, 380),
    NAME_FUNC_OFFSET(20302, glMultiTexCoord1iv, glMultiTexCoord1iv, NULL, 381),
    NAME_FUNC_OFFSET(20324, glMultiTexCoord1s, glMultiTexCoord1s, NULL, 382),
    NAME_FUNC_OFFSET(20345, glMultiTexCoord1sv, glMultiTexCoord1sv, NULL, 383),
    NAME_FUNC_OFFSET(20367, glMultiTexCoord2d, glMultiTexCoord2d, NULL, 384),
    NAME_FUNC_OFFSET(20388, glMultiTexCoord2dv, glMultiTexCoord2dv, NULL, 385),
    NAME_FUNC_OFFSET(20410, glMultiTexCoord2fARB, glMultiTexCoord2fARB, NULL, 386),
    NAME_FUNC_OFFSET(20428, glMultiTexCoord2fvARB, glMultiTexCoord2fvARB, NULL, 387),
    NAME_FUNC_OFFSET(20447, glMultiTexCoord2i, glMultiTexCoord2i, NULL, 388),
    NAME_FUNC_OFFSET(20468, glMultiTexCoord2iv, glMultiTexCoord2iv, NULL, 389),
    NAME_FUNC_OFFSET(20490, glMultiTexCoord2s, glMultiTexCoord2s, NULL, 390),
    NAME_FUNC_OFFSET(20511, glMultiTexCoord2sv, glMultiTexCoord2sv, NULL, 391),
    NAME_FUNC_OFFSET(20533, glMultiTexCoord3d, glMultiTexCoord3d, NULL, 392),
    NAME_FUNC_OFFSET(20554, glMultiTexCoord3dv, glMultiTexCoord3dv, NULL, 393),
    NAME_FUNC_OFFSET(20576, glMultiTexCoord3fARB, glMultiTexCoord3fARB, NULL, 394),
    NAME_FUNC_OFFSET(20594, glMultiTexCoord3fvARB, glMultiTexCoord3fvARB, NULL, 395),
    NAME_FUNC_OFFSET(20613, glMultiTexCoord3i, glMultiTexCoord3i, NULL, 396),
    NAME_FUNC_OFFSET(20634, glMultiTexCoord3iv, glMultiTexCoord3iv, NULL, 397),
    NAME_FUNC_OFFSET(20656, glMultiTexCoord3s, glMultiTexCoord3s, NULL, 398),
    NAME_FUNC_OFFSET(20677, glMultiTexCoord3sv, glMultiTexCoord3sv, NULL, 399),
    NAME_FUNC_OFFSET(20699, glMultiTexCoord4d, glMultiTexCoord4d, NULL, 400),
    NAME_FUNC_OFFSET(20720, glMultiTexCoord4dv, glMultiTexCoord4dv, NULL, 401),
    NAME_FUNC_OFFSET(20742, glMultiTexCoord4fARB, glMultiTexCoord4fARB, NULL, 402),
    NAME_FUNC_OFFSET(20760, glMultiTexCoord4fvARB, glMultiTexCoord4fvARB, NULL, 403),
    NAME_FUNC_OFFSET(20779, glMultiTexCoord4i, glMultiTexCoord4i, NULL, 404),
    NAME_FUNC_OFFSET(20800, glMultiTexCoord4iv, glMultiTexCoord4iv, NULL, 405),
    NAME_FUNC_OFFSET(20822, glMultiTexCoord4s, glMultiTexCoord4s, NULL, 406),
    NAME_FUNC_OFFSET(20843, glMultiTexCoord4sv, glMultiTexCoord4sv, NULL, 407),
    NAME_FUNC_OFFSET(20865, glCompressedTexImage1D, glCompressedTexImage1D, NULL, 408),
    NAME_FUNC_OFFSET(20891, glCompressedTexImage2D, glCompressedTexImage2D, NULL, 409),
    NAME_FUNC_OFFSET(20917, glCompressedTexImage3D, glCompressedTexImage3D, NULL, 410),
    NAME_FUNC_OFFSET(20943, glCompressedTexImage3D, glCompressedTexImage3D, NULL, 410),
    NAME_FUNC_OFFSET(20969, glCompressedTexSubImage1D, glCompressedTexSubImage1D, NULL, 411),
    NAME_FUNC_OFFSET(20998, glCompressedTexSubImage2D, glCompressedTexSubImage2D, NULL, 412),
    NAME_FUNC_OFFSET(21027, glCompressedTexSubImage3D, glCompressedTexSubImage3D, NULL, 413),
    NAME_FUNC_OFFSET(21056, glCompressedTexSubImage3D, glCompressedTexSubImage3D, NULL, 413),
    NAME_FUNC_OFFSET(21085, glGetCompressedTexImage, glGetCompressedTexImage, NULL, 414),
    NAME_FUNC_OFFSET(21112, glLoadTransposeMatrixd, glLoadTransposeMatrixd, NULL, 415),
    NAME_FUNC_OFFSET(21138, glLoadTransposeMatrixf, glLoadTransposeMatrixf, NULL, 416),
    NAME_FUNC_OFFSET(21164, glMultTransposeMatrixd, glMultTransposeMatrixd, NULL, 417),
    NAME_FUNC_OFFSET(21190, glMultTransposeMatrixf, glMultTransposeMatrixf, NULL, 418),
    NAME_FUNC_OFFSET(21216, glSampleCoverage, glSampleCoverage, NULL, 419),
    NAME_FUNC_OFFSET(21236, glBlendFuncSeparate, glBlendFuncSeparate, NULL, 420),
    NAME_FUNC_OFFSET(21259, glBlendFuncSeparate, glBlendFuncSeparate, NULL, 420),
    NAME_FUNC_OFFSET(21283, glBlendFuncSeparate, glBlendFuncSeparate, NULL, 420),
    NAME_FUNC_OFFSET(21306, glFogCoordPointer, glFogCoordPointer, NULL, 421),
    NAME_FUNC_OFFSET(21327, glFogCoordd, glFogCoordd, NULL, 422),
    NAME_FUNC_OFFSET(21342, glFogCoorddv, glFogCoorddv, NULL, 423),
    NAME_FUNC_OFFSET(21358, glMultiDrawArrays, glMultiDrawArrays, NULL, 424),
    NAME_FUNC_OFFSET(21379, glPointParameterf, glPointParameterf, NULL, 425),
    NAME_FUNC_OFFSET(21400, glPointParameterf, glPointParameterf, NULL, 425),
    NAME_FUNC_OFFSET(21421, glPointParameterf, glPointParameterf, NULL, 425),
    NAME_FUNC_OFFSET(21443, glPointParameterfv, glPointParameterfv, NULL, 426),
    NAME_FUNC_OFFSET(21465, glPointParameterfv, glPointParameterfv, NULL, 426),
    NAME_FUNC_OFFSET(21487, glPointParameterfv, glPointParameterfv, NULL, 426),
    NAME_FUNC_OFFSET(21510, glPointParameteri, glPointParameteri, NULL, 427),
    NAME_FUNC_OFFSET(21530, glPointParameteriv, glPointParameteriv, NULL, 428),
    NAME_FUNC_OFFSET(21551, glSecondaryColor3b, glSecondaryColor3b, NULL, 429),
    NAME_FUNC_OFFSET(21573, glSecondaryColor3bv, glSecondaryColor3bv, NULL, 430),
    NAME_FUNC_OFFSET(21596, glSecondaryColor3d, glSecondaryColor3d, NULL, 431),
    NAME_FUNC_OFFSET(21618, glSecondaryColor3dv, glSecondaryColor3dv, NULL, 432),
    NAME_FUNC_OFFSET(21641, glSecondaryColor3i, glSecondaryColor3i, NULL, 433),
    NAME_FUNC_OFFSET(21663, glSecondaryColor3iv, glSecondaryColor3iv, NULL, 434),
    NAME_FUNC_OFFSET(21686, glSecondaryColor3s, glSecondaryColor3s, NULL, 435),
    NAME_FUNC_OFFSET(21708, glSecondaryColor3sv, glSecondaryColor3sv, NULL, 436),
    NAME_FUNC_OFFSET(21731, glSecondaryColor3ub, glSecondaryColor3ub, NULL, 437),
    NAME_FUNC_OFFSET(21754, glSecondaryColor3ubv, glSecondaryColor3ubv, NULL, 438),
    NAME_FUNC_OFFSET(21778, glSecondaryColor3ui, glSecondaryColor3ui, NULL, 439),
    NAME_FUNC_OFFSET(21801, glSecondaryColor3uiv, glSecondaryColor3uiv, NULL, 440),
    NAME_FUNC_OFFSET(21825, glSecondaryColor3us, glSecondaryColor3us, NULL, 441),
    NAME_FUNC_OFFSET(21848, glSecondaryColor3usv, glSecondaryColor3usv, NULL, 442),
    NAME_FUNC_OFFSET(21872, glSecondaryColorPointer, glSecondaryColorPointer, NULL, 443),
    NAME_FUNC_OFFSET(21899, glWindowPos2d, glWindowPos2d, NULL, 444),
    NAME_FUNC_OFFSET(21916, glWindowPos2d, glWindowPos2d, NULL, 444),
    NAME_FUNC_OFFSET(21934, glWindowPos2dv, glWindowPos2dv, NULL, 445),
    NAME_FUNC_OFFSET(21952, glWindowPos2dv, glWindowPos2dv, NULL, 445),
    NAME_FUNC_OFFSET(21971, glWindowPos2f, glWindowPos2f, NULL, 446),
    NAME_FUNC_OFFSET(21988, glWindowPos2f, glWindowPos2f, NULL, 446),
    NAME_FUNC_OFFSET(22006, glWindowPos2fv, glWindowPos2fv, NULL, 447),
    NAME_FUNC_OFFSET(22024, glWindowPos2fv, glWindowPos2fv, NULL, 447),
    NAME_FUNC_OFFSET(22043, glWindowPos2i, glWindowPos2i, NULL, 448),
    NAME_FUNC_OFFSET(22060, glWindowPos2i, glWindowPos2i, NULL, 448),
    NAME_FUNC_OFFSET(22078, glWindowPos2iv, glWindowPos2iv, NULL, 449),
    NAME_FUNC_OFFSET(22096, glWindowPos2iv, glWindowPos2iv, NULL, 449),
    NAME_FUNC_OFFSET(22115, glWindowPos2s, glWindowPos2s, NULL, 450),
    NAME_FUNC_OFFSET(22132, glWindowPos2s, glWindowPos2s, NULL, 450),
    NAME_FUNC_OFFSET(22150, glWindowPos2sv, glWindowPos2sv, NULL, 451),
    NAME_FUNC_OFFSET(22168, glWindowPos2sv, glWindowPos2sv, NULL, 451),
    NAME_FUNC_OFFSET(22187, glWindowPos3d, glWindowPos3d, NULL, 452),
    NAME_FUNC_OFFSET(22204, glWindowPos3d, glWindowPos3d, NULL, 452),
    NAME_FUNC_OFFSET(22222, glWindowPos3dv, glWindowPos3dv, NULL, 453),
    NAME_FUNC_OFFSET(22240, glWindowPos3dv, glWindowPos3dv, NULL, 453),
    NAME_FUNC_OFFSET(22259, glWindowPos3f, glWindowPos3f, NULL, 454),
    NAME_FUNC_OFFSET(22276, glWindowPos3f, glWindowPos3f, NULL, 454),
    NAME_FUNC_OFFSET(22294, glWindowPos3fv, glWindowPos3fv, NULL, 455),
    NAME_FUNC_OFFSET(22312, glWindowPos3fv, glWindowPos3fv, NULL, 455),
    NAME_FUNC_OFFSET(22331, glWindowPos3i, glWindowPos3i, NULL, 456),
    NAME_FUNC_OFFSET(22348, glWindowPos3i, glWindowPos3i, NULL, 456),
    NAME_FUNC_OFFSET(22366, glWindowPos3iv, glWindowPos3iv, NULL, 457),
    NAME_FUNC_OFFSET(22384, glWindowPos3iv, glWindowPos3iv, NULL, 457),
    NAME_FUNC_OFFSET(22403, glWindowPos3s, glWindowPos3s, NULL, 458),
    NAME_FUNC_OFFSET(22420, glWindowPos3s, glWindowPos3s, NULL, 458),
    NAME_FUNC_OFFSET(22438, glWindowPos3sv, glWindowPos3sv, NULL, 459),
    NAME_FUNC_OFFSET(22456, glWindowPos3sv, glWindowPos3sv, NULL, 459),
    NAME_FUNC_OFFSET(22475, glBeginQuery, glBeginQuery, NULL, 460),
    NAME_FUNC_OFFSET(22491, glBindBuffer, glBindBuffer, NULL, 461),
    NAME_FUNC_OFFSET(22507, glBufferData, glBufferData, NULL, 462),
    NAME_FUNC_OFFSET(22523, glBufferSubData, glBufferSubData, NULL, 463),
    NAME_FUNC_OFFSET(22542, glDeleteBuffers, glDeleteBuffers, NULL, 464),
    NAME_FUNC_OFFSET(22561, glDeleteQueries, glDeleteQueries, NULL, 465),
    NAME_FUNC_OFFSET(22580, glEndQuery, glEndQuery, NULL, 466),
    NAME_FUNC_OFFSET(22594, glGenBuffers, glGenBuffers, NULL, 467),
    NAME_FUNC_OFFSET(22610, glGenQueries, glGenQueries, NULL, 468),
    NAME_FUNC_OFFSET(22626, glGetBufferParameteriv, glGetBufferParameteriv, NULL, 469),
    NAME_FUNC_OFFSET(22652, glGetBufferPointerv, glGetBufferPointerv, NULL, 470),
    NAME_FUNC_OFFSET(22675, glGetBufferPointerv, glGetBufferPointerv, NULL, 470),
    NAME_FUNC_OFFSET(22698, glGetBufferSubData, glGetBufferSubData, NULL, 471),
    NAME_FUNC_OFFSET(22720, glGetQueryObjectiv, glGetQueryObjectiv, NULL, 472),
    NAME_FUNC_OFFSET(22742, glGetQueryObjectuiv, glGetQueryObjectuiv, NULL, 473),
    NAME_FUNC_OFFSET(22765, glGetQueryiv, glGetQueryiv, NULL, 474),
    NAME_FUNC_OFFSET(22781, glIsBuffer, glIsBuffer, NULL, 475),
    NAME_FUNC_OFFSET(22795, glIsQuery, glIsQuery, NULL, 476),
    NAME_FUNC_OFFSET(22808, glMapBuffer, glMapBuffer, NULL, 477),
    NAME_FUNC_OFFSET(22823, glMapBuffer, glMapBuffer, NULL, 477),
    NAME_FUNC_OFFSET(22838, glUnmapBuffer, glUnmapBuffer, NULL, 478),
    NAME_FUNC_OFFSET(22855, glUnmapBuffer, glUnmapBuffer, NULL, 478),
    NAME_FUNC_OFFSET(22872, glBindAttribLocation, glBindAttribLocation, NULL, 480),
    NAME_FUNC_OFFSET(22896, glBlendEquationSeparate, glBlendEquationSeparate, NULL, 481),
    NAME_FUNC_OFFSET(22923, glBlendEquationSeparate, glBlendEquationSeparate, NULL, 481),
    NAME_FUNC_OFFSET(22950, glBlendEquationSeparate, glBlendEquationSeparate, NULL, 481),
    NAME_FUNC_OFFSET(22977, glCompileShader, glCompileShader, NULL, 482),
    NAME_FUNC_OFFSET(22996, glDisableVertexAttribArray, glDisableVertexAttribArray, NULL, 488),
    NAME_FUNC_OFFSET(23026, glDrawBuffers, glDrawBuffers, NULL, 489),
    NAME_FUNC_OFFSET(23043, glDrawBuffers, glDrawBuffers, NULL, 489),
    NAME_FUNC_OFFSET(23060, glDrawBuffers, glDrawBuffers, NULL, 489),
    NAME_FUNC_OFFSET(23076, glEnableVertexAttribArray, glEnableVertexAttribArray, NULL, 490),
    NAME_FUNC_OFFSET(23105, glGetActiveAttrib, glGetActiveAttrib, NULL, 491),
    NAME_FUNC_OFFSET(23126, glGetActiveUniform, glGetActiveUniform, NULL, 492),
    NAME_FUNC_OFFSET(23148, glGetAttribLocation, glGetAttribLocation, NULL, 494),
    NAME_FUNC_OFFSET(23171, glGetShaderSource, glGetShaderSource, NULL, 498),
    NAME_FUNC_OFFSET(23192, glGetUniformLocation, glGetUniformLocation, NULL, 500),
    NAME_FUNC_OFFSET(23216, glGetUniformfv, glGetUniformfv, NULL, 501),
    NAME_FUNC_OFFSET(23234, glGetUniformiv, glGetUniformiv, NULL, 502),
    NAME_FUNC_OFFSET(23252, glGetVertexAttribPointerv, glGetVertexAttribPointerv, NULL, 503),
    NAME_FUNC_OFFSET(23281, glGetVertexAttribPointerv, glGetVertexAttribPointerv, NULL, 503),
    NAME_FUNC_OFFSET(23309, glGetVertexAttribdv, glGetVertexAttribdv, NULL, 504),
    NAME_FUNC_OFFSET(23332, glGetVertexAttribfv, glGetVertexAttribfv, NULL, 505),
    NAME_FUNC_OFFSET(23355, glGetVertexAttribiv, glGetVertexAttribiv, NULL, 506),
    NAME_FUNC_OFFSET(23378, glLinkProgram, glLinkProgram, NULL, 509),
    NAME_FUNC_OFFSET(23395, glShaderSource, glShaderSource, NULL, 510),
    NAME_FUNC_OFFSET(23413, glStencilOpSeparate, glStencilOpSeparate, NULL, 513),
    NAME_FUNC_OFFSET(23436, glUniform1f, glUniform1f, NULL, 514),
    NAME_FUNC_OFFSET(23451, glUniform1fv, glUniform1fv, NULL, 515),
    NAME_FUNC_OFFSET(23467, glUniform1i, glUniform1i, NULL, 516),
    NAME_FUNC_OFFSET(23482, glUniform1iv, glUniform1iv, NULL, 517),
    NAME_FUNC_OFFSET(23498, glUniform2f, glUniform2f, NULL, 518),
    NAME_FUNC_OFFSET(23513, glUniform2fv, glUniform2fv, NULL, 519),
    NAME_FUNC_OFFSET(23529, glUniform2i, glUniform2i, NULL, 520),
    NAME_FUNC_OFFSET(23544, glUniform2iv, glUniform2iv, NULL, 521),
    NAME_FUNC_OFFSET(23560, glUniform3f, glUniform3f, NULL, 522),
    NAME_FUNC_OFFSET(23575, glUniform3fv, glUniform3fv, NULL, 523),
    NAME_FUNC_OFFSET(23591, glUniform3i, glUniform3i, NULL, 524),
    NAME_FUNC_OFFSET(23606, glUniform3iv, glUniform3iv, NULL, 525),
    NAME_FUNC_OFFSET(23622, glUniform4f, glUniform4f, NULL, 526),
    NAME_FUNC_OFFSET(23637, glUniform4fv, glUniform4fv, NULL, 527),
    NAME_FUNC_OFFSET(23653, glUniform4i, glUniform4i, NULL, 528),
    NAME_FUNC_OFFSET(23668, glUniform4iv, glUniform4iv, NULL, 529),
    NAME_FUNC_OFFSET(23684, glUniformMatrix2fv, glUniformMatrix2fv, NULL, 530),
    NAME_FUNC_OFFSET(23706, glUniformMatrix3fv, glUniformMatrix3fv, NULL, 531),
    NAME_FUNC_OFFSET(23728, glUniformMatrix4fv, glUniformMatrix4fv, NULL, 532),
    NAME_FUNC_OFFSET(23750, glUseProgram, glUseProgram, NULL, 533),
    NAME_FUNC_OFFSET(23772, glValidateProgram, glValidateProgram, NULL, 534),
    NAME_FUNC_OFFSET(23793, glVertexAttrib1d, glVertexAttrib1d, NULL, 535),
    NAME_FUNC_OFFSET(23813, glVertexAttrib1dv, glVertexAttrib1dv, NULL, 536),
    NAME_FUNC_OFFSET(23834, glVertexAttrib1s, glVertexAttrib1s, NULL, 537),
    NAME_FUNC_OFFSET(23854, glVertexAttrib1sv, glVertexAttrib1sv, NULL, 538),
    NAME_FUNC_OFFSET(23875, glVertexAttrib2d, glVertexAttrib2d, NULL, 539),
    NAME_FUNC_OFFSET(23895, glVertexAttrib2dv, glVertexAttrib2dv, NULL, 540),
    NAME_FUNC_OFFSET(23916, glVertexAttrib2s, glVertexAttrib2s, NULL, 541),
    NAME_FUNC_OFFSET(23936, glVertexAttrib2sv, glVertexAttrib2sv, NULL, 542),
    NAME_FUNC_OFFSET(23957, glVertexAttrib3d, glVertexAttrib3d, NULL, 543),
    NAME_FUNC_OFFSET(23977, glVertexAttrib3dv, glVertexAttrib3dv, NULL, 544),
    NAME_FUNC_OFFSET(23998, glVertexAttrib3s, glVertexAttrib3s, NULL, 545),
    NAME_FUNC_OFFSET(24018, glVertexAttrib3sv, glVertexAttrib3sv, NULL, 546),
    NAME_FUNC_OFFSET(24039, glVertexAttrib4Nbv, glVertexAttrib4Nbv, NULL, 547),
    NAME_FUNC_OFFSET(24061, glVertexAttrib4Niv, glVertexAttrib4Niv, NULL, 548),
    NAME_FUNC_OFFSET(24083, glVertexAttrib4Nsv, glVertexAttrib4Nsv, NULL, 549),
    NAME_FUNC_OFFSET(24105, glVertexAttrib4Nub, glVertexAttrib4Nub, NULL, 550),
    NAME_FUNC_OFFSET(24127, glVertexAttrib4Nubv, glVertexAttrib4Nubv, NULL, 551),
    NAME_FUNC_OFFSET(24150, glVertexAttrib4Nuiv, glVertexAttrib4Nuiv, NULL, 552),
    NAME_FUNC_OFFSET(24173, glVertexAttrib4Nusv, glVertexAttrib4Nusv, NULL, 553),
    NAME_FUNC_OFFSET(24196, glVertexAttrib4bv, glVertexAttrib4bv, NULL, 554),
    NAME_FUNC_OFFSET(24217, glVertexAttrib4d, glVertexAttrib4d, NULL, 555),
    NAME_FUNC_OFFSET(24237, glVertexAttrib4dv, glVertexAttrib4dv, NULL, 556),
    NAME_FUNC_OFFSET(24258, glVertexAttrib4iv, glVertexAttrib4iv, NULL, 557),
    NAME_FUNC_OFFSET(24279, glVertexAttrib4s, glVertexAttrib4s, NULL, 558),
    NAME_FUNC_OFFSET(24299, glVertexAttrib4sv, glVertexAttrib4sv, NULL, 559),
    NAME_FUNC_OFFSET(24320, glVertexAttrib4ubv, glVertexAttrib4ubv, NULL, 560),
    NAME_FUNC_OFFSET(24342, glVertexAttrib4uiv, glVertexAttrib4uiv, NULL, 561),
    NAME_FUNC_OFFSET(24364, glVertexAttrib4usv, glVertexAttrib4usv, NULL, 562),
    NAME_FUNC_OFFSET(24386, glVertexAttribPointer, glVertexAttribPointer, NULL, 563),
    NAME_FUNC_OFFSET(24411, glBeginConditionalRender, glBeginConditionalRender, NULL, 570),
    NAME_FUNC_OFFSET(24438, glBeginTransformFeedback, glBeginTransformFeedback, NULL, 571),
    NAME_FUNC_OFFSET(24466, glBindBufferBase, glBindBufferBase, NULL, 572),
    NAME_FUNC_OFFSET(24486, glBindBufferRange, glBindBufferRange, NULL, 573),
    NAME_FUNC_OFFSET(24507, glBindFragDataLocation, glBindFragDataLocation, NULL, 574),
    NAME_FUNC_OFFSET(24533, glClampColor, glClampColor, NULL, 575),
    NAME_FUNC_OFFSET(24549, glColorMaski, glColorMaski, NULL, 580),
    NAME_FUNC_OFFSET(24571, glDisablei, glDisablei, NULL, 581),
    NAME_FUNC_OFFSET(24591, glEnablei, glEnablei, NULL, 582),
    NAME_FUNC_OFFSET(24610, glEndConditionalRender, glEndConditionalRender, NULL, 583),
    NAME_FUNC_OFFSET(24635, glEndTransformFeedback, glEndTransformFeedback, NULL, 584),
    NAME_FUNC_OFFSET(24661, glGetBooleani_v, glGetBooleani_v, NULL, 585),
    NAME_FUNC_OFFSET(24685, glGetFragDataLocation, glGetFragDataLocation, NULL, 586),
    NAME_FUNC_OFFSET(24710, glGetIntegeri_v, glGetIntegeri_v, NULL, 587),
    NAME_FUNC_OFFSET(24734, glGetTexParameterIiv, glGetTexParameterIiv, NULL, 589),
    NAME_FUNC_OFFSET(24758, glGetTexParameterIuiv, glGetTexParameterIuiv, NULL, 590),
    NAME_FUNC_OFFSET(24783, glGetTransformFeedbackVarying, glGetTransformFeedbackVarying, NULL, 591),
    NAME_FUNC_OFFSET(24816, glGetUniformuiv, glGetUniformuiv, NULL, 592),
    NAME_FUNC_OFFSET(24835, glGetVertexAttribIiv, glGetVertexAttribIiv, NULL, 593),
    NAME_FUNC_OFFSET(24859, glGetVertexAttribIuiv, glGetVertexAttribIuiv, NULL, 594),
    NAME_FUNC_OFFSET(24884, glIsEnabledi, glIsEnabledi, NULL, 595),
    NAME_FUNC_OFFSET(24906, glTexParameterIiv, glTexParameterIiv, NULL, 596),
    NAME_FUNC_OFFSET(24927, glTexParameterIuiv, glTexParameterIuiv, NULL, 597),
    NAME_FUNC_OFFSET(24949, glTransformFeedbackVaryings, glTransformFeedbackVaryings, NULL, 598),
    NAME_FUNC_OFFSET(24980, glUniform1ui, glUniform1ui, NULL, 599),
    NAME_FUNC_OFFSET(24996, glUniform1uiv, glUniform1uiv, NULL, 600),
    NAME_FUNC_OFFSET(25013, glUniform2ui, glUniform2ui, NULL, 601),
    NAME_FUNC_OFFSET(25029, glUniform2uiv, glUniform2uiv, NULL, 602),
    NAME_FUNC_OFFSET(25046, glUniform3ui, glUniform3ui, NULL, 603),
    NAME_FUNC_OFFSET(25062, glUniform3uiv, glUniform3uiv, NULL, 604),
    NAME_FUNC_OFFSET(25079, glUniform4ui, glUniform4ui, NULL, 605),
    NAME_FUNC_OFFSET(25095, glUniform4uiv, glUniform4uiv, NULL, 606),
    NAME_FUNC_OFFSET(25112, glVertexAttribI1iv, glVertexAttribI1iv, NULL, 607),
    NAME_FUNC_OFFSET(25134, glVertexAttribI1uiv, glVertexAttribI1uiv, NULL, 608),
    NAME_FUNC_OFFSET(25157, glVertexAttribI4bv, glVertexAttribI4bv, NULL, 609),
    NAME_FUNC_OFFSET(25179, glVertexAttribI4sv, glVertexAttribI4sv, NULL, 610),
    NAME_FUNC_OFFSET(25201, glVertexAttribI4ubv, glVertexAttribI4ubv, NULL, 611),
    NAME_FUNC_OFFSET(25224, glVertexAttribI4usv, glVertexAttribI4usv, NULL, 612),
    NAME_FUNC_OFFSET(25247, glVertexAttribIPointer, glVertexAttribIPointer, NULL, 613),
    NAME_FUNC_OFFSET(25273, glPrimitiveRestartIndex, glPrimitiveRestartIndex, NULL, 614),
    NAME_FUNC_OFFSET(25299, glTexBuffer, glTexBuffer, NULL, 615),
    NAME_FUNC_OFFSET(25314, glFramebufferTexture, glFramebufferTexture, NULL, 616),
    NAME_FUNC_OFFSET(25338, glVertexAttribDivisor, glVertexAttribDivisor, NULL, 619),
    NAME_FUNC_OFFSET(25363, glMinSampleShading, glMinSampleShading, NULL, 620),
    NAME_FUNC_OFFSET(25385, glBindProgramARB, glBindProgramARB, NULL, 621),
    NAME_FUNC_OFFSET(25401, glDeleteProgramsARB, glDeleteProgramsARB, NULL, 622),
    NAME_FUNC_OFFSET(25420, glGenProgramsARB, glGenProgramsARB, NULL, 623),
    NAME_FUNC_OFFSET(25436, glIsProgramARB, glIsProgramARB, NULL, 630),
    NAME_FUNC_OFFSET(25450, glProgramEnvParameter4dARB, glProgramEnvParameter4dARB, NULL, 631),
    NAME_FUNC_OFFSET(25473, glProgramEnvParameter4dvARB, glProgramEnvParameter4dvARB, NULL, 632),
    NAME_FUNC_OFFSET(25497, glProgramEnvParameter4fARB, glProgramEnvParameter4fARB, NULL, 633),
    NAME_FUNC_OFFSET(25520, glProgramEnvParameter4fvARB, glProgramEnvParameter4fvARB, NULL, 634),
    NAME_FUNC_OFFSET(25544, glVertexAttrib1fARB, glVertexAttrib1fARB, NULL, 640),
    NAME_FUNC_OFFSET(25561, glVertexAttrib1fvARB, glVertexAttrib1fvARB, NULL, 641),
    NAME_FUNC_OFFSET(25579, glVertexAttrib2fARB, glVertexAttrib2fARB, NULL, 642),
    NAME_FUNC_OFFSET(25596, glVertexAttrib2fvARB, glVertexAttrib2fvARB, NULL, 643),
    NAME_FUNC_OFFSET(25614, glVertexAttrib3fARB, glVertexAttrib3fARB, NULL, 644),
    NAME_FUNC_OFFSET(25631, glVertexAttrib3fvARB, glVertexAttrib3fvARB, NULL, 645),
    NAME_FUNC_OFFSET(25649, glVertexAttrib4fARB, glVertexAttrib4fARB, NULL, 646),
    NAME_FUNC_OFFSET(25666, glVertexAttrib4fvARB, glVertexAttrib4fvARB, NULL, 647),
    NAME_FUNC_OFFSET(25684, glDrawArraysInstancedARB, glDrawArraysInstancedARB, NULL, 658),
    NAME_FUNC_OFFSET(25709, glDrawArraysInstancedARB, glDrawArraysInstancedARB, NULL, 658),
    NAME_FUNC_OFFSET(25731, glDrawElementsInstancedARB, glDrawElementsInstancedARB, NULL, 659),
    NAME_FUNC_OFFSET(25758, glDrawElementsInstancedARB, glDrawElementsInstancedARB, NULL, 659),
    NAME_FUNC_OFFSET(25782, glBindFramebuffer, glBindFramebuffer, NULL, 660),
    NAME_FUNC_OFFSET(25803, glBindRenderbuffer, glBindRenderbuffer, NULL, 661),
    NAME_FUNC_OFFSET(25825, glBlitFramebuffer, glBlitFramebuffer, NULL, 662),
    NAME_FUNC_OFFSET(25846, glCheckFramebufferStatus, glCheckFramebufferStatus, NULL, 663),
    NAME_FUNC_OFFSET(25874, glCheckFramebufferStatus, glCheckFramebufferStatus, NULL, 663),
    NAME_FUNC_OFFSET(25902, glDeleteFramebuffers, glDeleteFramebuffers, NULL, 664),
    NAME_FUNC_OFFSET(25926, glDeleteFramebuffers, glDeleteFramebuffers, NULL, 664),
    NAME_FUNC_OFFSET(25950, glDeleteRenderbuffers, glDeleteRenderbuffers, NULL, 665),
    NAME_FUNC_OFFSET(25975, glDeleteRenderbuffers, glDeleteRenderbuffers, NULL, 665),
    NAME_FUNC_OFFSET(26000, glFramebufferRenderbuffer, glFramebufferRenderbuffer, NULL, 666),
    NAME_FUNC_OFFSET(26029, glFramebufferRenderbuffer, glFramebufferRenderbuffer, NULL, 666),
    NAME_FUNC_OFFSET(26058, glFramebufferTexture1D, glFramebufferTexture1D, NULL, 667),
    NAME_FUNC_OFFSET(26084, glFramebufferTexture2D, glFramebufferTexture2D, NULL, 668),
    NAME_FUNC_OFFSET(26110, glFramebufferTexture2D, glFramebufferTexture2D, NULL, 668),
    NAME_FUNC_OFFSET(26136, glFramebufferTexture3D, glFramebufferTexture3D, NULL, 669),
    NAME_FUNC_OFFSET(26162, glFramebufferTexture3D, glFramebufferTexture3D, NULL, 669),
    NAME_FUNC_OFFSET(26188, glFramebufferTextureLayer, glFramebufferTextureLayer, NULL, 670),
    NAME_FUNC_OFFSET(26217, glFramebufferTextureLayer, glFramebufferTextureLayer, NULL, 670),
    NAME_FUNC_OFFSET(26246, glGenFramebuffers, glGenFramebuffers, NULL, 671),
    NAME_FUNC_OFFSET(26267, glGenFramebuffers, glGenFramebuffers, NULL, 671),
    NAME_FUNC_OFFSET(26288, glGenRenderbuffers, glGenRenderbuffers, NULL, 672),
    NAME_FUNC_OFFSET(26310, glGenRenderbuffers, glGenRenderbuffers, NULL, 672),
    NAME_FUNC_OFFSET(26332, glGenerateMipmap, glGenerateMipmap, NULL, 673),
    NAME_FUNC_OFFSET(26352, glGenerateMipmap, glGenerateMipmap, NULL, 673),
    NAME_FUNC_OFFSET(26372, glGetFramebufferAttachmentParameteriv, glGetFramebufferAttachmentParameteriv, NULL, 674),
    NAME_FUNC_OFFSET(26413, glGetFramebufferAttachmentParameteriv, glGetFramebufferAttachmentParameteriv, NULL, 674),
    NAME_FUNC_OFFSET(26454, glGetRenderbufferParameteriv, glGetRenderbufferParameteriv, NULL, 675),
    NAME_FUNC_OFFSET(26486, glGetRenderbufferParameteriv, glGetRenderbufferParameteriv, NULL, 675),
    NAME_FUNC_OFFSET(26518, glIsFramebuffer, glIsFramebuffer, NULL, 676),
    NAME_FUNC_OFFSET(26537, glIsFramebuffer, glIsFramebuffer, NULL, 676),
    NAME_FUNC_OFFSET(26556, glIsRenderbuffer, glIsRenderbuffer, NULL, 677),
    NAME_FUNC_OFFSET(26576, glIsRenderbuffer, glIsRenderbuffer, NULL, 677),
    NAME_FUNC_OFFSET(26596, glRenderbufferStorage, glRenderbufferStorage, NULL, 678),
    NAME_FUNC_OFFSET(26621, glRenderbufferStorage, glRenderbufferStorage, NULL, 678),
    NAME_FUNC_OFFSET(26646, glRenderbufferStorageMultisample, glRenderbufferStorageMultisample, NULL, 679),
    NAME_FUNC_OFFSET(26682, glFlushMappedBufferRange, glFlushMappedBufferRange, NULL, 681),
    NAME_FUNC_OFFSET(26710, glMapBufferRange, glMapBufferRange, NULL, 682),
    NAME_FUNC_OFFSET(26730, glBindVertexArray, glBindVertexArray, NULL, 683),
    NAME_FUNC_OFFSET(26751, glDeleteVertexArrays, glDeleteVertexArrays, NULL, 684),
    NAME_FUNC_OFFSET(26777, glDeleteVertexArrays, glDeleteVertexArrays, NULL, 684),
    NAME_FUNC_OFFSET(26801, glGenVertexArrays, glGenVertexArrays, NULL, 685),
    NAME_FUNC_OFFSET(26822, glIsVertexArray, glIsVertexArray, NULL, 686),
    NAME_FUNC_OFFSET(26843, glIsVertexArray, glIsVertexArray, NULL, 686),
    NAME_FUNC_OFFSET(26862, glProvokingVertex, glProvokingVertex, NULL, 706),
    NAME_FUNC_OFFSET(26883, glBlendEquationSeparateiARB, glBlendEquationSeparateiARB, NULL, 711),
    NAME_FUNC_OFFSET(26917, glBlendEquationiARB, glBlendEquationiARB, NULL, 712),
    NAME_FUNC_OFFSET(26943, glBlendFuncSeparateiARB, glBlendFuncSeparateiARB, NULL, 713),
    NAME_FUNC_OFFSET(26973, glBlendFunciARB, glBlendFunciARB, NULL, 714),
    NAME_FUNC_OFFSET(26995, gl_dispatch_stub_731, gl_dispatch_stub_731, NULL, 731),
    NAME_FUNC_OFFSET(27019, gl_dispatch_stub_732, gl_dispatch_stub_732, NULL, 732),
    NAME_FUNC_OFFSET(27044, glClearDepthf, glClearDepthf, NULL, 783),
    NAME_FUNC_OFFSET(27061, glDepthRangef, glDepthRangef, NULL, 784),
    NAME_FUNC_OFFSET(27078, glGetProgramBinary, glGetProgramBinary, NULL, 788),
    NAME_FUNC_OFFSET(27100, glProgramBinary, glProgramBinary, NULL, 789),
    NAME_FUNC_OFFSET(27119, glProgramParameteri, glProgramParameteri, NULL, 790),
    NAME_FUNC_OFFSET(27142, gl_dispatch_stub_848, gl_dispatch_stub_848, NULL, 848),
    NAME_FUNC_OFFSET(27158, gl_dispatch_stub_849, gl_dispatch_stub_849, NULL, 849),
    NAME_FUNC_OFFSET(27177, glSecondaryColor3fEXT, glSecondaryColor3fEXT, NULL, 869),
    NAME_FUNC_OFFSET(27196, glSecondaryColor3fvEXT, glSecondaryColor3fvEXT, NULL, 870),
    NAME_FUNC_OFFSET(27216, glMultiDrawElementsEXT, glMultiDrawElementsEXT, NULL, 871),
    NAME_FUNC_OFFSET(27236, glFogCoordfEXT, glFogCoordfEXT, NULL, 872),
    NAME_FUNC_OFFSET(27248, glFogCoordfvEXT, glFogCoordfvEXT, NULL, 873),
    NAME_FUNC_OFFSET(27261, glVertexAttribI1iEXT, glVertexAttribI1iEXT, NULL, 976),
    NAME_FUNC_OFFSET(27279, glVertexAttribI1uiEXT, glVertexAttribI1uiEXT, NULL, 977),
    NAME_FUNC_OFFSET(27298, glVertexAttribI2iEXT, glVertexAttribI2iEXT, NULL, 978),
    NAME_FUNC_OFFSET(27316, glVertexAttribI2ivEXT, glVertexAttribI2ivEXT, NULL, 979),
    NAME_FUNC_OFFSET(27335, glVertexAttribI2uiEXT, glVertexAttribI2uiEXT, NULL, 980),
    NAME_FUNC_OFFSET(27354, glVertexAttribI2uivEXT, glVertexAttribI2uivEXT, NULL, 981),
    NAME_FUNC_OFFSET(27374, glVertexAttribI3iEXT, glVertexAttribI3iEXT, NULL, 982),
    NAME_FUNC_OFFSET(27392, glVertexAttribI3ivEXT, glVertexAttribI3ivEXT, NULL, 983),
    NAME_FUNC_OFFSET(27411, glVertexAttribI3uiEXT, glVertexAttribI3uiEXT, NULL, 984),
    NAME_FUNC_OFFSET(27430, glVertexAttribI3uivEXT, glVertexAttribI3uivEXT, NULL, 985),
    NAME_FUNC_OFFSET(27450, glVertexAttribI4iEXT, glVertexAttribI4iEXT, NULL, 986),
    NAME_FUNC_OFFSET(27468, glVertexAttribI4ivEXT, glVertexAttribI4ivEXT, NULL, 987),
    NAME_FUNC_OFFSET(27487, glVertexAttribI4uiEXT, glVertexAttribI4uiEXT, NULL, 988),
    NAME_FUNC_OFFSET(27506, glVertexAttribI4uivEXT, glVertexAttribI4uivEXT, NULL, 989),
    NAME_FUNC_OFFSET(27526, gl_dispatch_stub_1026, gl_dispatch_stub_1026, NULL, 1026),
    NAME_FUNC_OFFSET(27542, gl_dispatch_stub_1027, gl_dispatch_stub_1027, NULL, 1027),
    NAME_FUNC_OFFSET(27559, gl_dispatch_stub_1028, gl_dispatch_stub_1028, NULL, 1028),
    NAME_FUNC_OFFSET(27576, gl_dispatch_stub_1029, gl_dispatch_stub_1029, NULL, 1029),
    NAME_FUNC_OFFSET(27589, gl_dispatch_stub_1030, gl_dispatch_stub_1030, NULL, 1030),
    NAME_FUNC_OFFSET(27606, gl_dispatch_stub_1031, gl_dispatch_stub_1031, NULL, 1031),
    NAME_FUNC_OFFSET(27616, gl_dispatch_stub_1032, gl_dispatch_stub_1032, NULL, 1032),
    NAME_FUNC_OFFSET(27627, gl_dispatch_stub_1033, gl_dispatch_stub_1033, NULL, 1033),
    NAME_FUNC_OFFSET(27641, gl_dispatch_stub_1034, gl_dispatch_stub_1034, NULL, 1034),
    NAME_FUNC_OFFSET(27655, gl_dispatch_stub_1035, gl_dispatch_stub_1035, NULL, 1035),
    NAME_FUNC_OFFSET(27672, gl_dispatch_stub_1036, gl_dispatch_stub_1036, NULL, 1036),
    NAME_FUNC_OFFSET(27690, gl_dispatch_stub_1037, gl_dispatch_stub_1037, NULL, 1037),
    NAME_FUNC_OFFSET(27702, gl_dispatch_stub_1038, gl_dispatch_stub_1038, NULL, 1038),
    NAME_FUNC_OFFSET(27715, gl_dispatch_stub_1039, gl_dispatch_stub_1039, NULL, 1039),
    NAME_FUNC_OFFSET(27731, gl_dispatch_stub_1040, gl_dispatch_stub_1040, NULL, 1040),
    NAME_FUNC_OFFSET(27748, gl_dispatch_stub_1041, gl_dispatch_stub_1041, NULL, 1041),
    NAME_FUNC_OFFSET(27763, gl_dispatch_stub_1042, gl_dispatch_stub_1042, NULL, 1042),
    NAME_FUNC_OFFSET(27779, gl_dispatch_stub_1043, gl_dispatch_stub_1043, NULL, 1043),
    NAME_FUNC_OFFSET(27796, gl_dispatch_stub_1044, gl_dispatch_stub_1044, NULL, 1044),
    NAME_FUNC_OFFSET(27817, gl_dispatch_stub_1045, gl_dispatch_stub_1045, NULL, 1045),
    NAME_FUNC_OFFSET(27831, gl_dispatch_stub_1046, gl_dispatch_stub_1046, NULL, 1046),
    NAME_FUNC_OFFSET(27843, gl_dispatch_stub_1047, gl_dispatch_stub_1047, NULL, 1047),
    NAME_FUNC_OFFSET(27855, gl_dispatch_stub_1048, gl_dispatch_stub_1048, NULL, 1048),
    NAME_FUNC_OFFSET(27871, gl_dispatch_stub_1049, gl_dispatch_stub_1049, NULL, 1049),
    NAME_FUNC_OFFSET(27891, gl_dispatch_stub_1050, gl_dispatch_stub_1050, NULL, 1050),
    NAME_FUNC_OFFSET(27904, gl_dispatch_stub_1051, gl_dispatch_stub_1051, NULL, 1051),
    NAME_FUNC_OFFSET(27925, gl_dispatch_stub_1052, gl_dispatch_stub_1052, NULL, 1052),
    NAME_FUNC_OFFSET(27937, gl_dispatch_stub_1053, gl_dispatch_stub_1053, NULL, 1053),
    NAME_FUNC_OFFSET(27950, gl_dispatch_stub_1054, gl_dispatch_stub_1054, NULL, 1054),
    NAME_FUNC_OFFSET(27964, gl_dispatch_stub_1055, gl_dispatch_stub_1055, NULL, 1055),
    NAME_FUNC_OFFSET(27983, gl_dispatch_stub_1056, gl_dispatch_stub_1056, NULL, 1056),
    NAME_FUNC_OFFSET(27999, gl_dispatch_stub_1057, gl_dispatch_stub_1057, NULL, 1057),
    NAME_FUNC_OFFSET(28015, gl_dispatch_stub_1058, gl_dispatch_stub_1058, NULL, 1058),
    NAME_FUNC_OFFSET(28031, gl_dispatch_stub_1059, gl_dispatch_stub_1059, NULL, 1059),
    NAME_FUNC_OFFSET(28050, gl_dispatch_stub_1060, gl_dispatch_stub_1060, NULL, 1060),
    NAME_FUNC_OFFSET(28069, gl_dispatch_stub_1061, gl_dispatch_stub_1061, NULL, 1061),
    NAME_FUNC_OFFSET(28084, gl_dispatch_stub_1062, gl_dispatch_stub_1062, NULL, 1062),
    NAME_FUNC_OFFSET(28100, gl_dispatch_stub_1063, gl_dispatch_stub_1063, NULL, 1063),
    NAME_FUNC_OFFSET(28119, gl_dispatch_stub_1064, gl_dispatch_stub_1064, NULL, 1064),
    NAME_FUNC_OFFSET(28136, gl_dispatch_stub_1065, gl_dispatch_stub_1065, NULL, 1065),
    NAME_FUNC_OFFSET(28159, gl_dispatch_stub_1066, gl_dispatch_stub_1066, NULL, 1066),
    NAME_FUNC_OFFSET(28180, gl_dispatch_stub_1067, gl_dispatch_stub_1067, NULL, 1067),
    NAME_FUNC_OFFSET(28202, gl_dispatch_stub_1068, gl_dispatch_stub_1068, NULL, 1068),
    NAME_FUNC_OFFSET(-1, NULL, NULL, NULL, 0)
};

#undef NAME_FUNC_OFFSET
