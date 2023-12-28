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
   /* _mesa_function_pool[0]: NewList (dynamic) */
   "glNewList\0"
   /* _mesa_function_pool[10]: EndList (offset 1) */
   "glEndList\0"
   /* _mesa_function_pool[20]: CallList (offset 2) */
   "glCallList\0"
   /* _mesa_function_pool[31]: CallLists (offset 3) */
   "glCallLists\0"
   /* _mesa_function_pool[43]: DeleteLists (offset 4) */
   "glDeleteLists\0"
   /* _mesa_function_pool[57]: GenLists (offset 5) */
   "glGenLists\0"
   /* _mesa_function_pool[68]: ListBase (offset 6) */
   "glListBase\0"
   /* _mesa_function_pool[79]: Begin (offset 7) */
   "glBegin\0"
   /* _mesa_function_pool[87]: Bitmap (offset 8) */
   "glBitmap\0"
   /* _mesa_function_pool[96]: Color3b (offset 9) */
   "glColor3b\0"
   /* _mesa_function_pool[106]: Color3bv (offset 10) */
   "glColor3bv\0"
   /* _mesa_function_pool[117]: Color3d (offset 11) */
   "glColor3d\0"
   /* _mesa_function_pool[127]: Color3dv (offset 12) */
   "glColor3dv\0"
   /* _mesa_function_pool[138]: Color3f (offset 13) */
   "glColor3f\0"
   /* _mesa_function_pool[148]: Color3fv (offset 14) */
   "glColor3fv\0"
   /* _mesa_function_pool[159]: Color3i (offset 15) */
   "glColor3i\0"
   /* _mesa_function_pool[169]: Color3iv (offset 16) */
   "glColor3iv\0"
   /* _mesa_function_pool[180]: Color3s (offset 17) */
   "glColor3s\0"
   /* _mesa_function_pool[190]: Color3sv (offset 18) */
   "glColor3sv\0"
   /* _mesa_function_pool[201]: Color3ub (offset 19) */
   "glColor3ub\0"
   /* _mesa_function_pool[212]: Color3ubv (offset 20) */
   "glColor3ubv\0"
   /* _mesa_function_pool[224]: Color3ui (offset 21) */
   "glColor3ui\0"
   /* _mesa_function_pool[235]: Color3uiv (offset 22) */
   "glColor3uiv\0"
   /* _mesa_function_pool[247]: Color3us (offset 23) */
   "glColor3us\0"
   /* _mesa_function_pool[258]: Color3usv (offset 24) */
   "glColor3usv\0"
   /* _mesa_function_pool[270]: Color4b (offset 25) */
   "glColor4b\0"
   /* _mesa_function_pool[280]: Color4bv (offset 26) */
   "glColor4bv\0"
   /* _mesa_function_pool[291]: Color4d (offset 27) */
   "glColor4d\0"
   /* _mesa_function_pool[301]: Color4dv (offset 28) */
   "glColor4dv\0"
   /* _mesa_function_pool[312]: Color4f (offset 29) */
   "glColor4f\0"
   /* _mesa_function_pool[322]: Color4fv (offset 30) */
   "glColor4fv\0"
   /* _mesa_function_pool[333]: Color4i (offset 31) */
   "glColor4i\0"
   /* _mesa_function_pool[343]: Color4iv (offset 32) */
   "glColor4iv\0"
   /* _mesa_function_pool[354]: Color4s (offset 33) */
   "glColor4s\0"
   /* _mesa_function_pool[364]: Color4sv (offset 34) */
   "glColor4sv\0"
   /* _mesa_function_pool[375]: Color4ub (offset 35) */
   "glColor4ub\0"
   /* _mesa_function_pool[386]: Color4ubv (offset 36) */
   "glColor4ubv\0"
   /* _mesa_function_pool[398]: Color4ui (offset 37) */
   "glColor4ui\0"
   /* _mesa_function_pool[409]: Color4uiv (offset 38) */
   "glColor4uiv\0"
   /* _mesa_function_pool[421]: Color4us (offset 39) */
   "glColor4us\0"
   /* _mesa_function_pool[432]: Color4usv (offset 40) */
   "glColor4usv\0"
   /* _mesa_function_pool[444]: EdgeFlag (offset 41) */
   "glEdgeFlag\0"
   /* _mesa_function_pool[455]: EdgeFlagv (offset 42) */
   "glEdgeFlagv\0"
   /* _mesa_function_pool[467]: End (offset 43) */
   "glEnd\0"
   /* _mesa_function_pool[473]: Indexd (offset 44) */
   "glIndexd\0"
   /* _mesa_function_pool[482]: Indexdv (offset 45) */
   "glIndexdv\0"
   /* _mesa_function_pool[492]: Indexf (offset 46) */
   "glIndexf\0"
   /* _mesa_function_pool[501]: Indexfv (offset 47) */
   "glIndexfv\0"
   /* _mesa_function_pool[511]: Indexi (offset 48) */
   "glIndexi\0"
   /* _mesa_function_pool[520]: Indexiv (offset 49) */
   "glIndexiv\0"
   /* _mesa_function_pool[530]: Indexs (offset 50) */
   "glIndexs\0"
   /* _mesa_function_pool[539]: Indexsv (offset 51) */
   "glIndexsv\0"
   /* _mesa_function_pool[549]: Normal3b (offset 52) */
   "glNormal3b\0"
   /* _mesa_function_pool[560]: Normal3bv (offset 53) */
   "glNormal3bv\0"
   /* _mesa_function_pool[572]: Normal3d (offset 54) */
   "glNormal3d\0"
   /* _mesa_function_pool[583]: Normal3dv (offset 55) */
   "glNormal3dv\0"
   /* _mesa_function_pool[595]: Normal3f (offset 56) */
   "glNormal3f\0"
   /* _mesa_function_pool[606]: Normal3fv (offset 57) */
   "glNormal3fv\0"
   /* _mesa_function_pool[618]: Normal3i (offset 58) */
   "glNormal3i\0"
   /* _mesa_function_pool[629]: Normal3iv (offset 59) */
   "glNormal3iv\0"
   /* _mesa_function_pool[641]: Normal3s (offset 60) */
   "glNormal3s\0"
   /* _mesa_function_pool[652]: Normal3sv (offset 61) */
   "glNormal3sv\0"
   /* _mesa_function_pool[664]: RasterPos2d (offset 62) */
   "glRasterPos2d\0"
   /* _mesa_function_pool[678]: RasterPos2dv (offset 63) */
   "glRasterPos2dv\0"
   /* _mesa_function_pool[693]: RasterPos2f (offset 64) */
   "glRasterPos2f\0"
   /* _mesa_function_pool[707]: RasterPos2fv (offset 65) */
   "glRasterPos2fv\0"
   /* _mesa_function_pool[722]: RasterPos2i (offset 66) */
   "glRasterPos2i\0"
   /* _mesa_function_pool[736]: RasterPos2iv (offset 67) */
   "glRasterPos2iv\0"
   /* _mesa_function_pool[751]: RasterPos2s (offset 68) */
   "glRasterPos2s\0"
   /* _mesa_function_pool[765]: RasterPos2sv (offset 69) */
   "glRasterPos2sv\0"
   /* _mesa_function_pool[780]: RasterPos3d (offset 70) */
   "glRasterPos3d\0"
   /* _mesa_function_pool[794]: RasterPos3dv (offset 71) */
   "glRasterPos3dv\0"
   /* _mesa_function_pool[809]: RasterPos3f (offset 72) */
   "glRasterPos3f\0"
   /* _mesa_function_pool[823]: RasterPos3fv (offset 73) */
   "glRasterPos3fv\0"
   /* _mesa_function_pool[838]: RasterPos3i (offset 74) */
   "glRasterPos3i\0"
   /* _mesa_function_pool[852]: RasterPos3iv (offset 75) */
   "glRasterPos3iv\0"
   /* _mesa_function_pool[867]: RasterPos3s (offset 76) */
   "glRasterPos3s\0"
   /* _mesa_function_pool[881]: RasterPos3sv (offset 77) */
   "glRasterPos3sv\0"
   /* _mesa_function_pool[896]: RasterPos4d (offset 78) */
   "glRasterPos4d\0"
   /* _mesa_function_pool[910]: RasterPos4dv (offset 79) */
   "glRasterPos4dv\0"
   /* _mesa_function_pool[925]: RasterPos4f (offset 80) */
   "glRasterPos4f\0"
   /* _mesa_function_pool[939]: RasterPos4fv (offset 81) */
   "glRasterPos4fv\0"
   /* _mesa_function_pool[954]: RasterPos4i (offset 82) */
   "glRasterPos4i\0"
   /* _mesa_function_pool[968]: RasterPos4iv (offset 83) */
   "glRasterPos4iv\0"
   /* _mesa_function_pool[983]: RasterPos4s (offset 84) */
   "glRasterPos4s\0"
   /* _mesa_function_pool[997]: RasterPos4sv (offset 85) */
   "glRasterPos4sv\0"
   /* _mesa_function_pool[1012]: Rectd (offset 86) */
   "glRectd\0"
   /* _mesa_function_pool[1020]: Rectdv (offset 87) */
   "glRectdv\0"
   /* _mesa_function_pool[1029]: Rectf (offset 88) */
   "glRectf\0"
   /* _mesa_function_pool[1037]: Rectfv (offset 89) */
   "glRectfv\0"
   /* _mesa_function_pool[1046]: Recti (offset 90) */
   "glRecti\0"
   /* _mesa_function_pool[1054]: Rectiv (offset 91) */
   "glRectiv\0"
   /* _mesa_function_pool[1063]: Rects (offset 92) */
   "glRects\0"
   /* _mesa_function_pool[1071]: Rectsv (offset 93) */
   "glRectsv\0"
   /* _mesa_function_pool[1080]: TexCoord1d (offset 94) */
   "glTexCoord1d\0"
   /* _mesa_function_pool[1093]: TexCoord1dv (offset 95) */
   "glTexCoord1dv\0"
   /* _mesa_function_pool[1107]: TexCoord1f (offset 96) */
   "glTexCoord1f\0"
   /* _mesa_function_pool[1120]: TexCoord1fv (offset 97) */
   "glTexCoord1fv\0"
   /* _mesa_function_pool[1134]: TexCoord1i (offset 98) */
   "glTexCoord1i\0"
   /* _mesa_function_pool[1147]: TexCoord1iv (offset 99) */
   "glTexCoord1iv\0"
   /* _mesa_function_pool[1161]: TexCoord1s (offset 100) */
   "glTexCoord1s\0"
   /* _mesa_function_pool[1174]: TexCoord1sv (offset 101) */
   "glTexCoord1sv\0"
   /* _mesa_function_pool[1188]: TexCoord2d (offset 102) */
   "glTexCoord2d\0"
   /* _mesa_function_pool[1201]: TexCoord2dv (offset 103) */
   "glTexCoord2dv\0"
   /* _mesa_function_pool[1215]: TexCoord2f (offset 104) */
   "glTexCoord2f\0"
   /* _mesa_function_pool[1228]: TexCoord2fv (offset 105) */
   "glTexCoord2fv\0"
   /* _mesa_function_pool[1242]: TexCoord2i (offset 106) */
   "glTexCoord2i\0"
   /* _mesa_function_pool[1255]: TexCoord2iv (offset 107) */
   "glTexCoord2iv\0"
   /* _mesa_function_pool[1269]: TexCoord2s (offset 108) */
   "glTexCoord2s\0"
   /* _mesa_function_pool[1282]: TexCoord2sv (offset 109) */
   "glTexCoord2sv\0"
   /* _mesa_function_pool[1296]: TexCoord3d (offset 110) */
   "glTexCoord3d\0"
   /* _mesa_function_pool[1309]: TexCoord3dv (offset 111) */
   "glTexCoord3dv\0"
   /* _mesa_function_pool[1323]: TexCoord3f (offset 112) */
   "glTexCoord3f\0"
   /* _mesa_function_pool[1336]: TexCoord3fv (offset 113) */
   "glTexCoord3fv\0"
   /* _mesa_function_pool[1350]: TexCoord3i (offset 114) */
   "glTexCoord3i\0"
   /* _mesa_function_pool[1363]: TexCoord3iv (offset 115) */
   "glTexCoord3iv\0"
   /* _mesa_function_pool[1377]: TexCoord3s (offset 116) */
   "glTexCoord3s\0"
   /* _mesa_function_pool[1390]: TexCoord3sv (offset 117) */
   "glTexCoord3sv\0"
   /* _mesa_function_pool[1404]: TexCoord4d (offset 118) */
   "glTexCoord4d\0"
   /* _mesa_function_pool[1417]: TexCoord4dv (offset 119) */
   "glTexCoord4dv\0"
   /* _mesa_function_pool[1431]: TexCoord4f (offset 120) */
   "glTexCoord4f\0"
   /* _mesa_function_pool[1444]: TexCoord4fv (offset 121) */
   "glTexCoord4fv\0"
   /* _mesa_function_pool[1458]: TexCoord4i (offset 122) */
   "glTexCoord4i\0"
   /* _mesa_function_pool[1471]: TexCoord4iv (offset 123) */
   "glTexCoord4iv\0"
   /* _mesa_function_pool[1485]: TexCoord4s (offset 124) */
   "glTexCoord4s\0"
   /* _mesa_function_pool[1498]: TexCoord4sv (offset 125) */
   "glTexCoord4sv\0"
   /* _mesa_function_pool[1512]: Vertex2d (offset 126) */
   "glVertex2d\0"
   /* _mesa_function_pool[1523]: Vertex2dv (offset 127) */
   "glVertex2dv\0"
   /* _mesa_function_pool[1535]: Vertex2f (offset 128) */
   "glVertex2f\0"
   /* _mesa_function_pool[1546]: Vertex2fv (offset 129) */
   "glVertex2fv\0"
   /* _mesa_function_pool[1558]: Vertex2i (offset 130) */
   "glVertex2i\0"
   /* _mesa_function_pool[1569]: Vertex2iv (offset 131) */
   "glVertex2iv\0"
   /* _mesa_function_pool[1581]: Vertex2s (offset 132) */
   "glVertex2s\0"
   /* _mesa_function_pool[1592]: Vertex2sv (offset 133) */
   "glVertex2sv\0"
   /* _mesa_function_pool[1604]: Vertex3d (offset 134) */
   "glVertex3d\0"
   /* _mesa_function_pool[1615]: Vertex3dv (offset 135) */
   "glVertex3dv\0"
   /* _mesa_function_pool[1627]: Vertex3f (offset 136) */
   "glVertex3f\0"
   /* _mesa_function_pool[1638]: Vertex3fv (offset 137) */
   "glVertex3fv\0"
   /* _mesa_function_pool[1650]: Vertex3i (offset 138) */
   "glVertex3i\0"
   /* _mesa_function_pool[1661]: Vertex3iv (offset 139) */
   "glVertex3iv\0"
   /* _mesa_function_pool[1673]: Vertex3s (offset 140) */
   "glVertex3s\0"
   /* _mesa_function_pool[1684]: Vertex3sv (offset 141) */
   "glVertex3sv\0"
   /* _mesa_function_pool[1696]: Vertex4d (offset 142) */
   "glVertex4d\0"
   /* _mesa_function_pool[1707]: Vertex4dv (offset 143) */
   "glVertex4dv\0"
   /* _mesa_function_pool[1719]: Vertex4f (offset 144) */
   "glVertex4f\0"
   /* _mesa_function_pool[1730]: Vertex4fv (offset 145) */
   "glVertex4fv\0"
   /* _mesa_function_pool[1742]: Vertex4i (offset 146) */
   "glVertex4i\0"
   /* _mesa_function_pool[1753]: Vertex4iv (offset 147) */
   "glVertex4iv\0"
   /* _mesa_function_pool[1765]: Vertex4s (offset 148) */
   "glVertex4s\0"
   /* _mesa_function_pool[1776]: Vertex4sv (offset 149) */
   "glVertex4sv\0"
   /* _mesa_function_pool[1788]: ClipPlane (offset 150) */
   "glClipPlane\0"
   /* _mesa_function_pool[1800]: ColorMaterial (offset 151) */
   "glColorMaterial\0"
   /* _mesa_function_pool[1816]: CullFace (offset 152) */
   "glCullFace\0"
   /* _mesa_function_pool[1827]: Fogf (offset 153) */
   "glFogf\0"
   /* _mesa_function_pool[1834]: Fogfv (offset 154) */
   "glFogfv\0"
   /* _mesa_function_pool[1842]: Fogi (offset 155) */
   "glFogi\0"
   /* _mesa_function_pool[1849]: Fogiv (offset 156) */
   "glFogiv\0"
   /* _mesa_function_pool[1857]: FrontFace (offset 157) */
   "glFrontFace\0"
   /* _mesa_function_pool[1869]: Hint (offset 158) */
   "glHint\0"
   /* _mesa_function_pool[1876]: Lightf (offset 159) */
   "glLightf\0"
   /* _mesa_function_pool[1885]: Lightfv (offset 160) */
   "glLightfv\0"
   /* _mesa_function_pool[1895]: Lighti (offset 161) */
   "glLighti\0"
   /* _mesa_function_pool[1904]: Lightiv (offset 162) */
   "glLightiv\0"
   /* _mesa_function_pool[1914]: LightModelf (offset 163) */
   "glLightModelf\0"
   /* _mesa_function_pool[1928]: LightModelfv (offset 164) */
   "glLightModelfv\0"
   /* _mesa_function_pool[1943]: LightModeli (offset 165) */
   "glLightModeli\0"
   /* _mesa_function_pool[1957]: LightModeliv (offset 166) */
   "glLightModeliv\0"
   /* _mesa_function_pool[1972]: LineStipple (offset 167) */
   "glLineStipple\0"
   /* _mesa_function_pool[1986]: LineWidth (offset 168) */
   "glLineWidth\0"
   /* _mesa_function_pool[1998]: Materialf (offset 169) */
   "glMaterialf\0"
   /* _mesa_function_pool[2010]: Materialfv (offset 170) */
   "glMaterialfv\0"
   /* _mesa_function_pool[2023]: Materiali (offset 171) */
   "glMateriali\0"
   /* _mesa_function_pool[2035]: Materialiv (offset 172) */
   "glMaterialiv\0"
   /* _mesa_function_pool[2048]: PointSize (offset 173) */
   "glPointSize\0"
   /* _mesa_function_pool[2060]: PolygonMode (offset 174) */
   "glPolygonMode\0"
   /* _mesa_function_pool[2074]: PolygonStipple (offset 175) */
   "glPolygonStipple\0"
   /* _mesa_function_pool[2091]: Scissor (offset 176) */
   "glScissor\0"
   /* _mesa_function_pool[2101]: ShadeModel (offset 177) */
   "glShadeModel\0"
   /* _mesa_function_pool[2114]: TexParameterf (offset 178) */
   "glTexParameterf\0"
   /* _mesa_function_pool[2130]: TexParameterfv (offset 179) */
   "glTexParameterfv\0"
   /* _mesa_function_pool[2147]: TexParameteri (offset 180) */
   "glTexParameteri\0"
   /* _mesa_function_pool[2163]: TexParameteriv (offset 181) */
   "glTexParameteriv\0"
   /* _mesa_function_pool[2180]: TexImage1D (offset 182) */
   "glTexImage1D\0"
   /* _mesa_function_pool[2193]: TexImage2D (offset 183) */
   "glTexImage2D\0"
   /* _mesa_function_pool[2206]: TexEnvf (offset 184) */
   "glTexEnvf\0"
   /* _mesa_function_pool[2216]: TexEnvfv (offset 185) */
   "glTexEnvfv\0"
   /* _mesa_function_pool[2227]: TexEnvi (offset 186) */
   "glTexEnvi\0"
   /* _mesa_function_pool[2237]: TexEnviv (offset 187) */
   "glTexEnviv\0"
   /* _mesa_function_pool[2248]: TexGend (offset 188) */
   "glTexGend\0"
   /* _mesa_function_pool[2258]: TexGendv (offset 189) */
   "glTexGendv\0"
   /* _mesa_function_pool[2269]: TexGenf (offset 190) */
   "glTexGenf\0"
   /* _mesa_function_pool[2279]: TexGenfv (offset 191) */
   "glTexGenfv\0"
   /* _mesa_function_pool[2290]: TexGeni (offset 192) */
   "glTexGeni\0"
   /* _mesa_function_pool[2300]: TexGeniv (offset 193) */
   "glTexGeniv\0"
   /* _mesa_function_pool[2311]: FeedbackBuffer (offset 194) */
   "glFeedbackBuffer\0"
   /* _mesa_function_pool[2328]: SelectBuffer (offset 195) */
   "glSelectBuffer\0"
   /* _mesa_function_pool[2343]: RenderMode (offset 196) */
   "glRenderMode\0"
   /* _mesa_function_pool[2356]: InitNames (offset 197) */
   "glInitNames\0"
   /* _mesa_function_pool[2368]: LoadName (offset 198) */
   "glLoadName\0"
   /* _mesa_function_pool[2379]: PassThrough (offset 199) */
   "glPassThrough\0"
   /* _mesa_function_pool[2393]: PopName (offset 200) */
   "glPopName\0"
   /* _mesa_function_pool[2403]: PushName (offset 201) */
   "glPushName\0"
   /* _mesa_function_pool[2414]: DrawBuffer (offset 202) */
   "glDrawBuffer\0"
   /* _mesa_function_pool[2427]: Clear (offset 203) */
   "glClear\0"
   /* _mesa_function_pool[2435]: ClearAccum (offset 204) */
   "glClearAccum\0"
   /* _mesa_function_pool[2448]: ClearIndex (offset 205) */
   "glClearIndex\0"
   /* _mesa_function_pool[2461]: ClearColor (offset 206) */
   "glClearColor\0"
   /* _mesa_function_pool[2474]: ClearStencil (offset 207) */
   "glClearStencil\0"
   /* _mesa_function_pool[2489]: ClearDepth (offset 208) */
   "glClearDepth\0"
   /* _mesa_function_pool[2502]: StencilMask (offset 209) */
   "glStencilMask\0"
   /* _mesa_function_pool[2516]: ColorMask (offset 210) */
   "glColorMask\0"
   /* _mesa_function_pool[2528]: DepthMask (offset 211) */
   "glDepthMask\0"
   /* _mesa_function_pool[2540]: IndexMask (offset 212) */
   "glIndexMask\0"
   /* _mesa_function_pool[2552]: Accum (offset 213) */
   "glAccum\0"
   /* _mesa_function_pool[2560]: Disable (offset 214) */
   "glDisable\0"
   /* _mesa_function_pool[2570]: Enable (offset 215) */
   "glEnable\0"
   /* _mesa_function_pool[2579]: Finish (offset 216) */
   "glFinish\0"
   /* _mesa_function_pool[2588]: Flush (offset 217) */
   "glFlush\0"
   /* _mesa_function_pool[2596]: PopAttrib (offset 218) */
   "glPopAttrib\0"
   /* _mesa_function_pool[2608]: PushAttrib (offset 219) */
   "glPushAttrib\0"
   /* _mesa_function_pool[2621]: Map1d (offset 220) */
   "glMap1d\0"
   /* _mesa_function_pool[2629]: Map1f (offset 221) */
   "glMap1f\0"
   /* _mesa_function_pool[2637]: Map2d (offset 222) */
   "glMap2d\0"
   /* _mesa_function_pool[2645]: Map2f (offset 223) */
   "glMap2f\0"
   /* _mesa_function_pool[2653]: MapGrid1d (offset 224) */
   "glMapGrid1d\0"
   /* _mesa_function_pool[2665]: MapGrid1f (offset 225) */
   "glMapGrid1f\0"
   /* _mesa_function_pool[2677]: MapGrid2d (offset 226) */
   "glMapGrid2d\0"
   /* _mesa_function_pool[2689]: MapGrid2f (offset 227) */
   "glMapGrid2f\0"
   /* _mesa_function_pool[2701]: EvalCoord1d (offset 228) */
   "glEvalCoord1d\0"
   /* _mesa_function_pool[2715]: EvalCoord1dv (offset 229) */
   "glEvalCoord1dv\0"
   /* _mesa_function_pool[2730]: EvalCoord1f (offset 230) */
   "glEvalCoord1f\0"
   /* _mesa_function_pool[2744]: EvalCoord1fv (offset 231) */
   "glEvalCoord1fv\0"
   /* _mesa_function_pool[2759]: EvalCoord2d (offset 232) */
   "glEvalCoord2d\0"
   /* _mesa_function_pool[2773]: EvalCoord2dv (offset 233) */
   "glEvalCoord2dv\0"
   /* _mesa_function_pool[2788]: EvalCoord2f (offset 234) */
   "glEvalCoord2f\0"
   /* _mesa_function_pool[2802]: EvalCoord2fv (offset 235) */
   "glEvalCoord2fv\0"
   /* _mesa_function_pool[2817]: EvalMesh1 (offset 236) */
   "glEvalMesh1\0"
   /* _mesa_function_pool[2829]: EvalPoint1 (offset 237) */
   "glEvalPoint1\0"
   /* _mesa_function_pool[2842]: EvalMesh2 (offset 238) */
   "glEvalMesh2\0"
   /* _mesa_function_pool[2854]: EvalPoint2 (offset 239) */
   "glEvalPoint2\0"
   /* _mesa_function_pool[2867]: AlphaFunc (offset 240) */
   "glAlphaFunc\0"
   /* _mesa_function_pool[2879]: BlendFunc (offset 241) */
   "glBlendFunc\0"
   /* _mesa_function_pool[2891]: LogicOp (offset 242) */
   "glLogicOp\0"
   /* _mesa_function_pool[2901]: StencilFunc (offset 243) */
   "glStencilFunc\0"
   /* _mesa_function_pool[2915]: StencilOp (offset 244) */
   "glStencilOp\0"
   /* _mesa_function_pool[2927]: DepthFunc (offset 245) */
   "glDepthFunc\0"
   /* _mesa_function_pool[2939]: PixelZoom (offset 246) */
   "glPixelZoom\0"
   /* _mesa_function_pool[2951]: PixelTransferf (offset 247) */
   "glPixelTransferf\0"
   /* _mesa_function_pool[2968]: PixelTransferi (offset 248) */
   "glPixelTransferi\0"
   /* _mesa_function_pool[2985]: PixelStoref (offset 249) */
   "glPixelStoref\0"
   /* _mesa_function_pool[2999]: PixelStorei (offset 250) */
   "glPixelStorei\0"
   /* _mesa_function_pool[3013]: PixelMapfv (offset 251) */
   "glPixelMapfv\0"
   /* _mesa_function_pool[3026]: PixelMapuiv (offset 252) */
   "glPixelMapuiv\0"
   /* _mesa_function_pool[3040]: PixelMapusv (offset 253) */
   "glPixelMapusv\0"
   /* _mesa_function_pool[3054]: ReadBuffer (offset 254) */
   "glReadBuffer\0"
   /* _mesa_function_pool[3067]: CopyPixels (offset 255) */
   "glCopyPixels\0"
   /* _mesa_function_pool[3080]: ReadPixels (offset 256) */
   "glReadPixels\0"
   /* _mesa_function_pool[3093]: DrawPixels (offset 257) */
   "glDrawPixels\0"
   /* _mesa_function_pool[3106]: GetBooleanv (offset 258) */
   "glGetBooleanv\0"
   /* _mesa_function_pool[3120]: GetClipPlane (offset 259) */
   "glGetClipPlane\0"
   /* _mesa_function_pool[3135]: GetDoublev (offset 260) */
   "glGetDoublev\0"
   /* _mesa_function_pool[3148]: GetError (offset 261) */
   "glGetError\0"
   /* _mesa_function_pool[3159]: GetFloatv (offset 262) */
   "glGetFloatv\0"
   /* _mesa_function_pool[3171]: GetIntegerv (offset 263) */
   "glGetIntegerv\0"
   /* _mesa_function_pool[3185]: GetLightfv (offset 264) */
   "glGetLightfv\0"
   /* _mesa_function_pool[3198]: GetLightiv (offset 265) */
   "glGetLightiv\0"
   /* _mesa_function_pool[3211]: GetMapdv (offset 266) */
   "glGetMapdv\0"
   /* _mesa_function_pool[3222]: GetMapfv (offset 267) */
   "glGetMapfv\0"
   /* _mesa_function_pool[3233]: GetMapiv (offset 268) */
   "glGetMapiv\0"
   /* _mesa_function_pool[3244]: GetMaterialfv (offset 269) */
   "glGetMaterialfv\0"
   /* _mesa_function_pool[3260]: GetMaterialiv (offset 270) */
   "glGetMaterialiv\0"
   /* _mesa_function_pool[3276]: GetPixelMapfv (offset 271) */
   "glGetPixelMapfv\0"
   /* _mesa_function_pool[3292]: GetPixelMapuiv (offset 272) */
   "glGetPixelMapuiv\0"
   /* _mesa_function_pool[3309]: GetPixelMapusv (offset 273) */
   "glGetPixelMapusv\0"
   /* _mesa_function_pool[3326]: GetPolygonStipple (offset 274) */
   "glGetPolygonStipple\0"
   /* _mesa_function_pool[3346]: GetString (offset 275) */
   "glGetString\0"
   /* _mesa_function_pool[3358]: GetTexEnvfv (offset 276) */
   "glGetTexEnvfv\0"
   /* _mesa_function_pool[3372]: GetTexEnviv (offset 277) */
   "glGetTexEnviv\0"
   /* _mesa_function_pool[3386]: GetTexGendv (offset 278) */
   "glGetTexGendv\0"
   /* _mesa_function_pool[3400]: GetTexGenfv (offset 279) */
   "glGetTexGenfv\0"
   /* _mesa_function_pool[3414]: GetTexGeniv (offset 280) */
   "glGetTexGeniv\0"
   /* _mesa_function_pool[3428]: GetTexImage (offset 281) */
   "glGetTexImage\0"
   /* _mesa_function_pool[3442]: GetTexParameterfv (offset 282) */
   "glGetTexParameterfv\0"
   /* _mesa_function_pool[3462]: GetTexParameteriv (offset 283) */
   "glGetTexParameteriv\0"
   /* _mesa_function_pool[3482]: GetTexLevelParameterfv (offset 284) */
   "glGetTexLevelParameterfv\0"
   /* _mesa_function_pool[3507]: GetTexLevelParameteriv (offset 285) */
   "glGetTexLevelParameteriv\0"
   /* _mesa_function_pool[3532]: IsEnabled (offset 286) */
   "glIsEnabled\0"
   /* _mesa_function_pool[3544]: IsList (offset 287) */
   "glIsList\0"
   /* _mesa_function_pool[3553]: DepthRange (offset 288) */
   "glDepthRange\0"
   /* _mesa_function_pool[3566]: Frustum (offset 289) */
   "glFrustum\0"
   /* _mesa_function_pool[3576]: LoadIdentity (offset 290) */
   "glLoadIdentity\0"
   /* _mesa_function_pool[3591]: LoadMatrixf (offset 291) */
   "glLoadMatrixf\0"
   /* _mesa_function_pool[3605]: LoadMatrixd (offset 292) */
   "glLoadMatrixd\0"
   /* _mesa_function_pool[3619]: MatrixMode (offset 293) */
   "glMatrixMode\0"
   /* _mesa_function_pool[3632]: MultMatrixf (offset 294) */
   "glMultMatrixf\0"
   /* _mesa_function_pool[3646]: MultMatrixd (offset 295) */
   "glMultMatrixd\0"
   /* _mesa_function_pool[3660]: Ortho (offset 296) */
   "glOrtho\0"
   /* _mesa_function_pool[3668]: PopMatrix (offset 297) */
   "glPopMatrix\0"
   /* _mesa_function_pool[3680]: PushMatrix (offset 298) */
   "glPushMatrix\0"
   /* _mesa_function_pool[3693]: Rotated (offset 299) */
   "glRotated\0"
   /* _mesa_function_pool[3703]: Rotatef (offset 300) */
   "glRotatef\0"
   /* _mesa_function_pool[3713]: Scaled (offset 301) */
   "glScaled\0"
   /* _mesa_function_pool[3722]: Scalef (offset 302) */
   "glScalef\0"
   /* _mesa_function_pool[3731]: Translated (offset 303) */
   "glTranslated\0"
   /* _mesa_function_pool[3744]: Translatef (offset 304) */
   "glTranslatef\0"
   /* _mesa_function_pool[3757]: Viewport (offset 305) */
   "glViewport\0"
   /* _mesa_function_pool[3768]: ArrayElement (offset 306) */
   "glArrayElement\0"
   /* _mesa_function_pool[3783]: ColorPointer (offset 308) */
   "glColorPointer\0"
   /* _mesa_function_pool[3798]: DisableClientState (offset 309) */
   "glDisableClientState\0"
   /* _mesa_function_pool[3819]: DrawArrays (offset 310) */
   "glDrawArrays\0"
   /* _mesa_function_pool[3832]: DrawElements (offset 311) */
   "glDrawElements\0"
   /* _mesa_function_pool[3847]: EdgeFlagPointer (offset 312) */
   "glEdgeFlagPointer\0"
   /* _mesa_function_pool[3865]: EnableClientState (offset 313) */
   "glEnableClientState\0"
   /* _mesa_function_pool[3885]: GetPointerv (offset 329) */
   "glGetPointerv\0"
   /* _mesa_function_pool[3899]: IndexPointer (offset 314) */
   "glIndexPointer\0"
   /* _mesa_function_pool[3914]: InterleavedArrays (offset 317) */
   "glInterleavedArrays\0"
   /* _mesa_function_pool[3934]: NormalPointer (offset 318) */
   "glNormalPointer\0"
   /* _mesa_function_pool[3950]: TexCoordPointer (offset 320) */
   "glTexCoordPointer\0"
   /* _mesa_function_pool[3968]: VertexPointer (offset 321) */
   "glVertexPointer\0"
   /* _mesa_function_pool[3984]: PolygonOffset (offset 319) */
   "glPolygonOffset\0"
   /* _mesa_function_pool[4000]: CopyTexImage1D (offset 323) */
   "glCopyTexImage1D\0"
   /* _mesa_function_pool[4017]: CopyTexImage2D (offset 324) */
   "glCopyTexImage2D\0"
   /* _mesa_function_pool[4034]: CopyTexSubImage1D (offset 325) */
   "glCopyTexSubImage1D\0"
   /* _mesa_function_pool[4054]: CopyTexSubImage2D (offset 326) */
   "glCopyTexSubImage2D\0"
   /* _mesa_function_pool[4074]: TexSubImage1D (offset 332) */
   "glTexSubImage1D\0"
   /* _mesa_function_pool[4090]: TexSubImage2D (offset 333) */
   "glTexSubImage2D\0"
   /* _mesa_function_pool[4106]: AreTexturesResident (offset 322) */
   "glAreTexturesResident\0"
   /* _mesa_function_pool[4128]: BindTexture (offset 307) */
   "glBindTexture\0"
   /* _mesa_function_pool[4142]: DeleteTextures (offset 327) */
   "glDeleteTextures\0"
   /* _mesa_function_pool[4159]: GenTextures (offset 328) */
   "glGenTextures\0"
   /* _mesa_function_pool[4173]: IsTexture (offset 330) */
   "glIsTexture\0"
   /* _mesa_function_pool[4185]: PrioritizeTextures (offset 331) */
   "glPrioritizeTextures\0"
   /* _mesa_function_pool[4206]: Indexub (offset 315) */
   "glIndexub\0"
   /* _mesa_function_pool[4216]: Indexubv (offset 316) */
   "glIndexubv\0"
   /* _mesa_function_pool[4227]: PopClientAttrib (offset 334) */
   "glPopClientAttrib\0"
   /* _mesa_function_pool[4245]: PushClientAttrib (offset 335) */
   "glPushClientAttrib\0"
   /* _mesa_function_pool[4264]: BlendColor (offset 336) */
   "glBlendColor\0"
   /* _mesa_function_pool[4277]: BlendEquation (offset 337) */
   "glBlendEquation\0"
   /* _mesa_function_pool[4293]: DrawRangeElements (offset 338) */
   "glDrawRangeElements\0"
   /* _mesa_function_pool[4313]: ColorTable (offset 339) */
   "glColorTable\0"
   /* _mesa_function_pool[4326]: ColorTableParameterfv (offset 340) */
   "glColorTableParameterfv\0"
   /* _mesa_function_pool[4350]: ColorTableParameteriv (offset 341) */
   "glColorTableParameteriv\0"
   /* _mesa_function_pool[4374]: CopyColorTable (offset 342) */
   "glCopyColorTable\0"
   /* _mesa_function_pool[4391]: GetColorTable (offset 343) */
   "glGetColorTable\0"
   /* _mesa_function_pool[4407]: GetColorTableParameterfv (offset 344) */
   "glGetColorTableParameterfv\0"
   /* _mesa_function_pool[4434]: GetColorTableParameteriv (offset 345) */
   "glGetColorTableParameteriv\0"
   /* _mesa_function_pool[4461]: ColorSubTable (offset 346) */
   "glColorSubTable\0"
   /* _mesa_function_pool[4477]: CopyColorSubTable (offset 347) */
   "glCopyColorSubTable\0"
   /* _mesa_function_pool[4497]: ConvolutionFilter1D (offset 348) */
   "glConvolutionFilter1D\0"
   /* _mesa_function_pool[4519]: ConvolutionFilter2D (offset 349) */
   "glConvolutionFilter2D\0"
   /* _mesa_function_pool[4541]: ConvolutionParameterf (offset 350) */
   "glConvolutionParameterf\0"
   /* _mesa_function_pool[4565]: ConvolutionParameterfv (offset 351) */
   "glConvolutionParameterfv\0"
   /* _mesa_function_pool[4590]: ConvolutionParameteri (offset 352) */
   "glConvolutionParameteri\0"
   /* _mesa_function_pool[4614]: ConvolutionParameteriv (offset 353) */
   "glConvolutionParameteriv\0"
   /* _mesa_function_pool[4639]: CopyConvolutionFilter1D (offset 354) */
   "glCopyConvolutionFilter1D\0"
   /* _mesa_function_pool[4665]: CopyConvolutionFilter2D (offset 355) */
   "glCopyConvolutionFilter2D\0"
   /* _mesa_function_pool[4691]: GetConvolutionFilter (offset 356) */
   "glGetConvolutionFilter\0"
   /* _mesa_function_pool[4714]: GetConvolutionParameterfv (offset 357) */
   "glGetConvolutionParameterfv\0"
   /* _mesa_function_pool[4742]: GetConvolutionParameteriv (offset 358) */
   "glGetConvolutionParameteriv\0"
   /* _mesa_function_pool[4770]: GetSeparableFilter (offset 359) */
   "glGetSeparableFilter\0"
   /* _mesa_function_pool[4791]: SeparableFilter2D (offset 360) */
   "glSeparableFilter2D\0"
   /* _mesa_function_pool[4811]: GetHistogram (offset 361) */
   "glGetHistogram\0"
   /* _mesa_function_pool[4826]: GetHistogramParameterfv (offset 362) */
   "glGetHistogramParameterfv\0"
   /* _mesa_function_pool[4852]: GetHistogramParameteriv (offset 363) */
   "glGetHistogramParameteriv\0"
   /* _mesa_function_pool[4878]: GetMinmax (offset 364) */
   "glGetMinmax\0"
   /* _mesa_function_pool[4890]: GetMinmaxParameterfv (offset 365) */
   "glGetMinmaxParameterfv\0"
   /* _mesa_function_pool[4913]: GetMinmaxParameteriv (offset 366) */
   "glGetMinmaxParameteriv\0"
   /* _mesa_function_pool[4936]: Histogram (offset 367) */
   "glHistogram\0"
   /* _mesa_function_pool[4948]: Minmax (offset 368) */
   "glMinmax\0"
   /* _mesa_function_pool[4957]: ResetHistogram (offset 369) */
   "glResetHistogram\0"
   /* _mesa_function_pool[4974]: ResetMinmax (offset 370) */
   "glResetMinmax\0"
   /* _mesa_function_pool[4988]: TexImage3D (offset 371) */
   "glTexImage3D\0"
   /* _mesa_function_pool[5001]: TexSubImage3D (offset 372) */
   "glTexSubImage3D\0"
   /* _mesa_function_pool[5017]: CopyTexSubImage3D (offset 373) */
   "glCopyTexSubImage3D\0"
   /* _mesa_function_pool[5037]: ActiveTexture (offset 374) */
   "glActiveTexture\0"
   /* _mesa_function_pool[5053]: ClientActiveTexture (offset 375) */
   "glClientActiveTexture\0"
   /* _mesa_function_pool[5075]: MultiTexCoord1d (offset 376) */
   "glMultiTexCoord1d\0"
   /* _mesa_function_pool[5093]: MultiTexCoord1dv (offset 377) */
   "glMultiTexCoord1dv\0"
   /* _mesa_function_pool[5112]: MultiTexCoord1fARB (offset 378) */
   "glMultiTexCoord1f\0"
   /* _mesa_function_pool[5130]: MultiTexCoord1fvARB (offset 379) */
   "glMultiTexCoord1fv\0"
   /* _mesa_function_pool[5149]: MultiTexCoord1i (offset 380) */
   "glMultiTexCoord1i\0"
   /* _mesa_function_pool[5167]: MultiTexCoord1iv (offset 381) */
   "glMultiTexCoord1iv\0"
   /* _mesa_function_pool[5186]: MultiTexCoord1s (offset 382) */
   "glMultiTexCoord1s\0"
   /* _mesa_function_pool[5204]: MultiTexCoord1sv (offset 383) */
   "glMultiTexCoord1sv\0"
   /* _mesa_function_pool[5223]: MultiTexCoord2d (offset 384) */
   "glMultiTexCoord2d\0"
   /* _mesa_function_pool[5241]: MultiTexCoord2dv (offset 385) */
   "glMultiTexCoord2dv\0"
   /* _mesa_function_pool[5260]: MultiTexCoord2fARB (offset 386) */
   "glMultiTexCoord2f\0"
   /* _mesa_function_pool[5278]: MultiTexCoord2fvARB (offset 387) */
   "glMultiTexCoord2fv\0"
   /* _mesa_function_pool[5297]: MultiTexCoord2i (offset 388) */
   "glMultiTexCoord2i\0"
   /* _mesa_function_pool[5315]: MultiTexCoord2iv (offset 389) */
   "glMultiTexCoord2iv\0"
   /* _mesa_function_pool[5334]: MultiTexCoord2s (offset 390) */
   "glMultiTexCoord2s\0"
   /* _mesa_function_pool[5352]: MultiTexCoord2sv (offset 391) */
   "glMultiTexCoord2sv\0"
   /* _mesa_function_pool[5371]: MultiTexCoord3d (offset 392) */
   "glMultiTexCoord3d\0"
   /* _mesa_function_pool[5389]: MultiTexCoord3dv (offset 393) */
   "glMultiTexCoord3dv\0"
   /* _mesa_function_pool[5408]: MultiTexCoord3fARB (offset 394) */
   "glMultiTexCoord3f\0"
   /* _mesa_function_pool[5426]: MultiTexCoord3fvARB (offset 395) */
   "glMultiTexCoord3fv\0"
   /* _mesa_function_pool[5445]: MultiTexCoord3i (offset 396) */
   "glMultiTexCoord3i\0"
   /* _mesa_function_pool[5463]: MultiTexCoord3iv (offset 397) */
   "glMultiTexCoord3iv\0"
   /* _mesa_function_pool[5482]: MultiTexCoord3s (offset 398) */
   "glMultiTexCoord3s\0"
   /* _mesa_function_pool[5500]: MultiTexCoord3sv (offset 399) */
   "glMultiTexCoord3sv\0"
   /* _mesa_function_pool[5519]: MultiTexCoord4d (offset 400) */
   "glMultiTexCoord4d\0"
   /* _mesa_function_pool[5537]: MultiTexCoord4dv (offset 401) */
   "glMultiTexCoord4dv\0"
   /* _mesa_function_pool[5556]: MultiTexCoord4fARB (offset 402) */
   "glMultiTexCoord4f\0"
   /* _mesa_function_pool[5574]: MultiTexCoord4fvARB (offset 403) */
   "glMultiTexCoord4fv\0"
   /* _mesa_function_pool[5593]: MultiTexCoord4i (offset 404) */
   "glMultiTexCoord4i\0"
   /* _mesa_function_pool[5611]: MultiTexCoord4iv (offset 405) */
   "glMultiTexCoord4iv\0"
   /* _mesa_function_pool[5630]: MultiTexCoord4s (offset 406) */
   "glMultiTexCoord4s\0"
   /* _mesa_function_pool[5648]: MultiTexCoord4sv (offset 407) */
   "glMultiTexCoord4sv\0"
   /* _mesa_function_pool[5667]: LoadTransposeMatrixf (will be remapped) */
   "glLoadTransposeMatrixf\0"
   /* _mesa_function_pool[5690]: LoadTransposeMatrixd (will be remapped) */
   "glLoadTransposeMatrixd\0"
   /* _mesa_function_pool[5713]: MultTransposeMatrixf (will be remapped) */
   "glMultTransposeMatrixf\0"
   /* _mesa_function_pool[5736]: MultTransposeMatrixd (will be remapped) */
   "glMultTransposeMatrixd\0"
   /* _mesa_function_pool[5759]: SampleCoverage (will be remapped) */
   "glSampleCoverage\0"
   /* _mesa_function_pool[5776]: CompressedTexImage3D (will be remapped) */
   "glCompressedTexImage3D\0"
   /* _mesa_function_pool[5799]: CompressedTexImage2D (will be remapped) */
   "glCompressedTexImage2D\0"
   /* _mesa_function_pool[5822]: CompressedTexImage1D (will be remapped) */
   "glCompressedTexImage1D\0"
   /* _mesa_function_pool[5845]: CompressedTexSubImage3D (will be remapped) */
   "glCompressedTexSubImage3D\0"
   /* _mesa_function_pool[5871]: CompressedTexSubImage2D (will be remapped) */
   "glCompressedTexSubImage2D\0"
   /* _mesa_function_pool[5897]: CompressedTexSubImage1D (will be remapped) */
   "glCompressedTexSubImage1D\0"
   /* _mesa_function_pool[5923]: GetCompressedTexImage (will be remapped) */
   "glGetCompressedTexImage\0"
   /* _mesa_function_pool[5947]: BlendFuncSeparate (will be remapped) */
   "glBlendFuncSeparate\0"
   /* _mesa_function_pool[5967]: FogCoordfEXT (will be remapped) */
   "glFogCoordf\0"
   /* _mesa_function_pool[5979]: FogCoordfvEXT (will be remapped) */
   "glFogCoordfv\0"
   /* _mesa_function_pool[5992]: FogCoordd (will be remapped) */
   "glFogCoordd\0"
   /* _mesa_function_pool[6004]: FogCoorddv (will be remapped) */
   "glFogCoorddv\0"
   /* _mesa_function_pool[6017]: FogCoordPointer (will be remapped) */
   "glFogCoordPointer\0"
   /* _mesa_function_pool[6035]: MultiDrawArrays (will be remapped) */
   "glMultiDrawArrays\0"
   /* _mesa_function_pool[6053]: MultiDrawElements (will be remapped) */
   "glMultiDrawElementsEXT\0"
   /* _mesa_function_pool[6076]: PointParameterf (will be remapped) */
   "glPointParameterf\0"
   /* _mesa_function_pool[6094]: PointParameterfv (will be remapped) */
   "glPointParameterfv\0"
   /* _mesa_function_pool[6113]: PointParameteri (will be remapped) */
   "glPointParameteri\0"
   /* _mesa_function_pool[6131]: PointParameteriv (will be remapped) */
   "glPointParameteriv\0"
   /* _mesa_function_pool[6150]: SecondaryColor3b (will be remapped) */
   "glSecondaryColor3b\0"
   /* _mesa_function_pool[6169]: SecondaryColor3bv (will be remapped) */
   "glSecondaryColor3bv\0"
   /* _mesa_function_pool[6189]: SecondaryColor3d (will be remapped) */
   "glSecondaryColor3d\0"
   /* _mesa_function_pool[6208]: SecondaryColor3dv (will be remapped) */
   "glSecondaryColor3dv\0"
   /* _mesa_function_pool[6228]: SecondaryColor3fEXT (will be remapped) */
   "glSecondaryColor3f\0"
   /* _mesa_function_pool[6247]: SecondaryColor3fvEXT (will be remapped) */
   "glSecondaryColor3fv\0"
   /* _mesa_function_pool[6267]: SecondaryColor3i (will be remapped) */
   "glSecondaryColor3i\0"
   /* _mesa_function_pool[6286]: SecondaryColor3iv (will be remapped) */
   "glSecondaryColor3iv\0"
   /* _mesa_function_pool[6306]: SecondaryColor3s (will be remapped) */
   "glSecondaryColor3s\0"
   /* _mesa_function_pool[6325]: SecondaryColor3sv (will be remapped) */
   "glSecondaryColor3sv\0"
   /* _mesa_function_pool[6345]: SecondaryColor3ub (will be remapped) */
   "glSecondaryColor3ub\0"
   /* _mesa_function_pool[6365]: SecondaryColor3ubv (will be remapped) */
   "glSecondaryColor3ubv\0"
   /* _mesa_function_pool[6386]: SecondaryColor3ui (will be remapped) */
   "glSecondaryColor3ui\0"
   /* _mesa_function_pool[6406]: SecondaryColor3uiv (will be remapped) */
   "glSecondaryColor3uiv\0"
   /* _mesa_function_pool[6427]: SecondaryColor3us (will be remapped) */
   "glSecondaryColor3us\0"
   /* _mesa_function_pool[6447]: SecondaryColor3usv (will be remapped) */
   "glSecondaryColor3usv\0"
   /* _mesa_function_pool[6468]: SecondaryColorPointer (will be remapped) */
   "glSecondaryColorPointer\0"
   /* _mesa_function_pool[6492]: WindowPos2d (will be remapped) */
   "glWindowPos2d\0"
   /* _mesa_function_pool[6506]: WindowPos2dv (will be remapped) */
   "glWindowPos2dv\0"
   /* _mesa_function_pool[6521]: WindowPos2f (will be remapped) */
   "glWindowPos2f\0"
   /* _mesa_function_pool[6535]: WindowPos2fv (will be remapped) */
   "glWindowPos2fv\0"
   /* _mesa_function_pool[6550]: WindowPos2i (will be remapped) */
   "glWindowPos2i\0"
   /* _mesa_function_pool[6564]: WindowPos2iv (will be remapped) */
   "glWindowPos2iv\0"
   /* _mesa_function_pool[6579]: WindowPos2s (will be remapped) */
   "glWindowPos2s\0"
   /* _mesa_function_pool[6593]: WindowPos2sv (will be remapped) */
   "glWindowPos2sv\0"
   /* _mesa_function_pool[6608]: WindowPos3d (will be remapped) */
   "glWindowPos3d\0"
   /* _mesa_function_pool[6622]: WindowPos3dv (will be remapped) */
   "glWindowPos3dv\0"
   /* _mesa_function_pool[6637]: WindowPos3f (will be remapped) */
   "glWindowPos3f\0"
   /* _mesa_function_pool[6651]: WindowPos3fv (will be remapped) */
   "glWindowPos3fv\0"
   /* _mesa_function_pool[6666]: WindowPos3i (will be remapped) */
   "glWindowPos3i\0"
   /* _mesa_function_pool[6680]: WindowPos3iv (will be remapped) */
   "glWindowPos3iv\0"
   /* _mesa_function_pool[6695]: WindowPos3s (will be remapped) */
   "glWindowPos3s\0"
   /* _mesa_function_pool[6709]: WindowPos3sv (will be remapped) */
   "glWindowPos3sv\0"
   /* _mesa_function_pool[6724]: BindBuffer (will be remapped) */
   "glBindBuffer\0"
   /* _mesa_function_pool[6737]: BufferData (will be remapped) */
   "glBufferData\0"
   /* _mesa_function_pool[6750]: BufferSubData (will be remapped) */
   "glBufferSubData\0"
   /* _mesa_function_pool[6766]: DeleteBuffers (will be remapped) */
   "glDeleteBuffers\0"
   /* _mesa_function_pool[6782]: GenBuffers (will be remapped) */
   "glGenBuffers\0"
   /* _mesa_function_pool[6795]: GetBufferParameteriv (will be remapped) */
   "glGetBufferParameteriv\0"
   /* _mesa_function_pool[6818]: GetBufferPointerv (will be remapped) */
   "glGetBufferPointerv\0"
   /* _mesa_function_pool[6838]: GetBufferSubData (will be remapped) */
   "glGetBufferSubData\0"
   /* _mesa_function_pool[6857]: IsBuffer (will be remapped) */
   "glIsBuffer\0"
   /* _mesa_function_pool[6868]: MapBuffer (will be remapped) */
   "glMapBuffer\0"
   /* _mesa_function_pool[6880]: UnmapBuffer (will be remapped) */
   "glUnmapBuffer\0"
   /* _mesa_function_pool[6894]: GenQueries (will be remapped) */
   "glGenQueries\0"
   /* _mesa_function_pool[6907]: DeleteQueries (will be remapped) */
   "glDeleteQueries\0"
   /* _mesa_function_pool[6923]: IsQuery (will be remapped) */
   "glIsQuery\0"
   /* _mesa_function_pool[6933]: BeginQuery (will be remapped) */
   "glBeginQuery\0"
   /* _mesa_function_pool[6946]: EndQuery (will be remapped) */
   "glEndQuery\0"
   /* _mesa_function_pool[6957]: GetQueryiv (will be remapped) */
   "glGetQueryiv\0"
   /* _mesa_function_pool[6970]: GetQueryObjectiv (will be remapped) */
   "glGetQueryObjectiv\0"
   /* _mesa_function_pool[6989]: GetQueryObjectuiv (will be remapped) */
   "glGetQueryObjectuiv\0"
   /* _mesa_function_pool[7009]: BlendEquationSeparate (will be remapped) */
   "glBlendEquationSeparate\0"
   /* _mesa_function_pool[7033]: DrawBuffers (will be remapped) */
   "glDrawBuffers\0"
   /* _mesa_function_pool[7047]: StencilFuncSeparate (will be remapped) */
   "glStencilFuncSeparate\0"
   /* _mesa_function_pool[7069]: StencilOpSeparate (will be remapped) */
   "glStencilOpSeparate\0"
   /* _mesa_function_pool[7089]: StencilMaskSeparate (will be remapped) */
   "glStencilMaskSeparate\0"
   /* _mesa_function_pool[7111]: AttachShader (will be remapped) */
   "glAttachShader\0"
   /* _mesa_function_pool[7126]: BindAttribLocation (will be remapped) */
   "glBindAttribLocation\0"
   /* _mesa_function_pool[7147]: CompileShader (will be remapped) */
   "glCompileShader\0"
   /* _mesa_function_pool[7163]: CreateProgram (will be remapped) */
   "glCreateProgram\0"
   /* _mesa_function_pool[7179]: CreateShader (will be remapped) */
   "glCreateShader\0"
   /* _mesa_function_pool[7194]: DeleteProgram (will be remapped) */
   "glDeleteProgram\0"
   /* _mesa_function_pool[7210]: DeleteShader (will be remapped) */
   "glDeleteShader\0"
   /* _mesa_function_pool[7225]: DetachShader (will be remapped) */
   "glDetachShader\0"
   /* _mesa_function_pool[7240]: DisableVertexAttribArray (will be remapped) */
   "glDisableVertexAttribArray\0"
   /* _mesa_function_pool[7267]: EnableVertexAttribArray (will be remapped) */
   "glEnableVertexAttribArray\0"
   /* _mesa_function_pool[7293]: GetActiveAttrib (will be remapped) */
   "glGetActiveAttrib\0"
   /* _mesa_function_pool[7311]: GetActiveUniform (will be remapped) */
   "glGetActiveUniform\0"
   /* _mesa_function_pool[7330]: GetAttachedShaders (will be remapped) */
   "glGetAttachedShaders\0"
   /* _mesa_function_pool[7351]: GetAttribLocation (will be remapped) */
   "glGetAttribLocation\0"
   /* _mesa_function_pool[7371]: GetProgramiv (will be remapped) */
   "glGetProgramiv\0"
   /* _mesa_function_pool[7386]: GetProgramInfoLog (will be remapped) */
   "glGetProgramInfoLog\0"
   /* _mesa_function_pool[7406]: GetShaderiv (will be remapped) */
   "glGetShaderiv\0"
   /* _mesa_function_pool[7420]: GetShaderInfoLog (will be remapped) */
   "glGetShaderInfoLog\0"
   /* _mesa_function_pool[7439]: GetShaderSource (will be remapped) */
   "glGetShaderSource\0"
   /* _mesa_function_pool[7457]: GetUniformLocation (will be remapped) */
   "glGetUniformLocation\0"
   /* _mesa_function_pool[7478]: GetUniformfv (will be remapped) */
   "glGetUniformfv\0"
   /* _mesa_function_pool[7493]: GetUniformiv (will be remapped) */
   "glGetUniformiv\0"
   /* _mesa_function_pool[7508]: GetVertexAttribdv (will be remapped) */
   "glGetVertexAttribdv\0"
   /* _mesa_function_pool[7528]: GetVertexAttribfv (will be remapped) */
   "glGetVertexAttribfv\0"
   /* _mesa_function_pool[7548]: GetVertexAttribiv (will be remapped) */
   "glGetVertexAttribiv\0"
   /* _mesa_function_pool[7568]: GetVertexAttribPointerv (will be remapped) */
   "glGetVertexAttribPointerv\0"
   /* _mesa_function_pool[7594]: IsProgram (will be remapped) */
   "glIsProgram\0"
   /* _mesa_function_pool[7606]: IsShader (will be remapped) */
   "glIsShader\0"
   /* _mesa_function_pool[7617]: LinkProgram (will be remapped) */
   "glLinkProgram\0"
   /* _mesa_function_pool[7631]: ShaderSource (will be remapped) */
   "glShaderSource\0"
   /* _mesa_function_pool[7646]: UseProgram (will be remapped) */
   "glUseProgram\0"
   /* _mesa_function_pool[7659]: Uniform1f (will be remapped) */
   "glUniform1f\0"
   /* _mesa_function_pool[7671]: Uniform2f (will be remapped) */
   "glUniform2f\0"
   /* _mesa_function_pool[7683]: Uniform3f (will be remapped) */
   "glUniform3f\0"
   /* _mesa_function_pool[7695]: Uniform4f (will be remapped) */
   "glUniform4f\0"
   /* _mesa_function_pool[7707]: Uniform1i (will be remapped) */
   "glUniform1i\0"
   /* _mesa_function_pool[7719]: Uniform2i (will be remapped) */
   "glUniform2i\0"
   /* _mesa_function_pool[7731]: Uniform3i (will be remapped) */
   "glUniform3i\0"
   /* _mesa_function_pool[7743]: Uniform4i (will be remapped) */
   "glUniform4i\0"
   /* _mesa_function_pool[7755]: Uniform1fv (will be remapped) */
   "glUniform1fv\0"
   /* _mesa_function_pool[7768]: Uniform2fv (will be remapped) */
   "glUniform2fv\0"
   /* _mesa_function_pool[7781]: Uniform3fv (will be remapped) */
   "glUniform3fv\0"
   /* _mesa_function_pool[7794]: Uniform4fv (will be remapped) */
   "glUniform4fv\0"
   /* _mesa_function_pool[7807]: Uniform1iv (will be remapped) */
   "glUniform1iv\0"
   /* _mesa_function_pool[7820]: Uniform2iv (will be remapped) */
   "glUniform2iv\0"
   /* _mesa_function_pool[7833]: Uniform3iv (will be remapped) */
   "glUniform3iv\0"
   /* _mesa_function_pool[7846]: Uniform4iv (will be remapped) */
   "glUniform4iv\0"
   /* _mesa_function_pool[7859]: UniformMatrix2fv (will be remapped) */
   "glUniformMatrix2fv\0"
   /* _mesa_function_pool[7878]: UniformMatrix3fv (will be remapped) */
   "glUniformMatrix3fv\0"
   /* _mesa_function_pool[7897]: UniformMatrix4fv (will be remapped) */
   "glUniformMatrix4fv\0"
   /* _mesa_function_pool[7916]: ValidateProgram (will be remapped) */
   "glValidateProgram\0"
   /* _mesa_function_pool[7934]: VertexAttrib1d (will be remapped) */
   "glVertexAttrib1d\0"
   /* _mesa_function_pool[7951]: VertexAttrib1dv (will be remapped) */
   "glVertexAttrib1dv\0"
   /* _mesa_function_pool[7969]: VertexAttrib1fARB (will be remapped) */
   "glVertexAttrib1f\0"
   /* _mesa_function_pool[7986]: VertexAttrib1fvARB (will be remapped) */
   "glVertexAttrib1fv\0"
   /* _mesa_function_pool[8004]: VertexAttrib1s (will be remapped) */
   "glVertexAttrib1s\0"
   /* _mesa_function_pool[8021]: VertexAttrib1sv (will be remapped) */
   "glVertexAttrib1sv\0"
   /* _mesa_function_pool[8039]: VertexAttrib2d (will be remapped) */
   "glVertexAttrib2d\0"
   /* _mesa_function_pool[8056]: VertexAttrib2dv (will be remapped) */
   "glVertexAttrib2dv\0"
   /* _mesa_function_pool[8074]: VertexAttrib2fARB (will be remapped) */
   "glVertexAttrib2f\0"
   /* _mesa_function_pool[8091]: VertexAttrib2fvARB (will be remapped) */
   "glVertexAttrib2fv\0"
   /* _mesa_function_pool[8109]: VertexAttrib2s (will be remapped) */
   "glVertexAttrib2s\0"
   /* _mesa_function_pool[8126]: VertexAttrib2sv (will be remapped) */
   "glVertexAttrib2sv\0"
   /* _mesa_function_pool[8144]: VertexAttrib3d (will be remapped) */
   "glVertexAttrib3d\0"
   /* _mesa_function_pool[8161]: VertexAttrib3dv (will be remapped) */
   "glVertexAttrib3dv\0"
   /* _mesa_function_pool[8179]: VertexAttrib3fARB (will be remapped) */
   "glVertexAttrib3f\0"
   /* _mesa_function_pool[8196]: VertexAttrib3fvARB (will be remapped) */
   "glVertexAttrib3fv\0"
   /* _mesa_function_pool[8214]: VertexAttrib3s (will be remapped) */
   "glVertexAttrib3s\0"
   /* _mesa_function_pool[8231]: VertexAttrib3sv (will be remapped) */
   "glVertexAttrib3sv\0"
   /* _mesa_function_pool[8249]: VertexAttrib4Nbv (will be remapped) */
   "glVertexAttrib4Nbv\0"
   /* _mesa_function_pool[8268]: VertexAttrib4Niv (will be remapped) */
   "glVertexAttrib4Niv\0"
   /* _mesa_function_pool[8287]: VertexAttrib4Nsv (will be remapped) */
   "glVertexAttrib4Nsv\0"
   /* _mesa_function_pool[8306]: VertexAttrib4Nub (will be remapped) */
   "glVertexAttrib4Nub\0"
   /* _mesa_function_pool[8325]: VertexAttrib4Nubv (will be remapped) */
   "glVertexAttrib4Nubv\0"
   /* _mesa_function_pool[8345]: VertexAttrib4Nuiv (will be remapped) */
   "glVertexAttrib4Nuiv\0"
   /* _mesa_function_pool[8365]: VertexAttrib4Nusv (will be remapped) */
   "glVertexAttrib4Nusv\0"
   /* _mesa_function_pool[8385]: VertexAttrib4bv (will be remapped) */
   "glVertexAttrib4bv\0"
   /* _mesa_function_pool[8403]: VertexAttrib4d (will be remapped) */
   "glVertexAttrib4d\0"
   /* _mesa_function_pool[8420]: VertexAttrib4dv (will be remapped) */
   "glVertexAttrib4dv\0"
   /* _mesa_function_pool[8438]: VertexAttrib4fARB (will be remapped) */
   "glVertexAttrib4f\0"
   /* _mesa_function_pool[8455]: VertexAttrib4fvARB (will be remapped) */
   "glVertexAttrib4fv\0"
   /* _mesa_function_pool[8473]: VertexAttrib4iv (will be remapped) */
   "glVertexAttrib4iv\0"
   /* _mesa_function_pool[8491]: VertexAttrib4s (will be remapped) */
   "glVertexAttrib4s\0"
   /* _mesa_function_pool[8508]: VertexAttrib4sv (will be remapped) */
   "glVertexAttrib4sv\0"
   /* _mesa_function_pool[8526]: VertexAttrib4ubv (will be remapped) */
   "glVertexAttrib4ubv\0"
   /* _mesa_function_pool[8545]: VertexAttrib4uiv (will be remapped) */
   "glVertexAttrib4uiv\0"
   /* _mesa_function_pool[8564]: VertexAttrib4usv (will be remapped) */
   "glVertexAttrib4usv\0"
   /* _mesa_function_pool[8583]: VertexAttribPointer (will be remapped) */
   "glVertexAttribPointer\0"
   /* _mesa_function_pool[8605]: UniformMatrix2x3fv (will be remapped) */
   "glUniformMatrix2x3fv\0"
   /* _mesa_function_pool[8626]: UniformMatrix3x2fv (will be remapped) */
   "glUniformMatrix3x2fv\0"
   /* _mesa_function_pool[8647]: UniformMatrix2x4fv (will be remapped) */
   "glUniformMatrix2x4fv\0"
   /* _mesa_function_pool[8668]: UniformMatrix4x2fv (will be remapped) */
   "glUniformMatrix4x2fv\0"
   /* _mesa_function_pool[8689]: UniformMatrix3x4fv (will be remapped) */
   "glUniformMatrix3x4fv\0"
   /* _mesa_function_pool[8710]: UniformMatrix4x3fv (will be remapped) */
   "glUniformMatrix4x3fv\0"
   /* _mesa_function_pool[8731]: WeightbvARB (dynamic) */
   "glWeightbvARB\0"
   /* _mesa_function_pool[8745]: WeightsvARB (dynamic) */
   "glWeightsvARB\0"
   /* _mesa_function_pool[8759]: WeightivARB (dynamic) */
   "glWeightivARB\0"
   /* _mesa_function_pool[8773]: WeightfvARB (dynamic) */
   "glWeightfvARB\0"
   /* _mesa_function_pool[8787]: WeightdvARB (dynamic) */
   "glWeightdvARB\0"
   /* _mesa_function_pool[8801]: WeightubvARB (dynamic) */
   "glWeightubvARB\0"
   /* _mesa_function_pool[8816]: WeightusvARB (dynamic) */
   "glWeightusvARB\0"
   /* _mesa_function_pool[8831]: WeightuivARB (dynamic) */
   "glWeightuivARB\0"
   /* _mesa_function_pool[8846]: WeightPointerARB (dynamic) */
   "glWeightPointerARB\0"
   /* _mesa_function_pool[8865]: VertexBlendARB (dynamic) */
   "glVertexBlendARB\0"
   /* _mesa_function_pool[8882]: CurrentPaletteMatrixARB (dynamic) */
   "glCurrentPaletteMatrixARB\0"
   /* _mesa_function_pool[8908]: MatrixIndexubvARB (dynamic) */
   "glMatrixIndexubvARB\0"
   /* _mesa_function_pool[8928]: MatrixIndexusvARB (dynamic) */
   "glMatrixIndexusvARB\0"
   /* _mesa_function_pool[8948]: MatrixIndexuivARB (dynamic) */
   "glMatrixIndexuivARB\0"
   /* _mesa_function_pool[8968]: MatrixIndexPointerARB (dynamic) */
   "glMatrixIndexPointerARB\0"
   /* _mesa_function_pool[8992]: ProgramStringARB (will be remapped) */
   "glProgramStringARB\0"
   /* _mesa_function_pool[9011]: BindProgramARB (will be remapped) */
   "glBindProgramARB\0"
   /* _mesa_function_pool[9028]: DeleteProgramsARB (will be remapped) */
   "glDeleteProgramsARB\0"
   /* _mesa_function_pool[9048]: GenProgramsARB (will be remapped) */
   "glGenProgramsARB\0"
   /* _mesa_function_pool[9065]: IsProgramARB (will be remapped) */
   "glIsProgramARB\0"
   /* _mesa_function_pool[9080]: ProgramEnvParameter4dARB (will be remapped) */
   "glProgramEnvParameter4dARB\0"
   /* _mesa_function_pool[9107]: ProgramEnvParameter4dvARB (will be remapped) */
   "glProgramEnvParameter4dvARB\0"
   /* _mesa_function_pool[9135]: ProgramEnvParameter4fARB (will be remapped) */
   "glProgramEnvParameter4fARB\0"
   /* _mesa_function_pool[9162]: ProgramEnvParameter4fvARB (will be remapped) */
   "glProgramEnvParameter4fvARB\0"
   /* _mesa_function_pool[9190]: ProgramLocalParameter4dARB (will be remapped) */
   "glProgramLocalParameter4dARB\0"
   /* _mesa_function_pool[9219]: ProgramLocalParameter4dvARB (will be remapped) */
   "glProgramLocalParameter4dvARB\0"
   /* _mesa_function_pool[9249]: ProgramLocalParameter4fARB (will be remapped) */
   "glProgramLocalParameter4fARB\0"
   /* _mesa_function_pool[9278]: ProgramLocalParameter4fvARB (will be remapped) */
   "glProgramLocalParameter4fvARB\0"
   /* _mesa_function_pool[9308]: GetProgramEnvParameterdvARB (will be remapped) */
   "glGetProgramEnvParameterdvARB\0"
   /* _mesa_function_pool[9338]: GetProgramEnvParameterfvARB (will be remapped) */
   "glGetProgramEnvParameterfvARB\0"
   /* _mesa_function_pool[9368]: GetProgramLocalParameterdvARB (will be remapped) */
   "glGetProgramLocalParameterdvARB\0"
   /* _mesa_function_pool[9400]: GetProgramLocalParameterfvARB (will be remapped) */
   "glGetProgramLocalParameterfvARB\0"
   /* _mesa_function_pool[9432]: GetProgramivARB (will be remapped) */
   "glGetProgramivARB\0"
   /* _mesa_function_pool[9450]: GetProgramStringARB (will be remapped) */
   "glGetProgramStringARB\0"
   /* _mesa_function_pool[9472]: DeleteObjectARB (will be remapped) */
   "glDeleteObjectARB\0"
   /* _mesa_function_pool[9490]: GetHandleARB (will be remapped) */
   "glGetHandleARB\0"
   /* _mesa_function_pool[9505]: DetachObjectARB (will be remapped) */
   "glDetachObjectARB\0"
   /* _mesa_function_pool[9523]: CreateShaderObjectARB (will be remapped) */
   "glCreateShaderObjectARB\0"
   /* _mesa_function_pool[9547]: CreateProgramObjectARB (will be remapped) */
   "glCreateProgramObjectARB\0"
   /* _mesa_function_pool[9572]: AttachObjectARB (will be remapped) */
   "glAttachObjectARB\0"
   /* _mesa_function_pool[9590]: GetObjectParameterfvARB (will be remapped) */
   "glGetObjectParameterfvARB\0"
   /* _mesa_function_pool[9616]: GetObjectParameterivARB (will be remapped) */
   "glGetObjectParameterivARB\0"
   /* _mesa_function_pool[9642]: GetInfoLogARB (will be remapped) */
   "glGetInfoLogARB\0"
   /* _mesa_function_pool[9658]: GetAttachedObjectsARB (will be remapped) */
   "glGetAttachedObjectsARB\0"
   /* _mesa_function_pool[9682]: ClampColor (will be remapped) */
   "glClampColorARB\0"
   /* _mesa_function_pool[9698]: DrawArraysInstanced (will be remapped) */
   "glDrawArraysInstanced\0"
   /* _mesa_function_pool[9720]: DrawElementsInstanced (will be remapped) */
   "glDrawElementsInstanced\0"
   /* _mesa_function_pool[9744]: IsRenderbuffer (will be remapped) */
   "glIsRenderbuffer\0"
   /* _mesa_function_pool[9761]: BindRenderbuffer (will be remapped) */
   "glBindRenderbuffer\0"
   /* _mesa_function_pool[9780]: DeleteRenderbuffers (will be remapped) */
   "glDeleteRenderbuffers\0"
   /* _mesa_function_pool[9802]: GenRenderbuffers (will be remapped) */
   "glGenRenderbuffers\0"
   /* _mesa_function_pool[9821]: RenderbufferStorage (will be remapped) */
   "glRenderbufferStorage\0"
   /* _mesa_function_pool[9843]: RenderbufferStorageMultisample (will be remapped) */
   "glRenderbufferStorageMultisample\0"
   /* _mesa_function_pool[9876]: GetRenderbufferParameteriv (will be remapped) */
   "glGetRenderbufferParameteriv\0"
   /* _mesa_function_pool[9905]: IsFramebuffer (will be remapped) */
   "glIsFramebuffer\0"
   /* _mesa_function_pool[9921]: BindFramebuffer (will be remapped) */
   "glBindFramebuffer\0"
   /* _mesa_function_pool[9939]: DeleteFramebuffers (will be remapped) */
   "glDeleteFramebuffers\0"
   /* _mesa_function_pool[9960]: GenFramebuffers (will be remapped) */
   "glGenFramebuffers\0"
   /* _mesa_function_pool[9978]: CheckFramebufferStatus (will be remapped) */
   "glCheckFramebufferStatus\0"
   /* _mesa_function_pool[10003]: FramebufferTexture1D (will be remapped) */
   "glFramebufferTexture1D\0"
   /* _mesa_function_pool[10026]: FramebufferTexture2D (will be remapped) */
   "glFramebufferTexture2D\0"
   /* _mesa_function_pool[10049]: FramebufferTexture3D (will be remapped) */
   "glFramebufferTexture3D\0"
   /* _mesa_function_pool[10072]: FramebufferTextureLayer (will be remapped) */
   "glFramebufferTextureLayer\0"
   /* _mesa_function_pool[10098]: FramebufferRenderbuffer (will be remapped) */
   "glFramebufferRenderbuffer\0"
   /* _mesa_function_pool[10124]: GetFramebufferAttachmentParameteriv (will be remapped) */
   "glGetFramebufferAttachmentParameteriv\0"
   /* _mesa_function_pool[10162]: BlitFramebuffer (will be remapped) */
   "glBlitFramebuffer\0"
   /* _mesa_function_pool[10180]: GenerateMipmap (will be remapped) */
   "glGenerateMipmap\0"
   /* _mesa_function_pool[10197]: VertexAttribDivisor (will be remapped) */
   "glVertexAttribDivisorARB\0"
   /* _mesa_function_pool[10222]: VertexArrayVertexAttribDivisorEXT (will be remapped) */
   "glVertexArrayVertexAttribDivisorEXT\0"
   /* _mesa_function_pool[10258]: MapBufferRange (will be remapped) */
   "glMapBufferRange\0"
   /* _mesa_function_pool[10275]: FlushMappedBufferRange (will be remapped) */
   "glFlushMappedBufferRange\0"
   /* _mesa_function_pool[10300]: TexBuffer (will be remapped) */
   "glTexBufferARB\0"
   /* _mesa_function_pool[10315]: BindVertexArray (will be remapped) */
   "glBindVertexArray\0"
   /* _mesa_function_pool[10333]: DeleteVertexArrays (will be remapped) */
   "glDeleteVertexArrays\0"
   /* _mesa_function_pool[10354]: GenVertexArrays (will be remapped) */
   "glGenVertexArrays\0"
   /* _mesa_function_pool[10372]: IsVertexArray (will be remapped) */
   "glIsVertexArray\0"
   /* _mesa_function_pool[10388]: GetUniformIndices (will be remapped) */
   "glGetUniformIndices\0"
   /* _mesa_function_pool[10408]: GetActiveUniformsiv (will be remapped) */
   "glGetActiveUniformsiv\0"
   /* _mesa_function_pool[10430]: GetActiveUniformName (will be remapped) */
   "glGetActiveUniformName\0"
   /* _mesa_function_pool[10453]: GetUniformBlockIndex (will be remapped) */
   "glGetUniformBlockIndex\0"
   /* _mesa_function_pool[10476]: GetActiveUniformBlockiv (will be remapped) */
   "glGetActiveUniformBlockiv\0"
   /* _mesa_function_pool[10502]: GetActiveUniformBlockName (will be remapped) */
   "glGetActiveUniformBlockName\0"
   /* _mesa_function_pool[10530]: UniformBlockBinding (will be remapped) */
   "glUniformBlockBinding\0"
   /* _mesa_function_pool[10552]: CopyBufferSubData (will be remapped) */
   "glCopyBufferSubData\0"
   /* _mesa_function_pool[10572]: DrawElementsBaseVertex (will be remapped) */
   "glDrawElementsBaseVertex\0"
   /* _mesa_function_pool[10597]: DrawRangeElementsBaseVertex (will be remapped) */
   "glDrawRangeElementsBaseVertex\0"
   /* _mesa_function_pool[10627]: MultiDrawElementsBaseVertex (will be remapped) */
   "glMultiDrawElementsBaseVertex\0"
   /* _mesa_function_pool[10657]: DrawElementsInstancedBaseVertex (will be remapped) */
   "glDrawElementsInstancedBaseVertex\0"
   /* _mesa_function_pool[10691]: FenceSync (will be remapped) */
   "glFenceSync\0"
   /* _mesa_function_pool[10703]: IsSync (will be remapped) */
   "glIsSync\0"
   /* _mesa_function_pool[10712]: DeleteSync (will be remapped) */
   "glDeleteSync\0"
   /* _mesa_function_pool[10725]: ClientWaitSync (will be remapped) */
   "glClientWaitSync\0"
   /* _mesa_function_pool[10742]: WaitSync (will be remapped) */
   "glWaitSync\0"
   /* _mesa_function_pool[10753]: GetInteger64v (will be remapped) */
   "glGetInteger64v\0"
   /* _mesa_function_pool[10769]: GetSynciv (will be remapped) */
   "glGetSynciv\0"
   /* _mesa_function_pool[10781]: TexImage2DMultisample (will be remapped) */
   "glTexImage2DMultisample\0"
   /* _mesa_function_pool[10805]: TexImage3DMultisample (will be remapped) */
   "glTexImage3DMultisample\0"
   /* _mesa_function_pool[10829]: GetMultisamplefv (will be remapped) */
   "glGetMultisamplefv\0"
   /* _mesa_function_pool[10848]: SampleMaski (will be remapped) */
   "glSampleMaski\0"
   /* _mesa_function_pool[10862]: BlendEquationiARB (will be remapped) */
   "glBlendEquationiARB\0"
   /* _mesa_function_pool[10882]: BlendEquationSeparateiARB (will be remapped) */
   "glBlendEquationSeparateiARB\0"
   /* _mesa_function_pool[10910]: BlendFunciARB (will be remapped) */
   "glBlendFunciARB\0"
   /* _mesa_function_pool[10926]: BlendFuncSeparateiARB (will be remapped) */
   "glBlendFuncSeparateiARB\0"
   /* _mesa_function_pool[10950]: MinSampleShading (will be remapped) */
   "glMinSampleShadingARB\0"
   /* _mesa_function_pool[10972]: NamedStringARB (will be remapped) */
   "glNamedStringARB\0"
   /* _mesa_function_pool[10989]: DeleteNamedStringARB (will be remapped) */
   "glDeleteNamedStringARB\0"
   /* _mesa_function_pool[11012]: CompileShaderIncludeARB (will be remapped) */
   "glCompileShaderIncludeARB\0"
   /* _mesa_function_pool[11038]: IsNamedStringARB (will be remapped) */
   "glIsNamedStringARB\0"
   /* _mesa_function_pool[11057]: GetNamedStringARB (will be remapped) */
   "glGetNamedStringARB\0"
   /* _mesa_function_pool[11077]: GetNamedStringivARB (will be remapped) */
   "glGetNamedStringivARB\0"
   /* _mesa_function_pool[11099]: BindFragDataLocationIndexed (will be remapped) */
   "glBindFragDataLocationIndexed\0"
   /* _mesa_function_pool[11129]: GetFragDataIndex (will be remapped) */
   "glGetFragDataIndex\0"
   /* _mesa_function_pool[11148]: GenSamplers (will be remapped) */
   "glGenSamplers\0"
   /* _mesa_function_pool[11162]: DeleteSamplers (will be remapped) */
   "glDeleteSamplers\0"
   /* _mesa_function_pool[11179]: IsSampler (will be remapped) */
   "glIsSampler\0"
   /* _mesa_function_pool[11191]: BindSampler (will be remapped) */
   "glBindSampler\0"
   /* _mesa_function_pool[11205]: SamplerParameteri (will be remapped) */
   "glSamplerParameteri\0"
   /* _mesa_function_pool[11225]: SamplerParameterf (will be remapped) */
   "glSamplerParameterf\0"
   /* _mesa_function_pool[11245]: SamplerParameteriv (will be remapped) */
   "glSamplerParameteriv\0"
   /* _mesa_function_pool[11266]: SamplerParameterfv (will be remapped) */
   "glSamplerParameterfv\0"
   /* _mesa_function_pool[11287]: SamplerParameterIiv (will be remapped) */
   "glSamplerParameterIiv\0"
   /* _mesa_function_pool[11309]: SamplerParameterIuiv (will be remapped) */
   "glSamplerParameterIuiv\0"
   /* _mesa_function_pool[11332]: GetSamplerParameteriv (will be remapped) */
   "glGetSamplerParameteriv\0"
   /* _mesa_function_pool[11356]: GetSamplerParameterfv (will be remapped) */
   "glGetSamplerParameterfv\0"
   /* _mesa_function_pool[11380]: GetSamplerParameterIiv (will be remapped) */
   "glGetSamplerParameterIiv\0"
   /* _mesa_function_pool[11405]: GetSamplerParameterIuiv (will be remapped) */
   "glGetSamplerParameterIuiv\0"
   /* _mesa_function_pool[11431]: GetQueryObjecti64v (will be remapped) */
   "glGetQueryObjecti64v\0"
   /* _mesa_function_pool[11452]: GetQueryObjectui64v (will be remapped) */
   "glGetQueryObjectui64v\0"
   /* _mesa_function_pool[11474]: QueryCounter (will be remapped) */
   "glQueryCounter\0"
   /* _mesa_function_pool[11489]: VertexP2ui (will be remapped) */
   "glVertexP2ui\0"
   /* _mesa_function_pool[11502]: VertexP3ui (will be remapped) */
   "glVertexP3ui\0"
   /* _mesa_function_pool[11515]: VertexP4ui (will be remapped) */
   "glVertexP4ui\0"
   /* _mesa_function_pool[11528]: VertexP2uiv (will be remapped) */
   "glVertexP2uiv\0"
   /* _mesa_function_pool[11542]: VertexP3uiv (will be remapped) */
   "glVertexP3uiv\0"
   /* _mesa_function_pool[11556]: VertexP4uiv (will be remapped) */
   "glVertexP4uiv\0"
   /* _mesa_function_pool[11570]: TexCoordP1ui (will be remapped) */
   "glTexCoordP1ui\0"
   /* _mesa_function_pool[11585]: TexCoordP2ui (will be remapped) */
   "glTexCoordP2ui\0"
   /* _mesa_function_pool[11600]: TexCoordP3ui (will be remapped) */
   "glTexCoordP3ui\0"
   /* _mesa_function_pool[11615]: TexCoordP4ui (will be remapped) */
   "glTexCoordP4ui\0"
   /* _mesa_function_pool[11630]: TexCoordP1uiv (will be remapped) */
   "glTexCoordP1uiv\0"
   /* _mesa_function_pool[11646]: TexCoordP2uiv (will be remapped) */
   "glTexCoordP2uiv\0"
   /* _mesa_function_pool[11662]: TexCoordP3uiv (will be remapped) */
   "glTexCoordP3uiv\0"
   /* _mesa_function_pool[11678]: TexCoordP4uiv (will be remapped) */
   "glTexCoordP4uiv\0"
   /* _mesa_function_pool[11694]: MultiTexCoordP1ui (will be remapped) */
   "glMultiTexCoordP1ui\0"
   /* _mesa_function_pool[11714]: MultiTexCoordP2ui (will be remapped) */
   "glMultiTexCoordP2ui\0"
   /* _mesa_function_pool[11734]: MultiTexCoordP3ui (will be remapped) */
   "glMultiTexCoordP3ui\0"
   /* _mesa_function_pool[11754]: MultiTexCoordP4ui (will be remapped) */
   "glMultiTexCoordP4ui\0"
   /* _mesa_function_pool[11774]: MultiTexCoordP1uiv (will be remapped) */
   "glMultiTexCoordP1uiv\0"
   /* _mesa_function_pool[11795]: MultiTexCoordP2uiv (will be remapped) */
   "glMultiTexCoordP2uiv\0"
   /* _mesa_function_pool[11816]: MultiTexCoordP3uiv (will be remapped) */
   "glMultiTexCoordP3uiv\0"
   /* _mesa_function_pool[11837]: MultiTexCoordP4uiv (will be remapped) */
   "glMultiTexCoordP4uiv\0"
   /* _mesa_function_pool[11858]: NormalP3ui (will be remapped) */
   "glNormalP3ui\0"
   /* _mesa_function_pool[11871]: NormalP3uiv (will be remapped) */
   "glNormalP3uiv\0"
   /* _mesa_function_pool[11885]: ColorP3ui (will be remapped) */
   "glColorP3ui\0"
   /* _mesa_function_pool[11897]: ColorP4ui (will be remapped) */
   "glColorP4ui\0"
   /* _mesa_function_pool[11909]: ColorP3uiv (will be remapped) */
   "glColorP3uiv\0"
   /* _mesa_function_pool[11922]: ColorP4uiv (will be remapped) */
   "glColorP4uiv\0"
   /* _mesa_function_pool[11935]: SecondaryColorP3ui (will be remapped) */
   "glSecondaryColorP3ui\0"
   /* _mesa_function_pool[11956]: SecondaryColorP3uiv (will be remapped) */
   "glSecondaryColorP3uiv\0"
   /* _mesa_function_pool[11978]: VertexAttribP1ui (will be remapped) */
   "glVertexAttribP1ui\0"
   /* _mesa_function_pool[11997]: VertexAttribP2ui (will be remapped) */
   "glVertexAttribP2ui\0"
   /* _mesa_function_pool[12016]: VertexAttribP3ui (will be remapped) */
   "glVertexAttribP3ui\0"
   /* _mesa_function_pool[12035]: VertexAttribP4ui (will be remapped) */
   "glVertexAttribP4ui\0"
   /* _mesa_function_pool[12054]: VertexAttribP1uiv (will be remapped) */
   "glVertexAttribP1uiv\0"
   /* _mesa_function_pool[12074]: VertexAttribP2uiv (will be remapped) */
   "glVertexAttribP2uiv\0"
   /* _mesa_function_pool[12094]: VertexAttribP3uiv (will be remapped) */
   "glVertexAttribP3uiv\0"
   /* _mesa_function_pool[12114]: VertexAttribP4uiv (will be remapped) */
   "glVertexAttribP4uiv\0"
   /* _mesa_function_pool[12134]: GetSubroutineUniformLocation (will be remapped) */
   "glGetSubroutineUniformLocation\0"
   /* _mesa_function_pool[12165]: GetSubroutineIndex (will be remapped) */
   "glGetSubroutineIndex\0"
   /* _mesa_function_pool[12186]: GetActiveSubroutineUniformiv (will be remapped) */
   "glGetActiveSubroutineUniformiv\0"
   /* _mesa_function_pool[12217]: GetActiveSubroutineUniformName (will be remapped) */
   "glGetActiveSubroutineUniformName\0"
   /* _mesa_function_pool[12250]: GetActiveSubroutineName (will be remapped) */
   "glGetActiveSubroutineName\0"
   /* _mesa_function_pool[12276]: UniformSubroutinesuiv (will be remapped) */
   "glUniformSubroutinesuiv\0"
   /* _mesa_function_pool[12300]: GetUniformSubroutineuiv (will be remapped) */
   "glGetUniformSubroutineuiv\0"
   /* _mesa_function_pool[12326]: GetProgramStageiv (will be remapped) */
   "glGetProgramStageiv\0"
   /* _mesa_function_pool[12346]: PatchParameteri (will be remapped) */
   "glPatchParameteri\0"
   /* _mesa_function_pool[12364]: PatchParameterfv (will be remapped) */
   "glPatchParameterfv\0"
   /* _mesa_function_pool[12383]: DrawArraysIndirect (will be remapped) */
   "glDrawArraysIndirect\0"
   /* _mesa_function_pool[12404]: DrawElementsIndirect (will be remapped) */
   "glDrawElementsIndirect\0"
   /* _mesa_function_pool[12427]: MultiDrawArraysIndirect (will be remapped) */
   "glMultiDrawArraysIndirect\0"
   /* _mesa_function_pool[12453]: MultiDrawElementsIndirect (will be remapped) */
   "glMultiDrawElementsIndirect\0"
   /* _mesa_function_pool[12481]: Uniform1d (will be remapped) */
   "glUniform1d\0"
   /* _mesa_function_pool[12493]: Uniform2d (will be remapped) */
   "glUniform2d\0"
   /* _mesa_function_pool[12505]: Uniform3d (will be remapped) */
   "glUniform3d\0"
   /* _mesa_function_pool[12517]: Uniform4d (will be remapped) */
   "glUniform4d\0"
   /* _mesa_function_pool[12529]: Uniform1dv (will be remapped) */
   "glUniform1dv\0"
   /* _mesa_function_pool[12542]: Uniform2dv (will be remapped) */
   "glUniform2dv\0"
   /* _mesa_function_pool[12555]: Uniform3dv (will be remapped) */
   "glUniform3dv\0"
   /* _mesa_function_pool[12568]: Uniform4dv (will be remapped) */
   "glUniform4dv\0"
   /* _mesa_function_pool[12581]: UniformMatrix2dv (will be remapped) */
   "glUniformMatrix2dv\0"
   /* _mesa_function_pool[12600]: UniformMatrix3dv (will be remapped) */
   "glUniformMatrix3dv\0"
   /* _mesa_function_pool[12619]: UniformMatrix4dv (will be remapped) */
   "glUniformMatrix4dv\0"
   /* _mesa_function_pool[12638]: UniformMatrix2x3dv (will be remapped) */
   "glUniformMatrix2x3dv\0"
   /* _mesa_function_pool[12659]: UniformMatrix2x4dv (will be remapped) */
   "glUniformMatrix2x4dv\0"
   /* _mesa_function_pool[12680]: UniformMatrix3x2dv (will be remapped) */
   "glUniformMatrix3x2dv\0"
   /* _mesa_function_pool[12701]: UniformMatrix3x4dv (will be remapped) */
   "glUniformMatrix3x4dv\0"
   /* _mesa_function_pool[12722]: UniformMatrix4x2dv (will be remapped) */
   "glUniformMatrix4x2dv\0"
   /* _mesa_function_pool[12743]: UniformMatrix4x3dv (will be remapped) */
   "glUniformMatrix4x3dv\0"
   /* _mesa_function_pool[12764]: GetUniformdv (will be remapped) */
   "glGetUniformdv\0"
   /* _mesa_function_pool[12779]: ProgramUniform1d (will be remapped) */
   "glProgramUniform1dEXT\0"
   /* _mesa_function_pool[12801]: ProgramUniform2d (will be remapped) */
   "glProgramUniform2dEXT\0"
   /* _mesa_function_pool[12823]: ProgramUniform3d (will be remapped) */
   "glProgramUniform3dEXT\0"
   /* _mesa_function_pool[12845]: ProgramUniform4d (will be remapped) */
   "glProgramUniform4dEXT\0"
   /* _mesa_function_pool[12867]: ProgramUniform1dv (will be remapped) */
   "glProgramUniform1dvEXT\0"
   /* _mesa_function_pool[12890]: ProgramUniform2dv (will be remapped) */
   "glProgramUniform2dvEXT\0"
   /* _mesa_function_pool[12913]: ProgramUniform3dv (will be remapped) */
   "glProgramUniform3dvEXT\0"
   /* _mesa_function_pool[12936]: ProgramUniform4dv (will be remapped) */
   "glProgramUniform4dvEXT\0"
   /* _mesa_function_pool[12959]: ProgramUniformMatrix2dv (will be remapped) */
   "glProgramUniformMatrix2dvEXT\0"
   /* _mesa_function_pool[12988]: ProgramUniformMatrix3dv (will be remapped) */
   "glProgramUniformMatrix3dvEXT\0"
   /* _mesa_function_pool[13017]: ProgramUniformMatrix4dv (will be remapped) */
   "glProgramUniformMatrix4dvEXT\0"
   /* _mesa_function_pool[13046]: ProgramUniformMatrix2x3dv (will be remapped) */
   "glProgramUniformMatrix2x3dvEXT\0"
   /* _mesa_function_pool[13077]: ProgramUniformMatrix2x4dv (will be remapped) */
   "glProgramUniformMatrix2x4dvEXT\0"
   /* _mesa_function_pool[13108]: ProgramUniformMatrix3x2dv (will be remapped) */
   "glProgramUniformMatrix3x2dvEXT\0"
   /* _mesa_function_pool[13139]: ProgramUniformMatrix3x4dv (will be remapped) */
   "glProgramUniformMatrix3x4dvEXT\0"
   /* _mesa_function_pool[13170]: ProgramUniformMatrix4x2dv (will be remapped) */
   "glProgramUniformMatrix4x2dvEXT\0"
   /* _mesa_function_pool[13201]: ProgramUniformMatrix4x3dv (will be remapped) */
   "glProgramUniformMatrix4x3dvEXT\0"
   /* _mesa_function_pool[13232]: DrawTransformFeedbackStream (will be remapped) */
   "glDrawTransformFeedbackStream\0"
   /* _mesa_function_pool[13262]: BeginQueryIndexed (will be remapped) */
   "glBeginQueryIndexed\0"
   /* _mesa_function_pool[13282]: EndQueryIndexed (will be remapped) */
   "glEndQueryIndexed\0"
   /* _mesa_function_pool[13300]: GetQueryIndexediv (will be remapped) */
   "glGetQueryIndexediv\0"
   /* _mesa_function_pool[13320]: UseProgramStages (will be remapped) */
   "glUseProgramStages\0"
   /* _mesa_function_pool[13339]: ActiveShaderProgram (will be remapped) */
   "glActiveShaderProgram\0"
   /* _mesa_function_pool[13361]: CreateShaderProgramv (will be remapped) */
   "glCreateShaderProgramv\0"
   /* _mesa_function_pool[13384]: BindProgramPipeline (will be remapped) */
   "glBindProgramPipeline\0"
   /* _mesa_function_pool[13406]: DeleteProgramPipelines (will be remapped) */
   "glDeleteProgramPipelines\0"
   /* _mesa_function_pool[13431]: GenProgramPipelines (will be remapped) */
   "glGenProgramPipelines\0"
   /* _mesa_function_pool[13453]: IsProgramPipeline (will be remapped) */
   "glIsProgramPipeline\0"
   /* _mesa_function_pool[13473]: GetProgramPipelineiv (will be remapped) */
   "glGetProgramPipelineiv\0"
   /* _mesa_function_pool[13496]: ProgramUniform1i (will be remapped) */
   "glProgramUniform1i\0"
   /* _mesa_function_pool[13515]: ProgramUniform2i (will be remapped) */
   "glProgramUniform2i\0"
   /* _mesa_function_pool[13534]: ProgramUniform3i (will be remapped) */
   "glProgramUniform3i\0"
   /* _mesa_function_pool[13553]: ProgramUniform4i (will be remapped) */
   "glProgramUniform4i\0"
   /* _mesa_function_pool[13572]: ProgramUniform1ui (will be remapped) */
   "glProgramUniform1ui\0"
   /* _mesa_function_pool[13592]: ProgramUniform2ui (will be remapped) */
   "glProgramUniform2ui\0"
   /* _mesa_function_pool[13612]: ProgramUniform3ui (will be remapped) */
   "glProgramUniform3ui\0"
   /* _mesa_function_pool[13632]: ProgramUniform4ui (will be remapped) */
   "glProgramUniform4ui\0"
   /* _mesa_function_pool[13652]: ProgramUniform1f (will be remapped) */
   "glProgramUniform1f\0"
   /* _mesa_function_pool[13671]: ProgramUniform2f (will be remapped) */
   "glProgramUniform2f\0"
   /* _mesa_function_pool[13690]: ProgramUniform3f (will be remapped) */
   "glProgramUniform3f\0"
   /* _mesa_function_pool[13709]: ProgramUniform4f (will be remapped) */
   "glProgramUniform4f\0"
   /* _mesa_function_pool[13728]: ProgramUniform1iv (will be remapped) */
   "glProgramUniform1iv\0"
   /* _mesa_function_pool[13748]: ProgramUniform2iv (will be remapped) */
   "glProgramUniform2iv\0"
   /* _mesa_function_pool[13768]: ProgramUniform3iv (will be remapped) */
   "glProgramUniform3iv\0"
   /* _mesa_function_pool[13788]: ProgramUniform4iv (will be remapped) */
   "glProgramUniform4iv\0"
   /* _mesa_function_pool[13808]: ProgramUniform1uiv (will be remapped) */
   "glProgramUniform1uiv\0"
   /* _mesa_function_pool[13829]: ProgramUniform2uiv (will be remapped) */
   "glProgramUniform2uiv\0"
   /* _mesa_function_pool[13850]: ProgramUniform3uiv (will be remapped) */
   "glProgramUniform3uiv\0"
   /* _mesa_function_pool[13871]: ProgramUniform4uiv (will be remapped) */
   "glProgramUniform4uiv\0"
   /* _mesa_function_pool[13892]: ProgramUniform1fv (will be remapped) */
   "glProgramUniform1fv\0"
   /* _mesa_function_pool[13912]: ProgramUniform2fv (will be remapped) */
   "glProgramUniform2fv\0"
   /* _mesa_function_pool[13932]: ProgramUniform3fv (will be remapped) */
   "glProgramUniform3fv\0"
   /* _mesa_function_pool[13952]: ProgramUniform4fv (will be remapped) */
   "glProgramUniform4fv\0"
   /* _mesa_function_pool[13972]: ProgramUniformMatrix2fv (will be remapped) */
   "glProgramUniformMatrix2fv\0"
   /* _mesa_function_pool[13998]: ProgramUniformMatrix3fv (will be remapped) */
   "glProgramUniformMatrix3fv\0"
   /* _mesa_function_pool[14024]: ProgramUniformMatrix4fv (will be remapped) */
   "glProgramUniformMatrix4fv\0"
   /* _mesa_function_pool[14050]: ProgramUniformMatrix2x3fv (will be remapped) */
   "glProgramUniformMatrix2x3fv\0"
   /* _mesa_function_pool[14078]: ProgramUniformMatrix3x2fv (will be remapped) */
   "glProgramUniformMatrix3x2fv\0"
   /* _mesa_function_pool[14106]: ProgramUniformMatrix2x4fv (will be remapped) */
   "glProgramUniformMatrix2x4fv\0"
   /* _mesa_function_pool[14134]: ProgramUniformMatrix4x2fv (will be remapped) */
   "glProgramUniformMatrix4x2fv\0"
   /* _mesa_function_pool[14162]: ProgramUniformMatrix3x4fv (will be remapped) */
   "glProgramUniformMatrix3x4fv\0"
   /* _mesa_function_pool[14190]: ProgramUniformMatrix4x3fv (will be remapped) */
   "glProgramUniformMatrix4x3fv\0"
   /* _mesa_function_pool[14218]: ValidateProgramPipeline (will be remapped) */
   "glValidateProgramPipeline\0"
   /* _mesa_function_pool[14244]: GetProgramPipelineInfoLog (will be remapped) */
   "glGetProgramPipelineInfoLog\0"
   /* _mesa_function_pool[14272]: VertexAttribL1d (will be remapped) */
   "glVertexAttribL1d\0"
   /* _mesa_function_pool[14290]: VertexAttribL2d (will be remapped) */
   "glVertexAttribL2d\0"
   /* _mesa_function_pool[14308]: VertexAttribL3d (will be remapped) */
   "glVertexAttribL3d\0"
   /* _mesa_function_pool[14326]: VertexAttribL4d (will be remapped) */
   "glVertexAttribL4d\0"
   /* _mesa_function_pool[14344]: VertexAttribL1dv (will be remapped) */
   "glVertexAttribL1dv\0"
   /* _mesa_function_pool[14363]: VertexAttribL2dv (will be remapped) */
   "glVertexAttribL2dv\0"
   /* _mesa_function_pool[14382]: VertexAttribL3dv (will be remapped) */
   "glVertexAttribL3dv\0"
   /* _mesa_function_pool[14401]: VertexAttribL4dv (will be remapped) */
   "glVertexAttribL4dv\0"
   /* _mesa_function_pool[14420]: VertexAttribLPointer (will be remapped) */
   "glVertexAttribLPointer\0"
   /* _mesa_function_pool[14443]: GetVertexAttribLdv (will be remapped) */
   "glGetVertexAttribLdv\0"
   /* _mesa_function_pool[14464]: VertexArrayVertexAttribLOffsetEXT (will be remapped) */
   "glVertexArrayVertexAttribLOffsetEXT\0"
   /* _mesa_function_pool[14500]: GetShaderPrecisionFormat (will be remapped) */
   "glGetShaderPrecisionFormat\0"
   /* _mesa_function_pool[14527]: ReleaseShaderCompiler (will be remapped) */
   "glReleaseShaderCompiler\0"
   /* _mesa_function_pool[14551]: ShaderBinary (will be remapped) */
   "glShaderBinary\0"
   /* _mesa_function_pool[14566]: ClearDepthf (will be remapped) */
   "glClearDepthf\0"
   /* _mesa_function_pool[14580]: DepthRangef (will be remapped) */
   "glDepthRangef\0"
   /* _mesa_function_pool[14594]: GetProgramBinary (will be remapped) */
   "glGetProgramBinary\0"
   /* _mesa_function_pool[14613]: ProgramBinary (will be remapped) */
   "glProgramBinary\0"
   /* _mesa_function_pool[14629]: ProgramParameteri (will be remapped) */
   "glProgramParameteri\0"
   /* _mesa_function_pool[14649]: DebugMessageControl (will be remapped) */
   "glDebugMessageControlARB\0"
   /* _mesa_function_pool[14674]: DebugMessageInsert (will be remapped) */
   "glDebugMessageInsertARB\0"
   /* _mesa_function_pool[14698]: DebugMessageCallback (will be remapped) */
   "glDebugMessageCallbackARB\0"
   /* _mesa_function_pool[14724]: GetDebugMessageLog (will be remapped) */
   "glGetDebugMessageLogARB\0"
   /* _mesa_function_pool[14748]: GetGraphicsResetStatusARB (will be remapped) */
   "glGetGraphicsResetStatusARB\0"
   /* _mesa_function_pool[14776]: GetnMapdvARB (will be remapped) */
   "glGetnMapdvARB\0"
   /* _mesa_function_pool[14791]: GetnMapfvARB (will be remapped) */
   "glGetnMapfvARB\0"
   /* _mesa_function_pool[14806]: GetnMapivARB (will be remapped) */
   "glGetnMapivARB\0"
   /* _mesa_function_pool[14821]: GetnPixelMapfvARB (will be remapped) */
   "glGetnPixelMapfvARB\0"
   /* _mesa_function_pool[14841]: GetnPixelMapuivARB (will be remapped) */
   "glGetnPixelMapuivARB\0"
   /* _mesa_function_pool[14862]: GetnPixelMapusvARB (will be remapped) */
   "glGetnPixelMapusvARB\0"
   /* _mesa_function_pool[14883]: GetnPolygonStippleARB (will be remapped) */
   "glGetnPolygonStippleARB\0"
   /* _mesa_function_pool[14907]: GetnTexImageARB (will be remapped) */
   "glGetnTexImageARB\0"
   /* _mesa_function_pool[14925]: ReadnPixelsARB (will be remapped) */
   "glReadnPixelsARB\0"
   /* _mesa_function_pool[14942]: GetnColorTableARB (will be remapped) */
   "glGetnColorTableARB\0"
   /* _mesa_function_pool[14962]: GetnConvolutionFilterARB (will be remapped) */
   "glGetnConvolutionFilterARB\0"
   /* _mesa_function_pool[14989]: GetnSeparableFilterARB (will be remapped) */
   "glGetnSeparableFilterARB\0"
   /* _mesa_function_pool[15014]: GetnHistogramARB (will be remapped) */
   "glGetnHistogramARB\0"
   /* _mesa_function_pool[15033]: GetnMinmaxARB (will be remapped) */
   "glGetnMinmaxARB\0"
   /* _mesa_function_pool[15049]: GetnCompressedTexImageARB (will be remapped) */
   "glGetnCompressedTexImageARB\0"
   /* _mesa_function_pool[15077]: GetnUniformfvARB (will be remapped) */
   "glGetnUniformfvARB\0"
   /* _mesa_function_pool[15096]: GetnUniformivARB (will be remapped) */
   "glGetnUniformivARB\0"
   /* _mesa_function_pool[15115]: GetnUniformuivARB (will be remapped) */
   "glGetnUniformuivARB\0"
   /* _mesa_function_pool[15135]: GetnUniformdvARB (will be remapped) */
   "glGetnUniformdvARB\0"
   /* _mesa_function_pool[15154]: DrawArraysInstancedBaseInstance (will be remapped) */
   "glDrawArraysInstancedBaseInstance\0"
   /* _mesa_function_pool[15188]: DrawElementsInstancedBaseInstance (will be remapped) */
   "glDrawElementsInstancedBaseInstance\0"
   /* _mesa_function_pool[15224]: DrawElementsInstancedBaseVertexBaseInstance (will be remapped) */
   "glDrawElementsInstancedBaseVertexBaseInstance\0"
   /* _mesa_function_pool[15270]: DrawTransformFeedbackInstanced (will be remapped) */
   "glDrawTransformFeedbackInstanced\0"
   /* _mesa_function_pool[15303]: DrawTransformFeedbackStreamInstanced (will be remapped) */
   "glDrawTransformFeedbackStreamInstanced\0"
   /* _mesa_function_pool[15342]: GetInternalformativ (will be remapped) */
   "glGetInternalformativ\0"
   /* _mesa_function_pool[15364]: GetActiveAtomicCounterBufferiv (will be remapped) */
   "glGetActiveAtomicCounterBufferiv\0"
   /* _mesa_function_pool[15397]: BindImageTexture (will be remapped) */
   "glBindImageTexture\0"
   /* _mesa_function_pool[15416]: MemoryBarrier (will be remapped) */
   "glMemoryBarrier\0"
   /* _mesa_function_pool[15432]: TexStorage1D (will be remapped) */
   "glTexStorage1D\0"
   /* _mesa_function_pool[15447]: TexStorage2D (will be remapped) */
   "glTexStorage2D\0"
   /* _mesa_function_pool[15462]: TexStorage3D (will be remapped) */
   "glTexStorage3D\0"
   /* _mesa_function_pool[15477]: TextureStorage1DEXT (will be remapped) */
   "glTextureStorage1DEXT\0"
   /* _mesa_function_pool[15499]: TextureStorage2DEXT (will be remapped) */
   "glTextureStorage2DEXT\0"
   /* _mesa_function_pool[15521]: TextureStorage3DEXT (will be remapped) */
   "glTextureStorage3DEXT\0"
   /* _mesa_function_pool[15543]: PushDebugGroup (will be remapped) */
   "glPushDebugGroup\0"
   /* _mesa_function_pool[15560]: PopDebugGroup (will be remapped) */
   "glPopDebugGroup\0"
   /* _mesa_function_pool[15576]: ObjectLabel (will be remapped) */
   "glObjectLabel\0"
   /* _mesa_function_pool[15590]: GetObjectLabel (will be remapped) */
   "glGetObjectLabel\0"
   /* _mesa_function_pool[15607]: ObjectPtrLabel (will be remapped) */
   "glObjectPtrLabel\0"
   /* _mesa_function_pool[15624]: GetObjectPtrLabel (will be remapped) */
   "glGetObjectPtrLabel\0"
   /* _mesa_function_pool[15644]: ClearBufferData (will be remapped) */
   "glClearBufferData\0"
   /* _mesa_function_pool[15662]: ClearBufferSubData (will be remapped) */
   "glClearBufferSubData\0"
   /* _mesa_function_pool[15683]: ClearNamedBufferDataEXT (will be remapped) */
   "glClearNamedBufferDataEXT\0"
   /* _mesa_function_pool[15709]: ClearNamedBufferSubDataEXT (will be remapped) */
   "glClearNamedBufferSubDataEXT\0"
   /* _mesa_function_pool[15738]: DispatchCompute (will be remapped) */
   "glDispatchCompute\0"
   /* _mesa_function_pool[15756]: DispatchComputeIndirect (will be remapped) */
   "glDispatchComputeIndirect\0"
   /* _mesa_function_pool[15782]: CopyImageSubData (will be remapped) */
   "glCopyImageSubData\0"
   /* _mesa_function_pool[15801]: TextureView (will be remapped) */
   "glTextureView\0"
   /* _mesa_function_pool[15815]: BindVertexBuffer (will be remapped) */
   "glBindVertexBuffer\0"
   /* _mesa_function_pool[15834]: VertexAttribFormat (will be remapped) */
   "glVertexAttribFormat\0"
   /* _mesa_function_pool[15855]: VertexAttribIFormat (will be remapped) */
   "glVertexAttribIFormat\0"
   /* _mesa_function_pool[15877]: VertexAttribLFormat (will be remapped) */
   "glVertexAttribLFormat\0"
   /* _mesa_function_pool[15899]: VertexAttribBinding (will be remapped) */
   "glVertexAttribBinding\0"
   /* _mesa_function_pool[15921]: VertexBindingDivisor (will be remapped) */
   "glVertexBindingDivisor\0"
   /* _mesa_function_pool[15944]: VertexArrayBindVertexBufferEXT (will be remapped) */
   "glVertexArrayBindVertexBufferEXT\0"
   /* _mesa_function_pool[15977]: VertexArrayVertexAttribFormatEXT (will be remapped) */
   "glVertexArrayVertexAttribFormatEXT\0"
   /* _mesa_function_pool[16012]: VertexArrayVertexAttribIFormatEXT (will be remapped) */
   "glVertexArrayVertexAttribIFormatEXT\0"
   /* _mesa_function_pool[16048]: VertexArrayVertexAttribLFormatEXT (will be remapped) */
   "glVertexArrayVertexAttribLFormatEXT\0"
   /* _mesa_function_pool[16084]: VertexArrayVertexAttribBindingEXT (will be remapped) */
   "glVertexArrayVertexAttribBindingEXT\0"
   /* _mesa_function_pool[16120]: VertexArrayVertexBindingDivisorEXT (will be remapped) */
   "glVertexArrayVertexBindingDivisorEXT\0"
   /* _mesa_function_pool[16157]: FramebufferParameteri (will be remapped) */
   "glFramebufferParameteri\0"
   /* _mesa_function_pool[16181]: GetFramebufferParameteriv (will be remapped) */
   "glGetFramebufferParameteriv\0"
   /* _mesa_function_pool[16209]: NamedFramebufferParameteriEXT (will be remapped) */
   "glNamedFramebufferParameteriEXT\0"
   /* _mesa_function_pool[16241]: GetNamedFramebufferParameterivEXT (will be remapped) */
   "glGetNamedFramebufferParameterivEXT\0"
   /* _mesa_function_pool[16277]: GetInternalformati64v (will be remapped) */
   "glGetInternalformati64v\0"
   /* _mesa_function_pool[16301]: InvalidateTexSubImage (will be remapped) */
   "glInvalidateTexSubImage\0"
   /* _mesa_function_pool[16325]: InvalidateTexImage (will be remapped) */
   "glInvalidateTexImage\0"
   /* _mesa_function_pool[16346]: InvalidateBufferSubData (will be remapped) */
   "glInvalidateBufferSubData\0"
   /* _mesa_function_pool[16372]: InvalidateBufferData (will be remapped) */
   "glInvalidateBufferData\0"
   /* _mesa_function_pool[16395]: InvalidateSubFramebuffer (will be remapped) */
   "glInvalidateSubFramebuffer\0"
   /* _mesa_function_pool[16422]: InvalidateFramebuffer (will be remapped) */
   "glInvalidateFramebuffer\0"
   /* _mesa_function_pool[16446]: GetProgramInterfaceiv (will be remapped) */
   "glGetProgramInterfaceiv\0"
   /* _mesa_function_pool[16470]: GetProgramResourceIndex (will be remapped) */
   "glGetProgramResourceIndex\0"
   /* _mesa_function_pool[16496]: GetProgramResourceName (will be remapped) */
   "glGetProgramResourceName\0"
   /* _mesa_function_pool[16521]: GetProgramResourceiv (will be remapped) */
   "glGetProgramResourceiv\0"
   /* _mesa_function_pool[16544]: GetProgramResourceLocation (will be remapped) */
   "glGetProgramResourceLocation\0"
   /* _mesa_function_pool[16573]: GetProgramResourceLocationIndex (will be remapped) */
   "glGetProgramResourceLocationIndex\0"
   /* _mesa_function_pool[16607]: ShaderStorageBlockBinding (will be remapped) */
   "glShaderStorageBlockBinding\0"
   /* _mesa_function_pool[16635]: TexBufferRange (will be remapped) */
   "glTexBufferRange\0"
   /* _mesa_function_pool[16652]: TextureBufferRangeEXT (will be remapped) */
   "glTextureBufferRangeEXT\0"
   /* _mesa_function_pool[16676]: TexStorage2DMultisample (will be remapped) */
   "glTexStorage2DMultisample\0"
   /* _mesa_function_pool[16702]: TexStorage3DMultisample (will be remapped) */
   "glTexStorage3DMultisample\0"
   /* _mesa_function_pool[16728]: TextureStorage2DMultisampleEXT (will be remapped) */
   "glTextureStorage2DMultisampleEXT\0"
   /* _mesa_function_pool[16761]: TextureStorage3DMultisampleEXT (will be remapped) */
   "glTextureStorage3DMultisampleEXT\0"
   /* _mesa_function_pool[16794]: BufferStorage (will be remapped) */
   "glBufferStorage\0"
   /* _mesa_function_pool[16810]: NamedBufferStorageEXT (will be remapped) */
   "glNamedBufferStorageEXT\0"
   /* _mesa_function_pool[16834]: ClearTexImage (will be remapped) */
   "glClearTexImage\0"
   /* _mesa_function_pool[16850]: ClearTexSubImage (will be remapped) */
   "glClearTexSubImage\0"
   /* _mesa_function_pool[16869]: BindBuffersBase (will be remapped) */
   "glBindBuffersBase\0"
   /* _mesa_function_pool[16887]: BindBuffersRange (will be remapped) */
   "glBindBuffersRange\0"
   /* _mesa_function_pool[16906]: BindTextures (will be remapped) */
   "glBindTextures\0"
   /* _mesa_function_pool[16921]: BindSamplers (will be remapped) */
   "glBindSamplers\0"
   /* _mesa_function_pool[16936]: BindImageTextures (will be remapped) */
   "glBindImageTextures\0"
   /* _mesa_function_pool[16956]: BindVertexBuffers (will be remapped) */
   "glBindVertexBuffers\0"
   /* _mesa_function_pool[16976]: GetTextureHandleARB (will be remapped) */
   "glGetTextureHandleARB\0"
   /* _mesa_function_pool[16998]: GetTextureSamplerHandleARB (will be remapped) */
   "glGetTextureSamplerHandleARB\0"
   /* _mesa_function_pool[17027]: MakeTextureHandleResidentARB (will be remapped) */
   "glMakeTextureHandleResidentARB\0"
   /* _mesa_function_pool[17058]: MakeTextureHandleNonResidentARB (will be remapped) */
   "glMakeTextureHandleNonResidentARB\0"
   /* _mesa_function_pool[17092]: GetImageHandleARB (will be remapped) */
   "glGetImageHandleARB\0"
   /* _mesa_function_pool[17112]: MakeImageHandleResidentARB (will be remapped) */
   "glMakeImageHandleResidentARB\0"
   /* _mesa_function_pool[17141]: MakeImageHandleNonResidentARB (will be remapped) */
   "glMakeImageHandleNonResidentARB\0"
   /* _mesa_function_pool[17173]: UniformHandleui64ARB (will be remapped) */
   "glUniformHandleui64ARB\0"
   /* _mesa_function_pool[17196]: UniformHandleui64vARB (will be remapped) */
   "glUniformHandleui64vARB\0"
   /* _mesa_function_pool[17220]: ProgramUniformHandleui64ARB (will be remapped) */
   "glProgramUniformHandleui64ARB\0"
   /* _mesa_function_pool[17250]: ProgramUniformHandleui64vARB (will be remapped) */
   "glProgramUniformHandleui64vARB\0"
   /* _mesa_function_pool[17281]: IsTextureHandleResidentARB (will be remapped) */
   "glIsTextureHandleResidentARB\0"
   /* _mesa_function_pool[17310]: IsImageHandleResidentARB (will be remapped) */
   "glIsImageHandleResidentARB\0"
   /* _mesa_function_pool[17337]: VertexAttribL1ui64ARB (will be remapped) */
   "glVertexAttribL1ui64ARB\0"
   /* _mesa_function_pool[17361]: VertexAttribL1ui64vARB (will be remapped) */
   "glVertexAttribL1ui64vARB\0"
   /* _mesa_function_pool[17386]: GetVertexAttribLui64vARB (will be remapped) */
   "glGetVertexAttribLui64vARB\0"
   /* _mesa_function_pool[17413]: DispatchComputeGroupSizeARB (will be remapped) */
   "glDispatchComputeGroupSizeARB\0"
   /* _mesa_function_pool[17443]: MultiDrawArraysIndirectCountARB (will be remapped) */
   "glMultiDrawArraysIndirectCountARB\0"
   /* _mesa_function_pool[17477]: MultiDrawElementsIndirectCountARB (will be remapped) */
   "glMultiDrawElementsIndirectCountARB\0"
   /* _mesa_function_pool[17513]: TexPageCommitmentARB (will be remapped) */
   "glTexPageCommitmentARB\0"
   /* _mesa_function_pool[17536]: TexturePageCommitmentEXT (will be remapped) */
   "glTexturePageCommitmentEXT\0"
   /* _mesa_function_pool[17563]: ClipControl (will be remapped) */
   "glClipControl\0"
   /* _mesa_function_pool[17577]: CreateTransformFeedbacks (will be remapped) */
   "glCreateTransformFeedbacks\0"
   /* _mesa_function_pool[17604]: TransformFeedbackBufferBase (will be remapped) */
   "glTransformFeedbackBufferBase\0"
   /* _mesa_function_pool[17634]: TransformFeedbackBufferRange (will be remapped) */
   "glTransformFeedbackBufferRange\0"
   /* _mesa_function_pool[17665]: GetTransformFeedbackiv (will be remapped) */
   "glGetTransformFeedbackiv\0"
   /* _mesa_function_pool[17690]: GetTransformFeedbacki_v (will be remapped) */
   "glGetTransformFeedbacki_v\0"
   /* _mesa_function_pool[17716]: GetTransformFeedbacki64_v (will be remapped) */
   "glGetTransformFeedbacki64_v\0"
   /* _mesa_function_pool[17744]: CreateBuffers (will be remapped) */
   "glCreateBuffers\0"
   /* _mesa_function_pool[17760]: NamedBufferStorage (will be remapped) */
   "glNamedBufferStorage\0"
   /* _mesa_function_pool[17781]: NamedBufferData (will be remapped) */
   "glNamedBufferData\0"
   /* _mesa_function_pool[17799]: NamedBufferSubData (will be remapped) */
   "glNamedBufferSubData\0"
   /* _mesa_function_pool[17820]: CopyNamedBufferSubData (will be remapped) */
   "glCopyNamedBufferSubData\0"
   /* _mesa_function_pool[17845]: ClearNamedBufferData (will be remapped) */
   "glClearNamedBufferData\0"
   /* _mesa_function_pool[17868]: ClearNamedBufferSubData (will be remapped) */
   "glClearNamedBufferSubData\0"
   /* _mesa_function_pool[17894]: MapNamedBuffer (will be remapped) */
   "glMapNamedBuffer\0"
   /* _mesa_function_pool[17911]: MapNamedBufferRange (will be remapped) */
   "glMapNamedBufferRange\0"
   /* _mesa_function_pool[17933]: UnmapNamedBufferEXT (will be remapped) */
   "glUnmapNamedBuffer\0"
   /* _mesa_function_pool[17952]: FlushMappedNamedBufferRange (will be remapped) */
   "glFlushMappedNamedBufferRange\0"
   /* _mesa_function_pool[17982]: GetNamedBufferParameteriv (will be remapped) */
   "glGetNamedBufferParameteriv\0"
   /* _mesa_function_pool[18010]: GetNamedBufferParameteri64v (will be remapped) */
   "glGetNamedBufferParameteri64v\0"
   /* _mesa_function_pool[18040]: GetNamedBufferPointerv (will be remapped) */
   "glGetNamedBufferPointerv\0"
   /* _mesa_function_pool[18065]: GetNamedBufferSubData (will be remapped) */
   "glGetNamedBufferSubData\0"
   /* _mesa_function_pool[18089]: CreateFramebuffers (will be remapped) */
   "glCreateFramebuffers\0"
   /* _mesa_function_pool[18110]: NamedFramebufferRenderbuffer (will be remapped) */
   "glNamedFramebufferRenderbuffer\0"
   /* _mesa_function_pool[18141]: NamedFramebufferParameteri (will be remapped) */
   "glNamedFramebufferParameteri\0"
   /* _mesa_function_pool[18170]: NamedFramebufferTexture (will be remapped) */
   "glNamedFramebufferTexture\0"
   /* _mesa_function_pool[18196]: NamedFramebufferTextureLayer (will be remapped) */
   "glNamedFramebufferTextureLayer\0"
   /* _mesa_function_pool[18227]: NamedFramebufferDrawBuffer (will be remapped) */
   "glNamedFramebufferDrawBuffer\0"
   /* _mesa_function_pool[18256]: NamedFramebufferDrawBuffers (will be remapped) */
   "glNamedFramebufferDrawBuffers\0"
   /* _mesa_function_pool[18286]: NamedFramebufferReadBuffer (will be remapped) */
   "glNamedFramebufferReadBuffer\0"
   /* _mesa_function_pool[18315]: InvalidateNamedFramebufferData (will be remapped) */
   "glInvalidateNamedFramebufferData\0"
   /* _mesa_function_pool[18348]: InvalidateNamedFramebufferSubData (will be remapped) */
   "glInvalidateNamedFramebufferSubData\0"
   /* _mesa_function_pool[18384]: ClearNamedFramebufferiv (will be remapped) */
   "glClearNamedFramebufferiv\0"
   /* _mesa_function_pool[18410]: ClearNamedFramebufferuiv (will be remapped) */
   "glClearNamedFramebufferuiv\0"
   /* _mesa_function_pool[18437]: ClearNamedFramebufferfv (will be remapped) */
   "glClearNamedFramebufferfv\0"
   /* _mesa_function_pool[18463]: ClearNamedFramebufferfi (will be remapped) */
   "glClearNamedFramebufferfi\0"
   /* _mesa_function_pool[18489]: BlitNamedFramebuffer (will be remapped) */
   "glBlitNamedFramebuffer\0"
   /* _mesa_function_pool[18512]: CheckNamedFramebufferStatus (will be remapped) */
   "glCheckNamedFramebufferStatus\0"
   /* _mesa_function_pool[18542]: GetNamedFramebufferParameteriv (will be remapped) */
   "glGetNamedFramebufferParameteriv\0"
   /* _mesa_function_pool[18575]: GetNamedFramebufferAttachmentParameteriv (will be remapped) */
   "glGetNamedFramebufferAttachmentParameteriv\0"
   /* _mesa_function_pool[18618]: CreateRenderbuffers (will be remapped) */
   "glCreateRenderbuffers\0"
   /* _mesa_function_pool[18640]: NamedRenderbufferStorage (will be remapped) */
   "glNamedRenderbufferStorage\0"
   /* _mesa_function_pool[18667]: NamedRenderbufferStorageMultisample (will be remapped) */
   "glNamedRenderbufferStorageMultisample\0"
   /* _mesa_function_pool[18705]: GetNamedRenderbufferParameteriv (will be remapped) */
   "glGetNamedRenderbufferParameteriv\0"
   /* _mesa_function_pool[18739]: CreateTextures (will be remapped) */
   "glCreateTextures\0"
   /* _mesa_function_pool[18756]: TextureBuffer (will be remapped) */
   "glTextureBuffer\0"
   /* _mesa_function_pool[18772]: TextureBufferRange (will be remapped) */
   "glTextureBufferRange\0"
   /* _mesa_function_pool[18793]: TextureStorage1D (will be remapped) */
   "glTextureStorage1D\0"
   /* _mesa_function_pool[18812]: TextureStorage2D (will be remapped) */
   "glTextureStorage2D\0"
   /* _mesa_function_pool[18831]: TextureStorage3D (will be remapped) */
   "glTextureStorage3D\0"
   /* _mesa_function_pool[18850]: TextureStorage2DMultisample (will be remapped) */
   "glTextureStorage2DMultisample\0"
   /* _mesa_function_pool[18880]: TextureStorage3DMultisample (will be remapped) */
   "glTextureStorage3DMultisample\0"
   /* _mesa_function_pool[18910]: TextureSubImage1D (will be remapped) */
   "glTextureSubImage1D\0"
   /* _mesa_function_pool[18930]: TextureSubImage2D (will be remapped) */
   "glTextureSubImage2D\0"
   /* _mesa_function_pool[18950]: TextureSubImage3D (will be remapped) */
   "glTextureSubImage3D\0"
   /* _mesa_function_pool[18970]: CompressedTextureSubImage1D (will be remapped) */
   "glCompressedTextureSubImage1D\0"
   /* _mesa_function_pool[19000]: CompressedTextureSubImage2D (will be remapped) */
   "glCompressedTextureSubImage2D\0"
   /* _mesa_function_pool[19030]: CompressedTextureSubImage3D (will be remapped) */
   "glCompressedTextureSubImage3D\0"
   /* _mesa_function_pool[19060]: CopyTextureSubImage1D (will be remapped) */
   "glCopyTextureSubImage1D\0"
   /* _mesa_function_pool[19084]: CopyTextureSubImage2D (will be remapped) */
   "glCopyTextureSubImage2D\0"
   /* _mesa_function_pool[19108]: CopyTextureSubImage3D (will be remapped) */
   "glCopyTextureSubImage3D\0"
   /* _mesa_function_pool[19132]: TextureParameterf (will be remapped) */
   "glTextureParameterf\0"
   /* _mesa_function_pool[19152]: TextureParameterfv (will be remapped) */
   "glTextureParameterfv\0"
   /* _mesa_function_pool[19173]: TextureParameteri (will be remapped) */
   "glTextureParameteri\0"
   /* _mesa_function_pool[19193]: TextureParameterIiv (will be remapped) */
   "glTextureParameterIiv\0"
   /* _mesa_function_pool[19215]: TextureParameterIuiv (will be remapped) */
   "glTextureParameterIuiv\0"
   /* _mesa_function_pool[19238]: TextureParameteriv (will be remapped) */
   "glTextureParameteriv\0"
   /* _mesa_function_pool[19259]: GenerateTextureMipmap (will be remapped) */
   "glGenerateTextureMipmap\0"
   /* _mesa_function_pool[19283]: BindTextureUnit (will be remapped) */
   "glBindTextureUnit\0"
   /* _mesa_function_pool[19301]: GetTextureImage (will be remapped) */
   "glGetTextureImage\0"
   /* _mesa_function_pool[19319]: GetCompressedTextureImage (will be remapped) */
   "glGetCompressedTextureImage\0"
   /* _mesa_function_pool[19347]: GetTextureLevelParameterfv (will be remapped) */
   "glGetTextureLevelParameterfv\0"
   /* _mesa_function_pool[19376]: GetTextureLevelParameteriv (will be remapped) */
   "glGetTextureLevelParameteriv\0"
   /* _mesa_function_pool[19405]: GetTextureParameterfv (will be remapped) */
   "glGetTextureParameterfv\0"
   /* _mesa_function_pool[19429]: GetTextureParameterIiv (will be remapped) */
   "glGetTextureParameterIiv\0"
   /* _mesa_function_pool[19454]: GetTextureParameterIuiv (will be remapped) */
   "glGetTextureParameterIuiv\0"
   /* _mesa_function_pool[19480]: GetTextureParameteriv (will be remapped) */
   "glGetTextureParameteriv\0"
   /* _mesa_function_pool[19504]: CreateVertexArrays (will be remapped) */
   "glCreateVertexArrays\0"
   /* _mesa_function_pool[19525]: DisableVertexArrayAttrib (will be remapped) */
   "glDisableVertexArrayAttrib\0"
   /* _mesa_function_pool[19552]: EnableVertexArrayAttrib (will be remapped) */
   "glEnableVertexArrayAttrib\0"
   /* _mesa_function_pool[19578]: VertexArrayElementBuffer (will be remapped) */
   "glVertexArrayElementBuffer\0"
   /* _mesa_function_pool[19605]: VertexArrayVertexBuffer (will be remapped) */
   "glVertexArrayVertexBuffer\0"
   /* _mesa_function_pool[19631]: VertexArrayVertexBuffers (will be remapped) */
   "glVertexArrayVertexBuffers\0"
   /* _mesa_function_pool[19658]: VertexArrayAttribFormat (will be remapped) */
   "glVertexArrayAttribFormat\0"
   /* _mesa_function_pool[19684]: VertexArrayAttribIFormat (will be remapped) */
   "glVertexArrayAttribIFormat\0"
   /* _mesa_function_pool[19711]: VertexArrayAttribLFormat (will be remapped) */
   "glVertexArrayAttribLFormat\0"
   /* _mesa_function_pool[19738]: VertexArrayAttribBinding (will be remapped) */
   "glVertexArrayAttribBinding\0"
   /* _mesa_function_pool[19765]: VertexArrayBindingDivisor (will be remapped) */
   "glVertexArrayBindingDivisor\0"
   /* _mesa_function_pool[19793]: GetVertexArrayiv (will be remapped) */
   "glGetVertexArrayiv\0"
   /* _mesa_function_pool[19812]: GetVertexArrayIndexediv (will be remapped) */
   "glGetVertexArrayIndexediv\0"
   /* _mesa_function_pool[19838]: GetVertexArrayIndexed64iv (will be remapped) */
   "glGetVertexArrayIndexed64iv\0"
   /* _mesa_function_pool[19866]: CreateSamplers (will be remapped) */
   "glCreateSamplers\0"
   /* _mesa_function_pool[19883]: CreateProgramPipelines (will be remapped) */
   "glCreateProgramPipelines\0"
   /* _mesa_function_pool[19908]: CreateQueries (will be remapped) */
   "glCreateQueries\0"
   /* _mesa_function_pool[19924]: GetQueryBufferObjectiv (will be remapped) */
   "glGetQueryBufferObjectiv\0"
   /* _mesa_function_pool[19949]: GetQueryBufferObjectuiv (will be remapped) */
   "glGetQueryBufferObjectuiv\0"
   /* _mesa_function_pool[19975]: GetQueryBufferObjecti64v (will be remapped) */
   "glGetQueryBufferObjecti64v\0"
   /* _mesa_function_pool[20002]: GetQueryBufferObjectui64v (will be remapped) */
   "glGetQueryBufferObjectui64v\0"
   /* _mesa_function_pool[20030]: GetTextureSubImage (will be remapped) */
   "glGetTextureSubImage\0"
   /* _mesa_function_pool[20051]: GetCompressedTextureSubImage (will be remapped) */
   "glGetCompressedTextureSubImage\0"
   /* _mesa_function_pool[20082]: TextureBarrierNV (will be remapped) */
   "glTextureBarrier\0"
   /* _mesa_function_pool[20099]: BufferPageCommitmentARB (will be remapped) */
   "glBufferPageCommitmentARB\0"
   /* _mesa_function_pool[20125]: NamedBufferPageCommitmentEXT (will be remapped) */
   "glNamedBufferPageCommitmentEXT\0"
   /* _mesa_function_pool[20156]: NamedBufferPageCommitmentARB (will be remapped) */
   "glNamedBufferPageCommitmentARB\0"
   /* _mesa_function_pool[20187]: PrimitiveBoundingBox (will be remapped) */
   "glPrimitiveBoundingBox\0"
   /* _mesa_function_pool[20210]: BlendBarrier (will be remapped) */
   "glBlendBarrier\0"
   /* _mesa_function_pool[20225]: Uniform1i64ARB (will be remapped) */
   "glUniform1i64ARB\0"
   /* _mesa_function_pool[20242]: Uniform2i64ARB (will be remapped) */
   "glUniform2i64ARB\0"
   /* _mesa_function_pool[20259]: Uniform3i64ARB (will be remapped) */
   "glUniform3i64ARB\0"
   /* _mesa_function_pool[20276]: Uniform4i64ARB (will be remapped) */
   "glUniform4i64ARB\0"
   /* _mesa_function_pool[20293]: Uniform1i64vARB (will be remapped) */
   "glUniform1i64vARB\0"
   /* _mesa_function_pool[20311]: Uniform2i64vARB (will be remapped) */
   "glUniform2i64vARB\0"
   /* _mesa_function_pool[20329]: Uniform3i64vARB (will be remapped) */
   "glUniform3i64vARB\0"
   /* _mesa_function_pool[20347]: Uniform4i64vARB (will be remapped) */
   "glUniform4i64vARB\0"
   /* _mesa_function_pool[20365]: Uniform1ui64ARB (will be remapped) */
   "glUniform1ui64ARB\0"
   /* _mesa_function_pool[20383]: Uniform2ui64ARB (will be remapped) */
   "glUniform2ui64ARB\0"
   /* _mesa_function_pool[20401]: Uniform3ui64ARB (will be remapped) */
   "glUniform3ui64ARB\0"
   /* _mesa_function_pool[20419]: Uniform4ui64ARB (will be remapped) */
   "glUniform4ui64ARB\0"
   /* _mesa_function_pool[20437]: Uniform1ui64vARB (will be remapped) */
   "glUniform1ui64vARB\0"
   /* _mesa_function_pool[20456]: Uniform2ui64vARB (will be remapped) */
   "glUniform2ui64vARB\0"
   /* _mesa_function_pool[20475]: Uniform3ui64vARB (will be remapped) */
   "glUniform3ui64vARB\0"
   /* _mesa_function_pool[20494]: Uniform4ui64vARB (will be remapped) */
   "glUniform4ui64vARB\0"
   /* _mesa_function_pool[20513]: GetUniformi64vARB (will be remapped) */
   "glGetUniformi64vARB\0"
   /* _mesa_function_pool[20533]: GetUniformui64vARB (will be remapped) */
   "glGetUniformui64vARB\0"
   /* _mesa_function_pool[20554]: GetnUniformi64vARB (will be remapped) */
   "glGetnUniformi64vARB\0"
   /* _mesa_function_pool[20575]: GetnUniformui64vARB (will be remapped) */
   "glGetnUniformui64vARB\0"
   /* _mesa_function_pool[20597]: ProgramUniform1i64ARB (will be remapped) */
   "glProgramUniform1i64ARB\0"
   /* _mesa_function_pool[20621]: ProgramUniform2i64ARB (will be remapped) */
   "glProgramUniform2i64ARB\0"
   /* _mesa_function_pool[20645]: ProgramUniform3i64ARB (will be remapped) */
   "glProgramUniform3i64ARB\0"
   /* _mesa_function_pool[20669]: ProgramUniform4i64ARB (will be remapped) */
   "glProgramUniform4i64ARB\0"
   /* _mesa_function_pool[20693]: ProgramUniform1i64vARB (will be remapped) */
   "glProgramUniform1i64vARB\0"
   /* _mesa_function_pool[20718]: ProgramUniform2i64vARB (will be remapped) */
   "glProgramUniform2i64vARB\0"
   /* _mesa_function_pool[20743]: ProgramUniform3i64vARB (will be remapped) */
   "glProgramUniform3i64vARB\0"
   /* _mesa_function_pool[20768]: ProgramUniform4i64vARB (will be remapped) */
   "glProgramUniform4i64vARB\0"
   /* _mesa_function_pool[20793]: ProgramUniform1ui64ARB (will be remapped) */
   "glProgramUniform1ui64ARB\0"
   /* _mesa_function_pool[20818]: ProgramUniform2ui64ARB (will be remapped) */
   "glProgramUniform2ui64ARB\0"
   /* _mesa_function_pool[20843]: ProgramUniform3ui64ARB (will be remapped) */
   "glProgramUniform3ui64ARB\0"
   /* _mesa_function_pool[20868]: ProgramUniform4ui64ARB (will be remapped) */
   "glProgramUniform4ui64ARB\0"
   /* _mesa_function_pool[20893]: ProgramUniform1ui64vARB (will be remapped) */
   "glProgramUniform1ui64vARB\0"
   /* _mesa_function_pool[20919]: ProgramUniform2ui64vARB (will be remapped) */
   "glProgramUniform2ui64vARB\0"
   /* _mesa_function_pool[20945]: ProgramUniform3ui64vARB (will be remapped) */
   "glProgramUniform3ui64vARB\0"
   /* _mesa_function_pool[20971]: ProgramUniform4ui64vARB (will be remapped) */
   "glProgramUniform4ui64vARB\0"
   /* _mesa_function_pool[20997]: MaxShaderCompilerThreadsKHR (will be remapped) */
   "glMaxShaderCompilerThreadsKHR\0"
   /* _mesa_function_pool[21027]: SpecializeShaderARB (will be remapped) */
   "glSpecializeShaderARB\0"
   /* _mesa_function_pool[21049]: GetTexFilterFuncSGIS (dynamic) */
   "glGetTexFilterFuncSGIS\0"
   /* _mesa_function_pool[21072]: TexFilterFuncSGIS (dynamic) */
   "glTexFilterFuncSGIS\0"
   /* _mesa_function_pool[21092]: PixelTexGenParameteriSGIS (dynamic) */
   "glPixelTexGenParameteriSGIS\0"
   /* _mesa_function_pool[21120]: PixelTexGenParameterivSGIS (dynamic) */
   "glPixelTexGenParameterivSGIS\0"
   /* _mesa_function_pool[21149]: PixelTexGenParameterfSGIS (dynamic) */
   "glPixelTexGenParameterfSGIS\0"
   /* _mesa_function_pool[21177]: PixelTexGenParameterfvSGIS (dynamic) */
   "glPixelTexGenParameterfvSGIS\0"
   /* _mesa_function_pool[21206]: GetPixelTexGenParameterivSGIS (dynamic) */
   "glGetPixelTexGenParameterivSGIS\0"
   /* _mesa_function_pool[21238]: GetPixelTexGenParameterfvSGIS (dynamic) */
   "glGetPixelTexGenParameterfvSGIS\0"
   /* _mesa_function_pool[21270]: TexImage4DSGIS (dynamic) */
   "glTexImage4DSGIS\0"
   /* _mesa_function_pool[21287]: TexSubImage4DSGIS (dynamic) */
   "glTexSubImage4DSGIS\0"
   /* _mesa_function_pool[21307]: DetailTexFuncSGIS (dynamic) */
   "glDetailTexFuncSGIS\0"
   /* _mesa_function_pool[21327]: GetDetailTexFuncSGIS (dynamic) */
   "glGetDetailTexFuncSGIS\0"
   /* _mesa_function_pool[21350]: SharpenTexFuncSGIS (dynamic) */
   "glSharpenTexFuncSGIS\0"
   /* _mesa_function_pool[21371]: GetSharpenTexFuncSGIS (dynamic) */
   "glGetSharpenTexFuncSGIS\0"
   /* _mesa_function_pool[21395]: SampleMaskSGIS (will be remapped) */
   "glSampleMaskSGIS\0"
   /* _mesa_function_pool[21412]: SamplePatternSGIS (will be remapped) */
   "glSamplePatternSGIS\0"
   /* _mesa_function_pool[21432]: ColorPointerEXT (will be remapped) */
   "glColorPointerEXT\0"
   /* _mesa_function_pool[21450]: EdgeFlagPointerEXT (will be remapped) */
   "glEdgeFlagPointerEXT\0"
   /* _mesa_function_pool[21471]: IndexPointerEXT (will be remapped) */
   "glIndexPointerEXT\0"
   /* _mesa_function_pool[21489]: NormalPointerEXT (will be remapped) */
   "glNormalPointerEXT\0"
   /* _mesa_function_pool[21508]: TexCoordPointerEXT (will be remapped) */
   "glTexCoordPointerEXT\0"
   /* _mesa_function_pool[21529]: VertexPointerEXT (will be remapped) */
   "glVertexPointerEXT\0"
   /* _mesa_function_pool[21548]: SpriteParameterfSGIX (dynamic) */
   "glSpriteParameterfSGIX\0"
   /* _mesa_function_pool[21571]: SpriteParameterfvSGIX (dynamic) */
   "glSpriteParameterfvSGIX\0"
   /* _mesa_function_pool[21595]: SpriteParameteriSGIX (dynamic) */
   "glSpriteParameteriSGIX\0"
   /* _mesa_function_pool[21618]: SpriteParameterivSGIX (dynamic) */
   "glSpriteParameterivSGIX\0"
   /* _mesa_function_pool[21642]: GetInstrumentsSGIX (dynamic) */
   "glGetInstrumentsSGIX\0"
   /* _mesa_function_pool[21663]: InstrumentsBufferSGIX (dynamic) */
   "glInstrumentsBufferSGIX\0"
   /* _mesa_function_pool[21687]: PollInstrumentsSGIX (dynamic) */
   "glPollInstrumentsSGIX\0"
   /* _mesa_function_pool[21709]: ReadInstrumentsSGIX (dynamic) */
   "glReadInstrumentsSGIX\0"
   /* _mesa_function_pool[21731]: StartInstrumentsSGIX (dynamic) */
   "glStartInstrumentsSGIX\0"
   /* _mesa_function_pool[21754]: StopInstrumentsSGIX (dynamic) */
   "glStopInstrumentsSGIX\0"
   /* _mesa_function_pool[21776]: FrameZoomSGIX (dynamic) */
   "glFrameZoomSGIX\0"
   /* _mesa_function_pool[21792]: TagSampleBufferSGIX (dynamic) */
   "glTagSampleBufferSGIX\0"
   /* _mesa_function_pool[21814]: ReferencePlaneSGIX (dynamic) */
   "glReferencePlaneSGIX\0"
   /* _mesa_function_pool[21835]: FlushRasterSGIX (dynamic) */
   "glFlushRasterSGIX\0"
   /* _mesa_function_pool[21853]: FogFuncSGIS (dynamic) */
   "glFogFuncSGIS\0"
   /* _mesa_function_pool[21867]: GetFogFuncSGIS (dynamic) */
   "glGetFogFuncSGIS\0"
   /* _mesa_function_pool[21884]: ImageTransformParameteriHP (dynamic) */
   "glImageTransformParameteriHP\0"
   /* _mesa_function_pool[21913]: ImageTransformParameterfHP (dynamic) */
   "glImageTransformParameterfHP\0"
   /* _mesa_function_pool[21942]: ImageTransformParameterivHP (dynamic) */
   "glImageTransformParameterivHP\0"
   /* _mesa_function_pool[21972]: ImageTransformParameterfvHP (dynamic) */
   "glImageTransformParameterfvHP\0"
   /* _mesa_function_pool[22002]: GetImageTransformParameterivHP (dynamic) */
   "glGetImageTransformParameterivHP\0"
   /* _mesa_function_pool[22035]: GetImageTransformParameterfvHP (dynamic) */
   "glGetImageTransformParameterfvHP\0"
   /* _mesa_function_pool[22068]: HintPGI (dynamic) */
   "glHintPGI\0"
   /* _mesa_function_pool[22078]: GetListParameterfvSGIX (dynamic) */
   "glGetListParameterfvSGIX\0"
   /* _mesa_function_pool[22103]: GetListParameterivSGIX (dynamic) */
   "glGetListParameterivSGIX\0"
   /* _mesa_function_pool[22128]: ListParameterfSGIX (dynamic) */
   "glListParameterfSGIX\0"
   /* _mesa_function_pool[22149]: ListParameterfvSGIX (dynamic) */
   "glListParameterfvSGIX\0"
   /* _mesa_function_pool[22171]: ListParameteriSGIX (dynamic) */
   "glListParameteriSGIX\0"
   /* _mesa_function_pool[22192]: ListParameterivSGIX (dynamic) */
   "glListParameterivSGIX\0"
   /* _mesa_function_pool[22214]: IndexMaterialEXT (dynamic) */
   "glIndexMaterialEXT\0"
   /* _mesa_function_pool[22233]: IndexFuncEXT (dynamic) */
   "glIndexFuncEXT\0"
   /* _mesa_function_pool[22248]: LockArraysEXT (will be remapped) */
   "glLockArraysEXT\0"
   /* _mesa_function_pool[22264]: UnlockArraysEXT (will be remapped) */
   "glUnlockArraysEXT\0"
   /* _mesa_function_pool[22282]: CullParameterdvEXT (dynamic) */
   "glCullParameterdvEXT\0"
   /* _mesa_function_pool[22303]: CullParameterfvEXT (dynamic) */
   "glCullParameterfvEXT\0"
   /* _mesa_function_pool[22324]: ViewportArrayv (will be remapped) */
   "glViewportArrayv\0"
   /* _mesa_function_pool[22341]: ViewportIndexedf (will be remapped) */
   "glViewportIndexedf\0"
   /* _mesa_function_pool[22360]: ViewportIndexedfv (will be remapped) */
   "glViewportIndexedfv\0"
   /* _mesa_function_pool[22380]: ScissorArrayv (will be remapped) */
   "glScissorArrayv\0"
   /* _mesa_function_pool[22396]: ScissorIndexed (will be remapped) */
   "glScissorIndexed\0"
   /* _mesa_function_pool[22413]: ScissorIndexedv (will be remapped) */
   "glScissorIndexedv\0"
   /* _mesa_function_pool[22431]: DepthRangeArrayv (will be remapped) */
   "glDepthRangeArrayv\0"
   /* _mesa_function_pool[22450]: DepthRangeIndexed (will be remapped) */
   "glDepthRangeIndexed\0"
   /* _mesa_function_pool[22470]: GetFloati_v (will be remapped) */
   "glGetFloati_v\0"
   /* _mesa_function_pool[22484]: GetDoublei_v (will be remapped) */
   "glGetDoublei_v\0"
   /* _mesa_function_pool[22499]: FragmentColorMaterialSGIX (dynamic) */
   "glFragmentColorMaterialSGIX\0"
   /* _mesa_function_pool[22527]: FragmentLightfSGIX (dynamic) */
   "glFragmentLightfSGIX\0"
   /* _mesa_function_pool[22548]: FragmentLightfvSGIX (dynamic) */
   "glFragmentLightfvSGIX\0"
   /* _mesa_function_pool[22570]: FragmentLightiSGIX (dynamic) */
   "glFragmentLightiSGIX\0"
   /* _mesa_function_pool[22591]: FragmentLightivSGIX (dynamic) */
   "glFragmentLightivSGIX\0"
   /* _mesa_function_pool[22613]: FragmentLightModelfSGIX (dynamic) */
   "glFragmentLightModelfSGIX\0"
   /* _mesa_function_pool[22639]: FragmentLightModelfvSGIX (dynamic) */
   "glFragmentLightModelfvSGIX\0"
   /* _mesa_function_pool[22666]: FragmentLightModeliSGIX (dynamic) */
   "glFragmentLightModeliSGIX\0"
   /* _mesa_function_pool[22692]: FragmentLightModelivSGIX (dynamic) */
   "glFragmentLightModelivSGIX\0"
   /* _mesa_function_pool[22719]: FragmentMaterialfSGIX (dynamic) */
   "glFragmentMaterialfSGIX\0"
   /* _mesa_function_pool[22743]: FragmentMaterialfvSGIX (dynamic) */
   "glFragmentMaterialfvSGIX\0"
   /* _mesa_function_pool[22768]: FragmentMaterialiSGIX (dynamic) */
   "glFragmentMaterialiSGIX\0"
   /* _mesa_function_pool[22792]: FragmentMaterialivSGIX (dynamic) */
   "glFragmentMaterialivSGIX\0"
   /* _mesa_function_pool[22817]: GetFragmentLightfvSGIX (dynamic) */
   "glGetFragmentLightfvSGIX\0"
   /* _mesa_function_pool[22842]: GetFragmentLightivSGIX (dynamic) */
   "glGetFragmentLightivSGIX\0"
   /* _mesa_function_pool[22867]: GetFragmentMaterialfvSGIX (dynamic) */
   "glGetFragmentMaterialfvSGIX\0"
   /* _mesa_function_pool[22895]: GetFragmentMaterialivSGIX (dynamic) */
   "glGetFragmentMaterialivSGIX\0"
   /* _mesa_function_pool[22923]: LightEnviSGIX (dynamic) */
   "glLightEnviSGIX\0"
   /* _mesa_function_pool[22939]: ApplyTextureEXT (dynamic) */
   "glApplyTextureEXT\0"
   /* _mesa_function_pool[22957]: TextureLightEXT (dynamic) */
   "glTextureLightEXT\0"
   /* _mesa_function_pool[22975]: TextureMaterialEXT (dynamic) */
   "glTextureMaterialEXT\0"
   /* _mesa_function_pool[22996]: AsyncMarkerSGIX (dynamic) */
   "glAsyncMarkerSGIX\0"
   /* _mesa_function_pool[23014]: FinishAsyncSGIX (dynamic) */
   "glFinishAsyncSGIX\0"
   /* _mesa_function_pool[23032]: PollAsyncSGIX (dynamic) */
   "glPollAsyncSGIX\0"
   /* _mesa_function_pool[23048]: GenAsyncMarkersSGIX (dynamic) */
   "glGenAsyncMarkersSGIX\0"
   /* _mesa_function_pool[23070]: DeleteAsyncMarkersSGIX (dynamic) */
   "glDeleteAsyncMarkersSGIX\0"
   /* _mesa_function_pool[23095]: IsAsyncMarkerSGIX (dynamic) */
   "glIsAsyncMarkerSGIX\0"
   /* _mesa_function_pool[23115]: VertexPointervINTEL (dynamic) */
   "glVertexPointervINTEL\0"
   /* _mesa_function_pool[23137]: NormalPointervINTEL (dynamic) */
   "glNormalPointervINTEL\0"
   /* _mesa_function_pool[23159]: ColorPointervINTEL (dynamic) */
   "glColorPointervINTEL\0"
   /* _mesa_function_pool[23180]: TexCoordPointervINTEL (dynamic) */
   "glTexCoordPointervINTEL\0"
   /* _mesa_function_pool[23204]: PixelTransformParameteriEXT (dynamic) */
   "glPixelTransformParameteriEXT\0"
   /* _mesa_function_pool[23234]: PixelTransformParameterfEXT (dynamic) */
   "glPixelTransformParameterfEXT\0"
   /* _mesa_function_pool[23264]: PixelTransformParameterivEXT (dynamic) */
   "glPixelTransformParameterivEXT\0"
   /* _mesa_function_pool[23295]: PixelTransformParameterfvEXT (dynamic) */
   "glPixelTransformParameterfvEXT\0"
   /* _mesa_function_pool[23326]: TextureNormalEXT (dynamic) */
   "glTextureNormalEXT\0"
   /* _mesa_function_pool[23345]: Tangent3bEXT (dynamic) */
   "glTangent3bEXT\0"
   /* _mesa_function_pool[23360]: Tangent3bvEXT (dynamic) */
   "glTangent3bvEXT\0"
   /* _mesa_function_pool[23376]: Tangent3dEXT (dynamic) */
   "glTangent3dEXT\0"
   /* _mesa_function_pool[23391]: Tangent3dvEXT (dynamic) */
   "glTangent3dvEXT\0"
   /* _mesa_function_pool[23407]: Tangent3fEXT (dynamic) */
   "glTangent3fEXT\0"
   /* _mesa_function_pool[23422]: Tangent3fvEXT (dynamic) */
   "glTangent3fvEXT\0"
   /* _mesa_function_pool[23438]: Tangent3iEXT (dynamic) */
   "glTangent3iEXT\0"
   /* _mesa_function_pool[23453]: Tangent3ivEXT (dynamic) */
   "glTangent3ivEXT\0"
   /* _mesa_function_pool[23469]: Tangent3sEXT (dynamic) */
   "glTangent3sEXT\0"
   /* _mesa_function_pool[23484]: Tangent3svEXT (dynamic) */
   "glTangent3svEXT\0"
   /* _mesa_function_pool[23500]: Binormal3bEXT (dynamic) */
   "glBinormal3bEXT\0"
   /* _mesa_function_pool[23516]: Binormal3bvEXT (dynamic) */
   "glBinormal3bvEXT\0"
   /* _mesa_function_pool[23533]: Binormal3dEXT (dynamic) */
   "glBinormal3dEXT\0"
   /* _mesa_function_pool[23549]: Binormal3dvEXT (dynamic) */
   "glBinormal3dvEXT\0"
   /* _mesa_function_pool[23566]: Binormal3fEXT (dynamic) */
   "glBinormal3fEXT\0"
   /* _mesa_function_pool[23582]: Binormal3fvEXT (dynamic) */
   "glBinormal3fvEXT\0"
   /* _mesa_function_pool[23599]: Binormal3iEXT (dynamic) */
   "glBinormal3iEXT\0"
   /* _mesa_function_pool[23615]: Binormal3ivEXT (dynamic) */
   "glBinormal3ivEXT\0"
   /* _mesa_function_pool[23632]: Binormal3sEXT (dynamic) */
   "glBinormal3sEXT\0"
   /* _mesa_function_pool[23648]: Binormal3svEXT (dynamic) */
   "glBinormal3svEXT\0"
   /* _mesa_function_pool[23665]: TangentPointerEXT (dynamic) */
   "glTangentPointerEXT\0"
   /* _mesa_function_pool[23685]: BinormalPointerEXT (dynamic) */
   "glBinormalPointerEXT\0"
   /* _mesa_function_pool[23706]: PixelTexGenSGIX (dynamic) */
   "glPixelTexGenSGIX\0"
   /* _mesa_function_pool[23724]: FinishTextureSUNX (dynamic) */
   "glFinishTextureSUNX\0"
   /* _mesa_function_pool[23744]: GlobalAlphaFactorbSUN (dynamic) */
   "glGlobalAlphaFactorbSUN\0"
   /* _mesa_function_pool[23768]: GlobalAlphaFactorsSUN (dynamic) */
   "glGlobalAlphaFactorsSUN\0"
   /* _mesa_function_pool[23792]: GlobalAlphaFactoriSUN (dynamic) */
   "glGlobalAlphaFactoriSUN\0"
   /* _mesa_function_pool[23816]: GlobalAlphaFactorfSUN (dynamic) */
   "glGlobalAlphaFactorfSUN\0"
   /* _mesa_function_pool[23840]: GlobalAlphaFactordSUN (dynamic) */
   "glGlobalAlphaFactordSUN\0"
   /* _mesa_function_pool[23864]: GlobalAlphaFactorubSUN (dynamic) */
   "glGlobalAlphaFactorubSUN\0"
   /* _mesa_function_pool[23889]: GlobalAlphaFactorusSUN (dynamic) */
   "glGlobalAlphaFactorusSUN\0"
   /* _mesa_function_pool[23914]: GlobalAlphaFactoruiSUN (dynamic) */
   "glGlobalAlphaFactoruiSUN\0"
   /* _mesa_function_pool[23939]: ReplacementCodeuiSUN (dynamic) */
   "glReplacementCodeuiSUN\0"
   /* _mesa_function_pool[23962]: ReplacementCodeusSUN (dynamic) */
   "glReplacementCodeusSUN\0"
   /* _mesa_function_pool[23985]: ReplacementCodeubSUN (dynamic) */
   "glReplacementCodeubSUN\0"
   /* _mesa_function_pool[24008]: ReplacementCodeuivSUN (dynamic) */
   "glReplacementCodeuivSUN\0"
   /* _mesa_function_pool[24032]: ReplacementCodeusvSUN (dynamic) */
   "glReplacementCodeusvSUN\0"
   /* _mesa_function_pool[24056]: ReplacementCodeubvSUN (dynamic) */
   "glReplacementCodeubvSUN\0"
   /* _mesa_function_pool[24080]: ReplacementCodePointerSUN (dynamic) */
   "glReplacementCodePointerSUN\0"
   /* _mesa_function_pool[24108]: Color4ubVertex2fSUN (dynamic) */
   "glColor4ubVertex2fSUN\0"
   /* _mesa_function_pool[24130]: Color4ubVertex2fvSUN (dynamic) */
   "glColor4ubVertex2fvSUN\0"
   /* _mesa_function_pool[24153]: Color4ubVertex3fSUN (dynamic) */
   "glColor4ubVertex3fSUN\0"
   /* _mesa_function_pool[24175]: Color4ubVertex3fvSUN (dynamic) */
   "glColor4ubVertex3fvSUN\0"
   /* _mesa_function_pool[24198]: Color3fVertex3fSUN (dynamic) */
   "glColor3fVertex3fSUN\0"
   /* _mesa_function_pool[24219]: Color3fVertex3fvSUN (dynamic) */
   "glColor3fVertex3fvSUN\0"
   /* _mesa_function_pool[24241]: Normal3fVertex3fSUN (dynamic) */
   "glNormal3fVertex3fSUN\0"
   /* _mesa_function_pool[24263]: Normal3fVertex3fvSUN (dynamic) */
   "glNormal3fVertex3fvSUN\0"
   /* _mesa_function_pool[24286]: Color4fNormal3fVertex3fSUN (dynamic) */
   "glColor4fNormal3fVertex3fSUN\0"
   /* _mesa_function_pool[24315]: Color4fNormal3fVertex3fvSUN (dynamic) */
   "glColor4fNormal3fVertex3fvSUN\0"
   /* _mesa_function_pool[24345]: TexCoord2fVertex3fSUN (dynamic) */
   "glTexCoord2fVertex3fSUN\0"
   /* _mesa_function_pool[24369]: TexCoord2fVertex3fvSUN (dynamic) */
   "glTexCoord2fVertex3fvSUN\0"
   /* _mesa_function_pool[24394]: TexCoord4fVertex4fSUN (dynamic) */
   "glTexCoord4fVertex4fSUN\0"
   /* _mesa_function_pool[24418]: TexCoord4fVertex4fvSUN (dynamic) */
   "glTexCoord4fVertex4fvSUN\0"
   /* _mesa_function_pool[24443]: TexCoord2fColor4ubVertex3fSUN (dynamic) */
   "glTexCoord2fColor4ubVertex3fSUN\0"
   /* _mesa_function_pool[24475]: TexCoord2fColor4ubVertex3fvSUN (dynamic) */
   "glTexCoord2fColor4ubVertex3fvSUN\0"
   /* _mesa_function_pool[24508]: TexCoord2fColor3fVertex3fSUN (dynamic) */
   "glTexCoord2fColor3fVertex3fSUN\0"
   /* _mesa_function_pool[24539]: TexCoord2fColor3fVertex3fvSUN (dynamic) */
   "glTexCoord2fColor3fVertex3fvSUN\0"
   /* _mesa_function_pool[24571]: TexCoord2fNormal3fVertex3fSUN (dynamic) */
   "glTexCoord2fNormal3fVertex3fSUN\0"
   /* _mesa_function_pool[24603]: TexCoord2fNormal3fVertex3fvSUN (dynamic) */
   "glTexCoord2fNormal3fVertex3fvSUN\0"
   /* _mesa_function_pool[24636]: TexCoord2fColor4fNormal3fVertex3fSUN (dynamic) */
   "glTexCoord2fColor4fNormal3fVertex3fSUN\0"
   /* _mesa_function_pool[24675]: TexCoord2fColor4fNormal3fVertex3fvSUN (dynamic) */
   "glTexCoord2fColor4fNormal3fVertex3fvSUN\0"
   /* _mesa_function_pool[24715]: TexCoord4fColor4fNormal3fVertex4fSUN (dynamic) */
   "glTexCoord4fColor4fNormal3fVertex4fSUN\0"
   /* _mesa_function_pool[24754]: TexCoord4fColor4fNormal3fVertex4fvSUN (dynamic) */
   "glTexCoord4fColor4fNormal3fVertex4fvSUN\0"
   /* _mesa_function_pool[24794]: ReplacementCodeuiVertex3fSUN (dynamic) */
   "glReplacementCodeuiVertex3fSUN\0"
   /* _mesa_function_pool[24825]: ReplacementCodeuiVertex3fvSUN (dynamic) */
   "glReplacementCodeuiVertex3fvSUN\0"
   /* _mesa_function_pool[24857]: ReplacementCodeuiColor4ubVertex3fSUN (dynamic) */
   "glReplacementCodeuiColor4ubVertex3fSUN\0"
   /* _mesa_function_pool[24896]: ReplacementCodeuiColor4ubVertex3fvSUN (dynamic) */
   "glReplacementCodeuiColor4ubVertex3fvSUN\0"
   /* _mesa_function_pool[24936]: ReplacementCodeuiColor3fVertex3fSUN (dynamic) */
   "glReplacementCodeuiColor3fVertex3fSUN\0"
   /* _mesa_function_pool[24974]: ReplacementCodeuiColor3fVertex3fvSUN (dynamic) */
   "glReplacementCodeuiColor3fVertex3fvSUN\0"
   /* _mesa_function_pool[25013]: ReplacementCodeuiNormal3fVertex3fSUN (dynamic) */
   "glReplacementCodeuiNormal3fVertex3fSUN\0"
   /* _mesa_function_pool[25052]: ReplacementCodeuiNormal3fVertex3fvSUN (dynamic) */
   "glReplacementCodeuiNormal3fVertex3fvSUN\0"
   /* _mesa_function_pool[25092]: ReplacementCodeuiColor4fNormal3fVertex3fSUN (dynamic) */
   "glReplacementCodeuiColor4fNormal3fVertex3fSUN\0"
   /* _mesa_function_pool[25138]: ReplacementCodeuiColor4fNormal3fVertex3fvSUN (dynamic) */
   "glReplacementCodeuiColor4fNormal3fVertex3fvSUN\0"
   /* _mesa_function_pool[25185]: ReplacementCodeuiTexCoord2fVertex3fSUN (dynamic) */
   "glReplacementCodeuiTexCoord2fVertex3fSUN\0"
   /* _mesa_function_pool[25226]: ReplacementCodeuiTexCoord2fVertex3fvSUN (dynamic) */
   "glReplacementCodeuiTexCoord2fVertex3fvSUN\0"
   /* _mesa_function_pool[25268]: ReplacementCodeuiTexCoord2fNormal3fVertex3fSUN (dynamic) */
   "glReplacementCodeuiTexCoord2fNormal3fVertex3fSUN\0"
   /* _mesa_function_pool[25317]: ReplacementCodeuiTexCoord2fNormal3fVertex3fvSUN (dynamic) */
   "glReplacementCodeuiTexCoord2fNormal3fVertex3fvSUN\0"
   /* _mesa_function_pool[25367]: ReplacementCodeuiTexCoord2fColor4fNormal3fVertex3fSUN (dynamic) */
   "glReplacementCodeuiTexCoord2fColor4fNormal3fVertex3fSUN\0"
   /* _mesa_function_pool[25423]: ReplacementCodeuiTexCoord2fColor4fNormal3fVertex3fvSUN (dynamic) */
   "glReplacementCodeuiTexCoord2fColor4fNormal3fVertex3fvSUN\0"
   /* _mesa_function_pool[25480]: FramebufferSampleLocationsfvARB (will be remapped) */
   "glFramebufferSampleLocationsfvARB\0"
   /* _mesa_function_pool[25514]: NamedFramebufferSampleLocationsfvARB (will be remapped) */
   "glNamedFramebufferSampleLocationsfvARB\0"
   /* _mesa_function_pool[25553]: EvaluateDepthValuesARB (will be remapped) */
   "glEvaluateDepthValuesARB\0"
   /* _mesa_function_pool[25578]: VertexWeightfEXT (dynamic) */
   "glVertexWeightfEXT\0"
   /* _mesa_function_pool[25597]: VertexWeightfvEXT (dynamic) */
   "glVertexWeightfvEXT\0"
   /* _mesa_function_pool[25617]: VertexWeightPointerEXT (dynamic) */
   "glVertexWeightPointerEXT\0"
   /* _mesa_function_pool[25642]: FlushVertexArrayRangeNV (dynamic) */
   "glFlushVertexArrayRangeNV\0"
   /* _mesa_function_pool[25668]: VertexArrayRangeNV (dynamic) */
   "glVertexArrayRangeNV\0"
   /* _mesa_function_pool[25689]: CombinerParameterfvNV (dynamic) */
   "glCombinerParameterfvNV\0"
   /* _mesa_function_pool[25713]: CombinerParameterfNV (dynamic) */
   "glCombinerParameterfNV\0"
   /* _mesa_function_pool[25736]: CombinerParameterivNV (dynamic) */
   "glCombinerParameterivNV\0"
   /* _mesa_function_pool[25760]: CombinerParameteriNV (dynamic) */
   "glCombinerParameteriNV\0"
   /* _mesa_function_pool[25783]: CombinerInputNV (dynamic) */
   "glCombinerInputNV\0"
   /* _mesa_function_pool[25801]: CombinerOutputNV (dynamic) */
   "glCombinerOutputNV\0"
   /* _mesa_function_pool[25820]: FinalCombinerInputNV (dynamic) */
   "glFinalCombinerInputNV\0"
   /* _mesa_function_pool[25843]: GetCombinerInputParameterfvNV (dynamic) */
   "glGetCombinerInputParameterfvNV\0"
   /* _mesa_function_pool[25875]: GetCombinerInputParameterivNV (dynamic) */
   "glGetCombinerInputParameterivNV\0"
   /* _mesa_function_pool[25907]: GetCombinerOutputParameterfvNV (dynamic) */
   "glGetCombinerOutputParameterfvNV\0"
   /* _mesa_function_pool[25940]: GetCombinerOutputParameterivNV (dynamic) */
   "glGetCombinerOutputParameterivNV\0"
   /* _mesa_function_pool[25973]: GetFinalCombinerInputParameterfvNV (dynamic) */
   "glGetFinalCombinerInputParameterfvNV\0"
   /* _mesa_function_pool[26010]: GetFinalCombinerInputParameterivNV (dynamic) */
   "glGetFinalCombinerInputParameterivNV\0"
   /* _mesa_function_pool[26047]: ResizeBuffersMESA (will be remapped) */
   "glResizeBuffersMESA\0"
   /* _mesa_function_pool[26067]: WindowPos4dMESA (will be remapped) */
   "glWindowPos4dMESA\0"
   /* _mesa_function_pool[26085]: WindowPos4dvMESA (will be remapped) */
   "glWindowPos4dvMESA\0"
   /* _mesa_function_pool[26104]: WindowPos4fMESA (will be remapped) */
   "glWindowPos4fMESA\0"
   /* _mesa_function_pool[26122]: WindowPos4fvMESA (will be remapped) */
   "glWindowPos4fvMESA\0"
   /* _mesa_function_pool[26141]: WindowPos4iMESA (will be remapped) */
   "glWindowPos4iMESA\0"
   /* _mesa_function_pool[26159]: WindowPos4ivMESA (will be remapped) */
   "glWindowPos4ivMESA\0"
   /* _mesa_function_pool[26178]: WindowPos4sMESA (will be remapped) */
   "glWindowPos4sMESA\0"
   /* _mesa_function_pool[26196]: WindowPos4svMESA (will be remapped) */
   "glWindowPos4svMESA\0"
   /* _mesa_function_pool[26215]: MultiModeDrawArraysIBM (will be remapped) */
   "glMultiModeDrawArraysIBM\0"
   /* _mesa_function_pool[26240]: MultiModeDrawElementsIBM (will be remapped) */
   "glMultiModeDrawElementsIBM\0"
   /* _mesa_function_pool[26267]: ColorPointerListIBM (dynamic) */
   "glColorPointerListIBM\0"
   /* _mesa_function_pool[26289]: SecondaryColorPointerListIBM (dynamic) */
   "glSecondaryColorPointerListIBM\0"
   /* _mesa_function_pool[26320]: EdgeFlagPointerListIBM (dynamic) */
   "glEdgeFlagPointerListIBM\0"
   /* _mesa_function_pool[26345]: FogCoordPointerListIBM (dynamic) */
   "glFogCoordPointerListIBM\0"
   /* _mesa_function_pool[26370]: IndexPointerListIBM (dynamic) */
   "glIndexPointerListIBM\0"
   /* _mesa_function_pool[26392]: NormalPointerListIBM (dynamic) */
   "glNormalPointerListIBM\0"
   /* _mesa_function_pool[26415]: TexCoordPointerListIBM (dynamic) */
   "glTexCoordPointerListIBM\0"
   /* _mesa_function_pool[26440]: VertexPointerListIBM (dynamic) */
   "glVertexPointerListIBM\0"
   /* _mesa_function_pool[26463]: TbufferMask3DFX (dynamic) */
   "glTbufferMask3DFX\0"
   /* _mesa_function_pool[26481]: TextureColorMaskSGIS (dynamic) */
   "glTextureColorMaskSGIS\0"
   /* _mesa_function_pool[26504]: DeleteFencesNV (dynamic) */
   "glDeleteFencesNV\0"
   /* _mesa_function_pool[26521]: GenFencesNV (dynamic) */
   "glGenFencesNV\0"
   /* _mesa_function_pool[26535]: IsFenceNV (dynamic) */
   "glIsFenceNV\0"
   /* _mesa_function_pool[26547]: TestFenceNV (dynamic) */
   "glTestFenceNV\0"
   /* _mesa_function_pool[26561]: GetFenceivNV (dynamic) */
   "glGetFenceivNV\0"
   /* _mesa_function_pool[26576]: FinishFenceNV (dynamic) */
   "glFinishFenceNV\0"
   /* _mesa_function_pool[26592]: SetFenceNV (dynamic) */
   "glSetFenceNV\0"
   /* _mesa_function_pool[26605]: MapControlPointsNV (dynamic) */
   "glMapControlPointsNV\0"
   /* _mesa_function_pool[26626]: MapParameterivNV (dynamic) */
   "glMapParameterivNV\0"
   /* _mesa_function_pool[26645]: MapParameterfvNV (dynamic) */
   "glMapParameterfvNV\0"
   /* _mesa_function_pool[26664]: GetMapControlPointsNV (dynamic) */
   "glGetMapControlPointsNV\0"
   /* _mesa_function_pool[26688]: GetMapParameterivNV (dynamic) */
   "glGetMapParameterivNV\0"
   /* _mesa_function_pool[26710]: GetMapParameterfvNV (dynamic) */
   "glGetMapParameterfvNV\0"
   /* _mesa_function_pool[26732]: GetMapAttribParameterivNV (dynamic) */
   "glGetMapAttribParameterivNV\0"
   /* _mesa_function_pool[26760]: GetMapAttribParameterfvNV (dynamic) */
   "glGetMapAttribParameterfvNV\0"
   /* _mesa_function_pool[26788]: EvalMapsNV (dynamic) */
   "glEvalMapsNV\0"
   /* _mesa_function_pool[26801]: CombinerStageParameterfvNV (dynamic) */
   "glCombinerStageParameterfvNV\0"
   /* _mesa_function_pool[26830]: GetCombinerStageParameterfvNV (dynamic) */
   "glGetCombinerStageParameterfvNV\0"
   /* _mesa_function_pool[26862]: AreProgramsResidentNV (will be remapped) */
   "glAreProgramsResidentNV\0"
   /* _mesa_function_pool[26886]: ExecuteProgramNV (will be remapped) */
   "glExecuteProgramNV\0"
   /* _mesa_function_pool[26905]: GetProgramParameterdvNV (will be remapped) */
   "glGetProgramParameterdvNV\0"
   /* _mesa_function_pool[26931]: GetProgramParameterfvNV (will be remapped) */
   "glGetProgramParameterfvNV\0"
   /* _mesa_function_pool[26957]: GetProgramivNV (will be remapped) */
   "glGetProgramivNV\0"
   /* _mesa_function_pool[26974]: GetProgramStringNV (will be remapped) */
   "glGetProgramStringNV\0"
   /* _mesa_function_pool[26995]: GetTrackMatrixivNV (will be remapped) */
   "glGetTrackMatrixivNV\0"
   /* _mesa_function_pool[27016]: GetVertexAttribdvNV (will be remapped) */
   "glGetVertexAttribdvNV\0"
   /* _mesa_function_pool[27038]: GetVertexAttribfvNV (will be remapped) */
   "glGetVertexAttribfvNV\0"
   /* _mesa_function_pool[27060]: GetVertexAttribivNV (will be remapped) */
   "glGetVertexAttribivNV\0"
   /* _mesa_function_pool[27082]: LoadProgramNV (will be remapped) */
   "glLoadProgramNV\0"
   /* _mesa_function_pool[27098]: ProgramParameters4dvNV (will be remapped) */
   "glProgramParameters4dvNV\0"
   /* _mesa_function_pool[27123]: ProgramParameters4fvNV (will be remapped) */
   "glProgramParameters4fvNV\0"
   /* _mesa_function_pool[27148]: RequestResidentProgramsNV (will be remapped) */
   "glRequestResidentProgramsNV\0"
   /* _mesa_function_pool[27176]: TrackMatrixNV (will be remapped) */
   "glTrackMatrixNV\0"
   /* _mesa_function_pool[27192]: VertexAttribPointerNV (will be remapped) */
   "glVertexAttribPointerNV\0"
   /* _mesa_function_pool[27216]: VertexAttrib1sNV (will be remapped) */
   "glVertexAttrib1sNV\0"
   /* _mesa_function_pool[27235]: VertexAttrib1svNV (will be remapped) */
   "glVertexAttrib1svNV\0"
   /* _mesa_function_pool[27255]: VertexAttrib2sNV (will be remapped) */
   "glVertexAttrib2sNV\0"
   /* _mesa_function_pool[27274]: VertexAttrib2svNV (will be remapped) */
   "glVertexAttrib2svNV\0"
   /* _mesa_function_pool[27294]: VertexAttrib3sNV (will be remapped) */
   "glVertexAttrib3sNV\0"
   /* _mesa_function_pool[27313]: VertexAttrib3svNV (will be remapped) */
   "glVertexAttrib3svNV\0"
   /* _mesa_function_pool[27333]: VertexAttrib4sNV (will be remapped) */
   "glVertexAttrib4sNV\0"
   /* _mesa_function_pool[27352]: VertexAttrib4svNV (will be remapped) */
   "glVertexAttrib4svNV\0"
   /* _mesa_function_pool[27372]: VertexAttrib1fNV (will be remapped) */
   "glVertexAttrib1fNV\0"
   /* _mesa_function_pool[27391]: VertexAttrib1fvNV (will be remapped) */
   "glVertexAttrib1fvNV\0"
   /* _mesa_function_pool[27411]: VertexAttrib2fNV (will be remapped) */
   "glVertexAttrib2fNV\0"
   /* _mesa_function_pool[27430]: VertexAttrib2fvNV (will be remapped) */
   "glVertexAttrib2fvNV\0"
   /* _mesa_function_pool[27450]: VertexAttrib3fNV (will be remapped) */
   "glVertexAttrib3fNV\0"
   /* _mesa_function_pool[27469]: VertexAttrib3fvNV (will be remapped) */
   "glVertexAttrib3fvNV\0"
   /* _mesa_function_pool[27489]: VertexAttrib4fNV (will be remapped) */
   "glVertexAttrib4fNV\0"
   /* _mesa_function_pool[27508]: VertexAttrib4fvNV (will be remapped) */
   "glVertexAttrib4fvNV\0"
   /* _mesa_function_pool[27528]: VertexAttrib1dNV (will be remapped) */
   "glVertexAttrib1dNV\0"
   /* _mesa_function_pool[27547]: VertexAttrib1dvNV (will be remapped) */
   "glVertexAttrib1dvNV\0"
   /* _mesa_function_pool[27567]: VertexAttrib2dNV (will be remapped) */
   "glVertexAttrib2dNV\0"
   /* _mesa_function_pool[27586]: VertexAttrib2dvNV (will be remapped) */
   "glVertexAttrib2dvNV\0"
   /* _mesa_function_pool[27606]: VertexAttrib3dNV (will be remapped) */
   "glVertexAttrib3dNV\0"
   /* _mesa_function_pool[27625]: VertexAttrib3dvNV (will be remapped) */
   "glVertexAttrib3dvNV\0"
   /* _mesa_function_pool[27645]: VertexAttrib4dNV (will be remapped) */
   "glVertexAttrib4dNV\0"
   /* _mesa_function_pool[27664]: VertexAttrib4dvNV (will be remapped) */
   "glVertexAttrib4dvNV\0"
   /* _mesa_function_pool[27684]: VertexAttrib4ubNV (will be remapped) */
   "glVertexAttrib4ubNV\0"
   /* _mesa_function_pool[27704]: VertexAttrib4ubvNV (will be remapped) */
   "glVertexAttrib4ubvNV\0"
   /* _mesa_function_pool[27725]: VertexAttribs1svNV (will be remapped) */
   "glVertexAttribs1svNV\0"
   /* _mesa_function_pool[27746]: VertexAttribs2svNV (will be remapped) */
   "glVertexAttribs2svNV\0"
   /* _mesa_function_pool[27767]: VertexAttribs3svNV (will be remapped) */
   "glVertexAttribs3svNV\0"
   /* _mesa_function_pool[27788]: VertexAttribs4svNV (will be remapped) */
   "glVertexAttribs4svNV\0"
   /* _mesa_function_pool[27809]: VertexAttribs1fvNV (will be remapped) */
   "glVertexAttribs1fvNV\0"
   /* _mesa_function_pool[27830]: VertexAttribs2fvNV (will be remapped) */
   "glVertexAttribs2fvNV\0"
   /* _mesa_function_pool[27851]: VertexAttribs3fvNV (will be remapped) */
   "glVertexAttribs3fvNV\0"
   /* _mesa_function_pool[27872]: VertexAttribs4fvNV (will be remapped) */
   "glVertexAttribs4fvNV\0"
   /* _mesa_function_pool[27893]: VertexAttribs1dvNV (will be remapped) */
   "glVertexAttribs1dvNV\0"
   /* _mesa_function_pool[27914]: VertexAttribs2dvNV (will be remapped) */
   "glVertexAttribs2dvNV\0"
   /* _mesa_function_pool[27935]: VertexAttribs3dvNV (will be remapped) */
   "glVertexAttribs3dvNV\0"
   /* _mesa_function_pool[27956]: VertexAttribs4dvNV (will be remapped) */
   "glVertexAttribs4dvNV\0"
   /* _mesa_function_pool[27977]: VertexAttribs4ubvNV (will be remapped) */
   "glVertexAttribs4ubvNV\0"
   /* _mesa_function_pool[27999]: TexBumpParameterfvATI (will be remapped) */
   "glTexBumpParameterfvATI\0"
   /* _mesa_function_pool[28023]: TexBumpParameterivATI (will be remapped) */
   "glTexBumpParameterivATI\0"
   /* _mesa_function_pool[28047]: GetTexBumpParameterfvATI (will be remapped) */
   "glGetTexBumpParameterfvATI\0"
   /* _mesa_function_pool[28074]: GetTexBumpParameterivATI (will be remapped) */
   "glGetTexBumpParameterivATI\0"
   /* _mesa_function_pool[28101]: GenFragmentShadersATI (will be remapped) */
   "glGenFragmentShadersATI\0"
   /* _mesa_function_pool[28125]: BindFragmentShaderATI (will be remapped) */
   "glBindFragmentShaderATI\0"
   /* _mesa_function_pool[28149]: DeleteFragmentShaderATI (will be remapped) */
   "glDeleteFragmentShaderATI\0"
   /* _mesa_function_pool[28175]: BeginFragmentShaderATI (will be remapped) */
   "glBeginFragmentShaderATI\0"
   /* _mesa_function_pool[28200]: EndFragmentShaderATI (will be remapped) */
   "glEndFragmentShaderATI\0"
   /* _mesa_function_pool[28223]: PassTexCoordATI (will be remapped) */
   "glPassTexCoordATI\0"
   /* _mesa_function_pool[28241]: SampleMapATI (will be remapped) */
   "glSampleMapATI\0"
   /* _mesa_function_pool[28256]: ColorFragmentOp1ATI (will be remapped) */
   "glColorFragmentOp1ATI\0"
   /* _mesa_function_pool[28278]: ColorFragmentOp2ATI (will be remapped) */
   "glColorFragmentOp2ATI\0"
   /* _mesa_function_pool[28300]: ColorFragmentOp3ATI (will be remapped) */
   "glColorFragmentOp3ATI\0"
   /* _mesa_function_pool[28322]: AlphaFragmentOp1ATI (will be remapped) */
   "glAlphaFragmentOp1ATI\0"
   /* _mesa_function_pool[28344]: AlphaFragmentOp2ATI (will be remapped) */
   "glAlphaFragmentOp2ATI\0"
   /* _mesa_function_pool[28366]: AlphaFragmentOp3ATI (will be remapped) */
   "glAlphaFragmentOp3ATI\0"
   /* _mesa_function_pool[28388]: SetFragmentShaderConstantATI (will be remapped) */
   "glSetFragmentShaderConstantATI\0"
   /* _mesa_function_pool[28419]: DrawMeshArraysSUN (dynamic) */
   "glDrawMeshArraysSUN\0"
   /* _mesa_function_pool[28439]: ActiveStencilFaceEXT (will be remapped) */
   "glActiveStencilFaceEXT\0"
   /* _mesa_function_pool[28462]: ObjectPurgeableAPPLE (will be remapped) */
   "glObjectPurgeableAPPLE\0"
   /* _mesa_function_pool[28485]: ObjectUnpurgeableAPPLE (will be remapped) */
   "glObjectUnpurgeableAPPLE\0"
   /* _mesa_function_pool[28510]: GetObjectParameterivAPPLE (will be remapped) */
   "glGetObjectParameterivAPPLE\0"
   /* _mesa_function_pool[28538]: BindVertexArrayAPPLE (dynamic) */
   "glBindVertexArrayAPPLE\0"
   /* _mesa_function_pool[28561]: DeleteVertexArraysAPPLE (dynamic) */
   "glDeleteVertexArraysAPPLE\0"
   /* _mesa_function_pool[28587]: GenVertexArraysAPPLE (dynamic) */
   "glGenVertexArraysAPPLE\0"
   /* _mesa_function_pool[28610]: IsVertexArrayAPPLE (dynamic) */
   "glIsVertexArrayAPPLE\0"
   /* _mesa_function_pool[28631]: ProgramNamedParameter4fNV (will be remapped) */
   "glProgramNamedParameter4fNV\0"
   /* _mesa_function_pool[28659]: ProgramNamedParameter4dNV (will be remapped) */
   "glProgramNamedParameter4dNV\0"
   /* _mesa_function_pool[28687]: ProgramNamedParameter4fvNV (will be remapped) */
   "glProgramNamedParameter4fvNV\0"
   /* _mesa_function_pool[28716]: ProgramNamedParameter4dvNV (will be remapped) */
   "glProgramNamedParameter4dvNV\0"
   /* _mesa_function_pool[28745]: GetProgramNamedParameterfvNV (will be remapped) */
   "glGetProgramNamedParameterfvNV\0"
   /* _mesa_function_pool[28776]: GetProgramNamedParameterdvNV (will be remapped) */
   "glGetProgramNamedParameterdvNV\0"
   /* _mesa_function_pool[28807]: DepthBoundsEXT (will be remapped) */
   "glDepthBoundsEXT\0"
   /* _mesa_function_pool[28824]: BindRenderbufferEXT (will be remapped) */
   "glBindRenderbufferEXT\0"
   /* _mesa_function_pool[28846]: BindFramebufferEXT (will be remapped) */
   "glBindFramebufferEXT\0"
   /* _mesa_function_pool[28867]: StringMarkerGREMEDY (will be remapped) */
   "glStringMarkerGREMEDY\0"
   /* _mesa_function_pool[28889]: ProvokingVertex (will be remapped) */
   "glProvokingVertexEXT\0"
   /* _mesa_function_pool[28910]: ColorMaski (will be remapped) */
   "glColorMaskIndexedEXT\0"
   /* _mesa_function_pool[28932]: GetBooleani_v (will be remapped) */
   "glGetBooleanIndexedvEXT\0"
   /* _mesa_function_pool[28956]: GetIntegeri_v (will be remapped) */
   "glGetIntegerIndexedvEXT\0"
   /* _mesa_function_pool[28980]: Enablei (will be remapped) */
   "glEnableIndexedEXT\0"
   /* _mesa_function_pool[28999]: Disablei (will be remapped) */
   "glDisableIndexedEXT\0"
   /* _mesa_function_pool[29019]: IsEnabledi (will be remapped) */
   "glIsEnabledIndexedEXT\0"
   /* _mesa_function_pool[29041]: BufferParameteriAPPLE (will be remapped) */
   "glBufferParameteriAPPLE\0"
   /* _mesa_function_pool[29065]: FlushMappedBufferRangeAPPLE (will be remapped) */
   "glFlushMappedBufferRangeAPPLE\0"
   /* _mesa_function_pool[29095]: GetPerfMonitorGroupsAMD (will be remapped) */
   "glGetPerfMonitorGroupsAMD\0"
   /* _mesa_function_pool[29121]: GetPerfMonitorCountersAMD (will be remapped) */
   "glGetPerfMonitorCountersAMD\0"
   /* _mesa_function_pool[29149]: GetPerfMonitorGroupStringAMD (will be remapped) */
   "glGetPerfMonitorGroupStringAMD\0"
   /* _mesa_function_pool[29180]: GetPerfMonitorCounterStringAMD (will be remapped) */
   "glGetPerfMonitorCounterStringAMD\0"
   /* _mesa_function_pool[29213]: GetPerfMonitorCounterInfoAMD (will be remapped) */
   "glGetPerfMonitorCounterInfoAMD\0"
   /* _mesa_function_pool[29244]: GenPerfMonitorsAMD (will be remapped) */
   "glGenPerfMonitorsAMD\0"
   /* _mesa_function_pool[29265]: DeletePerfMonitorsAMD (will be remapped) */
   "glDeletePerfMonitorsAMD\0"
   /* _mesa_function_pool[29289]: SelectPerfMonitorCountersAMD (will be remapped) */
   "glSelectPerfMonitorCountersAMD\0"
   /* _mesa_function_pool[29320]: BeginPerfMonitorAMD (will be remapped) */
   "glBeginPerfMonitorAMD\0"
   /* _mesa_function_pool[29342]: EndPerfMonitorAMD (will be remapped) */
   "glEndPerfMonitorAMD\0"
   /* _mesa_function_pool[29362]: GetPerfMonitorCounterDataAMD (will be remapped) */
   "glGetPerfMonitorCounterDataAMD\0"
   /* _mesa_function_pool[29393]: TextureRangeAPPLE (dynamic) */
   "glTextureRangeAPPLE\0"
   /* _mesa_function_pool[29413]: GetTexParameterPointervAPPLE (dynamic) */
   "glGetTexParameterPointervAPPLE\0"
   /* _mesa_function_pool[29444]: UseShaderProgramEXT (will be remapped) */
   "glUseShaderProgramEXT\0"
   /* _mesa_function_pool[29466]: ActiveProgramEXT (will be remapped) */
   "glActiveProgramEXT\0"
   /* _mesa_function_pool[29485]: CreateShaderProgramEXT (will be remapped) */
   "glCreateShaderProgramEXT\0"
   /* _mesa_function_pool[29510]: CopyImageSubDataNV (will be remapped) */
   "glCopyImageSubDataNV\0"
   /* _mesa_function_pool[29531]: MatrixLoadfEXT (will be remapped) */
   "glMatrixLoadfEXT\0"
   /* _mesa_function_pool[29548]: MatrixLoaddEXT (will be remapped) */
   "glMatrixLoaddEXT\0"
   /* _mesa_function_pool[29565]: MatrixMultfEXT (will be remapped) */
   "glMatrixMultfEXT\0"
   /* _mesa_function_pool[29582]: MatrixMultdEXT (will be remapped) */
   "glMatrixMultdEXT\0"
   /* _mesa_function_pool[29599]: MatrixLoadIdentityEXT (will be remapped) */
   "glMatrixLoadIdentityEXT\0"
   /* _mesa_function_pool[29623]: MatrixRotatefEXT (will be remapped) */
   "glMatrixRotatefEXT\0"
   /* _mesa_function_pool[29642]: MatrixRotatedEXT (will be remapped) */
   "glMatrixRotatedEXT\0"
   /* _mesa_function_pool[29661]: MatrixScalefEXT (will be remapped) */
   "glMatrixScalefEXT\0"
   /* _mesa_function_pool[29679]: MatrixScaledEXT (will be remapped) */
   "glMatrixScaledEXT\0"
   /* _mesa_function_pool[29697]: MatrixTranslatefEXT (will be remapped) */
   "glMatrixTranslatefEXT\0"
   /* _mesa_function_pool[29719]: MatrixTranslatedEXT (will be remapped) */
   "glMatrixTranslatedEXT\0"
   /* _mesa_function_pool[29741]: MatrixOrthoEXT (will be remapped) */
   "glMatrixOrthoEXT\0"
   /* _mesa_function_pool[29758]: MatrixFrustumEXT (will be remapped) */
   "glMatrixFrustumEXT\0"
   /* _mesa_function_pool[29777]: MatrixPushEXT (will be remapped) */
   "glMatrixPushEXT\0"
   /* _mesa_function_pool[29793]: MatrixPopEXT (will be remapped) */
   "glMatrixPopEXT\0"
   /* _mesa_function_pool[29808]: ClientAttribDefaultEXT (will be remapped) */
   "glClientAttribDefaultEXT\0"
   /* _mesa_function_pool[29833]: PushClientAttribDefaultEXT (will be remapped) */
   "glPushClientAttribDefaultEXT\0"
   /* _mesa_function_pool[29862]: GetTextureParameterivEXT (will be remapped) */
   "glGetTextureParameterivEXT\0"
   /* _mesa_function_pool[29889]: GetTextureParameterfvEXT (will be remapped) */
   "glGetTextureParameterfvEXT\0"
   /* _mesa_function_pool[29916]: GetTextureLevelParameterivEXT (will be remapped) */
   "glGetTextureLevelParameterivEXT\0"
   /* _mesa_function_pool[29948]: GetTextureLevelParameterfvEXT (will be remapped) */
   "glGetTextureLevelParameterfvEXT\0"
   /* _mesa_function_pool[29980]: TextureParameteriEXT (will be remapped) */
   "glTextureParameteriEXT\0"
   /* _mesa_function_pool[30003]: TextureParameterivEXT (will be remapped) */
   "glTextureParameterivEXT\0"
   /* _mesa_function_pool[30027]: TextureParameterfEXT (will be remapped) */
   "glTextureParameterfEXT\0"
   /* _mesa_function_pool[30050]: TextureParameterfvEXT (will be remapped) */
   "glTextureParameterfvEXT\0"
   /* _mesa_function_pool[30074]: TextureImage1DEXT (will be remapped) */
   "glTextureImage1DEXT\0"
   /* _mesa_function_pool[30094]: TextureImage2DEXT (will be remapped) */
   "glTextureImage2DEXT\0"
   /* _mesa_function_pool[30114]: TextureImage3DEXT (will be remapped) */
   "glTextureImage3DEXT\0"
   /* _mesa_function_pool[30134]: TextureSubImage1DEXT (will be remapped) */
   "glTextureSubImage1DEXT\0"
   /* _mesa_function_pool[30157]: TextureSubImage2DEXT (will be remapped) */
   "glTextureSubImage2DEXT\0"
   /* _mesa_function_pool[30180]: TextureSubImage3DEXT (will be remapped) */
   "glTextureSubImage3DEXT\0"
   /* _mesa_function_pool[30203]: CopyTextureImage1DEXT (will be remapped) */
   "glCopyTextureImage1DEXT\0"
   /* _mesa_function_pool[30227]: CopyTextureImage2DEXT (will be remapped) */
   "glCopyTextureImage2DEXT\0"
   /* _mesa_function_pool[30251]: CopyTextureSubImage1DEXT (will be remapped) */
   "glCopyTextureSubImage1DEXT\0"
   /* _mesa_function_pool[30278]: CopyTextureSubImage2DEXT (will be remapped) */
   "glCopyTextureSubImage2DEXT\0"
   /* _mesa_function_pool[30305]: CopyTextureSubImage3DEXT (will be remapped) */
   "glCopyTextureSubImage3DEXT\0"
   /* _mesa_function_pool[30332]: GetTextureImageEXT (will be remapped) */
   "glGetTextureImageEXT\0"
   /* _mesa_function_pool[30353]: BindMultiTextureEXT (will be remapped) */
   "glBindMultiTextureEXT\0"
   /* _mesa_function_pool[30375]: EnableClientStateiEXT (will be remapped) */
   "glEnableClientStateIndexedEXT\0"
   /* _mesa_function_pool[30405]: DisableClientStateiEXT (will be remapped) */
   "glDisableClientStateIndexedEXT\0"
   /* _mesa_function_pool[30436]: GetPointerIndexedvEXT (will be remapped) */
   "glGetPointerIndexedvEXT\0"
   /* _mesa_function_pool[30460]: MultiTexEnviEXT (will be remapped) */
   "glMultiTexEnviEXT\0"
   /* _mesa_function_pool[30478]: MultiTexEnvivEXT (will be remapped) */
   "glMultiTexEnvivEXT\0"
   /* _mesa_function_pool[30497]: MultiTexEnvfEXT (will be remapped) */
   "glMultiTexEnvfEXT\0"
   /* _mesa_function_pool[30515]: MultiTexEnvfvEXT (will be remapped) */
   "glMultiTexEnvfvEXT\0"
   /* _mesa_function_pool[30534]: GetMultiTexEnvivEXT (will be remapped) */
   "glGetMultiTexEnvivEXT\0"
   /* _mesa_function_pool[30556]: GetMultiTexEnvfvEXT (will be remapped) */
   "glGetMultiTexEnvfvEXT\0"
   /* _mesa_function_pool[30578]: MultiTexParameteriEXT (will be remapped) */
   "glMultiTexParameteriEXT\0"
   /* _mesa_function_pool[30602]: MultiTexParameterivEXT (will be remapped) */
   "glMultiTexParameterivEXT\0"
   /* _mesa_function_pool[30627]: MultiTexParameterfEXT (will be remapped) */
   "glMultiTexParameterfEXT\0"
   /* _mesa_function_pool[30651]: MultiTexParameterfvEXT (will be remapped) */
   "glMultiTexParameterfvEXT\0"
   /* _mesa_function_pool[30676]: GetMultiTexParameterivEXT (will be remapped) */
   "glGetMultiTexParameterivEXT\0"
   /* _mesa_function_pool[30704]: GetMultiTexParameterfvEXT (will be remapped) */
   "glGetMultiTexParameterfvEXT\0"
   /* _mesa_function_pool[30732]: GetMultiTexImageEXT (will be remapped) */
   "glGetMultiTexImageEXT\0"
   /* _mesa_function_pool[30754]: GetMultiTexLevelParameterivEXT (will be remapped) */
   "glGetMultiTexLevelParameterivEXT\0"
   /* _mesa_function_pool[30787]: GetMultiTexLevelParameterfvEXT (will be remapped) */
   "glGetMultiTexLevelParameterfvEXT\0"
   /* _mesa_function_pool[30820]: MultiTexImage1DEXT (will be remapped) */
   "glMultiTexImage1DEXT\0"
   /* _mesa_function_pool[30841]: MultiTexImage2DEXT (will be remapped) */
   "glMultiTexImage2DEXT\0"
   /* _mesa_function_pool[30862]: MultiTexImage3DEXT (will be remapped) */
   "glMultiTexImage3DEXT\0"
   /* _mesa_function_pool[30883]: MultiTexSubImage1DEXT (will be remapped) */
   "glMultiTexSubImage1DEXT\0"
   /* _mesa_function_pool[30907]: MultiTexSubImage2DEXT (will be remapped) */
   "glMultiTexSubImage2DEXT\0"
   /* _mesa_function_pool[30931]: MultiTexSubImage3DEXT (will be remapped) */
   "glMultiTexSubImage3DEXT\0"
   /* _mesa_function_pool[30955]: CopyMultiTexImage1DEXT (will be remapped) */
   "glCopyMultiTexImage1DEXT\0"
   /* _mesa_function_pool[30980]: CopyMultiTexImage2DEXT (will be remapped) */
   "glCopyMultiTexImage2DEXT\0"
   /* _mesa_function_pool[31005]: CopyMultiTexSubImage1DEXT (will be remapped) */
   "glCopyMultiTexSubImage1DEXT\0"
   /* _mesa_function_pool[31033]: CopyMultiTexSubImage2DEXT (will be remapped) */
   "glCopyMultiTexSubImage2DEXT\0"
   /* _mesa_function_pool[31061]: CopyMultiTexSubImage3DEXT (will be remapped) */
   "glCopyMultiTexSubImage3DEXT\0"
   /* _mesa_function_pool[31089]: MultiTexGendEXT (will be remapped) */
   "glMultiTexGendEXT\0"
   /* _mesa_function_pool[31107]: MultiTexGendvEXT (will be remapped) */
   "glMultiTexGendvEXT\0"
   /* _mesa_function_pool[31126]: MultiTexGenfEXT (will be remapped) */
   "glMultiTexGenfEXT\0"
   /* _mesa_function_pool[31144]: MultiTexGenfvEXT (will be remapped) */
   "glMultiTexGenfvEXT\0"
   /* _mesa_function_pool[31163]: MultiTexGeniEXT (will be remapped) */
   "glMultiTexGeniEXT\0"
   /* _mesa_function_pool[31181]: MultiTexGenivEXT (will be remapped) */
   "glMultiTexGenivEXT\0"
   /* _mesa_function_pool[31200]: GetMultiTexGendvEXT (will be remapped) */
   "glGetMultiTexGendvEXT\0"
   /* _mesa_function_pool[31222]: GetMultiTexGenfvEXT (will be remapped) */
   "glGetMultiTexGenfvEXT\0"
   /* _mesa_function_pool[31244]: GetMultiTexGenivEXT (will be remapped) */
   "glGetMultiTexGenivEXT\0"
   /* _mesa_function_pool[31266]: MultiTexCoordPointerEXT (will be remapped) */
   "glMultiTexCoordPointerEXT\0"
   /* _mesa_function_pool[31292]: MatrixLoadTransposefEXT (will be remapped) */
   "glMatrixLoadTransposefEXT\0"
   /* _mesa_function_pool[31318]: MatrixLoadTransposedEXT (will be remapped) */
   "glMatrixLoadTransposedEXT\0"
   /* _mesa_function_pool[31344]: MatrixMultTransposefEXT (will be remapped) */
   "glMatrixMultTransposefEXT\0"
   /* _mesa_function_pool[31370]: MatrixMultTransposedEXT (will be remapped) */
   "glMatrixMultTransposedEXT\0"
   /* _mesa_function_pool[31396]: CompressedTextureImage1DEXT (will be remapped) */
   "glCompressedTextureImage1DEXT\0"
   /* _mesa_function_pool[31426]: CompressedTextureImage2DEXT (will be remapped) */
   "glCompressedTextureImage2DEXT\0"
   /* _mesa_function_pool[31456]: CompressedTextureImage3DEXT (will be remapped) */
   "glCompressedTextureImage3DEXT\0"
   /* _mesa_function_pool[31486]: CompressedTextureSubImage1DEXT (will be remapped) */
   "glCompressedTextureSubImage1DEXT\0"
   /* _mesa_function_pool[31519]: CompressedTextureSubImage2DEXT (will be remapped) */
   "glCompressedTextureSubImage2DEXT\0"
   /* _mesa_function_pool[31552]: CompressedTextureSubImage3DEXT (will be remapped) */
   "glCompressedTextureSubImage3DEXT\0"
   /* _mesa_function_pool[31585]: GetCompressedTextureImageEXT (will be remapped) */
   "glGetCompressedTextureImageEXT\0"
   /* _mesa_function_pool[31616]: CompressedMultiTexImage1DEXT (will be remapped) */
   "glCompressedMultiTexImage1DEXT\0"
   /* _mesa_function_pool[31647]: CompressedMultiTexImage2DEXT (will be remapped) */
   "glCompressedMultiTexImage2DEXT\0"
   /* _mesa_function_pool[31678]: CompressedMultiTexImage3DEXT (will be remapped) */
   "glCompressedMultiTexImage3DEXT\0"
   /* _mesa_function_pool[31709]: CompressedMultiTexSubImage1DEXT (will be remapped) */
   "glCompressedMultiTexSubImage1DEXT\0"
   /* _mesa_function_pool[31743]: CompressedMultiTexSubImage2DEXT (will be remapped) */
   "glCompressedMultiTexSubImage2DEXT\0"
   /* _mesa_function_pool[31777]: CompressedMultiTexSubImage3DEXT (will be remapped) */
   "glCompressedMultiTexSubImage3DEXT\0"
   /* _mesa_function_pool[31811]: GetCompressedMultiTexImageEXT (will be remapped) */
   "glGetCompressedMultiTexImageEXT\0"
   /* _mesa_function_pool[31843]: NamedBufferDataEXT (will be remapped) */
   "glNamedBufferDataEXT\0"
   /* _mesa_function_pool[31864]: NamedBufferSubDataEXT (will be remapped) */
   "glNamedBufferSubDataEXT\0"
   /* _mesa_function_pool[31888]: MapNamedBufferEXT (will be remapped) */
   "glMapNamedBufferEXT\0"
   /* _mesa_function_pool[31908]: GetNamedBufferSubDataEXT (will be remapped) */
   "glGetNamedBufferSubDataEXT\0"
   /* _mesa_function_pool[31935]: GetNamedBufferPointervEXT (will be remapped) */
   "glGetNamedBufferPointervEXT\0"
   /* _mesa_function_pool[31963]: GetNamedBufferParameterivEXT (will be remapped) */
   "glGetNamedBufferParameterivEXT\0"
   /* _mesa_function_pool[31994]: FlushMappedNamedBufferRangeEXT (will be remapped) */
   "glFlushMappedNamedBufferRangeEXT\0"
   /* _mesa_function_pool[32027]: MapNamedBufferRangeEXT (will be remapped) */
   "glMapNamedBufferRangeEXT\0"
   /* _mesa_function_pool[32052]: FramebufferDrawBufferEXT (will be remapped) */
   "glFramebufferDrawBufferEXT\0"
   /* _mesa_function_pool[32079]: FramebufferDrawBuffersEXT (will be remapped) */
   "glFramebufferDrawBuffersEXT\0"
   /* _mesa_function_pool[32107]: FramebufferReadBufferEXT (will be remapped) */
   "glFramebufferReadBufferEXT\0"
   /* _mesa_function_pool[32134]: GetFramebufferParameterivEXT (will be remapped) */
   "glGetFramebufferParameterivEXT\0"
   /* _mesa_function_pool[32165]: CheckNamedFramebufferStatusEXT (will be remapped) */
   "glCheckNamedFramebufferStatusEXT\0"
   /* _mesa_function_pool[32198]: NamedFramebufferTexture1DEXT (will be remapped) */
   "glNamedFramebufferTexture1DEXT\0"
   /* _mesa_function_pool[32229]: NamedFramebufferTexture2DEXT (will be remapped) */
   "glNamedFramebufferTexture2DEXT\0"
   /* _mesa_function_pool[32260]: NamedFramebufferTexture3DEXT (will be remapped) */
   "glNamedFramebufferTexture3DEXT\0"
   /* _mesa_function_pool[32291]: NamedFramebufferRenderbufferEXT (will be remapped) */
   "glNamedFramebufferRenderbufferEXT\0"
   /* _mesa_function_pool[32325]: GetNamedFramebufferAttachmentParameterivEXT (will be remapped) */
   "glGetNamedFramebufferAttachmentParameterivEXT\0"
   /* _mesa_function_pool[32371]: NamedRenderbufferStorageEXT (will be remapped) */
   "glNamedRenderbufferStorageEXT\0"
   /* _mesa_function_pool[32401]: GetNamedRenderbufferParameterivEXT (will be remapped) */
   "glGetNamedRenderbufferParameterivEXT\0"
   /* _mesa_function_pool[32438]: GenerateTextureMipmapEXT (will be remapped) */
   "glGenerateTextureMipmapEXT\0"
   /* _mesa_function_pool[32465]: GenerateMultiTexMipmapEXT (will be remapped) */
   "glGenerateMultiTexMipmapEXT\0"
   /* _mesa_function_pool[32493]: NamedRenderbufferStorageMultisampleEXT (will be remapped) */
   "glNamedRenderbufferStorageMultisampleEXT\0"
   /* _mesa_function_pool[32534]: NamedCopyBufferSubDataEXT (will be remapped) */
   "glNamedCopyBufferSubDataEXT\0"
   /* _mesa_function_pool[32562]: VertexArrayVertexOffsetEXT (will be remapped) */
   "glVertexArrayVertexOffsetEXT\0"
   /* _mesa_function_pool[32591]: VertexArrayColorOffsetEXT (will be remapped) */
   "glVertexArrayColorOffsetEXT\0"
   /* _mesa_function_pool[32619]: VertexArrayEdgeFlagOffsetEXT (will be remapped) */
   "glVertexArrayEdgeFlagOffsetEXT\0"
   /* _mesa_function_pool[32650]: VertexArrayIndexOffsetEXT (will be remapped) */
   "glVertexArrayIndexOffsetEXT\0"
   /* _mesa_function_pool[32678]: VertexArrayNormalOffsetEXT (will be remapped) */
   "glVertexArrayNormalOffsetEXT\0"
   /* _mesa_function_pool[32707]: VertexArrayTexCoordOffsetEXT (will be remapped) */
   "glVertexArrayTexCoordOffsetEXT\0"
   /* _mesa_function_pool[32738]: VertexArrayMultiTexCoordOffsetEXT (will be remapped) */
   "glVertexArrayMultiTexCoordOffsetEXT\0"
   /* _mesa_function_pool[32774]: VertexArrayFogCoordOffsetEXT (will be remapped) */
   "glVertexArrayFogCoordOffsetEXT\0"
   /* _mesa_function_pool[32805]: VertexArraySecondaryColorOffsetEXT (will be remapped) */
   "glVertexArraySecondaryColorOffsetEXT\0"
   /* _mesa_function_pool[32842]: VertexArrayVertexAttribOffsetEXT (will be remapped) */
   "glVertexArrayVertexAttribOffsetEXT\0"
   /* _mesa_function_pool[32877]: VertexArrayVertexAttribIOffsetEXT (will be remapped) */
   "glVertexArrayVertexAttribIOffsetEXT\0"
   /* _mesa_function_pool[32913]: EnableVertexArrayEXT (will be remapped) */
   "glEnableVertexArrayEXT\0"
   /* _mesa_function_pool[32936]: DisableVertexArrayEXT (will be remapped) */
   "glDisableVertexArrayEXT\0"
   /* _mesa_function_pool[32960]: EnableVertexArrayAttribEXT (will be remapped) */
   "glEnableVertexArrayAttribEXT\0"
   /* _mesa_function_pool[32989]: DisableVertexArrayAttribEXT (will be remapped) */
   "glDisableVertexArrayAttribEXT\0"
   /* _mesa_function_pool[33019]: GetVertexArrayIntegervEXT (will be remapped) */
   "glGetVertexArrayIntegervEXT\0"
   /* _mesa_function_pool[33047]: GetVertexArrayPointervEXT (will be remapped) */
   "glGetVertexArrayPointervEXT\0"
   /* _mesa_function_pool[33075]: GetVertexArrayIntegeri_vEXT (will be remapped) */
   "glGetVertexArrayIntegeri_vEXT\0"
   /* _mesa_function_pool[33105]: GetVertexArrayPointeri_vEXT (will be remapped) */
   "glGetVertexArrayPointeri_vEXT\0"
   /* _mesa_function_pool[33135]: NamedProgramStringEXT (will be remapped) */
   "glNamedProgramStringEXT\0"
   /* _mesa_function_pool[33159]: GetNamedProgramStringEXT (will be remapped) */
   "glGetNamedProgramStringEXT\0"
   /* _mesa_function_pool[33186]: NamedProgramLocalParameter4fEXT (will be remapped) */
   "glNamedProgramLocalParameter4fEXT\0"
   /* _mesa_function_pool[33220]: NamedProgramLocalParameter4fvEXT (will be remapped) */
   "glNamedProgramLocalParameter4fvEXT\0"
   /* _mesa_function_pool[33255]: GetNamedProgramLocalParameterfvEXT (will be remapped) */
   "glGetNamedProgramLocalParameterfvEXT\0"
   /* _mesa_function_pool[33292]: NamedProgramLocalParameter4dEXT (will be remapped) */
   "glNamedProgramLocalParameter4dEXT\0"
   /* _mesa_function_pool[33326]: NamedProgramLocalParameter4dvEXT (will be remapped) */
   "glNamedProgramLocalParameter4dvEXT\0"
   /* _mesa_function_pool[33361]: GetNamedProgramLocalParameterdvEXT (will be remapped) */
   "glGetNamedProgramLocalParameterdvEXT\0"
   /* _mesa_function_pool[33398]: GetNamedProgramivEXT (will be remapped) */
   "glGetNamedProgramivEXT\0"
   /* _mesa_function_pool[33421]: TextureBufferEXT (will be remapped) */
   "glTextureBufferEXT\0"
   /* _mesa_function_pool[33440]: MultiTexBufferEXT (will be remapped) */
   "glMultiTexBufferEXT\0"
   /* _mesa_function_pool[33460]: TextureParameterIivEXT (will be remapped) */
   "glTextureParameterIivEXT\0"
   /* _mesa_function_pool[33485]: TextureParameterIuivEXT (will be remapped) */
   "glTextureParameterIuivEXT\0"
   /* _mesa_function_pool[33511]: GetTextureParameterIivEXT (will be remapped) */
   "glGetTextureParameterIivEXT\0"
   /* _mesa_function_pool[33539]: GetTextureParameterIuivEXT (will be remapped) */
   "glGetTextureParameterIuivEXT\0"
   /* _mesa_function_pool[33568]: MultiTexParameterIivEXT (will be remapped) */
   "glMultiTexParameterIivEXT\0"
   /* _mesa_function_pool[33594]: MultiTexParameterIuivEXT (will be remapped) */
   "glMultiTexParameterIuivEXT\0"
   /* _mesa_function_pool[33621]: GetMultiTexParameterIivEXT (will be remapped) */
   "glGetMultiTexParameterIivEXT\0"
   /* _mesa_function_pool[33650]: GetMultiTexParameterIuivEXT (will be remapped) */
   "glGetMultiTexParameterIuivEXT\0"
   /* _mesa_function_pool[33680]: NamedProgramLocalParameters4fvEXT (will be remapped) */
   "glNamedProgramLocalParameters4fvEXT\0"
   /* _mesa_function_pool[33716]: BindImageTextureEXT (will be remapped) */
   "glBindImageTextureEXT\0"
   /* _mesa_function_pool[33738]: LabelObjectEXT (will be remapped) */
   "glLabelObjectEXT\0"
   /* _mesa_function_pool[33755]: GetObjectLabelEXT (will be remapped) */
   "glGetObjectLabelEXT\0"
   /* _mesa_function_pool[33775]: SubpixelPrecisionBiasNV (will be remapped) */
   "glSubpixelPrecisionBiasNV\0"
   /* _mesa_function_pool[33801]: ConservativeRasterParameterfNV (will be remapped) */
   "glConservativeRasterParameterfNV\0"
   /* _mesa_function_pool[33834]: ConservativeRasterParameteriNV (will be remapped) */
   "glConservativeRasterParameteriNV\0"
   /* _mesa_function_pool[33867]: GetFirstPerfQueryIdINTEL (will be remapped) */
   "glGetFirstPerfQueryIdINTEL\0"
   /* _mesa_function_pool[33894]: GetNextPerfQueryIdINTEL (will be remapped) */
   "glGetNextPerfQueryIdINTEL\0"
   /* _mesa_function_pool[33920]: GetPerfQueryIdByNameINTEL (will be remapped) */
   "glGetPerfQueryIdByNameINTEL\0"
   /* _mesa_function_pool[33948]: GetPerfQueryInfoINTEL (will be remapped) */
   "glGetPerfQueryInfoINTEL\0"
   /* _mesa_function_pool[33972]: GetPerfCounterInfoINTEL (will be remapped) */
   "glGetPerfCounterInfoINTEL\0"
   /* _mesa_function_pool[33998]: CreatePerfQueryINTEL (will be remapped) */
   "glCreatePerfQueryINTEL\0"
   /* _mesa_function_pool[34021]: DeletePerfQueryINTEL (will be remapped) */
   "glDeletePerfQueryINTEL\0"
   /* _mesa_function_pool[34044]: BeginPerfQueryINTEL (will be remapped) */
   "glBeginPerfQueryINTEL\0"
   /* _mesa_function_pool[34066]: EndPerfQueryINTEL (will be remapped) */
   "glEndPerfQueryINTEL\0"
   /* _mesa_function_pool[34086]: GetPerfQueryDataINTEL (will be remapped) */
   "glGetPerfQueryDataINTEL\0"
   /* _mesa_function_pool[34110]: AlphaToCoverageDitherControlNV (will be remapped) */
   "glAlphaToCoverageDitherControlNV\0"
   /* _mesa_function_pool[34143]: PolygonOffsetClampEXT (will be remapped) */
   "glPolygonOffsetClampEXT\0"
   /* _mesa_function_pool[34167]: WindowRectanglesEXT (will be remapped) */
   "glWindowRectanglesEXT\0"
   /* _mesa_function_pool[34189]: FramebufferFetchBarrierEXT (will be remapped) */
   "glFramebufferFetchBarrierEXT\0"
   /* _mesa_function_pool[34218]: RenderbufferStorageMultisampleAdvancedAMD (will be remapped) */
   "glRenderbufferStorageMultisampleAdvancedAMD\0"
   /* _mesa_function_pool[34262]: NamedRenderbufferStorageMultisampleAdvancedAMD (will be remapped) */
   "glNamedRenderbufferStorageMultisampleAdvancedAMD\0"
   /* _mesa_function_pool[34311]: StencilFuncSeparateATI (will be remapped) */
   "glStencilFuncSeparateATI\0"
   /* _mesa_function_pool[34336]: ProgramEnvParameters4fvEXT (will be remapped) */
   "glProgramEnvParameters4fvEXT\0"
   /* _mesa_function_pool[34365]: ProgramLocalParameters4fvEXT (will be remapped) */
   "glProgramLocalParameters4fvEXT\0"
   /* _mesa_function_pool[34396]: IglooInterfaceSGIX (dynamic) */
   "glIglooInterfaceSGIX\0"
   /* _mesa_function_pool[34417]: DeformationMap3dSGIX (dynamic) */
   "glDeformationMap3dSGIX\0"
   /* _mesa_function_pool[34440]: DeformationMap3fSGIX (dynamic) */
   "glDeformationMap3fSGIX\0"
   /* _mesa_function_pool[34463]: DeformSGIX (dynamic) */
   "glDeformSGIX\0"
   /* _mesa_function_pool[34476]: LoadIdentityDeformationMapSGIX (dynamic) */
   "glLoadIdentityDeformationMapSGIX\0"
   /* _mesa_function_pool[34509]: InternalBufferSubDataCopyMESA (will be remapped) */
   "glInternalBufferSubDataCopyMESA\0"
   /* _mesa_function_pool[34541]: InternalSetError (will be remapped) */
   "glInternalSetError\0"
   /* _mesa_function_pool[34560]: DrawArraysUserBuf (will be remapped) */
   "glDrawArraysUserBuf\0"
   /* _mesa_function_pool[34580]: DrawElementsUserBuf (will be remapped) */
   "glDrawElementsUserBuf\0"
   /* _mesa_function_pool[34602]: MultiDrawArraysUserBuf (will be remapped) */
   "glMultiDrawArraysUserBuf\0"
   /* _mesa_function_pool[34627]: MultiDrawElementsUserBuf (will be remapped) */
   "glMultiDrawElementsUserBuf\0"
   /* _mesa_function_pool[34654]: DrawArraysInstancedBaseInstanceDrawID (will be remapped) */
   "glDrawArraysInstancedBaseInstanceDrawID\0"
   /* _mesa_function_pool[34694]: DrawElementsInstancedBaseVertexBaseInstanceDrawID (will be remapped) */
   "glDrawElementsInstancedBaseVertexBaseInstanceDrawID\0"
   /* _mesa_function_pool[34746]: InternalInvalidateFramebufferAncillaryMESA (will be remapped) */
   "glInternalInvalidateFramebufferAncillaryMESA\0"
   /* _mesa_function_pool[34791]: EGLImageTargetTexture2DOES (will be remapped) */
   "glEGLImageTargetTexture2DOES\0"
   /* _mesa_function_pool[34820]: EGLImageTargetRenderbufferStorageOES (will be remapped) */
   "glEGLImageTargetRenderbufferStorageOES\0"
   /* _mesa_function_pool[34859]: EGLImageTargetTexStorageEXT (will be remapped) */
   "glEGLImageTargetTexStorageEXT\0"
   /* _mesa_function_pool[34889]: EGLImageTargetTextureStorageEXT (will be remapped) */
   "glEGLImageTargetTextureStorageEXT\0"
   /* _mesa_function_pool[34923]: ClearColorIiEXT (will be remapped) */
   "glClearColorIiEXT\0"
   /* _mesa_function_pool[34941]: ClearColorIuiEXT (will be remapped) */
   "glClearColorIuiEXT\0"
   /* _mesa_function_pool[34960]: TexParameterIiv (will be remapped) */
   "glTexParameterIivEXT\0"
   /* _mesa_function_pool[34981]: TexParameterIuiv (will be remapped) */
   "glTexParameterIuivEXT\0"
   /* _mesa_function_pool[35003]: GetTexParameterIiv (will be remapped) */
   "glGetTexParameterIivEXT\0"
   /* _mesa_function_pool[35027]: GetTexParameterIuiv (will be remapped) */
   "glGetTexParameterIuivEXT\0"
   /* _mesa_function_pool[35052]: VertexAttribI1iEXT (will be remapped) */
   "glVertexAttribI1iEXT\0"
   /* _mesa_function_pool[35073]: VertexAttribI2iEXT (will be remapped) */
   "glVertexAttribI2iEXT\0"
   /* _mesa_function_pool[35094]: VertexAttribI3iEXT (will be remapped) */
   "glVertexAttribI3iEXT\0"
   /* _mesa_function_pool[35115]: VertexAttribI4iEXT (will be remapped) */
   "glVertexAttribI4iEXT\0"
   /* _mesa_function_pool[35136]: VertexAttribI1uiEXT (will be remapped) */
   "glVertexAttribI1uiEXT\0"
   /* _mesa_function_pool[35158]: VertexAttribI2uiEXT (will be remapped) */
   "glVertexAttribI2uiEXT\0"
   /* _mesa_function_pool[35180]: VertexAttribI3uiEXT (will be remapped) */
   "glVertexAttribI3uiEXT\0"
   /* _mesa_function_pool[35202]: VertexAttribI4uiEXT (will be remapped) */
   "glVertexAttribI4uiEXT\0"
   /* _mesa_function_pool[35224]: VertexAttribI1iv (will be remapped) */
   "glVertexAttribI1ivEXT\0"
   /* _mesa_function_pool[35246]: VertexAttribI2ivEXT (will be remapped) */
   "glVertexAttribI2ivEXT\0"
   /* _mesa_function_pool[35268]: VertexAttribI3ivEXT (will be remapped) */
   "glVertexAttribI3ivEXT\0"
   /* _mesa_function_pool[35290]: VertexAttribI4ivEXT (will be remapped) */
   "glVertexAttribI4ivEXT\0"
   /* _mesa_function_pool[35312]: VertexAttribI1uiv (will be remapped) */
   "glVertexAttribI1uivEXT\0"
   /* _mesa_function_pool[35335]: VertexAttribI2uivEXT (will be remapped) */
   "glVertexAttribI2uivEXT\0"
   /* _mesa_function_pool[35358]: VertexAttribI3uivEXT (will be remapped) */
   "glVertexAttribI3uivEXT\0"
   /* _mesa_function_pool[35381]: VertexAttribI4uivEXT (will be remapped) */
   "glVertexAttribI4uivEXT\0"
   /* _mesa_function_pool[35404]: VertexAttribI4bv (will be remapped) */
   "glVertexAttribI4bvEXT\0"
   /* _mesa_function_pool[35426]: VertexAttribI4sv (will be remapped) */
   "glVertexAttribI4svEXT\0"
   /* _mesa_function_pool[35448]: VertexAttribI4ubv (will be remapped) */
   "glVertexAttribI4ubvEXT\0"
   /* _mesa_function_pool[35471]: VertexAttribI4usv (will be remapped) */
   "glVertexAttribI4usvEXT\0"
   /* _mesa_function_pool[35494]: VertexAttribIPointer (will be remapped) */
   "glVertexAttribIPointerEXT\0"
   /* _mesa_function_pool[35520]: GetVertexAttribIiv (will be remapped) */
   "glGetVertexAttribIivEXT\0"
   /* _mesa_function_pool[35544]: GetVertexAttribIuiv (will be remapped) */
   "glGetVertexAttribIuivEXT\0"
   /* _mesa_function_pool[35569]: Uniform1ui (will be remapped) */
   "glUniform1uiEXT\0"
   /* _mesa_function_pool[35585]: Uniform2ui (will be remapped) */
   "glUniform2uiEXT\0"
   /* _mesa_function_pool[35601]: Uniform3ui (will be remapped) */
   "glUniform3uiEXT\0"
   /* _mesa_function_pool[35617]: Uniform4ui (will be remapped) */
   "glUniform4uiEXT\0"
   /* _mesa_function_pool[35633]: Uniform1uiv (will be remapped) */
   "glUniform1uivEXT\0"
   /* _mesa_function_pool[35650]: Uniform2uiv (will be remapped) */
   "glUniform2uivEXT\0"
   /* _mesa_function_pool[35667]: Uniform3uiv (will be remapped) */
   "glUniform3uivEXT\0"
   /* _mesa_function_pool[35684]: Uniform4uiv (will be remapped) */
   "glUniform4uivEXT\0"
   /* _mesa_function_pool[35701]: GetUniformuiv (will be remapped) */
   "glGetUniformuivEXT\0"
   /* _mesa_function_pool[35720]: BindFragDataLocation (will be remapped) */
   "glBindFragDataLocationEXT\0"
   /* _mesa_function_pool[35746]: GetFragDataLocation (will be remapped) */
   "glGetFragDataLocationEXT\0"
   /* _mesa_function_pool[35771]: ClearBufferiv (will be remapped) */
   "glClearBufferiv\0"
   /* _mesa_function_pool[35787]: ClearBufferuiv (will be remapped) */
   "glClearBufferuiv\0"
   /* _mesa_function_pool[35804]: ClearBufferfv (will be remapped) */
   "glClearBufferfv\0"
   /* _mesa_function_pool[35820]: ClearBufferfi (will be remapped) */
   "glClearBufferfi\0"
   /* _mesa_function_pool[35836]: GetStringi (will be remapped) */
   "glGetStringi\0"
   /* _mesa_function_pool[35849]: BeginTransformFeedback (will be remapped) */
   "glBeginTransformFeedback\0"
   /* _mesa_function_pool[35874]: EndTransformFeedback (will be remapped) */
   "glEndTransformFeedback\0"
   /* _mesa_function_pool[35897]: BindBufferRange (will be remapped) */
   "glBindBufferRange\0"
   /* _mesa_function_pool[35915]: BindBufferBase (will be remapped) */
   "glBindBufferBase\0"
   /* _mesa_function_pool[35932]: TransformFeedbackVaryings (will be remapped) */
   "glTransformFeedbackVaryings\0"
   /* _mesa_function_pool[35960]: GetTransformFeedbackVarying (will be remapped) */
   "glGetTransformFeedbackVarying\0"
   /* _mesa_function_pool[35990]: BeginConditionalRender (will be remapped) */
   "glBeginConditionalRender\0"
   /* _mesa_function_pool[36015]: EndConditionalRender (will be remapped) */
   "glEndConditionalRender\0"
   /* _mesa_function_pool[36038]: PrimitiveRestartIndex (will be remapped) */
   "glPrimitiveRestartIndex\0"
   /* _mesa_function_pool[36062]: GetInteger64i_v (will be remapped) */
   "glGetInteger64i_v\0"
   /* _mesa_function_pool[36080]: GetBufferParameteri64v (will be remapped) */
   "glGetBufferParameteri64v\0"
   /* _mesa_function_pool[36105]: FramebufferTexture (will be remapped) */
   "glFramebufferTexture\0"
   /* _mesa_function_pool[36126]: PrimitiveRestartNV (will be remapped) */
   "glPrimitiveRestartNV\0"
   /* _mesa_function_pool[36147]: BindBufferOffsetEXT (will be remapped) */
   "glBindBufferOffsetEXT\0"
   /* _mesa_function_pool[36169]: BindTransformFeedback (will be remapped) */
   "glBindTransformFeedback\0"
   /* _mesa_function_pool[36193]: DeleteTransformFeedbacks (will be remapped) */
   "glDeleteTransformFeedbacks\0"
   /* _mesa_function_pool[36220]: GenTransformFeedbacks (will be remapped) */
   "glGenTransformFeedbacks\0"
   /* _mesa_function_pool[36244]: IsTransformFeedback (will be remapped) */
   "glIsTransformFeedback\0"
   /* _mesa_function_pool[36266]: PauseTransformFeedback (will be remapped) */
   "glPauseTransformFeedback\0"
   /* _mesa_function_pool[36291]: ResumeTransformFeedback (will be remapped) */
   "glResumeTransformFeedback\0"
   /* _mesa_function_pool[36317]: DrawTransformFeedback (will be remapped) */
   "glDrawTransformFeedback\0"
   /* _mesa_function_pool[36341]: VDPAUInitNV (will be remapped) */
   "glVDPAUInitNV\0"
   /* _mesa_function_pool[36355]: VDPAUFiniNV (will be remapped) */
   "glVDPAUFiniNV\0"
   /* _mesa_function_pool[36369]: VDPAURegisterVideoSurfaceNV (will be remapped) */
   "glVDPAURegisterVideoSurfaceNV\0"
   /* _mesa_function_pool[36399]: VDPAURegisterOutputSurfaceNV (will be remapped) */
   "glVDPAURegisterOutputSurfaceNV\0"
   /* _mesa_function_pool[36430]: VDPAUIsSurfaceNV (will be remapped) */
   "glVDPAUIsSurfaceNV\0"
   /* _mesa_function_pool[36449]: VDPAUUnregisterSurfaceNV (will be remapped) */
   "glVDPAUUnregisterSurfaceNV\0"
   /* _mesa_function_pool[36476]: VDPAUGetSurfaceivNV (will be remapped) */
   "glVDPAUGetSurfaceivNV\0"
   /* _mesa_function_pool[36498]: VDPAUSurfaceAccessNV (will be remapped) */
   "glVDPAUSurfaceAccessNV\0"
   /* _mesa_function_pool[36521]: VDPAUMapSurfacesNV (will be remapped) */
   "glVDPAUMapSurfacesNV\0"
   /* _mesa_function_pool[36542]: VDPAUUnmapSurfacesNV (will be remapped) */
   "glVDPAUUnmapSurfacesNV\0"
   /* _mesa_function_pool[36565]: GetUnsignedBytevEXT (will be remapped) */
   "glGetUnsignedBytevEXT\0"
   /* _mesa_function_pool[36587]: GetUnsignedBytei_vEXT (will be remapped) */
   "glGetUnsignedBytei_vEXT\0"
   /* _mesa_function_pool[36611]: DeleteMemoryObjectsEXT (will be remapped) */
   "glDeleteMemoryObjectsEXT\0"
   /* _mesa_function_pool[36636]: IsMemoryObjectEXT (will be remapped) */
   "glIsMemoryObjectEXT\0"
   /* _mesa_function_pool[36656]: CreateMemoryObjectsEXT (will be remapped) */
   "glCreateMemoryObjectsEXT\0"
   /* _mesa_function_pool[36681]: MemoryObjectParameterivEXT (will be remapped) */
   "glMemoryObjectParameterivEXT\0"
   /* _mesa_function_pool[36710]: GetMemoryObjectParameterivEXT (will be remapped) */
   "glGetMemoryObjectParameterivEXT\0"
   /* _mesa_function_pool[36742]: TexStorageMem2DEXT (will be remapped) */
   "glTexStorageMem2DEXT\0"
   /* _mesa_function_pool[36763]: TexStorageMem2DMultisampleEXT (will be remapped) */
   "glTexStorageMem2DMultisampleEXT\0"
   /* _mesa_function_pool[36795]: TexStorageMem3DEXT (will be remapped) */
   "glTexStorageMem3DEXT\0"
   /* _mesa_function_pool[36816]: TexStorageMem3DMultisampleEXT (will be remapped) */
   "glTexStorageMem3DMultisampleEXT\0"
   /* _mesa_function_pool[36848]: BufferStorageMemEXT (will be remapped) */
   "glBufferStorageMemEXT\0"
   /* _mesa_function_pool[36870]: TextureStorageMem2DEXT (will be remapped) */
   "glTextureStorageMem2DEXT\0"
   /* _mesa_function_pool[36895]: TextureStorageMem2DMultisampleEXT (will be remapped) */
   "glTextureStorageMem2DMultisampleEXT\0"
   /* _mesa_function_pool[36931]: TextureStorageMem3DEXT (will be remapped) */
   "glTextureStorageMem3DEXT\0"
   /* _mesa_function_pool[36956]: TextureStorageMem3DMultisampleEXT (will be remapped) */
   "glTextureStorageMem3DMultisampleEXT\0"
   /* _mesa_function_pool[36992]: NamedBufferStorageMemEXT (will be remapped) */
   "glNamedBufferStorageMemEXT\0"
   /* _mesa_function_pool[37019]: TexStorageMem1DEXT (will be remapped) */
   "glTexStorageMem1DEXT\0"
   /* _mesa_function_pool[37040]: TextureStorageMem1DEXT (will be remapped) */
   "glTextureStorageMem1DEXT\0"
   /* _mesa_function_pool[37065]: GenSemaphoresEXT (will be remapped) */
   "glGenSemaphoresEXT\0"
   /* _mesa_function_pool[37084]: DeleteSemaphoresEXT (will be remapped) */
   "glDeleteSemaphoresEXT\0"
   /* _mesa_function_pool[37106]: IsSemaphoreEXT (will be remapped) */
   "glIsSemaphoreEXT\0"
   /* _mesa_function_pool[37123]: SemaphoreParameterui64vEXT (will be remapped) */
   "glSemaphoreParameterui64vEXT\0"
   /* _mesa_function_pool[37152]: GetSemaphoreParameterui64vEXT (will be remapped) */
   "glGetSemaphoreParameterui64vEXT\0"
   /* _mesa_function_pool[37184]: WaitSemaphoreEXT (will be remapped) */
   "glWaitSemaphoreEXT\0"
   /* _mesa_function_pool[37203]: SignalSemaphoreEXT (will be remapped) */
   "glSignalSemaphoreEXT\0"
   /* _mesa_function_pool[37224]: ImportMemoryFdEXT (will be remapped) */
   "glImportMemoryFdEXT\0"
   /* _mesa_function_pool[37244]: ImportSemaphoreFdEXT (will be remapped) */
   "glImportSemaphoreFdEXT\0"
   /* _mesa_function_pool[37267]: ImportMemoryWin32HandleEXT (will be remapped) */
   "glImportMemoryWin32HandleEXT\0"
   /* _mesa_function_pool[37296]: ImportMemoryWin32NameEXT (will be remapped) */
   "glImportMemoryWin32NameEXT\0"
   /* _mesa_function_pool[37323]: ImportSemaphoreWin32HandleEXT (will be remapped) */
   "glImportSemaphoreWin32HandleEXT\0"
   /* _mesa_function_pool[37355]: ImportSemaphoreWin32NameEXT (will be remapped) */
   "glImportSemaphoreWin32NameEXT\0"
   /* _mesa_function_pool[37385]: ViewportSwizzleNV (will be remapped) */
   "glViewportSwizzleNV\0"
   /* _mesa_function_pool[37405]: Vertex2hNV (will be remapped) */
   "glVertex2hNV\0"
   /* _mesa_function_pool[37418]: Vertex2hvNV (will be remapped) */
   "glVertex2hvNV\0"
   /* _mesa_function_pool[37432]: Vertex3hNV (will be remapped) */
   "glVertex3hNV\0"
   /* _mesa_function_pool[37445]: Vertex3hvNV (will be remapped) */
   "glVertex3hvNV\0"
   /* _mesa_function_pool[37459]: Vertex4hNV (will be remapped) */
   "glVertex4hNV\0"
   /* _mesa_function_pool[37472]: Vertex4hvNV (will be remapped) */
   "glVertex4hvNV\0"
   /* _mesa_function_pool[37486]: Normal3hNV (will be remapped) */
   "glNormal3hNV\0"
   /* _mesa_function_pool[37499]: Normal3hvNV (will be remapped) */
   "glNormal3hvNV\0"
   /* _mesa_function_pool[37513]: Color3hNV (will be remapped) */
   "glColor3hNV\0"
   /* _mesa_function_pool[37525]: Color3hvNV (will be remapped) */
   "glColor3hvNV\0"
   /* _mesa_function_pool[37538]: Color4hNV (will be remapped) */
   "glColor4hNV\0"
   /* _mesa_function_pool[37550]: Color4hvNV (will be remapped) */
   "glColor4hvNV\0"
   /* _mesa_function_pool[37563]: TexCoord1hNV (will be remapped) */
   "glTexCoord1hNV\0"
   /* _mesa_function_pool[37578]: TexCoord1hvNV (will be remapped) */
   "glTexCoord1hvNV\0"
   /* _mesa_function_pool[37594]: TexCoord2hNV (will be remapped) */
   "glTexCoord2hNV\0"
   /* _mesa_function_pool[37609]: TexCoord2hvNV (will be remapped) */
   "glTexCoord2hvNV\0"
   /* _mesa_function_pool[37625]: TexCoord3hNV (will be remapped) */
   "glTexCoord3hNV\0"
   /* _mesa_function_pool[37640]: TexCoord3hvNV (will be remapped) */
   "glTexCoord3hvNV\0"
   /* _mesa_function_pool[37656]: TexCoord4hNV (will be remapped) */
   "glTexCoord4hNV\0"
   /* _mesa_function_pool[37671]: TexCoord4hvNV (will be remapped) */
   "glTexCoord4hvNV\0"
   /* _mesa_function_pool[37687]: MultiTexCoord1hNV (will be remapped) */
   "glMultiTexCoord1hNV\0"
   /* _mesa_function_pool[37707]: MultiTexCoord1hvNV (will be remapped) */
   "glMultiTexCoord1hvNV\0"
   /* _mesa_function_pool[37728]: MultiTexCoord2hNV (will be remapped) */
   "glMultiTexCoord2hNV\0"
   /* _mesa_function_pool[37748]: MultiTexCoord2hvNV (will be remapped) */
   "glMultiTexCoord2hvNV\0"
   /* _mesa_function_pool[37769]: MultiTexCoord3hNV (will be remapped) */
   "glMultiTexCoord3hNV\0"
   /* _mesa_function_pool[37789]: MultiTexCoord3hvNV (will be remapped) */
   "glMultiTexCoord3hvNV\0"
   /* _mesa_function_pool[37810]: MultiTexCoord4hNV (will be remapped) */
   "glMultiTexCoord4hNV\0"
   /* _mesa_function_pool[37830]: MultiTexCoord4hvNV (will be remapped) */
   "glMultiTexCoord4hvNV\0"
   /* _mesa_function_pool[37851]: VertexAttrib1hNV (will be remapped) */
   "glVertexAttrib1hNV\0"
   /* _mesa_function_pool[37870]: VertexAttrib1hvNV (will be remapped) */
   "glVertexAttrib1hvNV\0"
   /* _mesa_function_pool[37890]: VertexAttrib2hNV (will be remapped) */
   "glVertexAttrib2hNV\0"
   /* _mesa_function_pool[37909]: VertexAttrib2hvNV (will be remapped) */
   "glVertexAttrib2hvNV\0"
   /* _mesa_function_pool[37929]: VertexAttrib3hNV (will be remapped) */
   "glVertexAttrib3hNV\0"
   /* _mesa_function_pool[37948]: VertexAttrib3hvNV (will be remapped) */
   "glVertexAttrib3hvNV\0"
   /* _mesa_function_pool[37968]: VertexAttrib4hNV (will be remapped) */
   "glVertexAttrib4hNV\0"
   /* _mesa_function_pool[37987]: VertexAttrib4hvNV (will be remapped) */
   "glVertexAttrib4hvNV\0"
   /* _mesa_function_pool[38007]: VertexAttribs1hvNV (will be remapped) */
   "glVertexAttribs1hvNV\0"
   /* _mesa_function_pool[38028]: VertexAttribs2hvNV (will be remapped) */
   "glVertexAttribs2hvNV\0"
   /* _mesa_function_pool[38049]: VertexAttribs3hvNV (will be remapped) */
   "glVertexAttribs3hvNV\0"
   /* _mesa_function_pool[38070]: VertexAttribs4hvNV (will be remapped) */
   "glVertexAttribs4hvNV\0"
   /* _mesa_function_pool[38091]: FogCoordhNV (will be remapped) */
   "glFogCoordhNV\0"
   /* _mesa_function_pool[38105]: FogCoordhvNV (will be remapped) */
   "glFogCoordhvNV\0"
   /* _mesa_function_pool[38120]: SecondaryColor3hNV (will be remapped) */
   "glSecondaryColor3hNV\0"
   /* _mesa_function_pool[38141]: SecondaryColor3hvNV (will be remapped) */
   "glSecondaryColor3hvNV\0"
   /* _mesa_function_pool[38163]: MemoryBarrierByRegion (will be remapped) */
   "glMemoryBarrierByRegion\0"
   /* _mesa_function_pool[38187]: AlphaFuncx (will be remapped) */
   "glAlphaFuncxOES\0"
   /* _mesa_function_pool[38203]: ClearColorx (will be remapped) */
   "glClearColorxOES\0"
   /* _mesa_function_pool[38220]: ClearDepthx (will be remapped) */
   "glClearDepthxOES\0"
   /* _mesa_function_pool[38237]: Color4x (will be remapped) */
   "glColor4xOES\0"
   /* _mesa_function_pool[38250]: DepthRangex (will be remapped) */
   "glDepthRangexOES\0"
   /* _mesa_function_pool[38267]: Fogx (will be remapped) */
   "glFogxOES\0"
   /* _mesa_function_pool[38277]: Fogxv (will be remapped) */
   "glFogxvOES\0"
   /* _mesa_function_pool[38288]: Frustumx (will be remapped) */
   "glFrustumxOES\0"
   /* _mesa_function_pool[38302]: LightModelx (will be remapped) */
   "glLightModelxOES\0"
   /* _mesa_function_pool[38319]: LightModelxv (will be remapped) */
   "glLightModelxvOES\0"
   /* _mesa_function_pool[38337]: Lightx (will be remapped) */
   "glLightxOES\0"
   /* _mesa_function_pool[38349]: Lightxv (will be remapped) */
   "glLightxvOES\0"
   /* _mesa_function_pool[38362]: LineWidthx (will be remapped) */
   "glLineWidthxOES\0"
   /* _mesa_function_pool[38378]: LoadMatrixx (will be remapped) */
   "glLoadMatrixxOES\0"
   /* _mesa_function_pool[38395]: Materialx (will be remapped) */
   "glMaterialxOES\0"
   /* _mesa_function_pool[38410]: Materialxv (will be remapped) */
   "glMaterialxvOES\0"
   /* _mesa_function_pool[38426]: MultMatrixx (will be remapped) */
   "glMultMatrixxOES\0"
   /* _mesa_function_pool[38443]: MultiTexCoord4x (will be remapped) */
   "glMultiTexCoord4xOES\0"
   /* _mesa_function_pool[38464]: Normal3x (will be remapped) */
   "glNormal3xOES\0"
   /* _mesa_function_pool[38478]: Orthox (will be remapped) */
   "glOrthoxOES\0"
   /* _mesa_function_pool[38490]: PointSizex (will be remapped) */
   "glPointSizexOES\0"
   /* _mesa_function_pool[38506]: PolygonOffsetx (will be remapped) */
   "glPolygonOffsetxOES\0"
   /* _mesa_function_pool[38526]: Rotatex (will be remapped) */
   "glRotatexOES\0"
   /* _mesa_function_pool[38539]: SampleCoveragex (will be remapped) */
   "glSampleCoveragexOES\0"
   /* _mesa_function_pool[38560]: Scalex (will be remapped) */
   "glScalexOES\0"
   /* _mesa_function_pool[38572]: TexEnvx (will be remapped) */
   "glTexEnvxOES\0"
   /* _mesa_function_pool[38585]: TexEnvxv (will be remapped) */
   "glTexEnvxvOES\0"
   /* _mesa_function_pool[38599]: TexParameterx (will be remapped) */
   "glTexParameterxOES\0"
   /* _mesa_function_pool[38618]: Translatex (will be remapped) */
   "glTranslatexOES\0"
   /* _mesa_function_pool[38634]: ClipPlanex (will be remapped) */
   "glClipPlanexOES\0"
   /* _mesa_function_pool[38650]: GetClipPlanex (will be remapped) */
   "glGetClipPlanexOES\0"
   /* _mesa_function_pool[38669]: GetFixedv (will be remapped) */
   "glGetFixedvOES\0"
   /* _mesa_function_pool[38684]: GetLightxv (will be remapped) */
   "glGetLightxvOES\0"
   /* _mesa_function_pool[38700]: GetMaterialxv (will be remapped) */
   "glGetMaterialxvOES\0"
   /* _mesa_function_pool[38719]: GetTexEnvxv (will be remapped) */
   "glGetTexEnvxvOES\0"
   /* _mesa_function_pool[38736]: GetTexParameterxv (will be remapped) */
   "glGetTexParameterxvOES\0"
   /* _mesa_function_pool[38759]: PointParameterx (will be remapped) */
   "glPointParameterxOES\0"
   /* _mesa_function_pool[38780]: PointParameterxv (will be remapped) */
   "glPointParameterxvOES\0"
   /* _mesa_function_pool[38802]: TexParameterxv (will be remapped) */
   "glTexParameterxvOES\0"
   /* _mesa_function_pool[38822]: GetTexGenxvOES (will be remapped) */
   "glGetTexGenxvOES\0"
   /* _mesa_function_pool[38839]: TexGenxOES (will be remapped) */
   "glTexGenxOES\0"
   /* _mesa_function_pool[38852]: TexGenxvOES (will be remapped) */
   "glTexGenxvOES\0"
   /* _mesa_function_pool[38866]: ClipPlanef (will be remapped) */
   "glClipPlanefOES\0"
   /* _mesa_function_pool[38882]: GetClipPlanef (will be remapped) */
   "glGetClipPlanefOES\0"
   /* _mesa_function_pool[38901]: Frustumf (will be remapped) */
   "glFrustumfOES\0"
   /* _mesa_function_pool[38915]: Orthof (will be remapped) */
   "glOrthofOES\0"
   /* _mesa_function_pool[38927]: DrawTexiOES (will be remapped) */
   "glDrawTexiOES\0"
   /* _mesa_function_pool[38941]: DrawTexivOES (will be remapped) */
   "glDrawTexivOES\0"
   /* _mesa_function_pool[38956]: DrawTexfOES (will be remapped) */
   "glDrawTexfOES\0"
   /* _mesa_function_pool[38970]: DrawTexfvOES (will be remapped) */
   "glDrawTexfvOES\0"
   /* _mesa_function_pool[38985]: DrawTexsOES (will be remapped) */
   "glDrawTexsOES\0"
   /* _mesa_function_pool[38999]: DrawTexsvOES (will be remapped) */
   "glDrawTexsvOES\0"
   /* _mesa_function_pool[39014]: DrawTexxOES (will be remapped) */
   "glDrawTexxOES\0"
   /* _mesa_function_pool[39028]: DrawTexxvOES (will be remapped) */
   "glDrawTexxvOES\0"
   /* _mesa_function_pool[39043]: LoadPaletteFromModelViewMatrixOES (dynamic) */
   "glLoadPaletteFromModelViewMatrixOES\0"
   /* _mesa_function_pool[39079]: PointSizePointerOES (will be remapped) */
   "glPointSizePointerOES\0"
   /* _mesa_function_pool[39101]: QueryMatrixxOES (will be remapped) */
   "glQueryMatrixxOES\0"
   /* _mesa_function_pool[39119]: DiscardFramebufferEXT (will be remapped) */
   "glDiscardFramebufferEXT\0"
   /* _mesa_function_pool[39143]: FramebufferTexture2DMultisampleEXT (will be remapped) */
   "glFramebufferTexture2DMultisampleEXT\0"
   /* _mesa_function_pool[39180]: DepthRangeArrayfvOES (will be remapped) */
   "glDepthRangeArrayfvOES\0"
   /* _mesa_function_pool[39203]: DepthRangeIndexedfOES (will be remapped) */
   "glDepthRangeIndexedfOES\0"
   /* _mesa_function_pool[39227]: FramebufferParameteriMESA (will be remapped) */
   "glFramebufferParameteriMESA\0"
   /* _mesa_function_pool[39255]: GetFramebufferParameterivMESA (will be remapped) */
   "glGetFramebufferParameterivMESA\0"
   ;

/* these functions need to be remapped */
static const struct gl_function_pool_remap MESA_remap_table_functions[] = {
   {  5822, CompressedTexImage1D_remap_index },
   {  5799, CompressedTexImage2D_remap_index },
   {  5776, CompressedTexImage3D_remap_index },
   {  5897, CompressedTexSubImage1D_remap_index },
   {  5871, CompressedTexSubImage2D_remap_index },
   {  5845, CompressedTexSubImage3D_remap_index },
   {  5923, GetCompressedTexImage_remap_index },
   {  5690, LoadTransposeMatrixd_remap_index },
   {  5667, LoadTransposeMatrixf_remap_index },
   {  5736, MultTransposeMatrixd_remap_index },
   {  5713, MultTransposeMatrixf_remap_index },
   {  5759, SampleCoverage_remap_index },
   {  5947, BlendFuncSeparate_remap_index },
   {  6017, FogCoordPointer_remap_index },
   {  5992, FogCoordd_remap_index },
   {  6004, FogCoorddv_remap_index },
   {  6035, MultiDrawArrays_remap_index },
   {  6076, PointParameterf_remap_index },
   {  6094, PointParameterfv_remap_index },
   {  6113, PointParameteri_remap_index },
   {  6131, PointParameteriv_remap_index },
   {  6150, SecondaryColor3b_remap_index },
   {  6169, SecondaryColor3bv_remap_index },
   {  6189, SecondaryColor3d_remap_index },
   {  6208, SecondaryColor3dv_remap_index },
   {  6267, SecondaryColor3i_remap_index },
   {  6286, SecondaryColor3iv_remap_index },
   {  6306, SecondaryColor3s_remap_index },
   {  6325, SecondaryColor3sv_remap_index },
   {  6345, SecondaryColor3ub_remap_index },
   {  6365, SecondaryColor3ubv_remap_index },
   {  6386, SecondaryColor3ui_remap_index },
   {  6406, SecondaryColor3uiv_remap_index },
   {  6427, SecondaryColor3us_remap_index },
   {  6447, SecondaryColor3usv_remap_index },
   {  6468, SecondaryColorPointer_remap_index },
   {  6492, WindowPos2d_remap_index },
   {  6506, WindowPos2dv_remap_index },
   {  6521, WindowPos2f_remap_index },
   {  6535, WindowPos2fv_remap_index },
   {  6550, WindowPos2i_remap_index },
   {  6564, WindowPos2iv_remap_index },
   {  6579, WindowPos2s_remap_index },
   {  6593, WindowPos2sv_remap_index },
   {  6608, WindowPos3d_remap_index },
   {  6622, WindowPos3dv_remap_index },
   {  6637, WindowPos3f_remap_index },
   {  6651, WindowPos3fv_remap_index },
   {  6666, WindowPos3i_remap_index },
   {  6680, WindowPos3iv_remap_index },
   {  6695, WindowPos3s_remap_index },
   {  6709, WindowPos3sv_remap_index },
   {  6933, BeginQuery_remap_index },
   {  6724, BindBuffer_remap_index },
   {  6737, BufferData_remap_index },
   {  6750, BufferSubData_remap_index },
   {  6766, DeleteBuffers_remap_index },
   {  6907, DeleteQueries_remap_index },
   {  6946, EndQuery_remap_index },
   {  6782, GenBuffers_remap_index },
   {  6894, GenQueries_remap_index },
   {  6795, GetBufferParameteriv_remap_index },
   {  6818, GetBufferPointerv_remap_index },
   {  6838, GetBufferSubData_remap_index },
   {  6970, GetQueryObjectiv_remap_index },
   {  6989, GetQueryObjectuiv_remap_index },
   {  6957, GetQueryiv_remap_index },
   {  6857, IsBuffer_remap_index },
   {  6923, IsQuery_remap_index },
   {  6868, MapBuffer_remap_index },
   {  6880, UnmapBuffer_remap_index },
   {  7111, AttachShader_remap_index },
   {  7126, BindAttribLocation_remap_index },
   {  7009, BlendEquationSeparate_remap_index },
   {  7147, CompileShader_remap_index },
   {  7163, CreateProgram_remap_index },
   {  7179, CreateShader_remap_index },
   {  7194, DeleteProgram_remap_index },
   {  7210, DeleteShader_remap_index },
   {  7225, DetachShader_remap_index },
   {  7240, DisableVertexAttribArray_remap_index },
   {  7033, DrawBuffers_remap_index },
   {  7267, EnableVertexAttribArray_remap_index },
   {  7293, GetActiveAttrib_remap_index },
   {  7311, GetActiveUniform_remap_index },
   {  7330, GetAttachedShaders_remap_index },
   {  7351, GetAttribLocation_remap_index },
   {  7386, GetProgramInfoLog_remap_index },
   {  7371, GetProgramiv_remap_index },
   {  7420, GetShaderInfoLog_remap_index },
   {  7439, GetShaderSource_remap_index },
   {  7406, GetShaderiv_remap_index },
   {  7457, GetUniformLocation_remap_index },
   {  7478, GetUniformfv_remap_index },
   {  7493, GetUniformiv_remap_index },
   {  7568, GetVertexAttribPointerv_remap_index },
   {  7508, GetVertexAttribdv_remap_index },
   {  7528, GetVertexAttribfv_remap_index },
   {  7548, GetVertexAttribiv_remap_index },
   {  7594, IsProgram_remap_index },
   {  7606, IsShader_remap_index },
   {  7617, LinkProgram_remap_index },
   {  7631, ShaderSource_remap_index },
   {  7047, StencilFuncSeparate_remap_index },
   {  7089, StencilMaskSeparate_remap_index },
   {  7069, StencilOpSeparate_remap_index },
   {  7659, Uniform1f_remap_index },
   {  7755, Uniform1fv_remap_index },
   {  7707, Uniform1i_remap_index },
   {  7807, Uniform1iv_remap_index },
   {  7671, Uniform2f_remap_index },
   {  7768, Uniform2fv_remap_index },
   {  7719, Uniform2i_remap_index },
   {  7820, Uniform2iv_remap_index },
   {  7683, Uniform3f_remap_index },
   {  7781, Uniform3fv_remap_index },
   {  7731, Uniform3i_remap_index },
   {  7833, Uniform3iv_remap_index },
   {  7695, Uniform4f_remap_index },
   {  7794, Uniform4fv_remap_index },
   {  7743, Uniform4i_remap_index },
   {  7846, Uniform4iv_remap_index },
   {  7859, UniformMatrix2fv_remap_index },
   {  7878, UniformMatrix3fv_remap_index },
   {  7897, UniformMatrix4fv_remap_index },
   {  7646, UseProgram_remap_index },
   {  7916, ValidateProgram_remap_index },
   {  7934, VertexAttrib1d_remap_index },
   {  7951, VertexAttrib1dv_remap_index },
   {  8004, VertexAttrib1s_remap_index },
   {  8021, VertexAttrib1sv_remap_index },
   {  8039, VertexAttrib2d_remap_index },
   {  8056, VertexAttrib2dv_remap_index },
   {  8109, VertexAttrib2s_remap_index },
   {  8126, VertexAttrib2sv_remap_index },
   {  8144, VertexAttrib3d_remap_index },
   {  8161, VertexAttrib3dv_remap_index },
   {  8214, VertexAttrib3s_remap_index },
   {  8231, VertexAttrib3sv_remap_index },
   {  8249, VertexAttrib4Nbv_remap_index },
   {  8268, VertexAttrib4Niv_remap_index },
   {  8287, VertexAttrib4Nsv_remap_index },
   {  8306, VertexAttrib4Nub_remap_index },
   {  8325, VertexAttrib4Nubv_remap_index },
   {  8345, VertexAttrib4Nuiv_remap_index },
   {  8365, VertexAttrib4Nusv_remap_index },
   {  8385, VertexAttrib4bv_remap_index },
   {  8403, VertexAttrib4d_remap_index },
   {  8420, VertexAttrib4dv_remap_index },
   {  8473, VertexAttrib4iv_remap_index },
   {  8491, VertexAttrib4s_remap_index },
   {  8508, VertexAttrib4sv_remap_index },
   {  8526, VertexAttrib4ubv_remap_index },
   {  8545, VertexAttrib4uiv_remap_index },
   {  8564, VertexAttrib4usv_remap_index },
   {  8583, VertexAttribPointer_remap_index },
   {  8605, UniformMatrix2x3fv_remap_index },
   {  8647, UniformMatrix2x4fv_remap_index },
   {  8626, UniformMatrix3x2fv_remap_index },
   {  8689, UniformMatrix3x4fv_remap_index },
   {  8668, UniformMatrix4x2fv_remap_index },
   {  8710, UniformMatrix4x3fv_remap_index },
   { 35990, BeginConditionalRender_remap_index },
   { 35849, BeginTransformFeedback_remap_index },
   { 35915, BindBufferBase_remap_index },
   { 35897, BindBufferRange_remap_index },
   { 35720, BindFragDataLocation_remap_index },
   {  9682, ClampColor_remap_index },
   { 35820, ClearBufferfi_remap_index },
   { 35804, ClearBufferfv_remap_index },
   { 35771, ClearBufferiv_remap_index },
   { 35787, ClearBufferuiv_remap_index },
   { 28910, ColorMaski_remap_index },
   { 28999, Disablei_remap_index },
   { 28980, Enablei_remap_index },
   { 36015, EndConditionalRender_remap_index },
   { 35874, EndTransformFeedback_remap_index },
   { 28932, GetBooleani_v_remap_index },
   { 35746, GetFragDataLocation_remap_index },
   { 28956, GetIntegeri_v_remap_index },
   { 35836, GetStringi_remap_index },
   { 35003, GetTexParameterIiv_remap_index },
   { 35027, GetTexParameterIuiv_remap_index },
   { 35960, GetTransformFeedbackVarying_remap_index },
   { 35701, GetUniformuiv_remap_index },
   { 35520, GetVertexAttribIiv_remap_index },
   { 35544, GetVertexAttribIuiv_remap_index },
   { 29019, IsEnabledi_remap_index },
   { 34960, TexParameterIiv_remap_index },
   { 34981, TexParameterIuiv_remap_index },
   { 35932, TransformFeedbackVaryings_remap_index },
   { 35569, Uniform1ui_remap_index },
   { 35633, Uniform1uiv_remap_index },
   { 35585, Uniform2ui_remap_index },
   { 35650, Uniform2uiv_remap_index },
   { 35601, Uniform3ui_remap_index },
   { 35667, Uniform3uiv_remap_index },
   { 35617, Uniform4ui_remap_index },
   { 35684, Uniform4uiv_remap_index },
   { 35224, VertexAttribI1iv_remap_index },
   { 35312, VertexAttribI1uiv_remap_index },
   { 35404, VertexAttribI4bv_remap_index },
   { 35426, VertexAttribI4sv_remap_index },
   { 35448, VertexAttribI4ubv_remap_index },
   { 35471, VertexAttribI4usv_remap_index },
   { 35494, VertexAttribIPointer_remap_index },
   { 36038, PrimitiveRestartIndex_remap_index },
   { 10300, TexBuffer_remap_index },
   { 36105, FramebufferTexture_remap_index },
   { 36080, GetBufferParameteri64v_remap_index },
   { 36062, GetInteger64i_v_remap_index },
   { 10197, VertexAttribDivisor_remap_index },
   { 10950, MinSampleShading_remap_index },
   { 38163, MemoryBarrierByRegion_remap_index },
   {  9011, BindProgramARB_remap_index },
   {  9028, DeleteProgramsARB_remap_index },
   {  9048, GenProgramsARB_remap_index },
   {  9308, GetProgramEnvParameterdvARB_remap_index },
   {  9338, GetProgramEnvParameterfvARB_remap_index },
   {  9368, GetProgramLocalParameterdvARB_remap_index },
   {  9400, GetProgramLocalParameterfvARB_remap_index },
   {  9450, GetProgramStringARB_remap_index },
   {  9432, GetProgramivARB_remap_index },
   {  9065, IsProgramARB_remap_index },
   {  9080, ProgramEnvParameter4dARB_remap_index },
   {  9107, ProgramEnvParameter4dvARB_remap_index },
   {  9135, ProgramEnvParameter4fARB_remap_index },
   {  9162, ProgramEnvParameter4fvARB_remap_index },
   {  9190, ProgramLocalParameter4dARB_remap_index },
   {  9219, ProgramLocalParameter4dvARB_remap_index },
   {  9249, ProgramLocalParameter4fARB_remap_index },
   {  9278, ProgramLocalParameter4fvARB_remap_index },
   {  8992, ProgramStringARB_remap_index },
   {  7969, VertexAttrib1fARB_remap_index },
   {  7986, VertexAttrib1fvARB_remap_index },
   {  8074, VertexAttrib2fARB_remap_index },
   {  8091, VertexAttrib2fvARB_remap_index },
   {  8179, VertexAttrib3fARB_remap_index },
   {  8196, VertexAttrib3fvARB_remap_index },
   {  8438, VertexAttrib4fARB_remap_index },
   {  8455, VertexAttrib4fvARB_remap_index },
   {  9572, AttachObjectARB_remap_index },
   {  9547, CreateProgramObjectARB_remap_index },
   {  9523, CreateShaderObjectARB_remap_index },
   {  9472, DeleteObjectARB_remap_index },
   {  9505, DetachObjectARB_remap_index },
   {  9658, GetAttachedObjectsARB_remap_index },
   {  9490, GetHandleARB_remap_index },
   {  9642, GetInfoLogARB_remap_index },
   {  9590, GetObjectParameterfvARB_remap_index },
   {  9616, GetObjectParameterivARB_remap_index },
   {  9698, DrawArraysInstanced_remap_index },
   {  9720, DrawElementsInstanced_remap_index },
   {  9921, BindFramebuffer_remap_index },
   {  9761, BindRenderbuffer_remap_index },
   { 10162, BlitFramebuffer_remap_index },
   {  9978, CheckFramebufferStatus_remap_index },
   {  9939, DeleteFramebuffers_remap_index },
   {  9780, DeleteRenderbuffers_remap_index },
   { 10098, FramebufferRenderbuffer_remap_index },
   { 10003, FramebufferTexture1D_remap_index },
   { 10026, FramebufferTexture2D_remap_index },
   { 10049, FramebufferTexture3D_remap_index },
   { 10072, FramebufferTextureLayer_remap_index },
   {  9960, GenFramebuffers_remap_index },
   {  9802, GenRenderbuffers_remap_index },
   { 10180, GenerateMipmap_remap_index },
   { 10124, GetFramebufferAttachmentParameteriv_remap_index },
   {  9876, GetRenderbufferParameteriv_remap_index },
   {  9905, IsFramebuffer_remap_index },
   {  9744, IsRenderbuffer_remap_index },
   {  9821, RenderbufferStorage_remap_index },
   {  9843, RenderbufferStorageMultisample_remap_index },
   { 10275, FlushMappedBufferRange_remap_index },
   { 10258, MapBufferRange_remap_index },
   { 10315, BindVertexArray_remap_index },
   { 10333, DeleteVertexArrays_remap_index },
   { 10354, GenVertexArrays_remap_index },
   { 10372, IsVertexArray_remap_index },
   { 10502, GetActiveUniformBlockName_remap_index },
   { 10476, GetActiveUniformBlockiv_remap_index },
   { 10430, GetActiveUniformName_remap_index },
   { 10408, GetActiveUniformsiv_remap_index },
   { 10453, GetUniformBlockIndex_remap_index },
   { 10388, GetUniformIndices_remap_index },
   { 10530, UniformBlockBinding_remap_index },
   { 10552, CopyBufferSubData_remap_index },
   { 10725, ClientWaitSync_remap_index },
   { 10712, DeleteSync_remap_index },
   { 10691, FenceSync_remap_index },
   { 10753, GetInteger64v_remap_index },
   { 10769, GetSynciv_remap_index },
   { 10703, IsSync_remap_index },
   { 10742, WaitSync_remap_index },
   { 10572, DrawElementsBaseVertex_remap_index },
   { 10657, DrawElementsInstancedBaseVertex_remap_index },
   { 10597, DrawRangeElementsBaseVertex_remap_index },
   { 10627, MultiDrawElementsBaseVertex_remap_index },
   { 28889, ProvokingVertex_remap_index },
   { 10829, GetMultisamplefv_remap_index },
   { 10848, SampleMaski_remap_index },
   { 10781, TexImage2DMultisample_remap_index },
   { 10805, TexImage3DMultisample_remap_index },
   { 10882, BlendEquationSeparateiARB_remap_index },
   { 10862, BlendEquationiARB_remap_index },
   { 10926, BlendFuncSeparateiARB_remap_index },
   { 10910, BlendFunciARB_remap_index },
   { 11099, BindFragDataLocationIndexed_remap_index },
   { 11129, GetFragDataIndex_remap_index },
   { 11191, BindSampler_remap_index },
   { 11162, DeleteSamplers_remap_index },
   { 11148, GenSamplers_remap_index },
   { 11380, GetSamplerParameterIiv_remap_index },
   { 11405, GetSamplerParameterIuiv_remap_index },
   { 11356, GetSamplerParameterfv_remap_index },
   { 11332, GetSamplerParameteriv_remap_index },
   { 11179, IsSampler_remap_index },
   { 11287, SamplerParameterIiv_remap_index },
   { 11309, SamplerParameterIuiv_remap_index },
   { 11225, SamplerParameterf_remap_index },
   { 11266, SamplerParameterfv_remap_index },
   { 11205, SamplerParameteri_remap_index },
   { 11245, SamplerParameteriv_remap_index },
   { 11431, GetQueryObjecti64v_remap_index },
   { 11452, GetQueryObjectui64v_remap_index },
   { 11474, QueryCounter_remap_index },
   { 11885, ColorP3ui_remap_index },
   { 11909, ColorP3uiv_remap_index },
   { 11897, ColorP4ui_remap_index },
   { 11922, ColorP4uiv_remap_index },
   { 11694, MultiTexCoordP1ui_remap_index },
   { 11774, MultiTexCoordP1uiv_remap_index },
   { 11714, MultiTexCoordP2ui_remap_index },
   { 11795, MultiTexCoordP2uiv_remap_index },
   { 11734, MultiTexCoordP3ui_remap_index },
   { 11816, MultiTexCoordP3uiv_remap_index },
   { 11754, MultiTexCoordP4ui_remap_index },
   { 11837, MultiTexCoordP4uiv_remap_index },
   { 11858, NormalP3ui_remap_index },
   { 11871, NormalP3uiv_remap_index },
   { 11935, SecondaryColorP3ui_remap_index },
   { 11956, SecondaryColorP3uiv_remap_index },
   { 11570, TexCoordP1ui_remap_index },
   { 11630, TexCoordP1uiv_remap_index },
   { 11585, TexCoordP2ui_remap_index },
   { 11646, TexCoordP2uiv_remap_index },
   { 11600, TexCoordP3ui_remap_index },
   { 11662, TexCoordP3uiv_remap_index },
   { 11615, TexCoordP4ui_remap_index },
   { 11678, TexCoordP4uiv_remap_index },
   { 11978, VertexAttribP1ui_remap_index },
   { 12054, VertexAttribP1uiv_remap_index },
   { 11997, VertexAttribP2ui_remap_index },
   { 12074, VertexAttribP2uiv_remap_index },
   { 12016, VertexAttribP3ui_remap_index },
   { 12094, VertexAttribP3uiv_remap_index },
   { 12035, VertexAttribP4ui_remap_index },
   { 12114, VertexAttribP4uiv_remap_index },
   { 11489, VertexP2ui_remap_index },
   { 11528, VertexP2uiv_remap_index },
   { 11502, VertexP3ui_remap_index },
   { 11542, VertexP3uiv_remap_index },
   { 11515, VertexP4ui_remap_index },
   { 11556, VertexP4uiv_remap_index },
   { 12383, DrawArraysIndirect_remap_index },
   { 12404, DrawElementsIndirect_remap_index },
   { 12764, GetUniformdv_remap_index },
   { 12481, Uniform1d_remap_index },
   { 12529, Uniform1dv_remap_index },
   { 12493, Uniform2d_remap_index },
   { 12542, Uniform2dv_remap_index },
   { 12505, Uniform3d_remap_index },
   { 12555, Uniform3dv_remap_index },
   { 12517, Uniform4d_remap_index },
   { 12568, Uniform4dv_remap_index },
   { 12581, UniformMatrix2dv_remap_index },
   { 12638, UniformMatrix2x3dv_remap_index },
   { 12659, UniformMatrix2x4dv_remap_index },
   { 12600, UniformMatrix3dv_remap_index },
   { 12680, UniformMatrix3x2dv_remap_index },
   { 12701, UniformMatrix3x4dv_remap_index },
   { 12619, UniformMatrix4dv_remap_index },
   { 12722, UniformMatrix4x2dv_remap_index },
   { 12743, UniformMatrix4x3dv_remap_index },
   { 12250, GetActiveSubroutineName_remap_index },
   { 12217, GetActiveSubroutineUniformName_remap_index },
   { 12186, GetActiveSubroutineUniformiv_remap_index },
   { 12326, GetProgramStageiv_remap_index },
   { 12165, GetSubroutineIndex_remap_index },
   { 12134, GetSubroutineUniformLocation_remap_index },
   { 12300, GetUniformSubroutineuiv_remap_index },
   { 12276, UniformSubroutinesuiv_remap_index },
   { 12364, PatchParameterfv_remap_index },
   { 12346, PatchParameteri_remap_index },
   { 36169, BindTransformFeedback_remap_index },
   { 36193, DeleteTransformFeedbacks_remap_index },
   { 36317, DrawTransformFeedback_remap_index },
   { 36220, GenTransformFeedbacks_remap_index },
   { 36244, IsTransformFeedback_remap_index },
   { 36266, PauseTransformFeedback_remap_index },
   { 36291, ResumeTransformFeedback_remap_index },
   { 13262, BeginQueryIndexed_remap_index },
   { 13232, DrawTransformFeedbackStream_remap_index },
   { 13282, EndQueryIndexed_remap_index },
   { 13300, GetQueryIndexediv_remap_index },
   { 14566, ClearDepthf_remap_index },
   { 14580, DepthRangef_remap_index },
   { 14500, GetShaderPrecisionFormat_remap_index },
   { 14527, ReleaseShaderCompiler_remap_index },
   { 14551, ShaderBinary_remap_index },
   { 14594, GetProgramBinary_remap_index },
   { 14613, ProgramBinary_remap_index },
   { 14629, ProgramParameteri_remap_index },
   { 14443, GetVertexAttribLdv_remap_index },
   { 14272, VertexAttribL1d_remap_index },
   { 14344, VertexAttribL1dv_remap_index },
   { 14290, VertexAttribL2d_remap_index },
   { 14363, VertexAttribL2dv_remap_index },
   { 14308, VertexAttribL3d_remap_index },
   { 14382, VertexAttribL3dv_remap_index },
   { 14326, VertexAttribL4d_remap_index },
   { 14401, VertexAttribL4dv_remap_index },
   { 14420, VertexAttribLPointer_remap_index },
   { 22431, DepthRangeArrayv_remap_index },
   { 22450, DepthRangeIndexed_remap_index },
   { 22484, GetDoublei_v_remap_index },
   { 22470, GetFloati_v_remap_index },
   { 22380, ScissorArrayv_remap_index },
   { 22396, ScissorIndexed_remap_index },
   { 22413, ScissorIndexedv_remap_index },
   { 22324, ViewportArrayv_remap_index },
   { 22341, ViewportIndexedf_remap_index },
   { 22360, ViewportIndexedfv_remap_index },
   { 14748, GetGraphicsResetStatusARB_remap_index },
   { 14942, GetnColorTableARB_remap_index },
   { 15049, GetnCompressedTexImageARB_remap_index },
   { 14962, GetnConvolutionFilterARB_remap_index },
   { 15014, GetnHistogramARB_remap_index },
   { 14776, GetnMapdvARB_remap_index },
   { 14791, GetnMapfvARB_remap_index },
   { 14806, GetnMapivARB_remap_index },
   { 15033, GetnMinmaxARB_remap_index },
   { 14821, GetnPixelMapfvARB_remap_index },
   { 14841, GetnPixelMapuivARB_remap_index },
   { 14862, GetnPixelMapusvARB_remap_index },
   { 14883, GetnPolygonStippleARB_remap_index },
   { 14989, GetnSeparableFilterARB_remap_index },
   { 14907, GetnTexImageARB_remap_index },
   { 15135, GetnUniformdvARB_remap_index },
   { 15077, GetnUniformfvARB_remap_index },
   { 15096, GetnUniformivARB_remap_index },
   { 15115, GetnUniformuivARB_remap_index },
   { 14925, ReadnPixelsARB_remap_index },
   { 15154, DrawArraysInstancedBaseInstance_remap_index },
   { 15188, DrawElementsInstancedBaseInstance_remap_index },
   { 15224, DrawElementsInstancedBaseVertexBaseInstance_remap_index },
   { 15270, DrawTransformFeedbackInstanced_remap_index },
   { 15303, DrawTransformFeedbackStreamInstanced_remap_index },
   { 15342, GetInternalformativ_remap_index },
   { 15364, GetActiveAtomicCounterBufferiv_remap_index },
   { 15397, BindImageTexture_remap_index },
   { 15416, MemoryBarrier_remap_index },
   { 15432, TexStorage1D_remap_index },
   { 15447, TexStorage2D_remap_index },
   { 15462, TexStorage3D_remap_index },
   { 15477, TextureStorage1DEXT_remap_index },
   { 15499, TextureStorage2DEXT_remap_index },
   { 15521, TextureStorage3DEXT_remap_index },
   { 15644, ClearBufferData_remap_index },
   { 15662, ClearBufferSubData_remap_index },
   { 15738, DispatchCompute_remap_index },
   { 15756, DispatchComputeIndirect_remap_index },
   { 15782, CopyImageSubData_remap_index },
   { 15801, TextureView_remap_index },
   { 15815, BindVertexBuffer_remap_index },
   { 15899, VertexAttribBinding_remap_index },
   { 15834, VertexAttribFormat_remap_index },
   { 15855, VertexAttribIFormat_remap_index },
   { 15877, VertexAttribLFormat_remap_index },
   { 15921, VertexBindingDivisor_remap_index },
   { 16157, FramebufferParameteri_remap_index },
   { 16181, GetFramebufferParameteriv_remap_index },
   { 16277, GetInternalformati64v_remap_index },
   { 12427, MultiDrawArraysIndirect_remap_index },
   { 12453, MultiDrawElementsIndirect_remap_index },
   { 16446, GetProgramInterfaceiv_remap_index },
   { 16470, GetProgramResourceIndex_remap_index },
   { 16544, GetProgramResourceLocation_remap_index },
   { 16573, GetProgramResourceLocationIndex_remap_index },
   { 16496, GetProgramResourceName_remap_index },
   { 16521, GetProgramResourceiv_remap_index },
   { 16607, ShaderStorageBlockBinding_remap_index },
   { 16635, TexBufferRange_remap_index },
   { 16676, TexStorage2DMultisample_remap_index },
   { 16702, TexStorage3DMultisample_remap_index },
   { 16794, BufferStorage_remap_index },
   { 16834, ClearTexImage_remap_index },
   { 16850, ClearTexSubImage_remap_index },
   { 16869, BindBuffersBase_remap_index },
   { 16887, BindBuffersRange_remap_index },
   { 16936, BindImageTextures_remap_index },
   { 16921, BindSamplers_remap_index },
   { 16906, BindTextures_remap_index },
   { 16956, BindVertexBuffers_remap_index },
   { 17092, GetImageHandleARB_remap_index },
   { 16976, GetTextureHandleARB_remap_index },
   { 16998, GetTextureSamplerHandleARB_remap_index },
   { 17386, GetVertexAttribLui64vARB_remap_index },
   { 17310, IsImageHandleResidentARB_remap_index },
   { 17281, IsTextureHandleResidentARB_remap_index },
   { 17141, MakeImageHandleNonResidentARB_remap_index },
   { 17112, MakeImageHandleResidentARB_remap_index },
   { 17058, MakeTextureHandleNonResidentARB_remap_index },
   { 17027, MakeTextureHandleResidentARB_remap_index },
   { 17220, ProgramUniformHandleui64ARB_remap_index },
   { 17250, ProgramUniformHandleui64vARB_remap_index },
   { 17173, UniformHandleui64ARB_remap_index },
   { 17196, UniformHandleui64vARB_remap_index },
   { 17337, VertexAttribL1ui64ARB_remap_index },
   { 17361, VertexAttribL1ui64vARB_remap_index },
   { 17413, DispatchComputeGroupSizeARB_remap_index },
   { 17443, MultiDrawArraysIndirectCountARB_remap_index },
   { 17477, MultiDrawElementsIndirectCountARB_remap_index },
   { 17563, ClipControl_remap_index },
   { 19283, BindTextureUnit_remap_index },
   { 18489, BlitNamedFramebuffer_remap_index },
   { 18512, CheckNamedFramebufferStatus_remap_index },
   { 17845, ClearNamedBufferData_remap_index },
   { 17868, ClearNamedBufferSubData_remap_index },
   { 18463, ClearNamedFramebufferfi_remap_index },
   { 18437, ClearNamedFramebufferfv_remap_index },
   { 18384, ClearNamedFramebufferiv_remap_index },
   { 18410, ClearNamedFramebufferuiv_remap_index },
   { 18970, CompressedTextureSubImage1D_remap_index },
   { 19000, CompressedTextureSubImage2D_remap_index },
   { 19030, CompressedTextureSubImage3D_remap_index },
   { 17820, CopyNamedBufferSubData_remap_index },
   { 19060, CopyTextureSubImage1D_remap_index },
   { 19084, CopyTextureSubImage2D_remap_index },
   { 19108, CopyTextureSubImage3D_remap_index },
   { 17744, CreateBuffers_remap_index },
   { 18089, CreateFramebuffers_remap_index },
   { 19883, CreateProgramPipelines_remap_index },
   { 19908, CreateQueries_remap_index },
   { 18618, CreateRenderbuffers_remap_index },
   { 19866, CreateSamplers_remap_index },
   { 18739, CreateTextures_remap_index },
   { 17577, CreateTransformFeedbacks_remap_index },
   { 19504, CreateVertexArrays_remap_index },
   { 19525, DisableVertexArrayAttrib_remap_index },
   { 19552, EnableVertexArrayAttrib_remap_index },
   { 17952, FlushMappedNamedBufferRange_remap_index },
   { 19259, GenerateTextureMipmap_remap_index },
   { 19319, GetCompressedTextureImage_remap_index },
   { 18010, GetNamedBufferParameteri64v_remap_index },
   { 17982, GetNamedBufferParameteriv_remap_index },
   { 18040, GetNamedBufferPointerv_remap_index },
   { 18065, GetNamedBufferSubData_remap_index },
   { 18575, GetNamedFramebufferAttachmentParameteriv_remap_index },
   { 18542, GetNamedFramebufferParameteriv_remap_index },
   { 18705, GetNamedRenderbufferParameteriv_remap_index },
   { 19975, GetQueryBufferObjecti64v_remap_index },
   { 19924, GetQueryBufferObjectiv_remap_index },
   { 20002, GetQueryBufferObjectui64v_remap_index },
   { 19949, GetQueryBufferObjectuiv_remap_index },
   { 19301, GetTextureImage_remap_index },
   { 19347, GetTextureLevelParameterfv_remap_index },
   { 19376, GetTextureLevelParameteriv_remap_index },
   { 19429, GetTextureParameterIiv_remap_index },
   { 19454, GetTextureParameterIuiv_remap_index },
   { 19405, GetTextureParameterfv_remap_index },
   { 19480, GetTextureParameteriv_remap_index },
   { 17716, GetTransformFeedbacki64_v_remap_index },
   { 17690, GetTransformFeedbacki_v_remap_index },
   { 17665, GetTransformFeedbackiv_remap_index },
   { 19838, GetVertexArrayIndexed64iv_remap_index },
   { 19812, GetVertexArrayIndexediv_remap_index },
   { 19793, GetVertexArrayiv_remap_index },
   { 18315, InvalidateNamedFramebufferData_remap_index },
   { 18348, InvalidateNamedFramebufferSubData_remap_index },
   { 17894, MapNamedBuffer_remap_index },
   { 17911, MapNamedBufferRange_remap_index },
   { 17781, NamedBufferData_remap_index },
   { 17760, NamedBufferStorage_remap_index },
   { 17799, NamedBufferSubData_remap_index },
   { 18227, NamedFramebufferDrawBuffer_remap_index },
   { 18256, NamedFramebufferDrawBuffers_remap_index },
   { 18141, NamedFramebufferParameteri_remap_index },
   { 18286, NamedFramebufferReadBuffer_remap_index },
   { 18110, NamedFramebufferRenderbuffer_remap_index },
   { 18170, NamedFramebufferTexture_remap_index },
   { 18196, NamedFramebufferTextureLayer_remap_index },
   { 18640, NamedRenderbufferStorage_remap_index },
   { 18667, NamedRenderbufferStorageMultisample_remap_index },
   { 18756, TextureBuffer_remap_index },
   { 18772, TextureBufferRange_remap_index },
   { 19193, TextureParameterIiv_remap_index },
   { 19215, TextureParameterIuiv_remap_index },
   { 19132, TextureParameterf_remap_index },
   { 19152, TextureParameterfv_remap_index },
   { 19173, TextureParameteri_remap_index },
   { 19238, TextureParameteriv_remap_index },
   { 18793, TextureStorage1D_remap_index },
   { 18812, TextureStorage2D_remap_index },
   { 18850, TextureStorage2DMultisample_remap_index },
   { 18831, TextureStorage3D_remap_index },
   { 18880, TextureStorage3DMultisample_remap_index },
   { 18910, TextureSubImage1D_remap_index },
   { 18930, TextureSubImage2D_remap_index },
   { 18950, TextureSubImage3D_remap_index },
   { 17604, TransformFeedbackBufferBase_remap_index },
   { 17634, TransformFeedbackBufferRange_remap_index },
   { 17933, UnmapNamedBufferEXT_remap_index },
   { 19738, VertexArrayAttribBinding_remap_index },
   { 19658, VertexArrayAttribFormat_remap_index },
   { 19684, VertexArrayAttribIFormat_remap_index },
   { 19711, VertexArrayAttribLFormat_remap_index },
   { 19765, VertexArrayBindingDivisor_remap_index },
   { 19578, VertexArrayElementBuffer_remap_index },
   { 19605, VertexArrayVertexBuffer_remap_index },
   { 19631, VertexArrayVertexBuffers_remap_index },
   { 20051, GetCompressedTextureSubImage_remap_index },
   { 20030, GetTextureSubImage_remap_index },
   { 20099, BufferPageCommitmentARB_remap_index },
   { 20156, NamedBufferPageCommitmentARB_remap_index },
   { 20513, GetUniformi64vARB_remap_index },
   { 20533, GetUniformui64vARB_remap_index },
   { 20554, GetnUniformi64vARB_remap_index },
   { 20575, GetnUniformui64vARB_remap_index },
   { 20597, ProgramUniform1i64ARB_remap_index },
   { 20693, ProgramUniform1i64vARB_remap_index },
   { 20793, ProgramUniform1ui64ARB_remap_index },
   { 20893, ProgramUniform1ui64vARB_remap_index },
   { 20621, ProgramUniform2i64ARB_remap_index },
   { 20718, ProgramUniform2i64vARB_remap_index },
   { 20818, ProgramUniform2ui64ARB_remap_index },
   { 20919, ProgramUniform2ui64vARB_remap_index },
   { 20645, ProgramUniform3i64ARB_remap_index },
   { 20743, ProgramUniform3i64vARB_remap_index },
   { 20843, ProgramUniform3ui64ARB_remap_index },
   { 20945, ProgramUniform3ui64vARB_remap_index },
   { 20669, ProgramUniform4i64ARB_remap_index },
   { 20768, ProgramUniform4i64vARB_remap_index },
   { 20868, ProgramUniform4ui64ARB_remap_index },
   { 20971, ProgramUniform4ui64vARB_remap_index },
   { 20225, Uniform1i64ARB_remap_index },
   { 20293, Uniform1i64vARB_remap_index },
   { 20365, Uniform1ui64ARB_remap_index },
   { 20437, Uniform1ui64vARB_remap_index },
   { 20242, Uniform2i64ARB_remap_index },
   { 20311, Uniform2i64vARB_remap_index },
   { 20383, Uniform2ui64ARB_remap_index },
   { 20456, Uniform2ui64vARB_remap_index },
   { 20259, Uniform3i64ARB_remap_index },
   { 20329, Uniform3i64vARB_remap_index },
   { 20401, Uniform3ui64ARB_remap_index },
   { 20475, Uniform3ui64vARB_remap_index },
   { 20276, Uniform4i64ARB_remap_index },
   { 20347, Uniform4i64vARB_remap_index },
   { 20419, Uniform4ui64ARB_remap_index },
   { 20494, Uniform4ui64vARB_remap_index },
   { 25553, EvaluateDepthValuesARB_remap_index },
   { 25480, FramebufferSampleLocationsfvARB_remap_index },
   { 25514, NamedFramebufferSampleLocationsfvARB_remap_index },
   { 21027, SpecializeShaderARB_remap_index },
   { 16372, InvalidateBufferData_remap_index },
   { 16346, InvalidateBufferSubData_remap_index },
   { 16422, InvalidateFramebuffer_remap_index },
   { 16395, InvalidateSubFramebuffer_remap_index },
   { 16325, InvalidateTexImage_remap_index },
   { 16301, InvalidateTexSubImage_remap_index },
   { 38956, DrawTexfOES_remap_index },
   { 38970, DrawTexfvOES_remap_index },
   { 38927, DrawTexiOES_remap_index },
   { 38941, DrawTexivOES_remap_index },
   { 38985, DrawTexsOES_remap_index },
   { 38999, DrawTexsvOES_remap_index },
   { 39014, DrawTexxOES_remap_index },
   { 39028, DrawTexxvOES_remap_index },
   { 39079, PointSizePointerOES_remap_index },
   { 39101, QueryMatrixxOES_remap_index },
   { 21395, SampleMaskSGIS_remap_index },
   { 21412, SamplePatternSGIS_remap_index },
   { 21432, ColorPointerEXT_remap_index },
   { 21450, EdgeFlagPointerEXT_remap_index },
   { 21471, IndexPointerEXT_remap_index },
   { 21489, NormalPointerEXT_remap_index },
   { 21508, TexCoordPointerEXT_remap_index },
   { 21529, VertexPointerEXT_remap_index },
   { 39119, DiscardFramebufferEXT_remap_index },
   { 13339, ActiveShaderProgram_remap_index },
   { 13384, BindProgramPipeline_remap_index },
   { 13361, CreateShaderProgramv_remap_index },
   { 13406, DeleteProgramPipelines_remap_index },
   { 13431, GenProgramPipelines_remap_index },
   { 14244, GetProgramPipelineInfoLog_remap_index },
   { 13473, GetProgramPipelineiv_remap_index },
   { 13453, IsProgramPipeline_remap_index },
   { 22248, LockArraysEXT_remap_index },
   { 12779, ProgramUniform1d_remap_index },
   { 12867, ProgramUniform1dv_remap_index },
   { 13652, ProgramUniform1f_remap_index },
   { 13892, ProgramUniform1fv_remap_index },
   { 13496, ProgramUniform1i_remap_index },
   { 13728, ProgramUniform1iv_remap_index },
   { 13572, ProgramUniform1ui_remap_index },
   { 13808, ProgramUniform1uiv_remap_index },
   { 12801, ProgramUniform2d_remap_index },
   { 12890, ProgramUniform2dv_remap_index },
   { 13671, ProgramUniform2f_remap_index },
   { 13912, ProgramUniform2fv_remap_index },
   { 13515, ProgramUniform2i_remap_index },
   { 13748, ProgramUniform2iv_remap_index },
   { 13592, ProgramUniform2ui_remap_index },
   { 13829, ProgramUniform2uiv_remap_index },
   { 12823, ProgramUniform3d_remap_index },
   { 12913, ProgramUniform3dv_remap_index },
   { 13690, ProgramUniform3f_remap_index },
   { 13932, ProgramUniform3fv_remap_index },
   { 13534, ProgramUniform3i_remap_index },
   { 13768, ProgramUniform3iv_remap_index },
   { 13612, ProgramUniform3ui_remap_index },
   { 13850, ProgramUniform3uiv_remap_index },
   { 12845, ProgramUniform4d_remap_index },
   { 12936, ProgramUniform4dv_remap_index },
   { 13709, ProgramUniform4f_remap_index },
   { 13952, ProgramUniform4fv_remap_index },
   { 13553, ProgramUniform4i_remap_index },
   { 13788, ProgramUniform4iv_remap_index },
   { 13632, ProgramUniform4ui_remap_index },
   { 13871, ProgramUniform4uiv_remap_index },
   { 12959, ProgramUniformMatrix2dv_remap_index },
   { 13972, ProgramUniformMatrix2fv_remap_index },
   { 13046, ProgramUniformMatrix2x3dv_remap_index },
   { 14050, ProgramUniformMatrix2x3fv_remap_index },
   { 13077, ProgramUniformMatrix2x4dv_remap_index },
   { 14106, ProgramUniformMatrix2x4fv_remap_index },
   { 12988, ProgramUniformMatrix3dv_remap_index },
   { 13998, ProgramUniformMatrix3fv_remap_index },
   { 13108, ProgramUniformMatrix3x2dv_remap_index },
   { 14078, ProgramUniformMatrix3x2fv_remap_index },
   { 13139, ProgramUniformMatrix3x4dv_remap_index },
   { 14162, ProgramUniformMatrix3x4fv_remap_index },
   { 13017, ProgramUniformMatrix4dv_remap_index },
   { 14024, ProgramUniformMatrix4fv_remap_index },
   { 13170, ProgramUniformMatrix4x2dv_remap_index },
   { 14134, ProgramUniformMatrix4x2fv_remap_index },
   { 13201, ProgramUniformMatrix4x3dv_remap_index },
   { 14190, ProgramUniformMatrix4x3fv_remap_index },
   { 22264, UnlockArraysEXT_remap_index },
   { 13320, UseProgramStages_remap_index },
   { 14218, ValidateProgramPipeline_remap_index },
   { 39143, FramebufferTexture2DMultisampleEXT_remap_index },
   { 14698, DebugMessageCallback_remap_index },
   { 14649, DebugMessageControl_remap_index },
   { 14674, DebugMessageInsert_remap_index },
   { 14724, GetDebugMessageLog_remap_index },
   { 15590, GetObjectLabel_remap_index },
   { 15624, GetObjectPtrLabel_remap_index },
   { 15576, ObjectLabel_remap_index },
   { 15607, ObjectPtrLabel_remap_index },
   { 15560, PopDebugGroup_remap_index },
   { 15543, PushDebugGroup_remap_index },
   {  6228, SecondaryColor3fEXT_remap_index },
   {  6247, SecondaryColor3fvEXT_remap_index },
   {  6053, MultiDrawElements_remap_index },
   {  5967, FogCoordfEXT_remap_index },
   {  5979, FogCoordfvEXT_remap_index },
   { 26047, ResizeBuffersMESA_remap_index },
   { 26067, WindowPos4dMESA_remap_index },
   { 26085, WindowPos4dvMESA_remap_index },
   { 26104, WindowPos4fMESA_remap_index },
   { 26122, WindowPos4fvMESA_remap_index },
   { 26141, WindowPos4iMESA_remap_index },
   { 26159, WindowPos4ivMESA_remap_index },
   { 26178, WindowPos4sMESA_remap_index },
   { 26196, WindowPos4svMESA_remap_index },
   { 26215, MultiModeDrawArraysIBM_remap_index },
   { 26240, MultiModeDrawElementsIBM_remap_index },
   { 26862, AreProgramsResidentNV_remap_index },
   { 26886, ExecuteProgramNV_remap_index },
   { 26905, GetProgramParameterdvNV_remap_index },
   { 26931, GetProgramParameterfvNV_remap_index },
   { 26974, GetProgramStringNV_remap_index },
   { 26957, GetProgramivNV_remap_index },
   { 26995, GetTrackMatrixivNV_remap_index },
   { 27016, GetVertexAttribdvNV_remap_index },
   { 27038, GetVertexAttribfvNV_remap_index },
   { 27060, GetVertexAttribivNV_remap_index },
   { 27082, LoadProgramNV_remap_index },
   { 27098, ProgramParameters4dvNV_remap_index },
   { 27123, ProgramParameters4fvNV_remap_index },
   { 27148, RequestResidentProgramsNV_remap_index },
   { 27176, TrackMatrixNV_remap_index },
   { 27528, VertexAttrib1dNV_remap_index },
   { 27547, VertexAttrib1dvNV_remap_index },
   { 27372, VertexAttrib1fNV_remap_index },
   { 27391, VertexAttrib1fvNV_remap_index },
   { 27216, VertexAttrib1sNV_remap_index },
   { 27235, VertexAttrib1svNV_remap_index },
   { 27567, VertexAttrib2dNV_remap_index },
   { 27586, VertexAttrib2dvNV_remap_index },
   { 27411, VertexAttrib2fNV_remap_index },
   { 27430, VertexAttrib2fvNV_remap_index },
   { 27255, VertexAttrib2sNV_remap_index },
   { 27274, VertexAttrib2svNV_remap_index },
   { 27606, VertexAttrib3dNV_remap_index },
   { 27625, VertexAttrib3dvNV_remap_index },
   { 27450, VertexAttrib3fNV_remap_index },
   { 27469, VertexAttrib3fvNV_remap_index },
   { 27294, VertexAttrib3sNV_remap_index },
   { 27313, VertexAttrib3svNV_remap_index },
   { 27645, VertexAttrib4dNV_remap_index },
   { 27664, VertexAttrib4dvNV_remap_index },
   { 27489, VertexAttrib4fNV_remap_index },
   { 27508, VertexAttrib4fvNV_remap_index },
   { 27333, VertexAttrib4sNV_remap_index },
   { 27352, VertexAttrib4svNV_remap_index },
   { 27684, VertexAttrib4ubNV_remap_index },
   { 27704, VertexAttrib4ubvNV_remap_index },
   { 27192, VertexAttribPointerNV_remap_index },
   { 27893, VertexAttribs1dvNV_remap_index },
   { 27809, VertexAttribs1fvNV_remap_index },
   { 27725, VertexAttribs1svNV_remap_index },
   { 27914, VertexAttribs2dvNV_remap_index },
   { 27830, VertexAttribs2fvNV_remap_index },
   { 27746, VertexAttribs2svNV_remap_index },
   { 27935, VertexAttribs3dvNV_remap_index },
   { 27851, VertexAttribs3fvNV_remap_index },
   { 27767, VertexAttribs3svNV_remap_index },
   { 27956, VertexAttribs4dvNV_remap_index },
   { 27872, VertexAttribs4fvNV_remap_index },
   { 27788, VertexAttribs4svNV_remap_index },
   { 27977, VertexAttribs4ubvNV_remap_index },
   { 28047, GetTexBumpParameterfvATI_remap_index },
   { 28074, GetTexBumpParameterivATI_remap_index },
   { 27999, TexBumpParameterfvATI_remap_index },
   { 28023, TexBumpParameterivATI_remap_index },
   { 28322, AlphaFragmentOp1ATI_remap_index },
   { 28344, AlphaFragmentOp2ATI_remap_index },
   { 28366, AlphaFragmentOp3ATI_remap_index },
   { 28175, BeginFragmentShaderATI_remap_index },
   { 28125, BindFragmentShaderATI_remap_index },
   { 28256, ColorFragmentOp1ATI_remap_index },
   { 28278, ColorFragmentOp2ATI_remap_index },
   { 28300, ColorFragmentOp3ATI_remap_index },
   { 28149, DeleteFragmentShaderATI_remap_index },
   { 28200, EndFragmentShaderATI_remap_index },
   { 28101, GenFragmentShadersATI_remap_index },
   { 28223, PassTexCoordATI_remap_index },
   { 28241, SampleMapATI_remap_index },
   { 28388, SetFragmentShaderConstantATI_remap_index },
   { 39180, DepthRangeArrayfvOES_remap_index },
   { 39203, DepthRangeIndexedfOES_remap_index },
   { 28439, ActiveStencilFaceEXT_remap_index },
   { 28776, GetProgramNamedParameterdvNV_remap_index },
   { 28745, GetProgramNamedParameterfvNV_remap_index },
   { 28659, ProgramNamedParameter4dNV_remap_index },
   { 28716, ProgramNamedParameter4dvNV_remap_index },
   { 28631, ProgramNamedParameter4fNV_remap_index },
   { 28687, ProgramNamedParameter4fvNV_remap_index },
   { 36126, PrimitiveRestartNV_remap_index },
   { 38822, GetTexGenxvOES_remap_index },
   { 38839, TexGenxOES_remap_index },
   { 38852, TexGenxvOES_remap_index },
   { 28807, DepthBoundsEXT_remap_index },
   { 28846, BindFramebufferEXT_remap_index },
   { 28824, BindRenderbufferEXT_remap_index },
   { 28867, StringMarkerGREMEDY_remap_index },
   { 29041, BufferParameteriAPPLE_remap_index },
   { 29065, FlushMappedBufferRangeAPPLE_remap_index },
   { 35052, VertexAttribI1iEXT_remap_index },
   { 35136, VertexAttribI1uiEXT_remap_index },
   { 35073, VertexAttribI2iEXT_remap_index },
   { 35246, VertexAttribI2ivEXT_remap_index },
   { 35158, VertexAttribI2uiEXT_remap_index },
   { 35335, VertexAttribI2uivEXT_remap_index },
   { 35094, VertexAttribI3iEXT_remap_index },
   { 35268, VertexAttribI3ivEXT_remap_index },
   { 35180, VertexAttribI3uiEXT_remap_index },
   { 35358, VertexAttribI3uivEXT_remap_index },
   { 35115, VertexAttribI4iEXT_remap_index },
   { 35290, VertexAttribI4ivEXT_remap_index },
   { 35202, VertexAttribI4uiEXT_remap_index },
   { 35381, VertexAttribI4uivEXT_remap_index },
   { 34923, ClearColorIiEXT_remap_index },
   { 34941, ClearColorIuiEXT_remap_index },
   { 36147, BindBufferOffsetEXT_remap_index },
   { 29320, BeginPerfMonitorAMD_remap_index },
   { 29265, DeletePerfMonitorsAMD_remap_index },
   { 29342, EndPerfMonitorAMD_remap_index },
   { 29244, GenPerfMonitorsAMD_remap_index },
   { 29362, GetPerfMonitorCounterDataAMD_remap_index },
   { 29213, GetPerfMonitorCounterInfoAMD_remap_index },
   { 29180, GetPerfMonitorCounterStringAMD_remap_index },
   { 29121, GetPerfMonitorCountersAMD_remap_index },
   { 29149, GetPerfMonitorGroupStringAMD_remap_index },
   { 29095, GetPerfMonitorGroupsAMD_remap_index },
   { 29289, SelectPerfMonitorCountersAMD_remap_index },
   { 28510, GetObjectParameterivAPPLE_remap_index },
   { 28462, ObjectPurgeableAPPLE_remap_index },
   { 28485, ObjectUnpurgeableAPPLE_remap_index },
   { 29466, ActiveProgramEXT_remap_index },
   { 29485, CreateShaderProgramEXT_remap_index },
   { 29444, UseShaderProgramEXT_remap_index },
   { 20082, TextureBarrierNV_remap_index },
   { 36355, VDPAUFiniNV_remap_index },
   { 36476, VDPAUGetSurfaceivNV_remap_index },
   { 36341, VDPAUInitNV_remap_index },
   { 36430, VDPAUIsSurfaceNV_remap_index },
   { 36521, VDPAUMapSurfacesNV_remap_index },
   { 36399, VDPAURegisterOutputSurfaceNV_remap_index },
   { 36369, VDPAURegisterVideoSurfaceNV_remap_index },
   { 36498, VDPAUSurfaceAccessNV_remap_index },
   { 36542, VDPAUUnmapSurfacesNV_remap_index },
   { 36449, VDPAUUnregisterSurfaceNV_remap_index },
   { 34044, BeginPerfQueryINTEL_remap_index },
   { 33998, CreatePerfQueryINTEL_remap_index },
   { 34021, DeletePerfQueryINTEL_remap_index },
   { 34066, EndPerfQueryINTEL_remap_index },
   { 33867, GetFirstPerfQueryIdINTEL_remap_index },
   { 33894, GetNextPerfQueryIdINTEL_remap_index },
   { 33972, GetPerfCounterInfoINTEL_remap_index },
   { 34086, GetPerfQueryDataINTEL_remap_index },
   { 33920, GetPerfQueryIdByNameINTEL_remap_index },
   { 33948, GetPerfQueryInfoINTEL_remap_index },
   { 34143, PolygonOffsetClampEXT_remap_index },
   { 33775, SubpixelPrecisionBiasNV_remap_index },
   { 33801, ConservativeRasterParameterfNV_remap_index },
   { 33834, ConservativeRasterParameteriNV_remap_index },
   { 34167, WindowRectanglesEXT_remap_index },
   { 36848, BufferStorageMemEXT_remap_index },
   { 36656, CreateMemoryObjectsEXT_remap_index },
   { 36611, DeleteMemoryObjectsEXT_remap_index },
   { 37084, DeleteSemaphoresEXT_remap_index },
   { 37065, GenSemaphoresEXT_remap_index },
   { 36710, GetMemoryObjectParameterivEXT_remap_index },
   { 37152, GetSemaphoreParameterui64vEXT_remap_index },
   { 36587, GetUnsignedBytei_vEXT_remap_index },
   { 36565, GetUnsignedBytevEXT_remap_index },
   { 36636, IsMemoryObjectEXT_remap_index },
   { 37106, IsSemaphoreEXT_remap_index },
   { 36681, MemoryObjectParameterivEXT_remap_index },
   { 36992, NamedBufferStorageMemEXT_remap_index },
   { 37123, SemaphoreParameterui64vEXT_remap_index },
   { 37203, SignalSemaphoreEXT_remap_index },
   { 37019, TexStorageMem1DEXT_remap_index },
   { 36742, TexStorageMem2DEXT_remap_index },
   { 36763, TexStorageMem2DMultisampleEXT_remap_index },
   { 36795, TexStorageMem3DEXT_remap_index },
   { 36816, TexStorageMem3DMultisampleEXT_remap_index },
   { 37040, TextureStorageMem1DEXT_remap_index },
   { 36870, TextureStorageMem2DEXT_remap_index },
   { 36895, TextureStorageMem2DMultisampleEXT_remap_index },
   { 36931, TextureStorageMem3DEXT_remap_index },
   { 36956, TextureStorageMem3DMultisampleEXT_remap_index },
   { 37184, WaitSemaphoreEXT_remap_index },
   { 37224, ImportMemoryFdEXT_remap_index },
   { 37244, ImportSemaphoreFdEXT_remap_index },
   { 34189, FramebufferFetchBarrierEXT_remap_index },
   { 34262, NamedRenderbufferStorageMultisampleAdvancedAMD_remap_index },
   { 34218, RenderbufferStorageMultisampleAdvancedAMD_remap_index },
   { 34311, StencilFuncSeparateATI_remap_index },
   { 34336, ProgramEnvParameters4fvEXT_remap_index },
   { 34365, ProgramLocalParameters4fvEXT_remap_index },
   { 34820, EGLImageTargetRenderbufferStorageOES_remap_index },
   { 34791, EGLImageTargetTexture2DOES_remap_index },
   { 38187, AlphaFuncx_remap_index },
   { 38203, ClearColorx_remap_index },
   { 38220, ClearDepthx_remap_index },
   { 38237, Color4x_remap_index },
   { 38250, DepthRangex_remap_index },
   { 38267, Fogx_remap_index },
   { 38277, Fogxv_remap_index },
   { 38901, Frustumf_remap_index },
   { 38288, Frustumx_remap_index },
   { 38302, LightModelx_remap_index },
   { 38319, LightModelxv_remap_index },
   { 38337, Lightx_remap_index },
   { 38349, Lightxv_remap_index },
   { 38362, LineWidthx_remap_index },
   { 38378, LoadMatrixx_remap_index },
   { 38395, Materialx_remap_index },
   { 38410, Materialxv_remap_index },
   { 38426, MultMatrixx_remap_index },
   { 38443, MultiTexCoord4x_remap_index },
   { 38464, Normal3x_remap_index },
   { 38915, Orthof_remap_index },
   { 38478, Orthox_remap_index },
   { 38490, PointSizex_remap_index },
   { 38506, PolygonOffsetx_remap_index },
   { 38526, Rotatex_remap_index },
   { 38539, SampleCoveragex_remap_index },
   { 38560, Scalex_remap_index },
   { 38572, TexEnvx_remap_index },
   { 38585, TexEnvxv_remap_index },
   { 38599, TexParameterx_remap_index },
   { 38618, Translatex_remap_index },
   { 38866, ClipPlanef_remap_index },
   { 38634, ClipPlanex_remap_index },
   { 38882, GetClipPlanef_remap_index },
   { 38650, GetClipPlanex_remap_index },
   { 38669, GetFixedv_remap_index },
   { 38684, GetLightxv_remap_index },
   { 38700, GetMaterialxv_remap_index },
   { 38719, GetTexEnvxv_remap_index },
   { 38736, GetTexParameterxv_remap_index },
   { 38759, PointParameterx_remap_index },
   { 38780, PointParameterxv_remap_index },
   { 38802, TexParameterxv_remap_index },
   { 20210, BlendBarrier_remap_index },
   { 20187, PrimitiveBoundingBox_remap_index },
   { 20997, MaxShaderCompilerThreadsKHR_remap_index },
   { 29531, MatrixLoadfEXT_remap_index },
   { 29548, MatrixLoaddEXT_remap_index },
   { 29565, MatrixMultfEXT_remap_index },
   { 29582, MatrixMultdEXT_remap_index },
   { 29599, MatrixLoadIdentityEXT_remap_index },
   { 29623, MatrixRotatefEXT_remap_index },
   { 29642, MatrixRotatedEXT_remap_index },
   { 29661, MatrixScalefEXT_remap_index },
   { 29679, MatrixScaledEXT_remap_index },
   { 29697, MatrixTranslatefEXT_remap_index },
   { 29719, MatrixTranslatedEXT_remap_index },
   { 29741, MatrixOrthoEXT_remap_index },
   { 29758, MatrixFrustumEXT_remap_index },
   { 29777, MatrixPushEXT_remap_index },
   { 29793, MatrixPopEXT_remap_index },
   { 31292, MatrixLoadTransposefEXT_remap_index },
   { 31318, MatrixLoadTransposedEXT_remap_index },
   { 31344, MatrixMultTransposefEXT_remap_index },
   { 31370, MatrixMultTransposedEXT_remap_index },
   { 30353, BindMultiTextureEXT_remap_index },
   { 31843, NamedBufferDataEXT_remap_index },
   { 31864, NamedBufferSubDataEXT_remap_index },
   { 16810, NamedBufferStorageEXT_remap_index },
   { 32027, MapNamedBufferRangeEXT_remap_index },
   { 30074, TextureImage1DEXT_remap_index },
   { 30094, TextureImage2DEXT_remap_index },
   { 30114, TextureImage3DEXT_remap_index },
   { 30134, TextureSubImage1DEXT_remap_index },
   { 30157, TextureSubImage2DEXT_remap_index },
   { 30180, TextureSubImage3DEXT_remap_index },
   { 30203, CopyTextureImage1DEXT_remap_index },
   { 30227, CopyTextureImage2DEXT_remap_index },
   { 30251, CopyTextureSubImage1DEXT_remap_index },
   { 30278, CopyTextureSubImage2DEXT_remap_index },
   { 30305, CopyTextureSubImage3DEXT_remap_index },
   { 31888, MapNamedBufferEXT_remap_index },
   { 29862, GetTextureParameterivEXT_remap_index },
   { 29889, GetTextureParameterfvEXT_remap_index },
   { 29980, TextureParameteriEXT_remap_index },
   { 30003, TextureParameterivEXT_remap_index },
   { 30027, TextureParameterfEXT_remap_index },
   { 30050, TextureParameterfvEXT_remap_index },
   { 30332, GetTextureImageEXT_remap_index },
   { 29916, GetTextureLevelParameterivEXT_remap_index },
   { 29948, GetTextureLevelParameterfvEXT_remap_index },
   { 31908, GetNamedBufferSubDataEXT_remap_index },
   { 31935, GetNamedBufferPointervEXT_remap_index },
   { 31963, GetNamedBufferParameterivEXT_remap_index },
   { 31994, FlushMappedNamedBufferRangeEXT_remap_index },
   { 32052, FramebufferDrawBufferEXT_remap_index },
   { 32079, FramebufferDrawBuffersEXT_remap_index },
   { 32107, FramebufferReadBufferEXT_remap_index },
   { 32134, GetFramebufferParameterivEXT_remap_index },
   { 32165, CheckNamedFramebufferStatusEXT_remap_index },
   { 32198, NamedFramebufferTexture1DEXT_remap_index },
   { 32229, NamedFramebufferTexture2DEXT_remap_index },
   { 32260, NamedFramebufferTexture3DEXT_remap_index },
   { 32291, NamedFramebufferRenderbufferEXT_remap_index },
   { 32325, GetNamedFramebufferAttachmentParameterivEXT_remap_index },
   { 30375, EnableClientStateiEXT_remap_index },
   { 30405, DisableClientStateiEXT_remap_index },
   { 30436, GetPointerIndexedvEXT_remap_index },
   { 30460, MultiTexEnviEXT_remap_index },
   { 30478, MultiTexEnvivEXT_remap_index },
   { 30497, MultiTexEnvfEXT_remap_index },
   { 30515, MultiTexEnvfvEXT_remap_index },
   { 30534, GetMultiTexEnvivEXT_remap_index },
   { 30556, GetMultiTexEnvfvEXT_remap_index },
   { 30578, MultiTexParameteriEXT_remap_index },
   { 30602, MultiTexParameterivEXT_remap_index },
   { 30627, MultiTexParameterfEXT_remap_index },
   { 30651, MultiTexParameterfvEXT_remap_index },
   { 30732, GetMultiTexImageEXT_remap_index },
   { 30820, MultiTexImage1DEXT_remap_index },
   { 30841, MultiTexImage2DEXT_remap_index },
   { 30862, MultiTexImage3DEXT_remap_index },
   { 30883, MultiTexSubImage1DEXT_remap_index },
   { 30907, MultiTexSubImage2DEXT_remap_index },
   { 30931, MultiTexSubImage3DEXT_remap_index },
   { 30676, GetMultiTexParameterivEXT_remap_index },
   { 30704, GetMultiTexParameterfvEXT_remap_index },
   { 30955, CopyMultiTexImage1DEXT_remap_index },
   { 30980, CopyMultiTexImage2DEXT_remap_index },
   { 31005, CopyMultiTexSubImage1DEXT_remap_index },
   { 31033, CopyMultiTexSubImage2DEXT_remap_index },
   { 31061, CopyMultiTexSubImage3DEXT_remap_index },
   { 31089, MultiTexGendEXT_remap_index },
   { 31107, MultiTexGendvEXT_remap_index },
   { 31126, MultiTexGenfEXT_remap_index },
   { 31144, MultiTexGenfvEXT_remap_index },
   { 31163, MultiTexGeniEXT_remap_index },
   { 31181, MultiTexGenivEXT_remap_index },
   { 31200, GetMultiTexGendvEXT_remap_index },
   { 31222, GetMultiTexGenfvEXT_remap_index },
   { 31244, GetMultiTexGenivEXT_remap_index },
   { 31266, MultiTexCoordPointerEXT_remap_index },
   { 33716, BindImageTextureEXT_remap_index },
   { 31396, CompressedTextureImage1DEXT_remap_index },
   { 31426, CompressedTextureImage2DEXT_remap_index },
   { 31456, CompressedTextureImage3DEXT_remap_index },
   { 31486, CompressedTextureSubImage1DEXT_remap_index },
   { 31519, CompressedTextureSubImage2DEXT_remap_index },
   { 31552, CompressedTextureSubImage3DEXT_remap_index },
   { 31585, GetCompressedTextureImageEXT_remap_index },
   { 31616, CompressedMultiTexImage1DEXT_remap_index },
   { 31647, CompressedMultiTexImage2DEXT_remap_index },
   { 31678, CompressedMultiTexImage3DEXT_remap_index },
   { 31709, CompressedMultiTexSubImage1DEXT_remap_index },
   { 31743, CompressedMultiTexSubImage2DEXT_remap_index },
   { 31777, CompressedMultiTexSubImage3DEXT_remap_index },
   { 31811, GetCompressedMultiTexImageEXT_remap_index },
   { 30754, GetMultiTexLevelParameterivEXT_remap_index },
   { 30787, GetMultiTexLevelParameterfvEXT_remap_index },
   { 39227, FramebufferParameteriMESA_remap_index },
   { 39255, GetFramebufferParameterivMESA_remap_index },
   { 32371, NamedRenderbufferStorageEXT_remap_index },
   { 32401, GetNamedRenderbufferParameterivEXT_remap_index },
   { 29808, ClientAttribDefaultEXT_remap_index },
   { 29833, PushClientAttribDefaultEXT_remap_index },
   { 33135, NamedProgramStringEXT_remap_index },
   { 33159, GetNamedProgramStringEXT_remap_index },
   { 33186, NamedProgramLocalParameter4fEXT_remap_index },
   { 33220, NamedProgramLocalParameter4fvEXT_remap_index },
   { 33255, GetNamedProgramLocalParameterfvEXT_remap_index },
   { 33292, NamedProgramLocalParameter4dEXT_remap_index },
   { 33326, NamedProgramLocalParameter4dvEXT_remap_index },
   { 33361, GetNamedProgramLocalParameterdvEXT_remap_index },
   { 33398, GetNamedProgramivEXT_remap_index },
   { 33421, TextureBufferEXT_remap_index },
   { 33440, MultiTexBufferEXT_remap_index },
   { 33460, TextureParameterIivEXT_remap_index },
   { 33485, TextureParameterIuivEXT_remap_index },
   { 33511, GetTextureParameterIivEXT_remap_index },
   { 33539, GetTextureParameterIuivEXT_remap_index },
   { 33568, MultiTexParameterIivEXT_remap_index },
   { 33594, MultiTexParameterIuivEXT_remap_index },
   { 33621, GetMultiTexParameterIivEXT_remap_index },
   { 33650, GetMultiTexParameterIuivEXT_remap_index },
   { 33680, NamedProgramLocalParameters4fvEXT_remap_index },
   { 32438, GenerateTextureMipmapEXT_remap_index },
   { 32465, GenerateMultiTexMipmapEXT_remap_index },
   { 32493, NamedRenderbufferStorageMultisampleEXT_remap_index },
   { 32534, NamedCopyBufferSubDataEXT_remap_index },
   { 32562, VertexArrayVertexOffsetEXT_remap_index },
   { 32591, VertexArrayColorOffsetEXT_remap_index },
   { 32619, VertexArrayEdgeFlagOffsetEXT_remap_index },
   { 32650, VertexArrayIndexOffsetEXT_remap_index },
   { 32678, VertexArrayNormalOffsetEXT_remap_index },
   { 32707, VertexArrayTexCoordOffsetEXT_remap_index },
   { 32738, VertexArrayMultiTexCoordOffsetEXT_remap_index },
   { 32774, VertexArrayFogCoordOffsetEXT_remap_index },
   { 32805, VertexArraySecondaryColorOffsetEXT_remap_index },
   { 32842, VertexArrayVertexAttribOffsetEXT_remap_index },
   { 32877, VertexArrayVertexAttribIOffsetEXT_remap_index },
   { 32913, EnableVertexArrayEXT_remap_index },
   { 32936, DisableVertexArrayEXT_remap_index },
   { 32960, EnableVertexArrayAttribEXT_remap_index },
   { 32989, DisableVertexArrayAttribEXT_remap_index },
   { 33019, GetVertexArrayIntegervEXT_remap_index },
   { 33047, GetVertexArrayPointervEXT_remap_index },
   { 33075, GetVertexArrayIntegeri_vEXT_remap_index },
   { 33105, GetVertexArrayPointeri_vEXT_remap_index },
   { 15683, ClearNamedBufferDataEXT_remap_index },
   { 15709, ClearNamedBufferSubDataEXT_remap_index },
   { 16209, NamedFramebufferParameteriEXT_remap_index },
   { 16241, GetNamedFramebufferParameterivEXT_remap_index },
   { 14464, VertexArrayVertexAttribLOffsetEXT_remap_index },
   { 10222, VertexArrayVertexAttribDivisorEXT_remap_index },
   { 16652, TextureBufferRangeEXT_remap_index },
   { 16728, TextureStorage2DMultisampleEXT_remap_index },
   { 16761, TextureStorage3DMultisampleEXT_remap_index },
   { 15944, VertexArrayBindVertexBufferEXT_remap_index },
   { 15977, VertexArrayVertexAttribFormatEXT_remap_index },
   { 16012, VertexArrayVertexAttribIFormatEXT_remap_index },
   { 16048, VertexArrayVertexAttribLFormatEXT_remap_index },
   { 16084, VertexArrayVertexAttribBindingEXT_remap_index },
   { 16120, VertexArrayVertexBindingDivisorEXT_remap_index },
   { 20125, NamedBufferPageCommitmentEXT_remap_index },
   { 10972, NamedStringARB_remap_index },
   { 10989, DeleteNamedStringARB_remap_index },
   { 11012, CompileShaderIncludeARB_remap_index },
   { 11038, IsNamedStringARB_remap_index },
   { 11057, GetNamedStringARB_remap_index },
   { 11077, GetNamedStringivARB_remap_index },
   { 34859, EGLImageTargetTexStorageEXT_remap_index },
   { 34889, EGLImageTargetTextureStorageEXT_remap_index },
   { 29510, CopyImageSubDataNV_remap_index },
   { 37385, ViewportSwizzleNV_remap_index },
   { 34110, AlphaToCoverageDitherControlNV_remap_index },
   { 34509, InternalBufferSubDataCopyMESA_remap_index },
   { 37405, Vertex2hNV_remap_index },
   { 37418, Vertex2hvNV_remap_index },
   { 37432, Vertex3hNV_remap_index },
   { 37445, Vertex3hvNV_remap_index },
   { 37459, Vertex4hNV_remap_index },
   { 37472, Vertex4hvNV_remap_index },
   { 37486, Normal3hNV_remap_index },
   { 37499, Normal3hvNV_remap_index },
   { 37513, Color3hNV_remap_index },
   { 37525, Color3hvNV_remap_index },
   { 37538, Color4hNV_remap_index },
   { 37550, Color4hvNV_remap_index },
   { 37563, TexCoord1hNV_remap_index },
   { 37578, TexCoord1hvNV_remap_index },
   { 37594, TexCoord2hNV_remap_index },
   { 37609, TexCoord2hvNV_remap_index },
   { 37625, TexCoord3hNV_remap_index },
   { 37640, TexCoord3hvNV_remap_index },
   { 37656, TexCoord4hNV_remap_index },
   { 37671, TexCoord4hvNV_remap_index },
   { 37687, MultiTexCoord1hNV_remap_index },
   { 37707, MultiTexCoord1hvNV_remap_index },
   { 37728, MultiTexCoord2hNV_remap_index },
   { 37748, MultiTexCoord2hvNV_remap_index },
   { 37769, MultiTexCoord3hNV_remap_index },
   { 37789, MultiTexCoord3hvNV_remap_index },
   { 37810, MultiTexCoord4hNV_remap_index },
   { 37830, MultiTexCoord4hvNV_remap_index },
   { 38091, FogCoordhNV_remap_index },
   { 38105, FogCoordhvNV_remap_index },
   { 38120, SecondaryColor3hNV_remap_index },
   { 38141, SecondaryColor3hvNV_remap_index },
   { 34541, InternalSetError_remap_index },
   { 37851, VertexAttrib1hNV_remap_index },
   { 37870, VertexAttrib1hvNV_remap_index },
   { 37890, VertexAttrib2hNV_remap_index },
   { 37909, VertexAttrib2hvNV_remap_index },
   { 37929, VertexAttrib3hNV_remap_index },
   { 37948, VertexAttrib3hvNV_remap_index },
   { 37968, VertexAttrib4hNV_remap_index },
   { 37987, VertexAttrib4hvNV_remap_index },
   { 38007, VertexAttribs1hvNV_remap_index },
   { 38028, VertexAttribs2hvNV_remap_index },
   { 38049, VertexAttribs3hvNV_remap_index },
   { 38070, VertexAttribs4hvNV_remap_index },
   { 17513, TexPageCommitmentARB_remap_index },
   { 17536, TexturePageCommitmentEXT_remap_index },
   { 37267, ImportMemoryWin32HandleEXT_remap_index },
   { 37323, ImportSemaphoreWin32HandleEXT_remap_index },
   { 37296, ImportMemoryWin32NameEXT_remap_index },
   { 37355, ImportSemaphoreWin32NameEXT_remap_index },
   { 33755, GetObjectLabelEXT_remap_index },
   { 33738, LabelObjectEXT_remap_index },
   { 34560, DrawArraysUserBuf_remap_index },
   { 34580, DrawElementsUserBuf_remap_index },
   { 34602, MultiDrawArraysUserBuf_remap_index },
   { 34627, MultiDrawElementsUserBuf_remap_index },
   { 34654, DrawArraysInstancedBaseInstanceDrawID_remap_index },
   { 34694, DrawElementsInstancedBaseVertexBaseInstanceDrawID_remap_index },
   { 34746, InternalInvalidateFramebufferAncillaryMESA_remap_index },
   {    -1, -1 }
};

