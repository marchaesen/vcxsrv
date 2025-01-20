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
   /* _mesa_function_pool[10197]: FramebufferTextureMultiviewOVR (will be remapped) */
   "glFramebufferTextureMultiviewOVR\0"
   /* _mesa_function_pool[10230]: NamedFramebufferTextureMultiviewOVR (will be remapped) */
   "glNamedFramebufferTextureMultiviewOVR\0"
   /* _mesa_function_pool[10268]: FramebufferTextureMultisampleMultiviewOVR (will be remapped) */
   "glFramebufferTextureMultisampleMultiviewOVR\0"
   /* _mesa_function_pool[10312]: VertexAttribDivisor (will be remapped) */
   "glVertexAttribDivisorARB\0"
   /* _mesa_function_pool[10337]: VertexArrayVertexAttribDivisorEXT (will be remapped) */
   "glVertexArrayVertexAttribDivisorEXT\0"
   /* _mesa_function_pool[10373]: MapBufferRange (will be remapped) */
   "glMapBufferRange\0"
   /* _mesa_function_pool[10390]: FlushMappedBufferRange (will be remapped) */
   "glFlushMappedBufferRange\0"
   /* _mesa_function_pool[10415]: TexBuffer (will be remapped) */
   "glTexBufferARB\0"
   /* _mesa_function_pool[10430]: BindVertexArray (will be remapped) */
   "glBindVertexArray\0"
   /* _mesa_function_pool[10448]: DeleteVertexArrays (will be remapped) */
   "glDeleteVertexArrays\0"
   /* _mesa_function_pool[10469]: GenVertexArrays (will be remapped) */
   "glGenVertexArrays\0"
   /* _mesa_function_pool[10487]: IsVertexArray (will be remapped) */
   "glIsVertexArray\0"
   /* _mesa_function_pool[10503]: GetUniformIndices (will be remapped) */
   "glGetUniformIndices\0"
   /* _mesa_function_pool[10523]: GetActiveUniformsiv (will be remapped) */
   "glGetActiveUniformsiv\0"
   /* _mesa_function_pool[10545]: GetActiveUniformName (will be remapped) */
   "glGetActiveUniformName\0"
   /* _mesa_function_pool[10568]: GetUniformBlockIndex (will be remapped) */
   "glGetUniformBlockIndex\0"
   /* _mesa_function_pool[10591]: GetActiveUniformBlockiv (will be remapped) */
   "glGetActiveUniformBlockiv\0"
   /* _mesa_function_pool[10617]: GetActiveUniformBlockName (will be remapped) */
   "glGetActiveUniformBlockName\0"
   /* _mesa_function_pool[10645]: UniformBlockBinding (will be remapped) */
   "glUniformBlockBinding\0"
   /* _mesa_function_pool[10667]: CopyBufferSubData (will be remapped) */
   "glCopyBufferSubData\0"
   /* _mesa_function_pool[10687]: DrawElementsBaseVertex (will be remapped) */
   "glDrawElementsBaseVertex\0"
   /* _mesa_function_pool[10712]: DrawRangeElementsBaseVertex (will be remapped) */
   "glDrawRangeElementsBaseVertex\0"
   /* _mesa_function_pool[10742]: MultiDrawElementsBaseVertex (will be remapped) */
   "glMultiDrawElementsBaseVertex\0"
   /* _mesa_function_pool[10772]: DrawElementsInstancedBaseVertex (will be remapped) */
   "glDrawElementsInstancedBaseVertex\0"
   /* _mesa_function_pool[10806]: FenceSync (will be remapped) */
   "glFenceSync\0"
   /* _mesa_function_pool[10818]: IsSync (will be remapped) */
   "glIsSync\0"
   /* _mesa_function_pool[10827]: DeleteSync (will be remapped) */
   "glDeleteSync\0"
   /* _mesa_function_pool[10840]: ClientWaitSync (will be remapped) */
   "glClientWaitSync\0"
   /* _mesa_function_pool[10857]: WaitSync (will be remapped) */
   "glWaitSync\0"
   /* _mesa_function_pool[10868]: GetInteger64v (will be remapped) */
   "glGetInteger64v\0"
   /* _mesa_function_pool[10884]: GetSynciv (will be remapped) */
   "glGetSynciv\0"
   /* _mesa_function_pool[10896]: TexImage2DMultisample (will be remapped) */
   "glTexImage2DMultisample\0"
   /* _mesa_function_pool[10920]: TexImage3DMultisample (will be remapped) */
   "glTexImage3DMultisample\0"
   /* _mesa_function_pool[10944]: GetMultisamplefv (will be remapped) */
   "glGetMultisamplefv\0"
   /* _mesa_function_pool[10963]: SampleMaski (will be remapped) */
   "glSampleMaski\0"
   /* _mesa_function_pool[10977]: BlendEquationiARB (will be remapped) */
   "glBlendEquationiARB\0"
   /* _mesa_function_pool[10997]: BlendEquationSeparateiARB (will be remapped) */
   "glBlendEquationSeparateiARB\0"
   /* _mesa_function_pool[11025]: BlendFunciARB (will be remapped) */
   "glBlendFunciARB\0"
   /* _mesa_function_pool[11041]: BlendFuncSeparateiARB (will be remapped) */
   "glBlendFuncSeparateiARB\0"
   /* _mesa_function_pool[11065]: MinSampleShading (will be remapped) */
   "glMinSampleShadingARB\0"
   /* _mesa_function_pool[11087]: NamedStringARB (will be remapped) */
   "glNamedStringARB\0"
   /* _mesa_function_pool[11104]: DeleteNamedStringARB (will be remapped) */
   "glDeleteNamedStringARB\0"
   /* _mesa_function_pool[11127]: CompileShaderIncludeARB (will be remapped) */
   "glCompileShaderIncludeARB\0"
   /* _mesa_function_pool[11153]: IsNamedStringARB (will be remapped) */
   "glIsNamedStringARB\0"
   /* _mesa_function_pool[11172]: GetNamedStringARB (will be remapped) */
   "glGetNamedStringARB\0"
   /* _mesa_function_pool[11192]: GetNamedStringivARB (will be remapped) */
   "glGetNamedStringivARB\0"
   /* _mesa_function_pool[11214]: BindFragDataLocationIndexed (will be remapped) */
   "glBindFragDataLocationIndexed\0"
   /* _mesa_function_pool[11244]: GetFragDataIndex (will be remapped) */
   "glGetFragDataIndex\0"
   /* _mesa_function_pool[11263]: GenSamplers (will be remapped) */
   "glGenSamplers\0"
   /* _mesa_function_pool[11277]: DeleteSamplers (will be remapped) */
   "glDeleteSamplers\0"
   /* _mesa_function_pool[11294]: IsSampler (will be remapped) */
   "glIsSampler\0"
   /* _mesa_function_pool[11306]: BindSampler (will be remapped) */
   "glBindSampler\0"
   /* _mesa_function_pool[11320]: SamplerParameteri (will be remapped) */
   "glSamplerParameteri\0"
   /* _mesa_function_pool[11340]: SamplerParameterf (will be remapped) */
   "glSamplerParameterf\0"
   /* _mesa_function_pool[11360]: SamplerParameteriv (will be remapped) */
   "glSamplerParameteriv\0"
   /* _mesa_function_pool[11381]: SamplerParameterfv (will be remapped) */
   "glSamplerParameterfv\0"
   /* _mesa_function_pool[11402]: SamplerParameterIiv (will be remapped) */
   "glSamplerParameterIiv\0"
   /* _mesa_function_pool[11424]: SamplerParameterIuiv (will be remapped) */
   "glSamplerParameterIuiv\0"
   /* _mesa_function_pool[11447]: GetSamplerParameteriv (will be remapped) */
   "glGetSamplerParameteriv\0"
   /* _mesa_function_pool[11471]: GetSamplerParameterfv (will be remapped) */
   "glGetSamplerParameterfv\0"
   /* _mesa_function_pool[11495]: GetSamplerParameterIiv (will be remapped) */
   "glGetSamplerParameterIiv\0"
   /* _mesa_function_pool[11520]: GetSamplerParameterIuiv (will be remapped) */
   "glGetSamplerParameterIuiv\0"
   /* _mesa_function_pool[11546]: GetQueryObjecti64v (will be remapped) */
   "glGetQueryObjecti64v\0"
   /* _mesa_function_pool[11567]: GetQueryObjectui64v (will be remapped) */
   "glGetQueryObjectui64v\0"
   /* _mesa_function_pool[11589]: QueryCounter (will be remapped) */
   "glQueryCounter\0"
   /* _mesa_function_pool[11604]: VertexP2ui (will be remapped) */
   "glVertexP2ui\0"
   /* _mesa_function_pool[11617]: VertexP3ui (will be remapped) */
   "glVertexP3ui\0"
   /* _mesa_function_pool[11630]: VertexP4ui (will be remapped) */
   "glVertexP4ui\0"
   /* _mesa_function_pool[11643]: VertexP2uiv (will be remapped) */
   "glVertexP2uiv\0"
   /* _mesa_function_pool[11657]: VertexP3uiv (will be remapped) */
   "glVertexP3uiv\0"
   /* _mesa_function_pool[11671]: VertexP4uiv (will be remapped) */
   "glVertexP4uiv\0"
   /* _mesa_function_pool[11685]: TexCoordP1ui (will be remapped) */
   "glTexCoordP1ui\0"
   /* _mesa_function_pool[11700]: TexCoordP2ui (will be remapped) */
   "glTexCoordP2ui\0"
   /* _mesa_function_pool[11715]: TexCoordP3ui (will be remapped) */
   "glTexCoordP3ui\0"
   /* _mesa_function_pool[11730]: TexCoordP4ui (will be remapped) */
   "glTexCoordP4ui\0"
   /* _mesa_function_pool[11745]: TexCoordP1uiv (will be remapped) */
   "glTexCoordP1uiv\0"
   /* _mesa_function_pool[11761]: TexCoordP2uiv (will be remapped) */
   "glTexCoordP2uiv\0"
   /* _mesa_function_pool[11777]: TexCoordP3uiv (will be remapped) */
   "glTexCoordP3uiv\0"
   /* _mesa_function_pool[11793]: TexCoordP4uiv (will be remapped) */
   "glTexCoordP4uiv\0"
   /* _mesa_function_pool[11809]: MultiTexCoordP1ui (will be remapped) */
   "glMultiTexCoordP1ui\0"
   /* _mesa_function_pool[11829]: MultiTexCoordP2ui (will be remapped) */
   "glMultiTexCoordP2ui\0"
   /* _mesa_function_pool[11849]: MultiTexCoordP3ui (will be remapped) */
   "glMultiTexCoordP3ui\0"
   /* _mesa_function_pool[11869]: MultiTexCoordP4ui (will be remapped) */
   "glMultiTexCoordP4ui\0"
   /* _mesa_function_pool[11889]: MultiTexCoordP1uiv (will be remapped) */
   "glMultiTexCoordP1uiv\0"
   /* _mesa_function_pool[11910]: MultiTexCoordP2uiv (will be remapped) */
   "glMultiTexCoordP2uiv\0"
   /* _mesa_function_pool[11931]: MultiTexCoordP3uiv (will be remapped) */
   "glMultiTexCoordP3uiv\0"
   /* _mesa_function_pool[11952]: MultiTexCoordP4uiv (will be remapped) */
   "glMultiTexCoordP4uiv\0"
   /* _mesa_function_pool[11973]: NormalP3ui (will be remapped) */
   "glNormalP3ui\0"
   /* _mesa_function_pool[11986]: NormalP3uiv (will be remapped) */
   "glNormalP3uiv\0"
   /* _mesa_function_pool[12000]: ColorP3ui (will be remapped) */
   "glColorP3ui\0"
   /* _mesa_function_pool[12012]: ColorP4ui (will be remapped) */
   "glColorP4ui\0"
   /* _mesa_function_pool[12024]: ColorP3uiv (will be remapped) */
   "glColorP3uiv\0"
   /* _mesa_function_pool[12037]: ColorP4uiv (will be remapped) */
   "glColorP4uiv\0"
   /* _mesa_function_pool[12050]: SecondaryColorP3ui (will be remapped) */
   "glSecondaryColorP3ui\0"
   /* _mesa_function_pool[12071]: SecondaryColorP3uiv (will be remapped) */
   "glSecondaryColorP3uiv\0"
   /* _mesa_function_pool[12093]: VertexAttribP1ui (will be remapped) */
   "glVertexAttribP1ui\0"
   /* _mesa_function_pool[12112]: VertexAttribP2ui (will be remapped) */
   "glVertexAttribP2ui\0"
   /* _mesa_function_pool[12131]: VertexAttribP3ui (will be remapped) */
   "glVertexAttribP3ui\0"
   /* _mesa_function_pool[12150]: VertexAttribP4ui (will be remapped) */
   "glVertexAttribP4ui\0"
   /* _mesa_function_pool[12169]: VertexAttribP1uiv (will be remapped) */
   "glVertexAttribP1uiv\0"
   /* _mesa_function_pool[12189]: VertexAttribP2uiv (will be remapped) */
   "glVertexAttribP2uiv\0"
   /* _mesa_function_pool[12209]: VertexAttribP3uiv (will be remapped) */
   "glVertexAttribP3uiv\0"
   /* _mesa_function_pool[12229]: VertexAttribP4uiv (will be remapped) */
   "glVertexAttribP4uiv\0"
   /* _mesa_function_pool[12249]: GetSubroutineUniformLocation (will be remapped) */
   "glGetSubroutineUniformLocation\0"
   /* _mesa_function_pool[12280]: GetSubroutineIndex (will be remapped) */
   "glGetSubroutineIndex\0"
   /* _mesa_function_pool[12301]: GetActiveSubroutineUniformiv (will be remapped) */
   "glGetActiveSubroutineUniformiv\0"
   /* _mesa_function_pool[12332]: GetActiveSubroutineUniformName (will be remapped) */
   "glGetActiveSubroutineUniformName\0"
   /* _mesa_function_pool[12365]: GetActiveSubroutineName (will be remapped) */
   "glGetActiveSubroutineName\0"
   /* _mesa_function_pool[12391]: UniformSubroutinesuiv (will be remapped) */
   "glUniformSubroutinesuiv\0"
   /* _mesa_function_pool[12415]: GetUniformSubroutineuiv (will be remapped) */
   "glGetUniformSubroutineuiv\0"
   /* _mesa_function_pool[12441]: GetProgramStageiv (will be remapped) */
   "glGetProgramStageiv\0"
   /* _mesa_function_pool[12461]: PatchParameteri (will be remapped) */
   "glPatchParameteri\0"
   /* _mesa_function_pool[12479]: PatchParameterfv (will be remapped) */
   "glPatchParameterfv\0"
   /* _mesa_function_pool[12498]: DrawArraysIndirect (will be remapped) */
   "glDrawArraysIndirect\0"
   /* _mesa_function_pool[12519]: DrawElementsIndirect (will be remapped) */
   "glDrawElementsIndirect\0"
   /* _mesa_function_pool[12542]: MultiDrawArraysIndirect (will be remapped) */
   "glMultiDrawArraysIndirect\0"
   /* _mesa_function_pool[12568]: MultiDrawElementsIndirect (will be remapped) */
   "glMultiDrawElementsIndirect\0"
   /* _mesa_function_pool[12596]: Uniform1d (will be remapped) */
   "glUniform1d\0"
   /* _mesa_function_pool[12608]: Uniform2d (will be remapped) */
   "glUniform2d\0"
   /* _mesa_function_pool[12620]: Uniform3d (will be remapped) */
   "glUniform3d\0"
   /* _mesa_function_pool[12632]: Uniform4d (will be remapped) */
   "glUniform4d\0"
   /* _mesa_function_pool[12644]: Uniform1dv (will be remapped) */
   "glUniform1dv\0"
   /* _mesa_function_pool[12657]: Uniform2dv (will be remapped) */
   "glUniform2dv\0"
   /* _mesa_function_pool[12670]: Uniform3dv (will be remapped) */
   "glUniform3dv\0"
   /* _mesa_function_pool[12683]: Uniform4dv (will be remapped) */
   "glUniform4dv\0"
   /* _mesa_function_pool[12696]: UniformMatrix2dv (will be remapped) */
   "glUniformMatrix2dv\0"
   /* _mesa_function_pool[12715]: UniformMatrix3dv (will be remapped) */
   "glUniformMatrix3dv\0"
   /* _mesa_function_pool[12734]: UniformMatrix4dv (will be remapped) */
   "glUniformMatrix4dv\0"
   /* _mesa_function_pool[12753]: UniformMatrix2x3dv (will be remapped) */
   "glUniformMatrix2x3dv\0"
   /* _mesa_function_pool[12774]: UniformMatrix2x4dv (will be remapped) */
   "glUniformMatrix2x4dv\0"
   /* _mesa_function_pool[12795]: UniformMatrix3x2dv (will be remapped) */
   "glUniformMatrix3x2dv\0"
   /* _mesa_function_pool[12816]: UniformMatrix3x4dv (will be remapped) */
   "glUniformMatrix3x4dv\0"
   /* _mesa_function_pool[12837]: UniformMatrix4x2dv (will be remapped) */
   "glUniformMatrix4x2dv\0"
   /* _mesa_function_pool[12858]: UniformMatrix4x3dv (will be remapped) */
   "glUniformMatrix4x3dv\0"
   /* _mesa_function_pool[12879]: GetUniformdv (will be remapped) */
   "glGetUniformdv\0"
   /* _mesa_function_pool[12894]: ProgramUniform1d (will be remapped) */
   "glProgramUniform1dEXT\0"
   /* _mesa_function_pool[12916]: ProgramUniform2d (will be remapped) */
   "glProgramUniform2dEXT\0"
   /* _mesa_function_pool[12938]: ProgramUniform3d (will be remapped) */
   "glProgramUniform3dEXT\0"
   /* _mesa_function_pool[12960]: ProgramUniform4d (will be remapped) */
   "glProgramUniform4dEXT\0"
   /* _mesa_function_pool[12982]: ProgramUniform1dv (will be remapped) */
   "glProgramUniform1dvEXT\0"
   /* _mesa_function_pool[13005]: ProgramUniform2dv (will be remapped) */
   "glProgramUniform2dvEXT\0"
   /* _mesa_function_pool[13028]: ProgramUniform3dv (will be remapped) */
   "glProgramUniform3dvEXT\0"
   /* _mesa_function_pool[13051]: ProgramUniform4dv (will be remapped) */
   "glProgramUniform4dvEXT\0"
   /* _mesa_function_pool[13074]: ProgramUniformMatrix2dv (will be remapped) */
   "glProgramUniformMatrix2dvEXT\0"
   /* _mesa_function_pool[13103]: ProgramUniformMatrix3dv (will be remapped) */
   "glProgramUniformMatrix3dvEXT\0"
   /* _mesa_function_pool[13132]: ProgramUniformMatrix4dv (will be remapped) */
   "glProgramUniformMatrix4dvEXT\0"
   /* _mesa_function_pool[13161]: ProgramUniformMatrix2x3dv (will be remapped) */
   "glProgramUniformMatrix2x3dvEXT\0"
   /* _mesa_function_pool[13192]: ProgramUniformMatrix2x4dv (will be remapped) */
   "glProgramUniformMatrix2x4dvEXT\0"
   /* _mesa_function_pool[13223]: ProgramUniformMatrix3x2dv (will be remapped) */
   "glProgramUniformMatrix3x2dvEXT\0"
   /* _mesa_function_pool[13254]: ProgramUniformMatrix3x4dv (will be remapped) */
   "glProgramUniformMatrix3x4dvEXT\0"
   /* _mesa_function_pool[13285]: ProgramUniformMatrix4x2dv (will be remapped) */
   "glProgramUniformMatrix4x2dvEXT\0"
   /* _mesa_function_pool[13316]: ProgramUniformMatrix4x3dv (will be remapped) */
   "glProgramUniformMatrix4x3dvEXT\0"
   /* _mesa_function_pool[13347]: DrawTransformFeedbackStream (will be remapped) */
   "glDrawTransformFeedbackStream\0"
   /* _mesa_function_pool[13377]: BeginQueryIndexed (will be remapped) */
   "glBeginQueryIndexed\0"
   /* _mesa_function_pool[13397]: EndQueryIndexed (will be remapped) */
   "glEndQueryIndexed\0"
   /* _mesa_function_pool[13415]: GetQueryIndexediv (will be remapped) */
   "glGetQueryIndexediv\0"
   /* _mesa_function_pool[13435]: UseProgramStages (will be remapped) */
   "glUseProgramStages\0"
   /* _mesa_function_pool[13454]: ActiveShaderProgram (will be remapped) */
   "glActiveShaderProgram\0"
   /* _mesa_function_pool[13476]: CreateShaderProgramv (will be remapped) */
   "glCreateShaderProgramv\0"
   /* _mesa_function_pool[13499]: BindProgramPipeline (will be remapped) */
   "glBindProgramPipeline\0"
   /* _mesa_function_pool[13521]: DeleteProgramPipelines (will be remapped) */
   "glDeleteProgramPipelines\0"
   /* _mesa_function_pool[13546]: GenProgramPipelines (will be remapped) */
   "glGenProgramPipelines\0"
   /* _mesa_function_pool[13568]: IsProgramPipeline (will be remapped) */
   "glIsProgramPipeline\0"
   /* _mesa_function_pool[13588]: GetProgramPipelineiv (will be remapped) */
   "glGetProgramPipelineiv\0"
   /* _mesa_function_pool[13611]: ProgramUniform1i (will be remapped) */
   "glProgramUniform1i\0"
   /* _mesa_function_pool[13630]: ProgramUniform2i (will be remapped) */
   "glProgramUniform2i\0"
   /* _mesa_function_pool[13649]: ProgramUniform3i (will be remapped) */
   "glProgramUniform3i\0"
   /* _mesa_function_pool[13668]: ProgramUniform4i (will be remapped) */
   "glProgramUniform4i\0"
   /* _mesa_function_pool[13687]: ProgramUniform1ui (will be remapped) */
   "glProgramUniform1ui\0"
   /* _mesa_function_pool[13707]: ProgramUniform2ui (will be remapped) */
   "glProgramUniform2ui\0"
   /* _mesa_function_pool[13727]: ProgramUniform3ui (will be remapped) */
   "glProgramUniform3ui\0"
   /* _mesa_function_pool[13747]: ProgramUniform4ui (will be remapped) */
   "glProgramUniform4ui\0"
   /* _mesa_function_pool[13767]: ProgramUniform1f (will be remapped) */
   "glProgramUniform1f\0"
   /* _mesa_function_pool[13786]: ProgramUniform2f (will be remapped) */
   "glProgramUniform2f\0"
   /* _mesa_function_pool[13805]: ProgramUniform3f (will be remapped) */
   "glProgramUniform3f\0"
   /* _mesa_function_pool[13824]: ProgramUniform4f (will be remapped) */
   "glProgramUniform4f\0"
   /* _mesa_function_pool[13843]: ProgramUniform1iv (will be remapped) */
   "glProgramUniform1iv\0"
   /* _mesa_function_pool[13863]: ProgramUniform2iv (will be remapped) */
   "glProgramUniform2iv\0"
   /* _mesa_function_pool[13883]: ProgramUniform3iv (will be remapped) */
   "glProgramUniform3iv\0"
   /* _mesa_function_pool[13903]: ProgramUniform4iv (will be remapped) */
   "glProgramUniform4iv\0"
   /* _mesa_function_pool[13923]: ProgramUniform1uiv (will be remapped) */
   "glProgramUniform1uiv\0"
   /* _mesa_function_pool[13944]: ProgramUniform2uiv (will be remapped) */
   "glProgramUniform2uiv\0"
   /* _mesa_function_pool[13965]: ProgramUniform3uiv (will be remapped) */
   "glProgramUniform3uiv\0"
   /* _mesa_function_pool[13986]: ProgramUniform4uiv (will be remapped) */
   "glProgramUniform4uiv\0"
   /* _mesa_function_pool[14007]: ProgramUniform1fv (will be remapped) */
   "glProgramUniform1fv\0"
   /* _mesa_function_pool[14027]: ProgramUniform2fv (will be remapped) */
   "glProgramUniform2fv\0"
   /* _mesa_function_pool[14047]: ProgramUniform3fv (will be remapped) */
   "glProgramUniform3fv\0"
   /* _mesa_function_pool[14067]: ProgramUniform4fv (will be remapped) */
   "glProgramUniform4fv\0"
   /* _mesa_function_pool[14087]: ProgramUniformMatrix2fv (will be remapped) */
   "glProgramUniformMatrix2fv\0"
   /* _mesa_function_pool[14113]: ProgramUniformMatrix3fv (will be remapped) */
   "glProgramUniformMatrix3fv\0"
   /* _mesa_function_pool[14139]: ProgramUniformMatrix4fv (will be remapped) */
   "glProgramUniformMatrix4fv\0"
   /* _mesa_function_pool[14165]: ProgramUniformMatrix2x3fv (will be remapped) */
   "glProgramUniformMatrix2x3fv\0"
   /* _mesa_function_pool[14193]: ProgramUniformMatrix3x2fv (will be remapped) */
   "glProgramUniformMatrix3x2fv\0"
   /* _mesa_function_pool[14221]: ProgramUniformMatrix2x4fv (will be remapped) */
   "glProgramUniformMatrix2x4fv\0"
   /* _mesa_function_pool[14249]: ProgramUniformMatrix4x2fv (will be remapped) */
   "glProgramUniformMatrix4x2fv\0"
   /* _mesa_function_pool[14277]: ProgramUniformMatrix3x4fv (will be remapped) */
   "glProgramUniformMatrix3x4fv\0"
   /* _mesa_function_pool[14305]: ProgramUniformMatrix4x3fv (will be remapped) */
   "glProgramUniformMatrix4x3fv\0"
   /* _mesa_function_pool[14333]: ValidateProgramPipeline (will be remapped) */
   "glValidateProgramPipeline\0"
   /* _mesa_function_pool[14359]: GetProgramPipelineInfoLog (will be remapped) */
   "glGetProgramPipelineInfoLog\0"
   /* _mesa_function_pool[14387]: VertexAttribL1d (will be remapped) */
   "glVertexAttribL1d\0"
   /* _mesa_function_pool[14405]: VertexAttribL2d (will be remapped) */
   "glVertexAttribL2d\0"
   /* _mesa_function_pool[14423]: VertexAttribL3d (will be remapped) */
   "glVertexAttribL3d\0"
   /* _mesa_function_pool[14441]: VertexAttribL4d (will be remapped) */
   "glVertexAttribL4d\0"
   /* _mesa_function_pool[14459]: VertexAttribL1dv (will be remapped) */
   "glVertexAttribL1dv\0"
   /* _mesa_function_pool[14478]: VertexAttribL2dv (will be remapped) */
   "glVertexAttribL2dv\0"
   /* _mesa_function_pool[14497]: VertexAttribL3dv (will be remapped) */
   "glVertexAttribL3dv\0"
   /* _mesa_function_pool[14516]: VertexAttribL4dv (will be remapped) */
   "glVertexAttribL4dv\0"
   /* _mesa_function_pool[14535]: VertexAttribLPointer (will be remapped) */
   "glVertexAttribLPointer\0"
   /* _mesa_function_pool[14558]: GetVertexAttribLdv (will be remapped) */
   "glGetVertexAttribLdv\0"
   /* _mesa_function_pool[14579]: VertexArrayVertexAttribLOffsetEXT (will be remapped) */
   "glVertexArrayVertexAttribLOffsetEXT\0"
   /* _mesa_function_pool[14615]: GetShaderPrecisionFormat (will be remapped) */
   "glGetShaderPrecisionFormat\0"
   /* _mesa_function_pool[14642]: ReleaseShaderCompiler (will be remapped) */
   "glReleaseShaderCompiler\0"
   /* _mesa_function_pool[14666]: ShaderBinary (will be remapped) */
   "glShaderBinary\0"
   /* _mesa_function_pool[14681]: ClearDepthf (will be remapped) */
   "glClearDepthf\0"
   /* _mesa_function_pool[14695]: DepthRangef (will be remapped) */
   "glDepthRangef\0"
   /* _mesa_function_pool[14709]: GetProgramBinary (will be remapped) */
   "glGetProgramBinary\0"
   /* _mesa_function_pool[14728]: ProgramBinary (will be remapped) */
   "glProgramBinary\0"
   /* _mesa_function_pool[14744]: ProgramParameteri (will be remapped) */
   "glProgramParameteri\0"
   /* _mesa_function_pool[14764]: DebugMessageControl (will be remapped) */
   "glDebugMessageControlARB\0"
   /* _mesa_function_pool[14789]: DebugMessageInsert (will be remapped) */
   "glDebugMessageInsertARB\0"
   /* _mesa_function_pool[14813]: DebugMessageCallback (will be remapped) */
   "glDebugMessageCallbackARB\0"
   /* _mesa_function_pool[14839]: GetDebugMessageLog (will be remapped) */
   "glGetDebugMessageLogARB\0"
   /* _mesa_function_pool[14863]: GetGraphicsResetStatusARB (will be remapped) */
   "glGetGraphicsResetStatusARB\0"
   /* _mesa_function_pool[14891]: GetnMapdvARB (will be remapped) */
   "glGetnMapdvARB\0"
   /* _mesa_function_pool[14906]: GetnMapfvARB (will be remapped) */
   "glGetnMapfvARB\0"
   /* _mesa_function_pool[14921]: GetnMapivARB (will be remapped) */
   "glGetnMapivARB\0"
   /* _mesa_function_pool[14936]: GetnPixelMapfvARB (will be remapped) */
   "glGetnPixelMapfvARB\0"
   /* _mesa_function_pool[14956]: GetnPixelMapuivARB (will be remapped) */
   "glGetnPixelMapuivARB\0"
   /* _mesa_function_pool[14977]: GetnPixelMapusvARB (will be remapped) */
   "glGetnPixelMapusvARB\0"
   /* _mesa_function_pool[14998]: GetnPolygonStippleARB (will be remapped) */
   "glGetnPolygonStippleARB\0"
   /* _mesa_function_pool[15022]: GetnTexImageARB (will be remapped) */
   "glGetnTexImageARB\0"
   /* _mesa_function_pool[15040]: ReadnPixelsARB (will be remapped) */
   "glReadnPixelsARB\0"
   /* _mesa_function_pool[15057]: GetnColorTableARB (will be remapped) */
   "glGetnColorTableARB\0"
   /* _mesa_function_pool[15077]: GetnConvolutionFilterARB (will be remapped) */
   "glGetnConvolutionFilterARB\0"
   /* _mesa_function_pool[15104]: GetnSeparableFilterARB (will be remapped) */
   "glGetnSeparableFilterARB\0"
   /* _mesa_function_pool[15129]: GetnHistogramARB (will be remapped) */
   "glGetnHistogramARB\0"
   /* _mesa_function_pool[15148]: GetnMinmaxARB (will be remapped) */
   "glGetnMinmaxARB\0"
   /* _mesa_function_pool[15164]: GetnCompressedTexImageARB (will be remapped) */
   "glGetnCompressedTexImageARB\0"
   /* _mesa_function_pool[15192]: GetnUniformfvARB (will be remapped) */
   "glGetnUniformfvARB\0"
   /* _mesa_function_pool[15211]: GetnUniformivARB (will be remapped) */
   "glGetnUniformivARB\0"
   /* _mesa_function_pool[15230]: GetnUniformuivARB (will be remapped) */
   "glGetnUniformuivARB\0"
   /* _mesa_function_pool[15250]: GetnUniformdvARB (will be remapped) */
   "glGetnUniformdvARB\0"
   /* _mesa_function_pool[15269]: DrawArraysInstancedBaseInstance (will be remapped) */
   "glDrawArraysInstancedBaseInstance\0"
   /* _mesa_function_pool[15303]: DrawElementsInstancedBaseInstance (will be remapped) */
   "glDrawElementsInstancedBaseInstance\0"
   /* _mesa_function_pool[15339]: DrawElementsInstancedBaseVertexBaseInstance (will be remapped) */
   "glDrawElementsInstancedBaseVertexBaseInstance\0"
   /* _mesa_function_pool[15385]: DrawTransformFeedbackInstanced (will be remapped) */
   "glDrawTransformFeedbackInstanced\0"
   /* _mesa_function_pool[15418]: DrawTransformFeedbackStreamInstanced (will be remapped) */
   "glDrawTransformFeedbackStreamInstanced\0"
   /* _mesa_function_pool[15457]: GetInternalformativ (will be remapped) */
   "glGetInternalformativ\0"
   /* _mesa_function_pool[15479]: GetActiveAtomicCounterBufferiv (will be remapped) */
   "glGetActiveAtomicCounterBufferiv\0"
   /* _mesa_function_pool[15512]: BindImageTexture (will be remapped) */
   "glBindImageTexture\0"
   /* _mesa_function_pool[15531]: MemoryBarrier (will be remapped) */
   "glMemoryBarrier\0"
   /* _mesa_function_pool[15547]: TexStorage1D (will be remapped) */
   "glTexStorage1D\0"
   /* _mesa_function_pool[15562]: TexStorage2D (will be remapped) */
   "glTexStorage2D\0"
   /* _mesa_function_pool[15577]: TexStorage3D (will be remapped) */
   "glTexStorage3D\0"
   /* _mesa_function_pool[15592]: PushDebugGroup (will be remapped) */
   "glPushDebugGroup\0"
   /* _mesa_function_pool[15609]: PopDebugGroup (will be remapped) */
   "glPopDebugGroup\0"
   /* _mesa_function_pool[15625]: ObjectLabel (will be remapped) */
   "glObjectLabel\0"
   /* _mesa_function_pool[15639]: GetObjectLabel (will be remapped) */
   "glGetObjectLabel\0"
   /* _mesa_function_pool[15656]: ObjectPtrLabel (will be remapped) */
   "glObjectPtrLabel\0"
   /* _mesa_function_pool[15673]: GetObjectPtrLabel (will be remapped) */
   "glGetObjectPtrLabel\0"
   /* _mesa_function_pool[15693]: ClearBufferData (will be remapped) */
   "glClearBufferData\0"
   /* _mesa_function_pool[15711]: ClearBufferSubData (will be remapped) */
   "glClearBufferSubData\0"
   /* _mesa_function_pool[15732]: ClearNamedBufferDataEXT (will be remapped) */
   "glClearNamedBufferDataEXT\0"
   /* _mesa_function_pool[15758]: ClearNamedBufferSubDataEXT (will be remapped) */
   "glClearNamedBufferSubDataEXT\0"
   /* _mesa_function_pool[15787]: DispatchCompute (will be remapped) */
   "glDispatchCompute\0"
   /* _mesa_function_pool[15805]: DispatchComputeIndirect (will be remapped) */
   "glDispatchComputeIndirect\0"
   /* _mesa_function_pool[15831]: CopyImageSubData (will be remapped) */
   "glCopyImageSubData\0"
   /* _mesa_function_pool[15850]: TextureView (will be remapped) */
   "glTextureView\0"
   /* _mesa_function_pool[15864]: BindVertexBuffer (will be remapped) */
   "glBindVertexBuffer\0"
   /* _mesa_function_pool[15883]: VertexAttribFormat (will be remapped) */
   "glVertexAttribFormat\0"
   /* _mesa_function_pool[15904]: VertexAttribIFormat (will be remapped) */
   "glVertexAttribIFormat\0"
   /* _mesa_function_pool[15926]: VertexAttribLFormat (will be remapped) */
   "glVertexAttribLFormat\0"
   /* _mesa_function_pool[15948]: VertexAttribBinding (will be remapped) */
   "glVertexAttribBinding\0"
   /* _mesa_function_pool[15970]: VertexBindingDivisor (will be remapped) */
   "glVertexBindingDivisor\0"
   /* _mesa_function_pool[15993]: VertexArrayBindVertexBufferEXT (will be remapped) */
   "glVertexArrayBindVertexBufferEXT\0"
   /* _mesa_function_pool[16026]: VertexArrayVertexAttribFormatEXT (will be remapped) */
   "glVertexArrayVertexAttribFormatEXT\0"
   /* _mesa_function_pool[16061]: VertexArrayVertexAttribIFormatEXT (will be remapped) */
   "glVertexArrayVertexAttribIFormatEXT\0"
   /* _mesa_function_pool[16097]: VertexArrayVertexAttribLFormatEXT (will be remapped) */
   "glVertexArrayVertexAttribLFormatEXT\0"
   /* _mesa_function_pool[16133]: VertexArrayVertexAttribBindingEXT (will be remapped) */
   "glVertexArrayVertexAttribBindingEXT\0"
   /* _mesa_function_pool[16169]: VertexArrayVertexBindingDivisorEXT (will be remapped) */
   "glVertexArrayVertexBindingDivisorEXT\0"
   /* _mesa_function_pool[16206]: FramebufferParameteri (will be remapped) */
   "glFramebufferParameteri\0"
   /* _mesa_function_pool[16230]: GetFramebufferParameteriv (will be remapped) */
   "glGetFramebufferParameteriv\0"
   /* _mesa_function_pool[16258]: NamedFramebufferParameteriEXT (will be remapped) */
   "glNamedFramebufferParameteriEXT\0"
   /* _mesa_function_pool[16290]: GetNamedFramebufferParameterivEXT (will be remapped) */
   "glGetNamedFramebufferParameterivEXT\0"
   /* _mesa_function_pool[16326]: GetInternalformati64v (will be remapped) */
   "glGetInternalformati64v\0"
   /* _mesa_function_pool[16350]: InvalidateTexSubImage (will be remapped) */
   "glInvalidateTexSubImage\0"
   /* _mesa_function_pool[16374]: InvalidateTexImage (will be remapped) */
   "glInvalidateTexImage\0"
   /* _mesa_function_pool[16395]: InvalidateBufferSubData (will be remapped) */
   "glInvalidateBufferSubData\0"
   /* _mesa_function_pool[16421]: InvalidateBufferData (will be remapped) */
   "glInvalidateBufferData\0"
   /* _mesa_function_pool[16444]: InvalidateSubFramebuffer (will be remapped) */
   "glInvalidateSubFramebuffer\0"
   /* _mesa_function_pool[16471]: InvalidateFramebuffer (will be remapped) */
   "glInvalidateFramebuffer\0"
   /* _mesa_function_pool[16495]: GetProgramInterfaceiv (will be remapped) */
   "glGetProgramInterfaceiv\0"
   /* _mesa_function_pool[16519]: GetProgramResourceIndex (will be remapped) */
   "glGetProgramResourceIndex\0"
   /* _mesa_function_pool[16545]: GetProgramResourceName (will be remapped) */
   "glGetProgramResourceName\0"
   /* _mesa_function_pool[16570]: GetProgramResourceiv (will be remapped) */
   "glGetProgramResourceiv\0"
   /* _mesa_function_pool[16593]: GetProgramResourceLocation (will be remapped) */
   "glGetProgramResourceLocation\0"
   /* _mesa_function_pool[16622]: GetProgramResourceLocationIndex (will be remapped) */
   "glGetProgramResourceLocationIndex\0"
   /* _mesa_function_pool[16656]: ShaderStorageBlockBinding (will be remapped) */
   "glShaderStorageBlockBinding\0"
   /* _mesa_function_pool[16684]: TexBufferRange (will be remapped) */
   "glTexBufferRange\0"
   /* _mesa_function_pool[16701]: TextureBufferRangeEXT (will be remapped) */
   "glTextureBufferRangeEXT\0"
   /* _mesa_function_pool[16725]: TexStorage2DMultisample (will be remapped) */
   "glTexStorage2DMultisample\0"
   /* _mesa_function_pool[16751]: TexStorage3DMultisample (will be remapped) */
   "glTexStorage3DMultisample\0"
   /* _mesa_function_pool[16777]: TextureStorage2DMultisampleEXT (will be remapped) */
   "glTextureStorage2DMultisampleEXT\0"
   /* _mesa_function_pool[16810]: TextureStorage3DMultisampleEXT (will be remapped) */
   "glTextureStorage3DMultisampleEXT\0"
   /* _mesa_function_pool[16843]: BufferStorage (will be remapped) */
   "glBufferStorage\0"
   /* _mesa_function_pool[16859]: NamedBufferStorageEXT (will be remapped) */
   "glNamedBufferStorageEXT\0"
   /* _mesa_function_pool[16883]: ClearTexImage (will be remapped) */
   "glClearTexImage\0"
   /* _mesa_function_pool[16899]: ClearTexSubImage (will be remapped) */
   "glClearTexSubImage\0"
   /* _mesa_function_pool[16918]: BindBuffersBase (will be remapped) */
   "glBindBuffersBase\0"
   /* _mesa_function_pool[16936]: BindBuffersRange (will be remapped) */
   "glBindBuffersRange\0"
   /* _mesa_function_pool[16955]: BindTextures (will be remapped) */
   "glBindTextures\0"
   /* _mesa_function_pool[16970]: BindSamplers (will be remapped) */
   "glBindSamplers\0"
   /* _mesa_function_pool[16985]: BindImageTextures (will be remapped) */
   "glBindImageTextures\0"
   /* _mesa_function_pool[17005]: BindVertexBuffers (will be remapped) */
   "glBindVertexBuffers\0"
   /* _mesa_function_pool[17025]: GetTextureHandleARB (will be remapped) */
   "glGetTextureHandleARB\0"
   /* _mesa_function_pool[17047]: GetTextureSamplerHandleARB (will be remapped) */
   "glGetTextureSamplerHandleARB\0"
   /* _mesa_function_pool[17076]: MakeTextureHandleResidentARB (will be remapped) */
   "glMakeTextureHandleResidentARB\0"
   /* _mesa_function_pool[17107]: MakeTextureHandleNonResidentARB (will be remapped) */
   "glMakeTextureHandleNonResidentARB\0"
   /* _mesa_function_pool[17141]: GetImageHandleARB (will be remapped) */
   "glGetImageHandleARB\0"
   /* _mesa_function_pool[17161]: MakeImageHandleResidentARB (will be remapped) */
   "glMakeImageHandleResidentARB\0"
   /* _mesa_function_pool[17190]: MakeImageHandleNonResidentARB (will be remapped) */
   "glMakeImageHandleNonResidentARB\0"
   /* _mesa_function_pool[17222]: UniformHandleui64ARB (will be remapped) */
   "glUniformHandleui64ARB\0"
   /* _mesa_function_pool[17245]: UniformHandleui64vARB (will be remapped) */
   "glUniformHandleui64vARB\0"
   /* _mesa_function_pool[17269]: ProgramUniformHandleui64ARB (will be remapped) */
   "glProgramUniformHandleui64ARB\0"
   /* _mesa_function_pool[17299]: ProgramUniformHandleui64vARB (will be remapped) */
   "glProgramUniformHandleui64vARB\0"
   /* _mesa_function_pool[17330]: IsTextureHandleResidentARB (will be remapped) */
   "glIsTextureHandleResidentARB\0"
   /* _mesa_function_pool[17359]: IsImageHandleResidentARB (will be remapped) */
   "glIsImageHandleResidentARB\0"
   /* _mesa_function_pool[17386]: VertexAttribL1ui64ARB (will be remapped) */
   "glVertexAttribL1ui64ARB\0"
   /* _mesa_function_pool[17410]: VertexAttribL1ui64vARB (will be remapped) */
   "glVertexAttribL1ui64vARB\0"
   /* _mesa_function_pool[17435]: GetVertexAttribLui64vARB (will be remapped) */
   "glGetVertexAttribLui64vARB\0"
   /* _mesa_function_pool[17462]: DispatchComputeGroupSizeARB (will be remapped) */
   "glDispatchComputeGroupSizeARB\0"
   /* _mesa_function_pool[17492]: MultiDrawArraysIndirectCountARB (will be remapped) */
   "glMultiDrawArraysIndirectCountARB\0"
   /* _mesa_function_pool[17526]: MultiDrawElementsIndirectCountARB (will be remapped) */
   "glMultiDrawElementsIndirectCountARB\0"
   /* _mesa_function_pool[17562]: TexPageCommitmentARB (will be remapped) */
   "glTexPageCommitmentARB\0"
   /* _mesa_function_pool[17585]: TexturePageCommitmentEXT (will be remapped) */
   "glTexturePageCommitmentEXT\0"
   /* _mesa_function_pool[17612]: ClipControl (will be remapped) */
   "glClipControl\0"
   /* _mesa_function_pool[17626]: CreateTransformFeedbacks (will be remapped) */
   "glCreateTransformFeedbacks\0"
   /* _mesa_function_pool[17653]: TransformFeedbackBufferBase (will be remapped) */
   "glTransformFeedbackBufferBase\0"
   /* _mesa_function_pool[17683]: TransformFeedbackBufferRange (will be remapped) */
   "glTransformFeedbackBufferRange\0"
   /* _mesa_function_pool[17714]: GetTransformFeedbackiv (will be remapped) */
   "glGetTransformFeedbackiv\0"
   /* _mesa_function_pool[17739]: GetTransformFeedbacki_v (will be remapped) */
   "glGetTransformFeedbacki_v\0"
   /* _mesa_function_pool[17765]: GetTransformFeedbacki64_v (will be remapped) */
   "glGetTransformFeedbacki64_v\0"
   /* _mesa_function_pool[17793]: CreateBuffers (will be remapped) */
   "glCreateBuffers\0"
   /* _mesa_function_pool[17809]: NamedBufferStorage (will be remapped) */
   "glNamedBufferStorage\0"
   /* _mesa_function_pool[17830]: NamedBufferData (will be remapped) */
   "glNamedBufferData\0"
   /* _mesa_function_pool[17848]: NamedBufferSubData (will be remapped) */
   "glNamedBufferSubData\0"
   /* _mesa_function_pool[17869]: CopyNamedBufferSubData (will be remapped) */
   "glCopyNamedBufferSubData\0"
   /* _mesa_function_pool[17894]: ClearNamedBufferData (will be remapped) */
   "glClearNamedBufferData\0"
   /* _mesa_function_pool[17917]: ClearNamedBufferSubData (will be remapped) */
   "glClearNamedBufferSubData\0"
   /* _mesa_function_pool[17943]: MapNamedBuffer (will be remapped) */
   "glMapNamedBuffer\0"
   /* _mesa_function_pool[17960]: MapNamedBufferRange (will be remapped) */
   "glMapNamedBufferRange\0"
   /* _mesa_function_pool[17982]: UnmapNamedBufferEXT (will be remapped) */
   "glUnmapNamedBuffer\0"
   /* _mesa_function_pool[18001]: FlushMappedNamedBufferRange (will be remapped) */
   "glFlushMappedNamedBufferRange\0"
   /* _mesa_function_pool[18031]: GetNamedBufferParameteriv (will be remapped) */
   "glGetNamedBufferParameteriv\0"
   /* _mesa_function_pool[18059]: GetNamedBufferParameteri64v (will be remapped) */
   "glGetNamedBufferParameteri64v\0"
   /* _mesa_function_pool[18089]: GetNamedBufferPointerv (will be remapped) */
   "glGetNamedBufferPointerv\0"
   /* _mesa_function_pool[18114]: GetNamedBufferSubData (will be remapped) */
   "glGetNamedBufferSubData\0"
   /* _mesa_function_pool[18138]: CreateFramebuffers (will be remapped) */
   "glCreateFramebuffers\0"
   /* _mesa_function_pool[18159]: NamedFramebufferRenderbuffer (will be remapped) */
   "glNamedFramebufferRenderbuffer\0"
   /* _mesa_function_pool[18190]: NamedFramebufferParameteri (will be remapped) */
   "glNamedFramebufferParameteri\0"
   /* _mesa_function_pool[18219]: NamedFramebufferTexture (will be remapped) */
   "glNamedFramebufferTexture\0"
   /* _mesa_function_pool[18245]: NamedFramebufferTextureLayer (will be remapped) */
   "glNamedFramebufferTextureLayer\0"
   /* _mesa_function_pool[18276]: NamedFramebufferDrawBuffer (will be remapped) */
   "glNamedFramebufferDrawBuffer\0"
   /* _mesa_function_pool[18305]: NamedFramebufferDrawBuffers (will be remapped) */
   "glNamedFramebufferDrawBuffers\0"
   /* _mesa_function_pool[18335]: NamedFramebufferReadBuffer (will be remapped) */
   "glNamedFramebufferReadBuffer\0"
   /* _mesa_function_pool[18364]: InvalidateNamedFramebufferData (will be remapped) */
   "glInvalidateNamedFramebufferData\0"
   /* _mesa_function_pool[18397]: InvalidateNamedFramebufferSubData (will be remapped) */
   "glInvalidateNamedFramebufferSubData\0"
   /* _mesa_function_pool[18433]: ClearNamedFramebufferiv (will be remapped) */
   "glClearNamedFramebufferiv\0"
   /* _mesa_function_pool[18459]: ClearNamedFramebufferuiv (will be remapped) */
   "glClearNamedFramebufferuiv\0"
   /* _mesa_function_pool[18486]: ClearNamedFramebufferfv (will be remapped) */
   "glClearNamedFramebufferfv\0"
   /* _mesa_function_pool[18512]: ClearNamedFramebufferfi (will be remapped) */
   "glClearNamedFramebufferfi\0"
   /* _mesa_function_pool[18538]: BlitNamedFramebuffer (will be remapped) */
   "glBlitNamedFramebuffer\0"
   /* _mesa_function_pool[18561]: CheckNamedFramebufferStatus (will be remapped) */
   "glCheckNamedFramebufferStatus\0"
   /* _mesa_function_pool[18591]: GetNamedFramebufferParameteriv (will be remapped) */
   "glGetNamedFramebufferParameteriv\0"
   /* _mesa_function_pool[18624]: GetNamedFramebufferAttachmentParameteriv (will be remapped) */
   "glGetNamedFramebufferAttachmentParameteriv\0"
   /* _mesa_function_pool[18667]: CreateRenderbuffers (will be remapped) */
   "glCreateRenderbuffers\0"
   /* _mesa_function_pool[18689]: NamedRenderbufferStorage (will be remapped) */
   "glNamedRenderbufferStorage\0"
   /* _mesa_function_pool[18716]: NamedRenderbufferStorageMultisample (will be remapped) */
   "glNamedRenderbufferStorageMultisample\0"
   /* _mesa_function_pool[18754]: GetNamedRenderbufferParameteriv (will be remapped) */
   "glGetNamedRenderbufferParameteriv\0"
   /* _mesa_function_pool[18788]: CreateTextures (will be remapped) */
   "glCreateTextures\0"
   /* _mesa_function_pool[18805]: TextureBuffer (will be remapped) */
   "glTextureBuffer\0"
   /* _mesa_function_pool[18821]: TextureBufferRange (will be remapped) */
   "glTextureBufferRange\0"
   /* _mesa_function_pool[18842]: TextureStorage1D (will be remapped) */
   "glTextureStorage1D\0"
   /* _mesa_function_pool[18861]: TextureStorage2D (will be remapped) */
   "glTextureStorage2D\0"
   /* _mesa_function_pool[18880]: TextureStorage3D (will be remapped) */
   "glTextureStorage3D\0"
   /* _mesa_function_pool[18899]: TextureStorage2DMultisample (will be remapped) */
   "glTextureStorage2DMultisample\0"
   /* _mesa_function_pool[18929]: TextureStorage3DMultisample (will be remapped) */
   "glTextureStorage3DMultisample\0"
   /* _mesa_function_pool[18959]: TextureSubImage1D (will be remapped) */
   "glTextureSubImage1D\0"
   /* _mesa_function_pool[18979]: TextureSubImage2D (will be remapped) */
   "glTextureSubImage2D\0"
   /* _mesa_function_pool[18999]: TextureSubImage3D (will be remapped) */
   "glTextureSubImage3D\0"
   /* _mesa_function_pool[19019]: CompressedTextureSubImage1D (will be remapped) */
   "glCompressedTextureSubImage1D\0"
   /* _mesa_function_pool[19049]: CompressedTextureSubImage2D (will be remapped) */
   "glCompressedTextureSubImage2D\0"
   /* _mesa_function_pool[19079]: CompressedTextureSubImage3D (will be remapped) */
   "glCompressedTextureSubImage3D\0"
   /* _mesa_function_pool[19109]: CopyTextureSubImage1D (will be remapped) */
   "glCopyTextureSubImage1D\0"
   /* _mesa_function_pool[19133]: CopyTextureSubImage2D (will be remapped) */
   "glCopyTextureSubImage2D\0"
   /* _mesa_function_pool[19157]: CopyTextureSubImage3D (will be remapped) */
   "glCopyTextureSubImage3D\0"
   /* _mesa_function_pool[19181]: TextureParameterf (will be remapped) */
   "glTextureParameterf\0"
   /* _mesa_function_pool[19201]: TextureParameterfv (will be remapped) */
   "glTextureParameterfv\0"
   /* _mesa_function_pool[19222]: TextureParameteri (will be remapped) */
   "glTextureParameteri\0"
   /* _mesa_function_pool[19242]: TextureParameterIiv (will be remapped) */
   "glTextureParameterIiv\0"
   /* _mesa_function_pool[19264]: TextureParameterIuiv (will be remapped) */
   "glTextureParameterIuiv\0"
   /* _mesa_function_pool[19287]: TextureParameteriv (will be remapped) */
   "glTextureParameteriv\0"
   /* _mesa_function_pool[19308]: GenerateTextureMipmap (will be remapped) */
   "glGenerateTextureMipmap\0"
   /* _mesa_function_pool[19332]: BindTextureUnit (will be remapped) */
   "glBindTextureUnit\0"
   /* _mesa_function_pool[19350]: GetTextureImage (will be remapped) */
   "glGetTextureImage\0"
   /* _mesa_function_pool[19368]: GetCompressedTextureImage (will be remapped) */
   "glGetCompressedTextureImage\0"
   /* _mesa_function_pool[19396]: GetTextureLevelParameterfv (will be remapped) */
   "glGetTextureLevelParameterfv\0"
   /* _mesa_function_pool[19425]: GetTextureLevelParameteriv (will be remapped) */
   "glGetTextureLevelParameteriv\0"
   /* _mesa_function_pool[19454]: GetTextureParameterfv (will be remapped) */
   "glGetTextureParameterfv\0"
   /* _mesa_function_pool[19478]: GetTextureParameterIiv (will be remapped) */
   "glGetTextureParameterIiv\0"
   /* _mesa_function_pool[19503]: GetTextureParameterIuiv (will be remapped) */
   "glGetTextureParameterIuiv\0"
   /* _mesa_function_pool[19529]: GetTextureParameteriv (will be remapped) */
   "glGetTextureParameteriv\0"
   /* _mesa_function_pool[19553]: CreateVertexArrays (will be remapped) */
   "glCreateVertexArrays\0"
   /* _mesa_function_pool[19574]: DisableVertexArrayAttrib (will be remapped) */
   "glDisableVertexArrayAttrib\0"
   /* _mesa_function_pool[19601]: EnableVertexArrayAttrib (will be remapped) */
   "glEnableVertexArrayAttrib\0"
   /* _mesa_function_pool[19627]: VertexArrayElementBuffer (will be remapped) */
   "glVertexArrayElementBuffer\0"
   /* _mesa_function_pool[19654]: VertexArrayVertexBuffer (will be remapped) */
   "glVertexArrayVertexBuffer\0"
   /* _mesa_function_pool[19680]: VertexArrayVertexBuffers (will be remapped) */
   "glVertexArrayVertexBuffers\0"
   /* _mesa_function_pool[19707]: VertexArrayAttribFormat (will be remapped) */
   "glVertexArrayAttribFormat\0"
   /* _mesa_function_pool[19733]: VertexArrayAttribIFormat (will be remapped) */
   "glVertexArrayAttribIFormat\0"
   /* _mesa_function_pool[19760]: VertexArrayAttribLFormat (will be remapped) */
   "glVertexArrayAttribLFormat\0"
   /* _mesa_function_pool[19787]: VertexArrayAttribBinding (will be remapped) */
   "glVertexArrayAttribBinding\0"
   /* _mesa_function_pool[19814]: VertexArrayBindingDivisor (will be remapped) */
   "glVertexArrayBindingDivisor\0"
   /* _mesa_function_pool[19842]: GetVertexArrayiv (will be remapped) */
   "glGetVertexArrayiv\0"
   /* _mesa_function_pool[19861]: GetVertexArrayIndexediv (will be remapped) */
   "glGetVertexArrayIndexediv\0"
   /* _mesa_function_pool[19887]: GetVertexArrayIndexed64iv (will be remapped) */
   "glGetVertexArrayIndexed64iv\0"
   /* _mesa_function_pool[19915]: CreateSamplers (will be remapped) */
   "glCreateSamplers\0"
   /* _mesa_function_pool[19932]: CreateProgramPipelines (will be remapped) */
   "glCreateProgramPipelines\0"
   /* _mesa_function_pool[19957]: CreateQueries (will be remapped) */
   "glCreateQueries\0"
   /* _mesa_function_pool[19973]: GetQueryBufferObjectiv (will be remapped) */
   "glGetQueryBufferObjectiv\0"
   /* _mesa_function_pool[19998]: GetQueryBufferObjectuiv (will be remapped) */
   "glGetQueryBufferObjectuiv\0"
   /* _mesa_function_pool[20024]: GetQueryBufferObjecti64v (will be remapped) */
   "glGetQueryBufferObjecti64v\0"
   /* _mesa_function_pool[20051]: GetQueryBufferObjectui64v (will be remapped) */
   "glGetQueryBufferObjectui64v\0"
   /* _mesa_function_pool[20079]: GetTextureSubImage (will be remapped) */
   "glGetTextureSubImage\0"
   /* _mesa_function_pool[20100]: GetCompressedTextureSubImage (will be remapped) */
   "glGetCompressedTextureSubImage\0"
   /* _mesa_function_pool[20131]: TextureBarrierNV (will be remapped) */
   "glTextureBarrier\0"
   /* _mesa_function_pool[20148]: BufferPageCommitmentARB (will be remapped) */
   "glBufferPageCommitmentARB\0"
   /* _mesa_function_pool[20174]: NamedBufferPageCommitmentEXT (will be remapped) */
   "glNamedBufferPageCommitmentEXT\0"
   /* _mesa_function_pool[20205]: NamedBufferPageCommitmentARB (will be remapped) */
   "glNamedBufferPageCommitmentARB\0"
   /* _mesa_function_pool[20236]: PrimitiveBoundingBox (will be remapped) */
   "glPrimitiveBoundingBox\0"
   /* _mesa_function_pool[20259]: BlendBarrier (will be remapped) */
   "glBlendBarrier\0"
   /* _mesa_function_pool[20274]: Uniform1i64ARB (will be remapped) */
   "glUniform1i64ARB\0"
   /* _mesa_function_pool[20291]: Uniform2i64ARB (will be remapped) */
   "glUniform2i64ARB\0"
   /* _mesa_function_pool[20308]: Uniform3i64ARB (will be remapped) */
   "glUniform3i64ARB\0"
   /* _mesa_function_pool[20325]: Uniform4i64ARB (will be remapped) */
   "glUniform4i64ARB\0"
   /* _mesa_function_pool[20342]: Uniform1i64vARB (will be remapped) */
   "glUniform1i64vARB\0"
   /* _mesa_function_pool[20360]: Uniform2i64vARB (will be remapped) */
   "glUniform2i64vARB\0"
   /* _mesa_function_pool[20378]: Uniform3i64vARB (will be remapped) */
   "glUniform3i64vARB\0"
   /* _mesa_function_pool[20396]: Uniform4i64vARB (will be remapped) */
   "glUniform4i64vARB\0"
   /* _mesa_function_pool[20414]: Uniform1ui64ARB (will be remapped) */
   "glUniform1ui64ARB\0"
   /* _mesa_function_pool[20432]: Uniform2ui64ARB (will be remapped) */
   "glUniform2ui64ARB\0"
   /* _mesa_function_pool[20450]: Uniform3ui64ARB (will be remapped) */
   "glUniform3ui64ARB\0"
   /* _mesa_function_pool[20468]: Uniform4ui64ARB (will be remapped) */
   "glUniform4ui64ARB\0"
   /* _mesa_function_pool[20486]: Uniform1ui64vARB (will be remapped) */
   "glUniform1ui64vARB\0"
   /* _mesa_function_pool[20505]: Uniform2ui64vARB (will be remapped) */
   "glUniform2ui64vARB\0"
   /* _mesa_function_pool[20524]: Uniform3ui64vARB (will be remapped) */
   "glUniform3ui64vARB\0"
   /* _mesa_function_pool[20543]: Uniform4ui64vARB (will be remapped) */
   "glUniform4ui64vARB\0"
   /* _mesa_function_pool[20562]: GetUniformi64vARB (will be remapped) */
   "glGetUniformi64vARB\0"
   /* _mesa_function_pool[20582]: GetUniformui64vARB (will be remapped) */
   "glGetUniformui64vARB\0"
   /* _mesa_function_pool[20603]: GetnUniformi64vARB (will be remapped) */
   "glGetnUniformi64vARB\0"
   /* _mesa_function_pool[20624]: GetnUniformui64vARB (will be remapped) */
   "glGetnUniformui64vARB\0"
   /* _mesa_function_pool[20646]: ProgramUniform1i64ARB (will be remapped) */
   "glProgramUniform1i64ARB\0"
   /* _mesa_function_pool[20670]: ProgramUniform2i64ARB (will be remapped) */
   "glProgramUniform2i64ARB\0"
   /* _mesa_function_pool[20694]: ProgramUniform3i64ARB (will be remapped) */
   "glProgramUniform3i64ARB\0"
   /* _mesa_function_pool[20718]: ProgramUniform4i64ARB (will be remapped) */
   "glProgramUniform4i64ARB\0"
   /* _mesa_function_pool[20742]: ProgramUniform1i64vARB (will be remapped) */
   "glProgramUniform1i64vARB\0"
   /* _mesa_function_pool[20767]: ProgramUniform2i64vARB (will be remapped) */
   "glProgramUniform2i64vARB\0"
   /* _mesa_function_pool[20792]: ProgramUniform3i64vARB (will be remapped) */
   "glProgramUniform3i64vARB\0"
   /* _mesa_function_pool[20817]: ProgramUniform4i64vARB (will be remapped) */
   "glProgramUniform4i64vARB\0"
   /* _mesa_function_pool[20842]: ProgramUniform1ui64ARB (will be remapped) */
   "glProgramUniform1ui64ARB\0"
   /* _mesa_function_pool[20867]: ProgramUniform2ui64ARB (will be remapped) */
   "glProgramUniform2ui64ARB\0"
   /* _mesa_function_pool[20892]: ProgramUniform3ui64ARB (will be remapped) */
   "glProgramUniform3ui64ARB\0"
   /* _mesa_function_pool[20917]: ProgramUniform4ui64ARB (will be remapped) */
   "glProgramUniform4ui64ARB\0"
   /* _mesa_function_pool[20942]: ProgramUniform1ui64vARB (will be remapped) */
   "glProgramUniform1ui64vARB\0"
   /* _mesa_function_pool[20968]: ProgramUniform2ui64vARB (will be remapped) */
   "glProgramUniform2ui64vARB\0"
   /* _mesa_function_pool[20994]: ProgramUniform3ui64vARB (will be remapped) */
   "glProgramUniform3ui64vARB\0"
   /* _mesa_function_pool[21020]: ProgramUniform4ui64vARB (will be remapped) */
   "glProgramUniform4ui64vARB\0"
   /* _mesa_function_pool[21046]: MaxShaderCompilerThreadsKHR (will be remapped) */
   "glMaxShaderCompilerThreadsKHR\0"
   /* _mesa_function_pool[21076]: SpecializeShaderARB (will be remapped) */
   "glSpecializeShaderARB\0"
   /* _mesa_function_pool[21098]: GetTexFilterFuncSGIS (dynamic) */
   "glGetTexFilterFuncSGIS\0"
   /* _mesa_function_pool[21121]: TexFilterFuncSGIS (dynamic) */
   "glTexFilterFuncSGIS\0"
   /* _mesa_function_pool[21141]: PixelTexGenParameteriSGIS (dynamic) */
   "glPixelTexGenParameteriSGIS\0"
   /* _mesa_function_pool[21169]: PixelTexGenParameterivSGIS (dynamic) */
   "glPixelTexGenParameterivSGIS\0"
   /* _mesa_function_pool[21198]: PixelTexGenParameterfSGIS (dynamic) */
   "glPixelTexGenParameterfSGIS\0"
   /* _mesa_function_pool[21226]: PixelTexGenParameterfvSGIS (dynamic) */
   "glPixelTexGenParameterfvSGIS\0"
   /* _mesa_function_pool[21255]: GetPixelTexGenParameterivSGIS (dynamic) */
   "glGetPixelTexGenParameterivSGIS\0"
   /* _mesa_function_pool[21287]: GetPixelTexGenParameterfvSGIS (dynamic) */
   "glGetPixelTexGenParameterfvSGIS\0"
   /* _mesa_function_pool[21319]: TexImage4DSGIS (dynamic) */
   "glTexImage4DSGIS\0"
   /* _mesa_function_pool[21336]: TexSubImage4DSGIS (dynamic) */
   "glTexSubImage4DSGIS\0"
   /* _mesa_function_pool[21356]: DetailTexFuncSGIS (dynamic) */
   "glDetailTexFuncSGIS\0"
   /* _mesa_function_pool[21376]: GetDetailTexFuncSGIS (dynamic) */
   "glGetDetailTexFuncSGIS\0"
   /* _mesa_function_pool[21399]: SharpenTexFuncSGIS (dynamic) */
   "glSharpenTexFuncSGIS\0"
   /* _mesa_function_pool[21420]: GetSharpenTexFuncSGIS (dynamic) */
   "glGetSharpenTexFuncSGIS\0"
   /* _mesa_function_pool[21444]: SampleMaskSGIS (will be remapped) */
   "glSampleMaskSGIS\0"
   /* _mesa_function_pool[21461]: SamplePatternSGIS (will be remapped) */
   "glSamplePatternSGIS\0"
   /* _mesa_function_pool[21481]: ColorPointerEXT (will be remapped) */
   "glColorPointerEXT\0"
   /* _mesa_function_pool[21499]: EdgeFlagPointerEXT (will be remapped) */
   "glEdgeFlagPointerEXT\0"
   /* _mesa_function_pool[21520]: IndexPointerEXT (will be remapped) */
   "glIndexPointerEXT\0"
   /* _mesa_function_pool[21538]: NormalPointerEXT (will be remapped) */
   "glNormalPointerEXT\0"
   /* _mesa_function_pool[21557]: TexCoordPointerEXT (will be remapped) */
   "glTexCoordPointerEXT\0"
   /* _mesa_function_pool[21578]: VertexPointerEXT (will be remapped) */
   "glVertexPointerEXT\0"
   /* _mesa_function_pool[21597]: SpriteParameterfSGIX (dynamic) */
   "glSpriteParameterfSGIX\0"
   /* _mesa_function_pool[21620]: SpriteParameterfvSGIX (dynamic) */
   "glSpriteParameterfvSGIX\0"
   /* _mesa_function_pool[21644]: SpriteParameteriSGIX (dynamic) */
   "glSpriteParameteriSGIX\0"
   /* _mesa_function_pool[21667]: SpriteParameterivSGIX (dynamic) */
   "glSpriteParameterivSGIX\0"
   /* _mesa_function_pool[21691]: GetInstrumentsSGIX (dynamic) */
   "glGetInstrumentsSGIX\0"
   /* _mesa_function_pool[21712]: InstrumentsBufferSGIX (dynamic) */
   "glInstrumentsBufferSGIX\0"
   /* _mesa_function_pool[21736]: PollInstrumentsSGIX (dynamic) */
   "glPollInstrumentsSGIX\0"
   /* _mesa_function_pool[21758]: ReadInstrumentsSGIX (dynamic) */
   "glReadInstrumentsSGIX\0"
   /* _mesa_function_pool[21780]: StartInstrumentsSGIX (dynamic) */
   "glStartInstrumentsSGIX\0"
   /* _mesa_function_pool[21803]: StopInstrumentsSGIX (dynamic) */
   "glStopInstrumentsSGIX\0"
   /* _mesa_function_pool[21825]: FrameZoomSGIX (dynamic) */
   "glFrameZoomSGIX\0"
   /* _mesa_function_pool[21841]: TagSampleBufferSGIX (dynamic) */
   "glTagSampleBufferSGIX\0"
   /* _mesa_function_pool[21863]: ReferencePlaneSGIX (dynamic) */
   "glReferencePlaneSGIX\0"
   /* _mesa_function_pool[21884]: FlushRasterSGIX (dynamic) */
   "glFlushRasterSGIX\0"
   /* _mesa_function_pool[21902]: FogFuncSGIS (dynamic) */
   "glFogFuncSGIS\0"
   /* _mesa_function_pool[21916]: GetFogFuncSGIS (dynamic) */
   "glGetFogFuncSGIS\0"
   /* _mesa_function_pool[21933]: ImageTransformParameteriHP (dynamic) */
   "glImageTransformParameteriHP\0"
   /* _mesa_function_pool[21962]: ImageTransformParameterfHP (dynamic) */
   "glImageTransformParameterfHP\0"
   /* _mesa_function_pool[21991]: ImageTransformParameterivHP (dynamic) */
   "glImageTransformParameterivHP\0"
   /* _mesa_function_pool[22021]: ImageTransformParameterfvHP (dynamic) */
   "glImageTransformParameterfvHP\0"
   /* _mesa_function_pool[22051]: GetImageTransformParameterivHP (dynamic) */
   "glGetImageTransformParameterivHP\0"
   /* _mesa_function_pool[22084]: GetImageTransformParameterfvHP (dynamic) */
   "glGetImageTransformParameterfvHP\0"
   /* _mesa_function_pool[22117]: HintPGI (dynamic) */
   "glHintPGI\0"
   /* _mesa_function_pool[22127]: GetListParameterfvSGIX (dynamic) */
   "glGetListParameterfvSGIX\0"
   /* _mesa_function_pool[22152]: GetListParameterivSGIX (dynamic) */
   "glGetListParameterivSGIX\0"
   /* _mesa_function_pool[22177]: ListParameterfSGIX (dynamic) */
   "glListParameterfSGIX\0"
   /* _mesa_function_pool[22198]: ListParameterfvSGIX (dynamic) */
   "glListParameterfvSGIX\0"
   /* _mesa_function_pool[22220]: ListParameteriSGIX (dynamic) */
   "glListParameteriSGIX\0"
   /* _mesa_function_pool[22241]: ListParameterivSGIX (dynamic) */
   "glListParameterivSGIX\0"
   /* _mesa_function_pool[22263]: IndexMaterialEXT (dynamic) */
   "glIndexMaterialEXT\0"
   /* _mesa_function_pool[22282]: IndexFuncEXT (dynamic) */
   "glIndexFuncEXT\0"
   /* _mesa_function_pool[22297]: LockArraysEXT (will be remapped) */
   "glLockArraysEXT\0"
   /* _mesa_function_pool[22313]: UnlockArraysEXT (will be remapped) */
   "glUnlockArraysEXT\0"
   /* _mesa_function_pool[22331]: CullParameterdvEXT (dynamic) */
   "glCullParameterdvEXT\0"
   /* _mesa_function_pool[22352]: CullParameterfvEXT (dynamic) */
   "glCullParameterfvEXT\0"
   /* _mesa_function_pool[22373]: ViewportArrayv (will be remapped) */
   "glViewportArrayv\0"
   /* _mesa_function_pool[22390]: ViewportIndexedf (will be remapped) */
   "glViewportIndexedf\0"
   /* _mesa_function_pool[22409]: ViewportIndexedfv (will be remapped) */
   "glViewportIndexedfv\0"
   /* _mesa_function_pool[22429]: ScissorArrayv (will be remapped) */
   "glScissorArrayv\0"
   /* _mesa_function_pool[22445]: ScissorIndexed (will be remapped) */
   "glScissorIndexed\0"
   /* _mesa_function_pool[22462]: ScissorIndexedv (will be remapped) */
   "glScissorIndexedv\0"
   /* _mesa_function_pool[22480]: DepthRangeArrayv (will be remapped) */
   "glDepthRangeArrayv\0"
   /* _mesa_function_pool[22499]: DepthRangeIndexed (will be remapped) */
   "glDepthRangeIndexed\0"
   /* _mesa_function_pool[22519]: GetFloati_v (will be remapped) */
   "glGetFloati_v\0"
   /* _mesa_function_pool[22533]: GetDoublei_v (will be remapped) */
   "glGetDoublei_v\0"
   /* _mesa_function_pool[22548]: FragmentColorMaterialSGIX (dynamic) */
   "glFragmentColorMaterialSGIX\0"
   /* _mesa_function_pool[22576]: FragmentLightfSGIX (dynamic) */
   "glFragmentLightfSGIX\0"
   /* _mesa_function_pool[22597]: FragmentLightfvSGIX (dynamic) */
   "glFragmentLightfvSGIX\0"
   /* _mesa_function_pool[22619]: FragmentLightiSGIX (dynamic) */
   "glFragmentLightiSGIX\0"
   /* _mesa_function_pool[22640]: FragmentLightivSGIX (dynamic) */
   "glFragmentLightivSGIX\0"
   /* _mesa_function_pool[22662]: FragmentLightModelfSGIX (dynamic) */
   "glFragmentLightModelfSGIX\0"
   /* _mesa_function_pool[22688]: FragmentLightModelfvSGIX (dynamic) */
   "glFragmentLightModelfvSGIX\0"
   /* _mesa_function_pool[22715]: FragmentLightModeliSGIX (dynamic) */
   "glFragmentLightModeliSGIX\0"
   /* _mesa_function_pool[22741]: FragmentLightModelivSGIX (dynamic) */
   "glFragmentLightModelivSGIX\0"
   /* _mesa_function_pool[22768]: FragmentMaterialfSGIX (dynamic) */
   "glFragmentMaterialfSGIX\0"
   /* _mesa_function_pool[22792]: FragmentMaterialfvSGIX (dynamic) */
   "glFragmentMaterialfvSGIX\0"
   /* _mesa_function_pool[22817]: FragmentMaterialiSGIX (dynamic) */
   "glFragmentMaterialiSGIX\0"
   /* _mesa_function_pool[22841]: FragmentMaterialivSGIX (dynamic) */
   "glFragmentMaterialivSGIX\0"
   /* _mesa_function_pool[22866]: GetFragmentLightfvSGIX (dynamic) */
   "glGetFragmentLightfvSGIX\0"
   /* _mesa_function_pool[22891]: GetFragmentLightivSGIX (dynamic) */
   "glGetFragmentLightivSGIX\0"
   /* _mesa_function_pool[22916]: GetFragmentMaterialfvSGIX (dynamic) */
   "glGetFragmentMaterialfvSGIX\0"
   /* _mesa_function_pool[22944]: GetFragmentMaterialivSGIX (dynamic) */
   "glGetFragmentMaterialivSGIX\0"
   /* _mesa_function_pool[22972]: LightEnviSGIX (dynamic) */
   "glLightEnviSGIX\0"
   /* _mesa_function_pool[22988]: ApplyTextureEXT (dynamic) */
   "glApplyTextureEXT\0"
   /* _mesa_function_pool[23006]: TextureLightEXT (dynamic) */
   "glTextureLightEXT\0"
   /* _mesa_function_pool[23024]: TextureMaterialEXT (dynamic) */
   "glTextureMaterialEXT\0"
   /* _mesa_function_pool[23045]: AsyncMarkerSGIX (dynamic) */
   "glAsyncMarkerSGIX\0"
   /* _mesa_function_pool[23063]: FinishAsyncSGIX (dynamic) */
   "glFinishAsyncSGIX\0"
   /* _mesa_function_pool[23081]: PollAsyncSGIX (dynamic) */
   "glPollAsyncSGIX\0"
   /* _mesa_function_pool[23097]: GenAsyncMarkersSGIX (dynamic) */
   "glGenAsyncMarkersSGIX\0"
   /* _mesa_function_pool[23119]: DeleteAsyncMarkersSGIX (dynamic) */
   "glDeleteAsyncMarkersSGIX\0"
   /* _mesa_function_pool[23144]: IsAsyncMarkerSGIX (dynamic) */
   "glIsAsyncMarkerSGIX\0"
   /* _mesa_function_pool[23164]: VertexPointervINTEL (dynamic) */
   "glVertexPointervINTEL\0"
   /* _mesa_function_pool[23186]: NormalPointervINTEL (dynamic) */
   "glNormalPointervINTEL\0"
   /* _mesa_function_pool[23208]: ColorPointervINTEL (dynamic) */
   "glColorPointervINTEL\0"
   /* _mesa_function_pool[23229]: TexCoordPointervINTEL (dynamic) */
   "glTexCoordPointervINTEL\0"
   /* _mesa_function_pool[23253]: PixelTransformParameteriEXT (dynamic) */
   "glPixelTransformParameteriEXT\0"
   /* _mesa_function_pool[23283]: PixelTransformParameterfEXT (dynamic) */
   "glPixelTransformParameterfEXT\0"
   /* _mesa_function_pool[23313]: PixelTransformParameterivEXT (dynamic) */
   "glPixelTransformParameterivEXT\0"
   /* _mesa_function_pool[23344]: PixelTransformParameterfvEXT (dynamic) */
   "glPixelTransformParameterfvEXT\0"
   /* _mesa_function_pool[23375]: TextureNormalEXT (dynamic) */
   "glTextureNormalEXT\0"
   /* _mesa_function_pool[23394]: Tangent3bEXT (dynamic) */
   "glTangent3bEXT\0"
   /* _mesa_function_pool[23409]: Tangent3bvEXT (dynamic) */
   "glTangent3bvEXT\0"
   /* _mesa_function_pool[23425]: Tangent3dEXT (dynamic) */
   "glTangent3dEXT\0"
   /* _mesa_function_pool[23440]: Tangent3dvEXT (dynamic) */
   "glTangent3dvEXT\0"
   /* _mesa_function_pool[23456]: Tangent3fEXT (dynamic) */
   "glTangent3fEXT\0"
   /* _mesa_function_pool[23471]: Tangent3fvEXT (dynamic) */
   "glTangent3fvEXT\0"
   /* _mesa_function_pool[23487]: Tangent3iEXT (dynamic) */
   "glTangent3iEXT\0"
   /* _mesa_function_pool[23502]: Tangent3ivEXT (dynamic) */
   "glTangent3ivEXT\0"
   /* _mesa_function_pool[23518]: Tangent3sEXT (dynamic) */
   "glTangent3sEXT\0"
   /* _mesa_function_pool[23533]: Tangent3svEXT (dynamic) */
   "glTangent3svEXT\0"
   /* _mesa_function_pool[23549]: Binormal3bEXT (dynamic) */
   "glBinormal3bEXT\0"
   /* _mesa_function_pool[23565]: Binormal3bvEXT (dynamic) */
   "glBinormal3bvEXT\0"
   /* _mesa_function_pool[23582]: Binormal3dEXT (dynamic) */
   "glBinormal3dEXT\0"
   /* _mesa_function_pool[23598]: Binormal3dvEXT (dynamic) */
   "glBinormal3dvEXT\0"
   /* _mesa_function_pool[23615]: Binormal3fEXT (dynamic) */
   "glBinormal3fEXT\0"
   /* _mesa_function_pool[23631]: Binormal3fvEXT (dynamic) */
   "glBinormal3fvEXT\0"
   /* _mesa_function_pool[23648]: Binormal3iEXT (dynamic) */
   "glBinormal3iEXT\0"
   /* _mesa_function_pool[23664]: Binormal3ivEXT (dynamic) */
   "glBinormal3ivEXT\0"
   /* _mesa_function_pool[23681]: Binormal3sEXT (dynamic) */
   "glBinormal3sEXT\0"
   /* _mesa_function_pool[23697]: Binormal3svEXT (dynamic) */
   "glBinormal3svEXT\0"
   /* _mesa_function_pool[23714]: TangentPointerEXT (dynamic) */
   "glTangentPointerEXT\0"
   /* _mesa_function_pool[23734]: BinormalPointerEXT (dynamic) */
   "glBinormalPointerEXT\0"
   /* _mesa_function_pool[23755]: PixelTexGenSGIX (dynamic) */
   "glPixelTexGenSGIX\0"
   /* _mesa_function_pool[23773]: FinishTextureSUNX (dynamic) */
   "glFinishTextureSUNX\0"
   /* _mesa_function_pool[23793]: GlobalAlphaFactorbSUN (dynamic) */
   "glGlobalAlphaFactorbSUN\0"
   /* _mesa_function_pool[23817]: GlobalAlphaFactorsSUN (dynamic) */
   "glGlobalAlphaFactorsSUN\0"
   /* _mesa_function_pool[23841]: GlobalAlphaFactoriSUN (dynamic) */
   "glGlobalAlphaFactoriSUN\0"
   /* _mesa_function_pool[23865]: GlobalAlphaFactorfSUN (dynamic) */
   "glGlobalAlphaFactorfSUN\0"
   /* _mesa_function_pool[23889]: GlobalAlphaFactordSUN (dynamic) */
   "glGlobalAlphaFactordSUN\0"
   /* _mesa_function_pool[23913]: GlobalAlphaFactorubSUN (dynamic) */
   "glGlobalAlphaFactorubSUN\0"
   /* _mesa_function_pool[23938]: GlobalAlphaFactorusSUN (dynamic) */
   "glGlobalAlphaFactorusSUN\0"
   /* _mesa_function_pool[23963]: GlobalAlphaFactoruiSUN (dynamic) */
   "glGlobalAlphaFactoruiSUN\0"
   /* _mesa_function_pool[23988]: ReplacementCodeuiSUN (dynamic) */
   "glReplacementCodeuiSUN\0"
   /* _mesa_function_pool[24011]: ReplacementCodeusSUN (dynamic) */
   "glReplacementCodeusSUN\0"
   /* _mesa_function_pool[24034]: ReplacementCodeubSUN (dynamic) */
   "glReplacementCodeubSUN\0"
   /* _mesa_function_pool[24057]: ReplacementCodeuivSUN (dynamic) */
   "glReplacementCodeuivSUN\0"
   /* _mesa_function_pool[24081]: ReplacementCodeusvSUN (dynamic) */
   "glReplacementCodeusvSUN\0"
   /* _mesa_function_pool[24105]: ReplacementCodeubvSUN (dynamic) */
   "glReplacementCodeubvSUN\0"
   /* _mesa_function_pool[24129]: ReplacementCodePointerSUN (dynamic) */
   "glReplacementCodePointerSUN\0"
   /* _mesa_function_pool[24157]: Color4ubVertex2fSUN (dynamic) */
   "glColor4ubVertex2fSUN\0"
   /* _mesa_function_pool[24179]: Color4ubVertex2fvSUN (dynamic) */
   "glColor4ubVertex2fvSUN\0"
   /* _mesa_function_pool[24202]: Color4ubVertex3fSUN (dynamic) */
   "glColor4ubVertex3fSUN\0"
   /* _mesa_function_pool[24224]: Color4ubVertex3fvSUN (dynamic) */
   "glColor4ubVertex3fvSUN\0"
   /* _mesa_function_pool[24247]: Color3fVertex3fSUN (dynamic) */
   "glColor3fVertex3fSUN\0"
   /* _mesa_function_pool[24268]: Color3fVertex3fvSUN (dynamic) */
   "glColor3fVertex3fvSUN\0"
   /* _mesa_function_pool[24290]: Normal3fVertex3fSUN (dynamic) */
   "glNormal3fVertex3fSUN\0"
   /* _mesa_function_pool[24312]: Normal3fVertex3fvSUN (dynamic) */
   "glNormal3fVertex3fvSUN\0"
   /* _mesa_function_pool[24335]: Color4fNormal3fVertex3fSUN (dynamic) */
   "glColor4fNormal3fVertex3fSUN\0"
   /* _mesa_function_pool[24364]: Color4fNormal3fVertex3fvSUN (dynamic) */
   "glColor4fNormal3fVertex3fvSUN\0"
   /* _mesa_function_pool[24394]: TexCoord2fVertex3fSUN (dynamic) */
   "glTexCoord2fVertex3fSUN\0"
   /* _mesa_function_pool[24418]: TexCoord2fVertex3fvSUN (dynamic) */
   "glTexCoord2fVertex3fvSUN\0"
   /* _mesa_function_pool[24443]: TexCoord4fVertex4fSUN (dynamic) */
   "glTexCoord4fVertex4fSUN\0"
   /* _mesa_function_pool[24467]: TexCoord4fVertex4fvSUN (dynamic) */
   "glTexCoord4fVertex4fvSUN\0"
   /* _mesa_function_pool[24492]: TexCoord2fColor4ubVertex3fSUN (dynamic) */
   "glTexCoord2fColor4ubVertex3fSUN\0"
   /* _mesa_function_pool[24524]: TexCoord2fColor4ubVertex3fvSUN (dynamic) */
   "glTexCoord2fColor4ubVertex3fvSUN\0"
   /* _mesa_function_pool[24557]: TexCoord2fColor3fVertex3fSUN (dynamic) */
   "glTexCoord2fColor3fVertex3fSUN\0"
   /* _mesa_function_pool[24588]: TexCoord2fColor3fVertex3fvSUN (dynamic) */
   "glTexCoord2fColor3fVertex3fvSUN\0"
   /* _mesa_function_pool[24620]: TexCoord2fNormal3fVertex3fSUN (dynamic) */
   "glTexCoord2fNormal3fVertex3fSUN\0"
   /* _mesa_function_pool[24652]: TexCoord2fNormal3fVertex3fvSUN (dynamic) */
   "glTexCoord2fNormal3fVertex3fvSUN\0"
   /* _mesa_function_pool[24685]: TexCoord2fColor4fNormal3fVertex3fSUN (dynamic) */
   "glTexCoord2fColor4fNormal3fVertex3fSUN\0"
   /* _mesa_function_pool[24724]: TexCoord2fColor4fNormal3fVertex3fvSUN (dynamic) */
   "glTexCoord2fColor4fNormal3fVertex3fvSUN\0"
   /* _mesa_function_pool[24764]: TexCoord4fColor4fNormal3fVertex4fSUN (dynamic) */
   "glTexCoord4fColor4fNormal3fVertex4fSUN\0"
   /* _mesa_function_pool[24803]: TexCoord4fColor4fNormal3fVertex4fvSUN (dynamic) */
   "glTexCoord4fColor4fNormal3fVertex4fvSUN\0"
   /* _mesa_function_pool[24843]: ReplacementCodeuiVertex3fSUN (dynamic) */
   "glReplacementCodeuiVertex3fSUN\0"
   /* _mesa_function_pool[24874]: ReplacementCodeuiVertex3fvSUN (dynamic) */
   "glReplacementCodeuiVertex3fvSUN\0"
   /* _mesa_function_pool[24906]: ReplacementCodeuiColor4ubVertex3fSUN (dynamic) */
   "glReplacementCodeuiColor4ubVertex3fSUN\0"
   /* _mesa_function_pool[24945]: ReplacementCodeuiColor4ubVertex3fvSUN (dynamic) */
   "glReplacementCodeuiColor4ubVertex3fvSUN\0"
   /* _mesa_function_pool[24985]: ReplacementCodeuiColor3fVertex3fSUN (dynamic) */
   "glReplacementCodeuiColor3fVertex3fSUN\0"
   /* _mesa_function_pool[25023]: ReplacementCodeuiColor3fVertex3fvSUN (dynamic) */
   "glReplacementCodeuiColor3fVertex3fvSUN\0"
   /* _mesa_function_pool[25062]: ReplacementCodeuiNormal3fVertex3fSUN (dynamic) */
   "glReplacementCodeuiNormal3fVertex3fSUN\0"
   /* _mesa_function_pool[25101]: ReplacementCodeuiNormal3fVertex3fvSUN (dynamic) */
   "glReplacementCodeuiNormal3fVertex3fvSUN\0"
   /* _mesa_function_pool[25141]: ReplacementCodeuiColor4fNormal3fVertex3fSUN (dynamic) */
   "glReplacementCodeuiColor4fNormal3fVertex3fSUN\0"
   /* _mesa_function_pool[25187]: ReplacementCodeuiColor4fNormal3fVertex3fvSUN (dynamic) */
   "glReplacementCodeuiColor4fNormal3fVertex3fvSUN\0"
   /* _mesa_function_pool[25234]: ReplacementCodeuiTexCoord2fVertex3fSUN (dynamic) */
   "glReplacementCodeuiTexCoord2fVertex3fSUN\0"
   /* _mesa_function_pool[25275]: ReplacementCodeuiTexCoord2fVertex3fvSUN (dynamic) */
   "glReplacementCodeuiTexCoord2fVertex3fvSUN\0"
   /* _mesa_function_pool[25317]: ReplacementCodeuiTexCoord2fNormal3fVertex3fSUN (dynamic) */
   "glReplacementCodeuiTexCoord2fNormal3fVertex3fSUN\0"
   /* _mesa_function_pool[25366]: ReplacementCodeuiTexCoord2fNormal3fVertex3fvSUN (dynamic) */
   "glReplacementCodeuiTexCoord2fNormal3fVertex3fvSUN\0"
   /* _mesa_function_pool[25416]: ReplacementCodeuiTexCoord2fColor4fNormal3fVertex3fSUN (dynamic) */
   "glReplacementCodeuiTexCoord2fColor4fNormal3fVertex3fSUN\0"
   /* _mesa_function_pool[25472]: ReplacementCodeuiTexCoord2fColor4fNormal3fVertex3fvSUN (dynamic) */
   "glReplacementCodeuiTexCoord2fColor4fNormal3fVertex3fvSUN\0"
   /* _mesa_function_pool[25529]: FramebufferSampleLocationsfvARB (will be remapped) */
   "glFramebufferSampleLocationsfvARB\0"
   /* _mesa_function_pool[25563]: NamedFramebufferSampleLocationsfvARB (will be remapped) */
   "glNamedFramebufferSampleLocationsfvARB\0"
   /* _mesa_function_pool[25602]: EvaluateDepthValuesARB (will be remapped) */
   "glEvaluateDepthValuesARB\0"
   /* _mesa_function_pool[25627]: VertexWeightfEXT (dynamic) */
   "glVertexWeightfEXT\0"
   /* _mesa_function_pool[25646]: VertexWeightfvEXT (dynamic) */
   "glVertexWeightfvEXT\0"
   /* _mesa_function_pool[25666]: VertexWeightPointerEXT (dynamic) */
   "glVertexWeightPointerEXT\0"
   /* _mesa_function_pool[25691]: FlushVertexArrayRangeNV (dynamic) */
   "glFlushVertexArrayRangeNV\0"
   /* _mesa_function_pool[25717]: VertexArrayRangeNV (dynamic) */
   "glVertexArrayRangeNV\0"
   /* _mesa_function_pool[25738]: CombinerParameterfvNV (dynamic) */
   "glCombinerParameterfvNV\0"
   /* _mesa_function_pool[25762]: CombinerParameterfNV (dynamic) */
   "glCombinerParameterfNV\0"
   /* _mesa_function_pool[25785]: CombinerParameterivNV (dynamic) */
   "glCombinerParameterivNV\0"
   /* _mesa_function_pool[25809]: CombinerParameteriNV (dynamic) */
   "glCombinerParameteriNV\0"
   /* _mesa_function_pool[25832]: CombinerInputNV (dynamic) */
   "glCombinerInputNV\0"
   /* _mesa_function_pool[25850]: CombinerOutputNV (dynamic) */
   "glCombinerOutputNV\0"
   /* _mesa_function_pool[25869]: FinalCombinerInputNV (dynamic) */
   "glFinalCombinerInputNV\0"
   /* _mesa_function_pool[25892]: GetCombinerInputParameterfvNV (dynamic) */
   "glGetCombinerInputParameterfvNV\0"
   /* _mesa_function_pool[25924]: GetCombinerInputParameterivNV (dynamic) */
   "glGetCombinerInputParameterivNV\0"
   /* _mesa_function_pool[25956]: GetCombinerOutputParameterfvNV (dynamic) */
   "glGetCombinerOutputParameterfvNV\0"
   /* _mesa_function_pool[25989]: GetCombinerOutputParameterivNV (dynamic) */
   "glGetCombinerOutputParameterivNV\0"
   /* _mesa_function_pool[26022]: GetFinalCombinerInputParameterfvNV (dynamic) */
   "glGetFinalCombinerInputParameterfvNV\0"
   /* _mesa_function_pool[26059]: GetFinalCombinerInputParameterivNV (dynamic) */
   "glGetFinalCombinerInputParameterivNV\0"
   /* _mesa_function_pool[26096]: ResizeBuffersMESA (will be remapped) */
   "glResizeBuffersMESA\0"
   /* _mesa_function_pool[26116]: WindowPos4dMESA (will be remapped) */
   "glWindowPos4dMESA\0"
   /* _mesa_function_pool[26134]: WindowPos4dvMESA (will be remapped) */
   "glWindowPos4dvMESA\0"
   /* _mesa_function_pool[26153]: WindowPos4fMESA (will be remapped) */
   "glWindowPos4fMESA\0"
   /* _mesa_function_pool[26171]: WindowPos4fvMESA (will be remapped) */
   "glWindowPos4fvMESA\0"
   /* _mesa_function_pool[26190]: WindowPos4iMESA (will be remapped) */
   "glWindowPos4iMESA\0"
   /* _mesa_function_pool[26208]: WindowPos4ivMESA (will be remapped) */
   "glWindowPos4ivMESA\0"
   /* _mesa_function_pool[26227]: WindowPos4sMESA (will be remapped) */
   "glWindowPos4sMESA\0"
   /* _mesa_function_pool[26245]: WindowPos4svMESA (will be remapped) */
   "glWindowPos4svMESA\0"
   /* _mesa_function_pool[26264]: MultiModeDrawArraysIBM (will be remapped) */
   "glMultiModeDrawArraysIBM\0"
   /* _mesa_function_pool[26289]: MultiModeDrawElementsIBM (will be remapped) */
   "glMultiModeDrawElementsIBM\0"
   /* _mesa_function_pool[26316]: ColorPointerListIBM (dynamic) */
   "glColorPointerListIBM\0"
   /* _mesa_function_pool[26338]: SecondaryColorPointerListIBM (dynamic) */
   "glSecondaryColorPointerListIBM\0"
   /* _mesa_function_pool[26369]: EdgeFlagPointerListIBM (dynamic) */
   "glEdgeFlagPointerListIBM\0"
   /* _mesa_function_pool[26394]: FogCoordPointerListIBM (dynamic) */
   "glFogCoordPointerListIBM\0"
   /* _mesa_function_pool[26419]: IndexPointerListIBM (dynamic) */
   "glIndexPointerListIBM\0"
   /* _mesa_function_pool[26441]: NormalPointerListIBM (dynamic) */
   "glNormalPointerListIBM\0"
   /* _mesa_function_pool[26464]: TexCoordPointerListIBM (dynamic) */
   "glTexCoordPointerListIBM\0"
   /* _mesa_function_pool[26489]: VertexPointerListIBM (dynamic) */
   "glVertexPointerListIBM\0"
   /* _mesa_function_pool[26512]: TbufferMask3DFX (dynamic) */
   "glTbufferMask3DFX\0"
   /* _mesa_function_pool[26530]: TextureColorMaskSGIS (dynamic) */
   "glTextureColorMaskSGIS\0"
   /* _mesa_function_pool[26553]: DeleteFencesNV (dynamic) */
   "glDeleteFencesNV\0"
   /* _mesa_function_pool[26570]: GenFencesNV (dynamic) */
   "glGenFencesNV\0"
   /* _mesa_function_pool[26584]: IsFenceNV (dynamic) */
   "glIsFenceNV\0"
   /* _mesa_function_pool[26596]: TestFenceNV (dynamic) */
   "glTestFenceNV\0"
   /* _mesa_function_pool[26610]: GetFenceivNV (dynamic) */
   "glGetFenceivNV\0"
   /* _mesa_function_pool[26625]: FinishFenceNV (dynamic) */
   "glFinishFenceNV\0"
   /* _mesa_function_pool[26641]: SetFenceNV (dynamic) */
   "glSetFenceNV\0"
   /* _mesa_function_pool[26654]: MapControlPointsNV (dynamic) */
   "glMapControlPointsNV\0"
   /* _mesa_function_pool[26675]: MapParameterivNV (dynamic) */
   "glMapParameterivNV\0"
   /* _mesa_function_pool[26694]: MapParameterfvNV (dynamic) */
   "glMapParameterfvNV\0"
   /* _mesa_function_pool[26713]: GetMapControlPointsNV (dynamic) */
   "glGetMapControlPointsNV\0"
   /* _mesa_function_pool[26737]: GetMapParameterivNV (dynamic) */
   "glGetMapParameterivNV\0"
   /* _mesa_function_pool[26759]: GetMapParameterfvNV (dynamic) */
   "glGetMapParameterfvNV\0"
   /* _mesa_function_pool[26781]: GetMapAttribParameterivNV (dynamic) */
   "glGetMapAttribParameterivNV\0"
   /* _mesa_function_pool[26809]: GetMapAttribParameterfvNV (dynamic) */
   "glGetMapAttribParameterfvNV\0"
   /* _mesa_function_pool[26837]: EvalMapsNV (dynamic) */
   "glEvalMapsNV\0"
   /* _mesa_function_pool[26850]: CombinerStageParameterfvNV (dynamic) */
   "glCombinerStageParameterfvNV\0"
   /* _mesa_function_pool[26879]: GetCombinerStageParameterfvNV (dynamic) */
   "glGetCombinerStageParameterfvNV\0"
   /* _mesa_function_pool[26911]: AreProgramsResidentNV (will be remapped) */
   "glAreProgramsResidentNV\0"
   /* _mesa_function_pool[26935]: ExecuteProgramNV (will be remapped) */
   "glExecuteProgramNV\0"
   /* _mesa_function_pool[26954]: GetProgramParameterdvNV (will be remapped) */
   "glGetProgramParameterdvNV\0"
   /* _mesa_function_pool[26980]: GetProgramParameterfvNV (will be remapped) */
   "glGetProgramParameterfvNV\0"
   /* _mesa_function_pool[27006]: GetProgramivNV (will be remapped) */
   "glGetProgramivNV\0"
   /* _mesa_function_pool[27023]: GetProgramStringNV (will be remapped) */
   "glGetProgramStringNV\0"
   /* _mesa_function_pool[27044]: GetTrackMatrixivNV (will be remapped) */
   "glGetTrackMatrixivNV\0"
   /* _mesa_function_pool[27065]: GetVertexAttribdvNV (will be remapped) */
   "glGetVertexAttribdvNV\0"
   /* _mesa_function_pool[27087]: GetVertexAttribfvNV (will be remapped) */
   "glGetVertexAttribfvNV\0"
   /* _mesa_function_pool[27109]: GetVertexAttribivNV (will be remapped) */
   "glGetVertexAttribivNV\0"
   /* _mesa_function_pool[27131]: LoadProgramNV (will be remapped) */
   "glLoadProgramNV\0"
   /* _mesa_function_pool[27147]: ProgramParameters4dvNV (will be remapped) */
   "glProgramParameters4dvNV\0"
   /* _mesa_function_pool[27172]: ProgramParameters4fvNV (will be remapped) */
   "glProgramParameters4fvNV\0"
   /* _mesa_function_pool[27197]: RequestResidentProgramsNV (will be remapped) */
   "glRequestResidentProgramsNV\0"
   /* _mesa_function_pool[27225]: TrackMatrixNV (will be remapped) */
   "glTrackMatrixNV\0"
   /* _mesa_function_pool[27241]: VertexAttribPointerNV (will be remapped) */
   "glVertexAttribPointerNV\0"
   /* _mesa_function_pool[27265]: VertexAttrib1sNV (will be remapped) */
   "glVertexAttrib1sNV\0"
   /* _mesa_function_pool[27284]: VertexAttrib1svNV (will be remapped) */
   "glVertexAttrib1svNV\0"
   /* _mesa_function_pool[27304]: VertexAttrib2sNV (will be remapped) */
   "glVertexAttrib2sNV\0"
   /* _mesa_function_pool[27323]: VertexAttrib2svNV (will be remapped) */
   "glVertexAttrib2svNV\0"
   /* _mesa_function_pool[27343]: VertexAttrib3sNV (will be remapped) */
   "glVertexAttrib3sNV\0"
   /* _mesa_function_pool[27362]: VertexAttrib3svNV (will be remapped) */
   "glVertexAttrib3svNV\0"
   /* _mesa_function_pool[27382]: VertexAttrib4sNV (will be remapped) */
   "glVertexAttrib4sNV\0"
   /* _mesa_function_pool[27401]: VertexAttrib4svNV (will be remapped) */
   "glVertexAttrib4svNV\0"
   /* _mesa_function_pool[27421]: VertexAttrib1fNV (will be remapped) */
   "glVertexAttrib1fNV\0"
   /* _mesa_function_pool[27440]: VertexAttrib1fvNV (will be remapped) */
   "glVertexAttrib1fvNV\0"
   /* _mesa_function_pool[27460]: VertexAttrib2fNV (will be remapped) */
   "glVertexAttrib2fNV\0"
   /* _mesa_function_pool[27479]: VertexAttrib2fvNV (will be remapped) */
   "glVertexAttrib2fvNV\0"
   /* _mesa_function_pool[27499]: VertexAttrib3fNV (will be remapped) */
   "glVertexAttrib3fNV\0"
   /* _mesa_function_pool[27518]: VertexAttrib3fvNV (will be remapped) */
   "glVertexAttrib3fvNV\0"
   /* _mesa_function_pool[27538]: VertexAttrib4fNV (will be remapped) */
   "glVertexAttrib4fNV\0"
   /* _mesa_function_pool[27557]: VertexAttrib4fvNV (will be remapped) */
   "glVertexAttrib4fvNV\0"
   /* _mesa_function_pool[27577]: VertexAttrib1dNV (will be remapped) */
   "glVertexAttrib1dNV\0"
   /* _mesa_function_pool[27596]: VertexAttrib1dvNV (will be remapped) */
   "glVertexAttrib1dvNV\0"
   /* _mesa_function_pool[27616]: VertexAttrib2dNV (will be remapped) */
   "glVertexAttrib2dNV\0"
   /* _mesa_function_pool[27635]: VertexAttrib2dvNV (will be remapped) */
   "glVertexAttrib2dvNV\0"
   /* _mesa_function_pool[27655]: VertexAttrib3dNV (will be remapped) */
   "glVertexAttrib3dNV\0"
   /* _mesa_function_pool[27674]: VertexAttrib3dvNV (will be remapped) */
   "glVertexAttrib3dvNV\0"
   /* _mesa_function_pool[27694]: VertexAttrib4dNV (will be remapped) */
   "glVertexAttrib4dNV\0"
   /* _mesa_function_pool[27713]: VertexAttrib4dvNV (will be remapped) */
   "glVertexAttrib4dvNV\0"
   /* _mesa_function_pool[27733]: VertexAttrib4ubNV (will be remapped) */
   "glVertexAttrib4ubNV\0"
   /* _mesa_function_pool[27753]: VertexAttrib4ubvNV (will be remapped) */
   "glVertexAttrib4ubvNV\0"
   /* _mesa_function_pool[27774]: VertexAttribs1svNV (will be remapped) */
   "glVertexAttribs1svNV\0"
   /* _mesa_function_pool[27795]: VertexAttribs2svNV (will be remapped) */
   "glVertexAttribs2svNV\0"
   /* _mesa_function_pool[27816]: VertexAttribs3svNV (will be remapped) */
   "glVertexAttribs3svNV\0"
   /* _mesa_function_pool[27837]: VertexAttribs4svNV (will be remapped) */
   "glVertexAttribs4svNV\0"
   /* _mesa_function_pool[27858]: VertexAttribs1fvNV (will be remapped) */
   "glVertexAttribs1fvNV\0"
   /* _mesa_function_pool[27879]: VertexAttribs2fvNV (will be remapped) */
   "glVertexAttribs2fvNV\0"
   /* _mesa_function_pool[27900]: VertexAttribs3fvNV (will be remapped) */
   "glVertexAttribs3fvNV\0"
   /* _mesa_function_pool[27921]: VertexAttribs4fvNV (will be remapped) */
   "glVertexAttribs4fvNV\0"
   /* _mesa_function_pool[27942]: VertexAttribs1dvNV (will be remapped) */
   "glVertexAttribs1dvNV\0"
   /* _mesa_function_pool[27963]: VertexAttribs2dvNV (will be remapped) */
   "glVertexAttribs2dvNV\0"
   /* _mesa_function_pool[27984]: VertexAttribs3dvNV (will be remapped) */
   "glVertexAttribs3dvNV\0"
   /* _mesa_function_pool[28005]: VertexAttribs4dvNV (will be remapped) */
   "glVertexAttribs4dvNV\0"
   /* _mesa_function_pool[28026]: VertexAttribs4ubvNV (will be remapped) */
   "glVertexAttribs4ubvNV\0"
   /* _mesa_function_pool[28048]: TexBumpParameterfvATI (will be remapped) */
   "glTexBumpParameterfvATI\0"
   /* _mesa_function_pool[28072]: TexBumpParameterivATI (will be remapped) */
   "glTexBumpParameterivATI\0"
   /* _mesa_function_pool[28096]: GetTexBumpParameterfvATI (will be remapped) */
   "glGetTexBumpParameterfvATI\0"
   /* _mesa_function_pool[28123]: GetTexBumpParameterivATI (will be remapped) */
   "glGetTexBumpParameterivATI\0"
   /* _mesa_function_pool[28150]: GenFragmentShadersATI (will be remapped) */
   "glGenFragmentShadersATI\0"
   /* _mesa_function_pool[28174]: BindFragmentShaderATI (will be remapped) */
   "glBindFragmentShaderATI\0"
   /* _mesa_function_pool[28198]: DeleteFragmentShaderATI (will be remapped) */
   "glDeleteFragmentShaderATI\0"
   /* _mesa_function_pool[28224]: BeginFragmentShaderATI (will be remapped) */
   "glBeginFragmentShaderATI\0"
   /* _mesa_function_pool[28249]: EndFragmentShaderATI (will be remapped) */
   "glEndFragmentShaderATI\0"
   /* _mesa_function_pool[28272]: PassTexCoordATI (will be remapped) */
   "glPassTexCoordATI\0"
   /* _mesa_function_pool[28290]: SampleMapATI (will be remapped) */
   "glSampleMapATI\0"
   /* _mesa_function_pool[28305]: ColorFragmentOp1ATI (will be remapped) */
   "glColorFragmentOp1ATI\0"
   /* _mesa_function_pool[28327]: ColorFragmentOp2ATI (will be remapped) */
   "glColorFragmentOp2ATI\0"
   /* _mesa_function_pool[28349]: ColorFragmentOp3ATI (will be remapped) */
   "glColorFragmentOp3ATI\0"
   /* _mesa_function_pool[28371]: AlphaFragmentOp1ATI (will be remapped) */
   "glAlphaFragmentOp1ATI\0"
   /* _mesa_function_pool[28393]: AlphaFragmentOp2ATI (will be remapped) */
   "glAlphaFragmentOp2ATI\0"
   /* _mesa_function_pool[28415]: AlphaFragmentOp3ATI (will be remapped) */
   "glAlphaFragmentOp3ATI\0"
   /* _mesa_function_pool[28437]: SetFragmentShaderConstantATI (will be remapped) */
   "glSetFragmentShaderConstantATI\0"
   /* _mesa_function_pool[28468]: DrawMeshArraysSUN (dynamic) */
   "glDrawMeshArraysSUN\0"
   /* _mesa_function_pool[28488]: ActiveStencilFaceEXT (will be remapped) */
   "glActiveStencilFaceEXT\0"
   /* _mesa_function_pool[28511]: ObjectPurgeableAPPLE (will be remapped) */
   "glObjectPurgeableAPPLE\0"
   /* _mesa_function_pool[28534]: ObjectUnpurgeableAPPLE (will be remapped) */
   "glObjectUnpurgeableAPPLE\0"
   /* _mesa_function_pool[28559]: GetObjectParameterivAPPLE (will be remapped) */
   "glGetObjectParameterivAPPLE\0"
   /* _mesa_function_pool[28587]: BindVertexArrayAPPLE (dynamic) */
   "glBindVertexArrayAPPLE\0"
   /* _mesa_function_pool[28610]: DeleteVertexArraysAPPLE (dynamic) */
   "glDeleteVertexArraysAPPLE\0"
   /* _mesa_function_pool[28636]: GenVertexArraysAPPLE (dynamic) */
   "glGenVertexArraysAPPLE\0"
   /* _mesa_function_pool[28659]: IsVertexArrayAPPLE (dynamic) */
   "glIsVertexArrayAPPLE\0"
   /* _mesa_function_pool[28680]: ProgramNamedParameter4fNV (will be remapped) */
   "glProgramNamedParameter4fNV\0"
   /* _mesa_function_pool[28708]: ProgramNamedParameter4dNV (will be remapped) */
   "glProgramNamedParameter4dNV\0"
   /* _mesa_function_pool[28736]: ProgramNamedParameter4fvNV (will be remapped) */
   "glProgramNamedParameter4fvNV\0"
   /* _mesa_function_pool[28765]: ProgramNamedParameter4dvNV (will be remapped) */
   "glProgramNamedParameter4dvNV\0"
   /* _mesa_function_pool[28794]: GetProgramNamedParameterfvNV (will be remapped) */
   "glGetProgramNamedParameterfvNV\0"
   /* _mesa_function_pool[28825]: GetProgramNamedParameterdvNV (will be remapped) */
   "glGetProgramNamedParameterdvNV\0"
   /* _mesa_function_pool[28856]: DepthBoundsEXT (will be remapped) */
   "glDepthBoundsEXT\0"
   /* _mesa_function_pool[28873]: BindRenderbufferEXT (will be remapped) */
   "glBindRenderbufferEXT\0"
   /* _mesa_function_pool[28895]: BindFramebufferEXT (will be remapped) */
   "glBindFramebufferEXT\0"
   /* _mesa_function_pool[28916]: StringMarkerGREMEDY (will be remapped) */
   "glStringMarkerGREMEDY\0"
   /* _mesa_function_pool[28938]: ProvokingVertex (will be remapped) */
   "glProvokingVertexEXT\0"
   /* _mesa_function_pool[28959]: ColorMaski (will be remapped) */
   "glColorMaskIndexedEXT\0"
   /* _mesa_function_pool[28981]: GetBooleani_v (will be remapped) */
   "glGetBooleanIndexedvEXT\0"
   /* _mesa_function_pool[29005]: GetIntegeri_v (will be remapped) */
   "glGetIntegerIndexedvEXT\0"
   /* _mesa_function_pool[29029]: Enablei (will be remapped) */
   "glEnableIndexedEXT\0"
   /* _mesa_function_pool[29048]: Disablei (will be remapped) */
   "glDisableIndexedEXT\0"
   /* _mesa_function_pool[29068]: IsEnabledi (will be remapped) */
   "glIsEnabledIndexedEXT\0"
   /* _mesa_function_pool[29090]: BufferParameteriAPPLE (will be remapped) */
   "glBufferParameteriAPPLE\0"
   /* _mesa_function_pool[29114]: FlushMappedBufferRangeAPPLE (will be remapped) */
   "glFlushMappedBufferRangeAPPLE\0"
   /* _mesa_function_pool[29144]: GetPerfMonitorGroupsAMD (will be remapped) */
   "glGetPerfMonitorGroupsAMD\0"
   /* _mesa_function_pool[29170]: GetPerfMonitorCountersAMD (will be remapped) */
   "glGetPerfMonitorCountersAMD\0"
   /* _mesa_function_pool[29198]: GetPerfMonitorGroupStringAMD (will be remapped) */
   "glGetPerfMonitorGroupStringAMD\0"
   /* _mesa_function_pool[29229]: GetPerfMonitorCounterStringAMD (will be remapped) */
   "glGetPerfMonitorCounterStringAMD\0"
   /* _mesa_function_pool[29262]: GetPerfMonitorCounterInfoAMD (will be remapped) */
   "glGetPerfMonitorCounterInfoAMD\0"
   /* _mesa_function_pool[29293]: GenPerfMonitorsAMD (will be remapped) */
   "glGenPerfMonitorsAMD\0"
   /* _mesa_function_pool[29314]: DeletePerfMonitorsAMD (will be remapped) */
   "glDeletePerfMonitorsAMD\0"
   /* _mesa_function_pool[29338]: SelectPerfMonitorCountersAMD (will be remapped) */
   "glSelectPerfMonitorCountersAMD\0"
   /* _mesa_function_pool[29369]: BeginPerfMonitorAMD (will be remapped) */
   "glBeginPerfMonitorAMD\0"
   /* _mesa_function_pool[29391]: EndPerfMonitorAMD (will be remapped) */
   "glEndPerfMonitorAMD\0"
   /* _mesa_function_pool[29411]: GetPerfMonitorCounterDataAMD (will be remapped) */
   "glGetPerfMonitorCounterDataAMD\0"
   /* _mesa_function_pool[29442]: TextureRangeAPPLE (dynamic) */
   "glTextureRangeAPPLE\0"
   /* _mesa_function_pool[29462]: GetTexParameterPointervAPPLE (dynamic) */
   "glGetTexParameterPointervAPPLE\0"
   /* _mesa_function_pool[29493]: UseShaderProgramEXT (will be remapped) */
   "glUseShaderProgramEXT\0"
   /* _mesa_function_pool[29515]: ActiveProgramEXT (will be remapped) */
   "glActiveProgramEXT\0"
   /* _mesa_function_pool[29534]: CreateShaderProgramEXT (will be remapped) */
   "glCreateShaderProgramEXT\0"
   /* _mesa_function_pool[29559]: CopyImageSubDataNV (will be remapped) */
   "glCopyImageSubDataNV\0"
   /* _mesa_function_pool[29580]: MatrixLoadfEXT (will be remapped) */
   "glMatrixLoadfEXT\0"
   /* _mesa_function_pool[29597]: MatrixLoaddEXT (will be remapped) */
   "glMatrixLoaddEXT\0"
   /* _mesa_function_pool[29614]: MatrixMultfEXT (will be remapped) */
   "glMatrixMultfEXT\0"
   /* _mesa_function_pool[29631]: MatrixMultdEXT (will be remapped) */
   "glMatrixMultdEXT\0"
   /* _mesa_function_pool[29648]: MatrixLoadIdentityEXT (will be remapped) */
   "glMatrixLoadIdentityEXT\0"
   /* _mesa_function_pool[29672]: MatrixRotatefEXT (will be remapped) */
   "glMatrixRotatefEXT\0"
   /* _mesa_function_pool[29691]: MatrixRotatedEXT (will be remapped) */
   "glMatrixRotatedEXT\0"
   /* _mesa_function_pool[29710]: MatrixScalefEXT (will be remapped) */
   "glMatrixScalefEXT\0"
   /* _mesa_function_pool[29728]: MatrixScaledEXT (will be remapped) */
   "glMatrixScaledEXT\0"
   /* _mesa_function_pool[29746]: MatrixTranslatefEXT (will be remapped) */
   "glMatrixTranslatefEXT\0"
   /* _mesa_function_pool[29768]: MatrixTranslatedEXT (will be remapped) */
   "glMatrixTranslatedEXT\0"
   /* _mesa_function_pool[29790]: MatrixOrthoEXT (will be remapped) */
   "glMatrixOrthoEXT\0"
   /* _mesa_function_pool[29807]: MatrixFrustumEXT (will be remapped) */
   "glMatrixFrustumEXT\0"
   /* _mesa_function_pool[29826]: MatrixPushEXT (will be remapped) */
   "glMatrixPushEXT\0"
   /* _mesa_function_pool[29842]: MatrixPopEXT (will be remapped) */
   "glMatrixPopEXT\0"
   /* _mesa_function_pool[29857]: ClientAttribDefaultEXT (will be remapped) */
   "glClientAttribDefaultEXT\0"
   /* _mesa_function_pool[29882]: PushClientAttribDefaultEXT (will be remapped) */
   "glPushClientAttribDefaultEXT\0"
   /* _mesa_function_pool[29911]: GetTextureParameterivEXT (will be remapped) */
   "glGetTextureParameterivEXT\0"
   /* _mesa_function_pool[29938]: GetTextureParameterfvEXT (will be remapped) */
   "glGetTextureParameterfvEXT\0"
   /* _mesa_function_pool[29965]: GetTextureLevelParameterivEXT (will be remapped) */
   "glGetTextureLevelParameterivEXT\0"
   /* _mesa_function_pool[29997]: GetTextureLevelParameterfvEXT (will be remapped) */
   "glGetTextureLevelParameterfvEXT\0"
   /* _mesa_function_pool[30029]: TextureParameteriEXT (will be remapped) */
   "glTextureParameteriEXT\0"
   /* _mesa_function_pool[30052]: TextureParameterivEXT (will be remapped) */
   "glTextureParameterivEXT\0"
   /* _mesa_function_pool[30076]: TextureParameterfEXT (will be remapped) */
   "glTextureParameterfEXT\0"
   /* _mesa_function_pool[30099]: TextureParameterfvEXT (will be remapped) */
   "glTextureParameterfvEXT\0"
   /* _mesa_function_pool[30123]: TextureImage1DEXT (will be remapped) */
   "glTextureImage1DEXT\0"
   /* _mesa_function_pool[30143]: TextureImage2DEXT (will be remapped) */
   "glTextureImage2DEXT\0"
   /* _mesa_function_pool[30163]: TextureImage3DEXT (will be remapped) */
   "glTextureImage3DEXT\0"
   /* _mesa_function_pool[30183]: TextureSubImage1DEXT (will be remapped) */
   "glTextureSubImage1DEXT\0"
   /* _mesa_function_pool[30206]: TextureSubImage2DEXT (will be remapped) */
   "glTextureSubImage2DEXT\0"
   /* _mesa_function_pool[30229]: TextureSubImage3DEXT (will be remapped) */
   "glTextureSubImage3DEXT\0"
   /* _mesa_function_pool[30252]: CopyTextureImage1DEXT (will be remapped) */
   "glCopyTextureImage1DEXT\0"
   /* _mesa_function_pool[30276]: CopyTextureImage2DEXT (will be remapped) */
   "glCopyTextureImage2DEXT\0"
   /* _mesa_function_pool[30300]: CopyTextureSubImage1DEXT (will be remapped) */
   "glCopyTextureSubImage1DEXT\0"
   /* _mesa_function_pool[30327]: CopyTextureSubImage2DEXT (will be remapped) */
   "glCopyTextureSubImage2DEXT\0"
   /* _mesa_function_pool[30354]: CopyTextureSubImage3DEXT (will be remapped) */
   "glCopyTextureSubImage3DEXT\0"
   /* _mesa_function_pool[30381]: GetTextureImageEXT (will be remapped) */
   "glGetTextureImageEXT\0"
   /* _mesa_function_pool[30402]: BindMultiTextureEXT (will be remapped) */
   "glBindMultiTextureEXT\0"
   /* _mesa_function_pool[30424]: EnableClientStateiEXT (will be remapped) */
   "glEnableClientStateIndexedEXT\0"
   /* _mesa_function_pool[30454]: DisableClientStateiEXT (will be remapped) */
   "glDisableClientStateIndexedEXT\0"
   /* _mesa_function_pool[30485]: GetPointerIndexedvEXT (will be remapped) */
   "glGetPointerIndexedvEXT\0"
   /* _mesa_function_pool[30509]: MultiTexEnviEXT (will be remapped) */
   "glMultiTexEnviEXT\0"
   /* _mesa_function_pool[30527]: MultiTexEnvivEXT (will be remapped) */
   "glMultiTexEnvivEXT\0"
   /* _mesa_function_pool[30546]: MultiTexEnvfEXT (will be remapped) */
   "glMultiTexEnvfEXT\0"
   /* _mesa_function_pool[30564]: MultiTexEnvfvEXT (will be remapped) */
   "glMultiTexEnvfvEXT\0"
   /* _mesa_function_pool[30583]: GetMultiTexEnvivEXT (will be remapped) */
   "glGetMultiTexEnvivEXT\0"
   /* _mesa_function_pool[30605]: GetMultiTexEnvfvEXT (will be remapped) */
   "glGetMultiTexEnvfvEXT\0"
   /* _mesa_function_pool[30627]: MultiTexParameteriEXT (will be remapped) */
   "glMultiTexParameteriEXT\0"
   /* _mesa_function_pool[30651]: MultiTexParameterivEXT (will be remapped) */
   "glMultiTexParameterivEXT\0"
   /* _mesa_function_pool[30676]: MultiTexParameterfEXT (will be remapped) */
   "glMultiTexParameterfEXT\0"
   /* _mesa_function_pool[30700]: MultiTexParameterfvEXT (will be remapped) */
   "glMultiTexParameterfvEXT\0"
   /* _mesa_function_pool[30725]: GetMultiTexParameterivEXT (will be remapped) */
   "glGetMultiTexParameterivEXT\0"
   /* _mesa_function_pool[30753]: GetMultiTexParameterfvEXT (will be remapped) */
   "glGetMultiTexParameterfvEXT\0"
   /* _mesa_function_pool[30781]: GetMultiTexImageEXT (will be remapped) */
   "glGetMultiTexImageEXT\0"
   /* _mesa_function_pool[30803]: GetMultiTexLevelParameterivEXT (will be remapped) */
   "glGetMultiTexLevelParameterivEXT\0"
   /* _mesa_function_pool[30836]: GetMultiTexLevelParameterfvEXT (will be remapped) */
   "glGetMultiTexLevelParameterfvEXT\0"
   /* _mesa_function_pool[30869]: MultiTexImage1DEXT (will be remapped) */
   "glMultiTexImage1DEXT\0"
   /* _mesa_function_pool[30890]: MultiTexImage2DEXT (will be remapped) */
   "glMultiTexImage2DEXT\0"
   /* _mesa_function_pool[30911]: MultiTexImage3DEXT (will be remapped) */
   "glMultiTexImage3DEXT\0"
   /* _mesa_function_pool[30932]: MultiTexSubImage1DEXT (will be remapped) */
   "glMultiTexSubImage1DEXT\0"
   /* _mesa_function_pool[30956]: MultiTexSubImage2DEXT (will be remapped) */
   "glMultiTexSubImage2DEXT\0"
   /* _mesa_function_pool[30980]: MultiTexSubImage3DEXT (will be remapped) */
   "glMultiTexSubImage3DEXT\0"
   /* _mesa_function_pool[31004]: CopyMultiTexImage1DEXT (will be remapped) */
   "glCopyMultiTexImage1DEXT\0"
   /* _mesa_function_pool[31029]: CopyMultiTexImage2DEXT (will be remapped) */
   "glCopyMultiTexImage2DEXT\0"
   /* _mesa_function_pool[31054]: CopyMultiTexSubImage1DEXT (will be remapped) */
   "glCopyMultiTexSubImage1DEXT\0"
   /* _mesa_function_pool[31082]: CopyMultiTexSubImage2DEXT (will be remapped) */
   "glCopyMultiTexSubImage2DEXT\0"
   /* _mesa_function_pool[31110]: CopyMultiTexSubImage3DEXT (will be remapped) */
   "glCopyMultiTexSubImage3DEXT\0"
   /* _mesa_function_pool[31138]: MultiTexGendEXT (will be remapped) */
   "glMultiTexGendEXT\0"
   /* _mesa_function_pool[31156]: MultiTexGendvEXT (will be remapped) */
   "glMultiTexGendvEXT\0"
   /* _mesa_function_pool[31175]: MultiTexGenfEXT (will be remapped) */
   "glMultiTexGenfEXT\0"
   /* _mesa_function_pool[31193]: MultiTexGenfvEXT (will be remapped) */
   "glMultiTexGenfvEXT\0"
   /* _mesa_function_pool[31212]: MultiTexGeniEXT (will be remapped) */
   "glMultiTexGeniEXT\0"
   /* _mesa_function_pool[31230]: MultiTexGenivEXT (will be remapped) */
   "glMultiTexGenivEXT\0"
   /* _mesa_function_pool[31249]: GetMultiTexGendvEXT (will be remapped) */
   "glGetMultiTexGendvEXT\0"
   /* _mesa_function_pool[31271]: GetMultiTexGenfvEXT (will be remapped) */
   "glGetMultiTexGenfvEXT\0"
   /* _mesa_function_pool[31293]: GetMultiTexGenivEXT (will be remapped) */
   "glGetMultiTexGenivEXT\0"
   /* _mesa_function_pool[31315]: MultiTexCoordPointerEXT (will be remapped) */
   "glMultiTexCoordPointerEXT\0"
   /* _mesa_function_pool[31341]: MatrixLoadTransposefEXT (will be remapped) */
   "glMatrixLoadTransposefEXT\0"
   /* _mesa_function_pool[31367]: MatrixLoadTransposedEXT (will be remapped) */
   "glMatrixLoadTransposedEXT\0"
   /* _mesa_function_pool[31393]: MatrixMultTransposefEXT (will be remapped) */
   "glMatrixMultTransposefEXT\0"
   /* _mesa_function_pool[31419]: MatrixMultTransposedEXT (will be remapped) */
   "glMatrixMultTransposedEXT\0"
   /* _mesa_function_pool[31445]: CompressedTextureImage1DEXT (will be remapped) */
   "glCompressedTextureImage1DEXT\0"
   /* _mesa_function_pool[31475]: CompressedTextureImage2DEXT (will be remapped) */
   "glCompressedTextureImage2DEXT\0"
   /* _mesa_function_pool[31505]: CompressedTextureImage3DEXT (will be remapped) */
   "glCompressedTextureImage3DEXT\0"
   /* _mesa_function_pool[31535]: CompressedTextureSubImage1DEXT (will be remapped) */
   "glCompressedTextureSubImage1DEXT\0"
   /* _mesa_function_pool[31568]: CompressedTextureSubImage2DEXT (will be remapped) */
   "glCompressedTextureSubImage2DEXT\0"
   /* _mesa_function_pool[31601]: CompressedTextureSubImage3DEXT (will be remapped) */
   "glCompressedTextureSubImage3DEXT\0"
   /* _mesa_function_pool[31634]: GetCompressedTextureImageEXT (will be remapped) */
   "glGetCompressedTextureImageEXT\0"
   /* _mesa_function_pool[31665]: CompressedMultiTexImage1DEXT (will be remapped) */
   "glCompressedMultiTexImage1DEXT\0"
   /* _mesa_function_pool[31696]: CompressedMultiTexImage2DEXT (will be remapped) */
   "glCompressedMultiTexImage2DEXT\0"
   /* _mesa_function_pool[31727]: CompressedMultiTexImage3DEXT (will be remapped) */
   "glCompressedMultiTexImage3DEXT\0"
   /* _mesa_function_pool[31758]: CompressedMultiTexSubImage1DEXT (will be remapped) */
   "glCompressedMultiTexSubImage1DEXT\0"
   /* _mesa_function_pool[31792]: CompressedMultiTexSubImage2DEXT (will be remapped) */
   "glCompressedMultiTexSubImage2DEXT\0"
   /* _mesa_function_pool[31826]: CompressedMultiTexSubImage3DEXT (will be remapped) */
   "glCompressedMultiTexSubImage3DEXT\0"
   /* _mesa_function_pool[31860]: GetCompressedMultiTexImageEXT (will be remapped) */
   "glGetCompressedMultiTexImageEXT\0"
   /* _mesa_function_pool[31892]: NamedBufferDataEXT (will be remapped) */
   "glNamedBufferDataEXT\0"
   /* _mesa_function_pool[31913]: NamedBufferSubDataEXT (will be remapped) */
   "glNamedBufferSubDataEXT\0"
   /* _mesa_function_pool[31937]: MapNamedBufferEXT (will be remapped) */
   "glMapNamedBufferEXT\0"
   /* _mesa_function_pool[31957]: GetNamedBufferSubDataEXT (will be remapped) */
   "glGetNamedBufferSubDataEXT\0"
   /* _mesa_function_pool[31984]: GetNamedBufferPointervEXT (will be remapped) */
   "glGetNamedBufferPointervEXT\0"
   /* _mesa_function_pool[32012]: GetNamedBufferParameterivEXT (will be remapped) */
   "glGetNamedBufferParameterivEXT\0"
   /* _mesa_function_pool[32043]: FlushMappedNamedBufferRangeEXT (will be remapped) */
   "glFlushMappedNamedBufferRangeEXT\0"
   /* _mesa_function_pool[32076]: MapNamedBufferRangeEXT (will be remapped) */
   "glMapNamedBufferRangeEXT\0"
   /* _mesa_function_pool[32101]: FramebufferDrawBufferEXT (will be remapped) */
   "glFramebufferDrawBufferEXT\0"
   /* _mesa_function_pool[32128]: FramebufferDrawBuffersEXT (will be remapped) */
   "glFramebufferDrawBuffersEXT\0"
   /* _mesa_function_pool[32156]: FramebufferReadBufferEXT (will be remapped) */
   "glFramebufferReadBufferEXT\0"
   /* _mesa_function_pool[32183]: GetFramebufferParameterivEXT (will be remapped) */
   "glGetFramebufferParameterivEXT\0"
   /* _mesa_function_pool[32214]: CheckNamedFramebufferStatusEXT (will be remapped) */
   "glCheckNamedFramebufferStatusEXT\0"
   /* _mesa_function_pool[32247]: NamedFramebufferTexture1DEXT (will be remapped) */
   "glNamedFramebufferTexture1DEXT\0"
   /* _mesa_function_pool[32278]: NamedFramebufferTexture2DEXT (will be remapped) */
   "glNamedFramebufferTexture2DEXT\0"
   /* _mesa_function_pool[32309]: NamedFramebufferTexture3DEXT (will be remapped) */
   "glNamedFramebufferTexture3DEXT\0"
   /* _mesa_function_pool[32340]: NamedFramebufferRenderbufferEXT (will be remapped) */
   "glNamedFramebufferRenderbufferEXT\0"
   /* _mesa_function_pool[32374]: GetNamedFramebufferAttachmentParameterivEXT (will be remapped) */
   "glGetNamedFramebufferAttachmentParameterivEXT\0"
   /* _mesa_function_pool[32420]: NamedRenderbufferStorageEXT (will be remapped) */
   "glNamedRenderbufferStorageEXT\0"
   /* _mesa_function_pool[32450]: GetNamedRenderbufferParameterivEXT (will be remapped) */
   "glGetNamedRenderbufferParameterivEXT\0"
   /* _mesa_function_pool[32487]: GenerateTextureMipmapEXT (will be remapped) */
   "glGenerateTextureMipmapEXT\0"
   /* _mesa_function_pool[32514]: GenerateMultiTexMipmapEXT (will be remapped) */
   "glGenerateMultiTexMipmapEXT\0"
   /* _mesa_function_pool[32542]: NamedRenderbufferStorageMultisampleEXT (will be remapped) */
   "glNamedRenderbufferStorageMultisampleEXT\0"
   /* _mesa_function_pool[32583]: NamedCopyBufferSubDataEXT (will be remapped) */
   "glNamedCopyBufferSubDataEXT\0"
   /* _mesa_function_pool[32611]: VertexArrayVertexOffsetEXT (will be remapped) */
   "glVertexArrayVertexOffsetEXT\0"
   /* _mesa_function_pool[32640]: VertexArrayColorOffsetEXT (will be remapped) */
   "glVertexArrayColorOffsetEXT\0"
   /* _mesa_function_pool[32668]: VertexArrayEdgeFlagOffsetEXT (will be remapped) */
   "glVertexArrayEdgeFlagOffsetEXT\0"
   /* _mesa_function_pool[32699]: VertexArrayIndexOffsetEXT (will be remapped) */
   "glVertexArrayIndexOffsetEXT\0"
   /* _mesa_function_pool[32727]: VertexArrayNormalOffsetEXT (will be remapped) */
   "glVertexArrayNormalOffsetEXT\0"
   /* _mesa_function_pool[32756]: VertexArrayTexCoordOffsetEXT (will be remapped) */
   "glVertexArrayTexCoordOffsetEXT\0"
   /* _mesa_function_pool[32787]: VertexArrayMultiTexCoordOffsetEXT (will be remapped) */
   "glVertexArrayMultiTexCoordOffsetEXT\0"
   /* _mesa_function_pool[32823]: VertexArrayFogCoordOffsetEXT (will be remapped) */
   "glVertexArrayFogCoordOffsetEXT\0"
   /* _mesa_function_pool[32854]: VertexArraySecondaryColorOffsetEXT (will be remapped) */
   "glVertexArraySecondaryColorOffsetEXT\0"
   /* _mesa_function_pool[32891]: VertexArrayVertexAttribOffsetEXT (will be remapped) */
   "glVertexArrayVertexAttribOffsetEXT\0"
   /* _mesa_function_pool[32926]: VertexArrayVertexAttribIOffsetEXT (will be remapped) */
   "glVertexArrayVertexAttribIOffsetEXT\0"
   /* _mesa_function_pool[32962]: EnableVertexArrayEXT (will be remapped) */
   "glEnableVertexArrayEXT\0"
   /* _mesa_function_pool[32985]: DisableVertexArrayEXT (will be remapped) */
   "glDisableVertexArrayEXT\0"
   /* _mesa_function_pool[33009]: EnableVertexArrayAttribEXT (will be remapped) */
   "glEnableVertexArrayAttribEXT\0"
   /* _mesa_function_pool[33038]: DisableVertexArrayAttribEXT (will be remapped) */
   "glDisableVertexArrayAttribEXT\0"
   /* _mesa_function_pool[33068]: GetVertexArrayIntegervEXT (will be remapped) */
   "glGetVertexArrayIntegervEXT\0"
   /* _mesa_function_pool[33096]: GetVertexArrayPointervEXT (will be remapped) */
   "glGetVertexArrayPointervEXT\0"
   /* _mesa_function_pool[33124]: GetVertexArrayIntegeri_vEXT (will be remapped) */
   "glGetVertexArrayIntegeri_vEXT\0"
   /* _mesa_function_pool[33154]: GetVertexArrayPointeri_vEXT (will be remapped) */
   "glGetVertexArrayPointeri_vEXT\0"
   /* _mesa_function_pool[33184]: NamedProgramStringEXT (will be remapped) */
   "glNamedProgramStringEXT\0"
   /* _mesa_function_pool[33208]: GetNamedProgramStringEXT (will be remapped) */
   "glGetNamedProgramStringEXT\0"
   /* _mesa_function_pool[33235]: NamedProgramLocalParameter4fEXT (will be remapped) */
   "glNamedProgramLocalParameter4fEXT\0"
   /* _mesa_function_pool[33269]: NamedProgramLocalParameter4fvEXT (will be remapped) */
   "glNamedProgramLocalParameter4fvEXT\0"
   /* _mesa_function_pool[33304]: GetNamedProgramLocalParameterfvEXT (will be remapped) */
   "glGetNamedProgramLocalParameterfvEXT\0"
   /* _mesa_function_pool[33341]: NamedProgramLocalParameter4dEXT (will be remapped) */
   "glNamedProgramLocalParameter4dEXT\0"
   /* _mesa_function_pool[33375]: NamedProgramLocalParameter4dvEXT (will be remapped) */
   "glNamedProgramLocalParameter4dvEXT\0"
   /* _mesa_function_pool[33410]: GetNamedProgramLocalParameterdvEXT (will be remapped) */
   "glGetNamedProgramLocalParameterdvEXT\0"
   /* _mesa_function_pool[33447]: GetNamedProgramivEXT (will be remapped) */
   "glGetNamedProgramivEXT\0"
   /* _mesa_function_pool[33470]: TextureBufferEXT (will be remapped) */
   "glTextureBufferEXT\0"
   /* _mesa_function_pool[33489]: MultiTexBufferEXT (will be remapped) */
   "glMultiTexBufferEXT\0"
   /* _mesa_function_pool[33509]: TextureParameterIivEXT (will be remapped) */
   "glTextureParameterIivEXT\0"
   /* _mesa_function_pool[33534]: TextureParameterIuivEXT (will be remapped) */
   "glTextureParameterIuivEXT\0"
   /* _mesa_function_pool[33560]: GetTextureParameterIivEXT (will be remapped) */
   "glGetTextureParameterIivEXT\0"
   /* _mesa_function_pool[33588]: GetTextureParameterIuivEXT (will be remapped) */
   "glGetTextureParameterIuivEXT\0"
   /* _mesa_function_pool[33617]: MultiTexParameterIivEXT (will be remapped) */
   "glMultiTexParameterIivEXT\0"
   /* _mesa_function_pool[33643]: MultiTexParameterIuivEXT (will be remapped) */
   "glMultiTexParameterIuivEXT\0"
   /* _mesa_function_pool[33670]: GetMultiTexParameterIivEXT (will be remapped) */
   "glGetMultiTexParameterIivEXT\0"
   /* _mesa_function_pool[33699]: GetMultiTexParameterIuivEXT (will be remapped) */
   "glGetMultiTexParameterIuivEXT\0"
   /* _mesa_function_pool[33729]: NamedProgramLocalParameters4fvEXT (will be remapped) */
   "glNamedProgramLocalParameters4fvEXT\0"
   /* _mesa_function_pool[33765]: BindImageTextureEXT (will be remapped) */
   "glBindImageTextureEXT\0"
   /* _mesa_function_pool[33787]: LabelObjectEXT (will be remapped) */
   "glLabelObjectEXT\0"
   /* _mesa_function_pool[33804]: GetObjectLabelEXT (will be remapped) */
   "glGetObjectLabelEXT\0"
   /* _mesa_function_pool[33824]: SubpixelPrecisionBiasNV (will be remapped) */
   "glSubpixelPrecisionBiasNV\0"
   /* _mesa_function_pool[33850]: ConservativeRasterParameterfNV (will be remapped) */
   "glConservativeRasterParameterfNV\0"
   /* _mesa_function_pool[33883]: ConservativeRasterParameteriNV (will be remapped) */
   "glConservativeRasterParameteriNV\0"
   /* _mesa_function_pool[33916]: GetFirstPerfQueryIdINTEL (will be remapped) */
   "glGetFirstPerfQueryIdINTEL\0"
   /* _mesa_function_pool[33943]: GetNextPerfQueryIdINTEL (will be remapped) */
   "glGetNextPerfQueryIdINTEL\0"
   /* _mesa_function_pool[33969]: GetPerfQueryIdByNameINTEL (will be remapped) */
   "glGetPerfQueryIdByNameINTEL\0"
   /* _mesa_function_pool[33997]: GetPerfQueryInfoINTEL (will be remapped) */
   "glGetPerfQueryInfoINTEL\0"
   /* _mesa_function_pool[34021]: GetPerfCounterInfoINTEL (will be remapped) */
   "glGetPerfCounterInfoINTEL\0"
   /* _mesa_function_pool[34047]: CreatePerfQueryINTEL (will be remapped) */
   "glCreatePerfQueryINTEL\0"
   /* _mesa_function_pool[34070]: DeletePerfQueryINTEL (will be remapped) */
   "glDeletePerfQueryINTEL\0"
   /* _mesa_function_pool[34093]: BeginPerfQueryINTEL (will be remapped) */
   "glBeginPerfQueryINTEL\0"
   /* _mesa_function_pool[34115]: EndPerfQueryINTEL (will be remapped) */
   "glEndPerfQueryINTEL\0"
   /* _mesa_function_pool[34135]: GetPerfQueryDataINTEL (will be remapped) */
   "glGetPerfQueryDataINTEL\0"
   /* _mesa_function_pool[34159]: AlphaToCoverageDitherControlNV (will be remapped) */
   "glAlphaToCoverageDitherControlNV\0"
   /* _mesa_function_pool[34192]: PolygonOffsetClampEXT (will be remapped) */
   "glPolygonOffsetClampEXT\0"
   /* _mesa_function_pool[34216]: WindowRectanglesEXT (will be remapped) */
   "glWindowRectanglesEXT\0"
   /* _mesa_function_pool[34238]: FramebufferFetchBarrierEXT (will be remapped) */
   "glFramebufferFetchBarrierEXT\0"
   /* _mesa_function_pool[34267]: TextureStorage1DEXT (will be remapped) */
   "glTextureStorage1DEXT\0"
   /* _mesa_function_pool[34289]: TextureStorage2DEXT (will be remapped) */
   "glTextureStorage2DEXT\0"
   /* _mesa_function_pool[34311]: TextureStorage3DEXT (will be remapped) */
   "glTextureStorage3DEXT\0"
   /* _mesa_function_pool[34333]: RenderbufferStorageMultisampleAdvancedAMD (will be remapped) */
   "glRenderbufferStorageMultisampleAdvancedAMD\0"
   /* _mesa_function_pool[34377]: NamedRenderbufferStorageMultisampleAdvancedAMD (will be remapped) */
   "glNamedRenderbufferStorageMultisampleAdvancedAMD\0"
   /* _mesa_function_pool[34426]: StencilFuncSeparateATI (will be remapped) */
   "glStencilFuncSeparateATI\0"
   /* _mesa_function_pool[34451]: ProgramEnvParameters4fvEXT (will be remapped) */
   "glProgramEnvParameters4fvEXT\0"
   /* _mesa_function_pool[34480]: ProgramLocalParameters4fvEXT (will be remapped) */
   "glProgramLocalParameters4fvEXT\0"
   /* _mesa_function_pool[34511]: IglooInterfaceSGIX (dynamic) */
   "glIglooInterfaceSGIX\0"
   /* _mesa_function_pool[34532]: DeformationMap3dSGIX (dynamic) */
   "glDeformationMap3dSGIX\0"
   /* _mesa_function_pool[34555]: DeformationMap3fSGIX (dynamic) */
   "glDeformationMap3fSGIX\0"
   /* _mesa_function_pool[34578]: DeformSGIX (dynamic) */
   "glDeformSGIX\0"
   /* _mesa_function_pool[34591]: LoadIdentityDeformationMapSGIX (dynamic) */
   "glLoadIdentityDeformationMapSGIX\0"
   /* _mesa_function_pool[34624]: InternalBufferSubDataCopyMESA (will be remapped) */
   "glInternalBufferSubDataCopyMESA\0"
   /* _mesa_function_pool[34656]: InternalSetError (will be remapped) */
   "glInternalSetError\0"
   /* _mesa_function_pool[34675]: DrawArraysUserBuf (will be remapped) */
   "glDrawArraysUserBuf\0"
   /* _mesa_function_pool[34695]: DrawElementsUserBuf (will be remapped) */
   "glDrawElementsUserBuf\0"
   /* _mesa_function_pool[34717]: DrawElementsUserBufPacked (will be remapped) */
   "glDrawElementsUserBufPacked\0"
   /* _mesa_function_pool[34745]: MultiDrawArraysUserBuf (will be remapped) */
   "glMultiDrawArraysUserBuf\0"
   /* _mesa_function_pool[34770]: MultiDrawElementsUserBuf (will be remapped) */
   "glMultiDrawElementsUserBuf\0"
   /* _mesa_function_pool[34797]: DrawArraysInstancedBaseInstanceDrawID (will be remapped) */
   "glDrawArraysInstancedBaseInstanceDrawID\0"
   /* _mesa_function_pool[34837]: DrawElementsInstancedBaseVertexBaseInstanceDrawID (will be remapped) */
   "glDrawElementsInstancedBaseVertexBaseInstanceDrawID\0"
   /* _mesa_function_pool[34889]: DrawElementsPacked (will be remapped) */
   "glDrawElementsPacked\0"
   /* _mesa_function_pool[34910]: InternalInvalidateFramebufferAncillaryMESA (will be remapped) */
   "glInternalInvalidateFramebufferAncillaryMESA\0"
   /* _mesa_function_pool[34955]: EGLImageTargetTexture2DOES (will be remapped) */
   "glEGLImageTargetTexture2DOES\0"
   /* _mesa_function_pool[34984]: EGLImageTargetRenderbufferStorageOES (will be remapped) */
   "glEGLImageTargetRenderbufferStorageOES\0"
   /* _mesa_function_pool[35023]: EGLImageTargetTexStorageEXT (will be remapped) */
   "glEGLImageTargetTexStorageEXT\0"
   /* _mesa_function_pool[35053]: EGLImageTargetTextureStorageEXT (will be remapped) */
   "glEGLImageTargetTextureStorageEXT\0"
   /* _mesa_function_pool[35087]: ClearColorIiEXT (will be remapped) */
   "glClearColorIiEXT\0"
   /* _mesa_function_pool[35105]: ClearColorIuiEXT (will be remapped) */
   "glClearColorIuiEXT\0"
   /* _mesa_function_pool[35124]: TexParameterIiv (will be remapped) */
   "glTexParameterIivEXT\0"
   /* _mesa_function_pool[35145]: TexParameterIuiv (will be remapped) */
   "glTexParameterIuivEXT\0"
   /* _mesa_function_pool[35167]: GetTexParameterIiv (will be remapped) */
   "glGetTexParameterIivEXT\0"
   /* _mesa_function_pool[35191]: GetTexParameterIuiv (will be remapped) */
   "glGetTexParameterIuivEXT\0"
   /* _mesa_function_pool[35216]: VertexAttribI1iEXT (will be remapped) */
   "glVertexAttribI1iEXT\0"
   /* _mesa_function_pool[35237]: VertexAttribI2iEXT (will be remapped) */
   "glVertexAttribI2iEXT\0"
   /* _mesa_function_pool[35258]: VertexAttribI3iEXT (will be remapped) */
   "glVertexAttribI3iEXT\0"
   /* _mesa_function_pool[35279]: VertexAttribI4iEXT (will be remapped) */
   "glVertexAttribI4iEXT\0"
   /* _mesa_function_pool[35300]: VertexAttribI1uiEXT (will be remapped) */
   "glVertexAttribI1uiEXT\0"
   /* _mesa_function_pool[35322]: VertexAttribI2uiEXT (will be remapped) */
   "glVertexAttribI2uiEXT\0"
   /* _mesa_function_pool[35344]: VertexAttribI3uiEXT (will be remapped) */
   "glVertexAttribI3uiEXT\0"
   /* _mesa_function_pool[35366]: VertexAttribI4uiEXT (will be remapped) */
   "glVertexAttribI4uiEXT\0"
   /* _mesa_function_pool[35388]: VertexAttribI1iv (will be remapped) */
   "glVertexAttribI1ivEXT\0"
   /* _mesa_function_pool[35410]: VertexAttribI2ivEXT (will be remapped) */
   "glVertexAttribI2ivEXT\0"
   /* _mesa_function_pool[35432]: VertexAttribI3ivEXT (will be remapped) */
   "glVertexAttribI3ivEXT\0"
   /* _mesa_function_pool[35454]: VertexAttribI4ivEXT (will be remapped) */
   "glVertexAttribI4ivEXT\0"
   /* _mesa_function_pool[35476]: VertexAttribI1uiv (will be remapped) */
   "glVertexAttribI1uivEXT\0"
   /* _mesa_function_pool[35499]: VertexAttribI2uivEXT (will be remapped) */
   "glVertexAttribI2uivEXT\0"
   /* _mesa_function_pool[35522]: VertexAttribI3uivEXT (will be remapped) */
   "glVertexAttribI3uivEXT\0"
   /* _mesa_function_pool[35545]: VertexAttribI4uivEXT (will be remapped) */
   "glVertexAttribI4uivEXT\0"
   /* _mesa_function_pool[35568]: VertexAttribI4bv (will be remapped) */
   "glVertexAttribI4bvEXT\0"
   /* _mesa_function_pool[35590]: VertexAttribI4sv (will be remapped) */
   "glVertexAttribI4svEXT\0"
   /* _mesa_function_pool[35612]: VertexAttribI4ubv (will be remapped) */
   "glVertexAttribI4ubvEXT\0"
   /* _mesa_function_pool[35635]: VertexAttribI4usv (will be remapped) */
   "glVertexAttribI4usvEXT\0"
   /* _mesa_function_pool[35658]: VertexAttribIPointer (will be remapped) */
   "glVertexAttribIPointerEXT\0"
   /* _mesa_function_pool[35684]: GetVertexAttribIiv (will be remapped) */
   "glGetVertexAttribIivEXT\0"
   /* _mesa_function_pool[35708]: GetVertexAttribIuiv (will be remapped) */
   "glGetVertexAttribIuivEXT\0"
   /* _mesa_function_pool[35733]: Uniform1ui (will be remapped) */
   "glUniform1uiEXT\0"
   /* _mesa_function_pool[35749]: Uniform2ui (will be remapped) */
   "glUniform2uiEXT\0"
   /* _mesa_function_pool[35765]: Uniform3ui (will be remapped) */
   "glUniform3uiEXT\0"
   /* _mesa_function_pool[35781]: Uniform4ui (will be remapped) */
   "glUniform4uiEXT\0"
   /* _mesa_function_pool[35797]: Uniform1uiv (will be remapped) */
   "glUniform1uivEXT\0"
   /* _mesa_function_pool[35814]: Uniform2uiv (will be remapped) */
   "glUniform2uivEXT\0"
   /* _mesa_function_pool[35831]: Uniform3uiv (will be remapped) */
   "glUniform3uivEXT\0"
   /* _mesa_function_pool[35848]: Uniform4uiv (will be remapped) */
   "glUniform4uivEXT\0"
   /* _mesa_function_pool[35865]: GetUniformuiv (will be remapped) */
   "glGetUniformuivEXT\0"
   /* _mesa_function_pool[35884]: BindFragDataLocation (will be remapped) */
   "glBindFragDataLocationEXT\0"
   /* _mesa_function_pool[35910]: GetFragDataLocation (will be remapped) */
   "glGetFragDataLocationEXT\0"
   /* _mesa_function_pool[35935]: ClearBufferiv (will be remapped) */
   "glClearBufferiv\0"
   /* _mesa_function_pool[35951]: ClearBufferuiv (will be remapped) */
   "glClearBufferuiv\0"
   /* _mesa_function_pool[35968]: ClearBufferfv (will be remapped) */
   "glClearBufferfv\0"
   /* _mesa_function_pool[35984]: ClearBufferfi (will be remapped) */
   "glClearBufferfi\0"
   /* _mesa_function_pool[36000]: GetStringi (will be remapped) */
   "glGetStringi\0"
   /* _mesa_function_pool[36013]: BeginTransformFeedback (will be remapped) */
   "glBeginTransformFeedback\0"
   /* _mesa_function_pool[36038]: EndTransformFeedback (will be remapped) */
   "glEndTransformFeedback\0"
   /* _mesa_function_pool[36061]: BindBufferRange (will be remapped) */
   "glBindBufferRange\0"
   /* _mesa_function_pool[36079]: BindBufferBase (will be remapped) */
   "glBindBufferBase\0"
   /* _mesa_function_pool[36096]: TransformFeedbackVaryings (will be remapped) */
   "glTransformFeedbackVaryings\0"
   /* _mesa_function_pool[36124]: GetTransformFeedbackVarying (will be remapped) */
   "glGetTransformFeedbackVarying\0"
   /* _mesa_function_pool[36154]: BeginConditionalRender (will be remapped) */
   "glBeginConditionalRender\0"
   /* _mesa_function_pool[36179]: EndConditionalRender (will be remapped) */
   "glEndConditionalRender\0"
   /* _mesa_function_pool[36202]: PrimitiveRestartIndex (will be remapped) */
   "glPrimitiveRestartIndex\0"
   /* _mesa_function_pool[36226]: GetInteger64i_v (will be remapped) */
   "glGetInteger64i_v\0"
   /* _mesa_function_pool[36244]: GetBufferParameteri64v (will be remapped) */
   "glGetBufferParameteri64v\0"
   /* _mesa_function_pool[36269]: FramebufferTexture (will be remapped) */
   "glFramebufferTexture\0"
   /* _mesa_function_pool[36290]: PrimitiveRestartNV (will be remapped) */
   "glPrimitiveRestartNV\0"
   /* _mesa_function_pool[36311]: BindBufferOffsetEXT (will be remapped) */
   "glBindBufferOffsetEXT\0"
   /* _mesa_function_pool[36333]: BindTransformFeedback (will be remapped) */
   "glBindTransformFeedback\0"
   /* _mesa_function_pool[36357]: DeleteTransformFeedbacks (will be remapped) */
   "glDeleteTransformFeedbacks\0"
   /* _mesa_function_pool[36384]: GenTransformFeedbacks (will be remapped) */
   "glGenTransformFeedbacks\0"
   /* _mesa_function_pool[36408]: IsTransformFeedback (will be remapped) */
   "glIsTransformFeedback\0"
   /* _mesa_function_pool[36430]: PauseTransformFeedback (will be remapped) */
   "glPauseTransformFeedback\0"
   /* _mesa_function_pool[36455]: ResumeTransformFeedback (will be remapped) */
   "glResumeTransformFeedback\0"
   /* _mesa_function_pool[36481]: DrawTransformFeedback (will be remapped) */
   "glDrawTransformFeedback\0"
   /* _mesa_function_pool[36505]: VDPAUInitNV (will be remapped) */
   "glVDPAUInitNV\0"
   /* _mesa_function_pool[36519]: VDPAUFiniNV (will be remapped) */
   "glVDPAUFiniNV\0"
   /* _mesa_function_pool[36533]: VDPAURegisterVideoSurfaceNV (will be remapped) */
   "glVDPAURegisterVideoSurfaceNV\0"
   /* _mesa_function_pool[36563]: VDPAURegisterOutputSurfaceNV (will be remapped) */
   "glVDPAURegisterOutputSurfaceNV\0"
   /* _mesa_function_pool[36594]: VDPAUIsSurfaceNV (will be remapped) */
   "glVDPAUIsSurfaceNV\0"
   /* _mesa_function_pool[36613]: VDPAUUnregisterSurfaceNV (will be remapped) */
   "glVDPAUUnregisterSurfaceNV\0"
   /* _mesa_function_pool[36640]: VDPAUGetSurfaceivNV (will be remapped) */
   "glVDPAUGetSurfaceivNV\0"
   /* _mesa_function_pool[36662]: VDPAUSurfaceAccessNV (will be remapped) */
   "glVDPAUSurfaceAccessNV\0"
   /* _mesa_function_pool[36685]: VDPAUMapSurfacesNV (will be remapped) */
   "glVDPAUMapSurfacesNV\0"
   /* _mesa_function_pool[36706]: VDPAUUnmapSurfacesNV (will be remapped) */
   "glVDPAUUnmapSurfacesNV\0"
   /* _mesa_function_pool[36729]: GetUnsignedBytevEXT (will be remapped) */
   "glGetUnsignedBytevEXT\0"
   /* _mesa_function_pool[36751]: GetUnsignedBytei_vEXT (will be remapped) */
   "glGetUnsignedBytei_vEXT\0"
   /* _mesa_function_pool[36775]: DeleteMemoryObjectsEXT (will be remapped) */
   "glDeleteMemoryObjectsEXT\0"
   /* _mesa_function_pool[36800]: IsMemoryObjectEXT (will be remapped) */
   "glIsMemoryObjectEXT\0"
   /* _mesa_function_pool[36820]: CreateMemoryObjectsEXT (will be remapped) */
   "glCreateMemoryObjectsEXT\0"
   /* _mesa_function_pool[36845]: MemoryObjectParameterivEXT (will be remapped) */
   "glMemoryObjectParameterivEXT\0"
   /* _mesa_function_pool[36874]: GetMemoryObjectParameterivEXT (will be remapped) */
   "glGetMemoryObjectParameterivEXT\0"
   /* _mesa_function_pool[36906]: TexStorageMem2DEXT (will be remapped) */
   "glTexStorageMem2DEXT\0"
   /* _mesa_function_pool[36927]: TexStorageMem2DMultisampleEXT (will be remapped) */
   "glTexStorageMem2DMultisampleEXT\0"
   /* _mesa_function_pool[36959]: TexStorageMem3DEXT (will be remapped) */
   "glTexStorageMem3DEXT\0"
   /* _mesa_function_pool[36980]: TexStorageMem3DMultisampleEXT (will be remapped) */
   "glTexStorageMem3DMultisampleEXT\0"
   /* _mesa_function_pool[37012]: BufferStorageMemEXT (will be remapped) */
   "glBufferStorageMemEXT\0"
   /* _mesa_function_pool[37034]: TextureStorageMem2DEXT (will be remapped) */
   "glTextureStorageMem2DEXT\0"
   /* _mesa_function_pool[37059]: TextureStorageMem2DMultisampleEXT (will be remapped) */
   "glTextureStorageMem2DMultisampleEXT\0"
   /* _mesa_function_pool[37095]: TextureStorageMem3DEXT (will be remapped) */
   "glTextureStorageMem3DEXT\0"
   /* _mesa_function_pool[37120]: TextureStorageMem3DMultisampleEXT (will be remapped) */
   "glTextureStorageMem3DMultisampleEXT\0"
   /* _mesa_function_pool[37156]: NamedBufferStorageMemEXT (will be remapped) */
   "glNamedBufferStorageMemEXT\0"
   /* _mesa_function_pool[37183]: TexStorageMem1DEXT (will be remapped) */
   "glTexStorageMem1DEXT\0"
   /* _mesa_function_pool[37204]: TextureStorageMem1DEXT (will be remapped) */
   "glTextureStorageMem1DEXT\0"
   /* _mesa_function_pool[37229]: GenSemaphoresEXT (will be remapped) */
   "glGenSemaphoresEXT\0"
   /* _mesa_function_pool[37248]: DeleteSemaphoresEXT (will be remapped) */
   "glDeleteSemaphoresEXT\0"
   /* _mesa_function_pool[37270]: IsSemaphoreEXT (will be remapped) */
   "glIsSemaphoreEXT\0"
   /* _mesa_function_pool[37287]: SemaphoreParameterui64vEXT (will be remapped) */
   "glSemaphoreParameterui64vEXT\0"
   /* _mesa_function_pool[37316]: GetSemaphoreParameterui64vEXT (will be remapped) */
   "glGetSemaphoreParameterui64vEXT\0"
   /* _mesa_function_pool[37348]: WaitSemaphoreEXT (will be remapped) */
   "glWaitSemaphoreEXT\0"
   /* _mesa_function_pool[37367]: SignalSemaphoreEXT (will be remapped) */
   "glSignalSemaphoreEXT\0"
   /* _mesa_function_pool[37388]: ImportMemoryFdEXT (will be remapped) */
   "glImportMemoryFdEXT\0"
   /* _mesa_function_pool[37408]: ImportSemaphoreFdEXT (will be remapped) */
   "glImportSemaphoreFdEXT\0"
   /* _mesa_function_pool[37431]: ImportMemoryWin32HandleEXT (will be remapped) */
   "glImportMemoryWin32HandleEXT\0"
   /* _mesa_function_pool[37460]: ImportMemoryWin32NameEXT (will be remapped) */
   "glImportMemoryWin32NameEXT\0"
   /* _mesa_function_pool[37487]: ImportSemaphoreWin32HandleEXT (will be remapped) */
   "glImportSemaphoreWin32HandleEXT\0"
   /* _mesa_function_pool[37519]: ImportSemaphoreWin32NameEXT (will be remapped) */
   "glImportSemaphoreWin32NameEXT\0"
   /* _mesa_function_pool[37549]: ViewportSwizzleNV (will be remapped) */
   "glViewportSwizzleNV\0"
   /* _mesa_function_pool[37569]: Vertex2hNV (will be remapped) */
   "glVertex2hNV\0"
   /* _mesa_function_pool[37582]: Vertex2hvNV (will be remapped) */
   "glVertex2hvNV\0"
   /* _mesa_function_pool[37596]: Vertex3hNV (will be remapped) */
   "glVertex3hNV\0"
   /* _mesa_function_pool[37609]: Vertex3hvNV (will be remapped) */
   "glVertex3hvNV\0"
   /* _mesa_function_pool[37623]: Vertex4hNV (will be remapped) */
   "glVertex4hNV\0"
   /* _mesa_function_pool[37636]: Vertex4hvNV (will be remapped) */
   "glVertex4hvNV\0"
   /* _mesa_function_pool[37650]: Normal3hNV (will be remapped) */
   "glNormal3hNV\0"
   /* _mesa_function_pool[37663]: Normal3hvNV (will be remapped) */
   "glNormal3hvNV\0"
   /* _mesa_function_pool[37677]: Color3hNV (will be remapped) */
   "glColor3hNV\0"
   /* _mesa_function_pool[37689]: Color3hvNV (will be remapped) */
   "glColor3hvNV\0"
   /* _mesa_function_pool[37702]: Color4hNV (will be remapped) */
   "glColor4hNV\0"
   /* _mesa_function_pool[37714]: Color4hvNV (will be remapped) */
   "glColor4hvNV\0"
   /* _mesa_function_pool[37727]: TexCoord1hNV (will be remapped) */
   "glTexCoord1hNV\0"
   /* _mesa_function_pool[37742]: TexCoord1hvNV (will be remapped) */
   "glTexCoord1hvNV\0"
   /* _mesa_function_pool[37758]: TexCoord2hNV (will be remapped) */
   "glTexCoord2hNV\0"
   /* _mesa_function_pool[37773]: TexCoord2hvNV (will be remapped) */
   "glTexCoord2hvNV\0"
   /* _mesa_function_pool[37789]: TexCoord3hNV (will be remapped) */
   "glTexCoord3hNV\0"
   /* _mesa_function_pool[37804]: TexCoord3hvNV (will be remapped) */
   "glTexCoord3hvNV\0"
   /* _mesa_function_pool[37820]: TexCoord4hNV (will be remapped) */
   "glTexCoord4hNV\0"
   /* _mesa_function_pool[37835]: TexCoord4hvNV (will be remapped) */
   "glTexCoord4hvNV\0"
   /* _mesa_function_pool[37851]: MultiTexCoord1hNV (will be remapped) */
   "glMultiTexCoord1hNV\0"
   /* _mesa_function_pool[37871]: MultiTexCoord1hvNV (will be remapped) */
   "glMultiTexCoord1hvNV\0"
   /* _mesa_function_pool[37892]: MultiTexCoord2hNV (will be remapped) */
   "glMultiTexCoord2hNV\0"
   /* _mesa_function_pool[37912]: MultiTexCoord2hvNV (will be remapped) */
   "glMultiTexCoord2hvNV\0"
   /* _mesa_function_pool[37933]: MultiTexCoord3hNV (will be remapped) */
   "glMultiTexCoord3hNV\0"
   /* _mesa_function_pool[37953]: MultiTexCoord3hvNV (will be remapped) */
   "glMultiTexCoord3hvNV\0"
   /* _mesa_function_pool[37974]: MultiTexCoord4hNV (will be remapped) */
   "glMultiTexCoord4hNV\0"
   /* _mesa_function_pool[37994]: MultiTexCoord4hvNV (will be remapped) */
   "glMultiTexCoord4hvNV\0"
   /* _mesa_function_pool[38015]: VertexAttrib1hNV (will be remapped) */
   "glVertexAttrib1hNV\0"
   /* _mesa_function_pool[38034]: VertexAttrib1hvNV (will be remapped) */
   "glVertexAttrib1hvNV\0"
   /* _mesa_function_pool[38054]: VertexAttrib2hNV (will be remapped) */
   "glVertexAttrib2hNV\0"
   /* _mesa_function_pool[38073]: VertexAttrib2hvNV (will be remapped) */
   "glVertexAttrib2hvNV\0"
   /* _mesa_function_pool[38093]: VertexAttrib3hNV (will be remapped) */
   "glVertexAttrib3hNV\0"
   /* _mesa_function_pool[38112]: VertexAttrib3hvNV (will be remapped) */
   "glVertexAttrib3hvNV\0"
   /* _mesa_function_pool[38132]: VertexAttrib4hNV (will be remapped) */
   "glVertexAttrib4hNV\0"
   /* _mesa_function_pool[38151]: VertexAttrib4hvNV (will be remapped) */
   "glVertexAttrib4hvNV\0"
   /* _mesa_function_pool[38171]: VertexAttribs1hvNV (will be remapped) */
   "glVertexAttribs1hvNV\0"
   /* _mesa_function_pool[38192]: VertexAttribs2hvNV (will be remapped) */
   "glVertexAttribs2hvNV\0"
   /* _mesa_function_pool[38213]: VertexAttribs3hvNV (will be remapped) */
   "glVertexAttribs3hvNV\0"
   /* _mesa_function_pool[38234]: VertexAttribs4hvNV (will be remapped) */
   "glVertexAttribs4hvNV\0"
   /* _mesa_function_pool[38255]: FogCoordhNV (will be remapped) */
   "glFogCoordhNV\0"
   /* _mesa_function_pool[38269]: FogCoordhvNV (will be remapped) */
   "glFogCoordhvNV\0"
   /* _mesa_function_pool[38284]: SecondaryColor3hNV (will be remapped) */
   "glSecondaryColor3hNV\0"
   /* _mesa_function_pool[38305]: SecondaryColor3hvNV (will be remapped) */
   "glSecondaryColor3hvNV\0"
   /* _mesa_function_pool[38327]: MemoryBarrierByRegion (will be remapped) */
   "glMemoryBarrierByRegion\0"
   /* _mesa_function_pool[38351]: AlphaFuncx (will be remapped) */
   "glAlphaFuncxOES\0"
   /* _mesa_function_pool[38367]: ClearColorx (will be remapped) */
   "glClearColorxOES\0"
   /* _mesa_function_pool[38384]: ClearDepthx (will be remapped) */
   "glClearDepthxOES\0"
   /* _mesa_function_pool[38401]: Color4x (will be remapped) */
   "glColor4xOES\0"
   /* _mesa_function_pool[38414]: DepthRangex (will be remapped) */
   "glDepthRangexOES\0"
   /* _mesa_function_pool[38431]: Fogx (will be remapped) */
   "glFogxOES\0"
   /* _mesa_function_pool[38441]: Fogxv (will be remapped) */
   "glFogxvOES\0"
   /* _mesa_function_pool[38452]: Frustumx (will be remapped) */
   "glFrustumxOES\0"
   /* _mesa_function_pool[38466]: LightModelx (will be remapped) */
   "glLightModelxOES\0"
   /* _mesa_function_pool[38483]: LightModelxv (will be remapped) */
   "glLightModelxvOES\0"
   /* _mesa_function_pool[38501]: Lightx (will be remapped) */
   "glLightxOES\0"
   /* _mesa_function_pool[38513]: Lightxv (will be remapped) */
   "glLightxvOES\0"
   /* _mesa_function_pool[38526]: LineWidthx (will be remapped) */
   "glLineWidthxOES\0"
   /* _mesa_function_pool[38542]: LoadMatrixx (will be remapped) */
   "glLoadMatrixxOES\0"
   /* _mesa_function_pool[38559]: Materialx (will be remapped) */
   "glMaterialxOES\0"
   /* _mesa_function_pool[38574]: Materialxv (will be remapped) */
   "glMaterialxvOES\0"
   /* _mesa_function_pool[38590]: MultMatrixx (will be remapped) */
   "glMultMatrixxOES\0"
   /* _mesa_function_pool[38607]: MultiTexCoord4x (will be remapped) */
   "glMultiTexCoord4xOES\0"
   /* _mesa_function_pool[38628]: Normal3x (will be remapped) */
   "glNormal3xOES\0"
   /* _mesa_function_pool[38642]: Orthox (will be remapped) */
   "glOrthoxOES\0"
   /* _mesa_function_pool[38654]: PointSizex (will be remapped) */
   "glPointSizexOES\0"
   /* _mesa_function_pool[38670]: PolygonOffsetx (will be remapped) */
   "glPolygonOffsetxOES\0"
   /* _mesa_function_pool[38690]: Rotatex (will be remapped) */
   "glRotatexOES\0"
   /* _mesa_function_pool[38703]: SampleCoveragex (will be remapped) */
   "glSampleCoveragexOES\0"
   /* _mesa_function_pool[38724]: Scalex (will be remapped) */
   "glScalexOES\0"
   /* _mesa_function_pool[38736]: TexEnvx (will be remapped) */
   "glTexEnvxOES\0"
   /* _mesa_function_pool[38749]: TexEnvxv (will be remapped) */
   "glTexEnvxvOES\0"
   /* _mesa_function_pool[38763]: TexParameterx (will be remapped) */
   "glTexParameterxOES\0"
   /* _mesa_function_pool[38782]: Translatex (will be remapped) */
   "glTranslatexOES\0"
   /* _mesa_function_pool[38798]: ClipPlanex (will be remapped) */
   "glClipPlanexOES\0"
   /* _mesa_function_pool[38814]: GetClipPlanex (will be remapped) */
   "glGetClipPlanexOES\0"
   /* _mesa_function_pool[38833]: GetFixedv (will be remapped) */
   "glGetFixedvOES\0"
   /* _mesa_function_pool[38848]: GetLightxv (will be remapped) */
   "glGetLightxvOES\0"
   /* _mesa_function_pool[38864]: GetMaterialxv (will be remapped) */
   "glGetMaterialxvOES\0"
   /* _mesa_function_pool[38883]: GetTexEnvxv (will be remapped) */
   "glGetTexEnvxvOES\0"
   /* _mesa_function_pool[38900]: GetTexParameterxv (will be remapped) */
   "glGetTexParameterxvOES\0"
   /* _mesa_function_pool[38923]: PointParameterx (will be remapped) */
   "glPointParameterxOES\0"
   /* _mesa_function_pool[38944]: PointParameterxv (will be remapped) */
   "glPointParameterxvOES\0"
   /* _mesa_function_pool[38966]: TexParameterxv (will be remapped) */
   "glTexParameterxvOES\0"
   /* _mesa_function_pool[38986]: GetTexGenxvOES (will be remapped) */
   "glGetTexGenxvOES\0"
   /* _mesa_function_pool[39003]: TexGenxOES (will be remapped) */
   "glTexGenxOES\0"
   /* _mesa_function_pool[39016]: TexGenxvOES (will be remapped) */
   "glTexGenxvOES\0"
   /* _mesa_function_pool[39030]: ClipPlanef (will be remapped) */
   "glClipPlanefOES\0"
   /* _mesa_function_pool[39046]: GetClipPlanef (will be remapped) */
   "glGetClipPlanefOES\0"
   /* _mesa_function_pool[39065]: Frustumf (will be remapped) */
   "glFrustumfOES\0"
   /* _mesa_function_pool[39079]: Orthof (will be remapped) */
   "glOrthofOES\0"
   /* _mesa_function_pool[39091]: DrawTexiOES (will be remapped) */
   "glDrawTexiOES\0"
   /* _mesa_function_pool[39105]: DrawTexivOES (will be remapped) */
   "glDrawTexivOES\0"
   /* _mesa_function_pool[39120]: DrawTexfOES (will be remapped) */
   "glDrawTexfOES\0"
   /* _mesa_function_pool[39134]: DrawTexfvOES (will be remapped) */
   "glDrawTexfvOES\0"
   /* _mesa_function_pool[39149]: DrawTexsOES (will be remapped) */
   "glDrawTexsOES\0"
   /* _mesa_function_pool[39163]: DrawTexsvOES (will be remapped) */
   "glDrawTexsvOES\0"
   /* _mesa_function_pool[39178]: DrawTexxOES (will be remapped) */
   "glDrawTexxOES\0"
   /* _mesa_function_pool[39192]: DrawTexxvOES (will be remapped) */
   "glDrawTexxvOES\0"
   /* _mesa_function_pool[39207]: LoadPaletteFromModelViewMatrixOES (dynamic) */
   "glLoadPaletteFromModelViewMatrixOES\0"
   /* _mesa_function_pool[39243]: PointSizePointerOES (will be remapped) */
   "glPointSizePointerOES\0"
   /* _mesa_function_pool[39265]: QueryMatrixxOES (will be remapped) */
   "glQueryMatrixxOES\0"
   /* _mesa_function_pool[39283]: DiscardFramebufferEXT (will be remapped) */
   "glDiscardFramebufferEXT\0"
   /* _mesa_function_pool[39307]: FramebufferTexture2DMultisampleEXT (will be remapped) */
   "glFramebufferTexture2DMultisampleEXT\0"
   /* _mesa_function_pool[39344]: DepthRangeArrayfvOES (will be remapped) */
   "glDepthRangeArrayfvOES\0"
   /* _mesa_function_pool[39367]: DepthRangeIndexedfOES (will be remapped) */
   "glDepthRangeIndexedfOES\0"
   /* _mesa_function_pool[39391]: FramebufferParameteriMESA (will be remapped) */
   "glFramebufferParameteriMESA\0"
   /* _mesa_function_pool[39419]: GetFramebufferParameterivMESA (will be remapped) */
   "glGetFramebufferParameterivMESA\0"
   /* _mesa_function_pool[39451]: TexStorageAttribs2DEXT (will be remapped) */
   "glTexStorageAttribs2DEXT\0"
   /* _mesa_function_pool[39476]: TexStorageAttribs3DEXT (will be remapped) */
   "glTexStorageAttribs3DEXT\0"
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
   { 36154, BeginConditionalRender_remap_index },
   { 36013, BeginTransformFeedback_remap_index },
   { 36079, BindBufferBase_remap_index },
   { 36061, BindBufferRange_remap_index },
   { 35884, BindFragDataLocation_remap_index },
   {  9682, ClampColor_remap_index },
   { 35984, ClearBufferfi_remap_index },
   { 35968, ClearBufferfv_remap_index },
   { 35935, ClearBufferiv_remap_index },
   { 35951, ClearBufferuiv_remap_index },
   { 28959, ColorMaski_remap_index },
   { 29048, Disablei_remap_index },
   { 29029, Enablei_remap_index },
   { 36179, EndConditionalRender_remap_index },
   { 36038, EndTransformFeedback_remap_index },
   { 28981, GetBooleani_v_remap_index },
   { 35910, GetFragDataLocation_remap_index },
   { 29005, GetIntegeri_v_remap_index },
   { 36000, GetStringi_remap_index },
   { 35167, GetTexParameterIiv_remap_index },
   { 35191, GetTexParameterIuiv_remap_index },
   { 36124, GetTransformFeedbackVarying_remap_index },
   { 35865, GetUniformuiv_remap_index },
   { 35684, GetVertexAttribIiv_remap_index },
   { 35708, GetVertexAttribIuiv_remap_index },
   { 29068, IsEnabledi_remap_index },
   { 35124, TexParameterIiv_remap_index },
   { 35145, TexParameterIuiv_remap_index },
   { 36096, TransformFeedbackVaryings_remap_index },
   { 35733, Uniform1ui_remap_index },
   { 35797, Uniform1uiv_remap_index },
   { 35749, Uniform2ui_remap_index },
   { 35814, Uniform2uiv_remap_index },
   { 35765, Uniform3ui_remap_index },
   { 35831, Uniform3uiv_remap_index },
   { 35781, Uniform4ui_remap_index },
   { 35848, Uniform4uiv_remap_index },
   { 35388, VertexAttribI1iv_remap_index },
   { 35476, VertexAttribI1uiv_remap_index },
   { 35568, VertexAttribI4bv_remap_index },
   { 35590, VertexAttribI4sv_remap_index },
   { 35612, VertexAttribI4ubv_remap_index },
   { 35635, VertexAttribI4usv_remap_index },
   { 35658, VertexAttribIPointer_remap_index },
   { 36202, PrimitiveRestartIndex_remap_index },
   { 10415, TexBuffer_remap_index },
   { 36269, FramebufferTexture_remap_index },
   { 36244, GetBufferParameteri64v_remap_index },
   { 36226, GetInteger64i_v_remap_index },
   { 10312, VertexAttribDivisor_remap_index },
   { 11065, MinSampleShading_remap_index },
   { 38327, MemoryBarrierByRegion_remap_index },
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
   { 10390, FlushMappedBufferRange_remap_index },
   { 10373, MapBufferRange_remap_index },
   { 10430, BindVertexArray_remap_index },
   { 10448, DeleteVertexArrays_remap_index },
   { 10469, GenVertexArrays_remap_index },
   { 10487, IsVertexArray_remap_index },
   { 10617, GetActiveUniformBlockName_remap_index },
   { 10591, GetActiveUniformBlockiv_remap_index },
   { 10545, GetActiveUniformName_remap_index },
   { 10523, GetActiveUniformsiv_remap_index },
   { 10568, GetUniformBlockIndex_remap_index },
   { 10503, GetUniformIndices_remap_index },
   { 10645, UniformBlockBinding_remap_index },
   { 10667, CopyBufferSubData_remap_index },
   { 10840, ClientWaitSync_remap_index },
   { 10827, DeleteSync_remap_index },
   { 10806, FenceSync_remap_index },
   { 10868, GetInteger64v_remap_index },
   { 10884, GetSynciv_remap_index },
   { 10818, IsSync_remap_index },
   { 10857, WaitSync_remap_index },
   { 10687, DrawElementsBaseVertex_remap_index },
   { 10772, DrawElementsInstancedBaseVertex_remap_index },
   { 10712, DrawRangeElementsBaseVertex_remap_index },
   { 10742, MultiDrawElementsBaseVertex_remap_index },
   { 28938, ProvokingVertex_remap_index },
   { 10944, GetMultisamplefv_remap_index },
   { 10963, SampleMaski_remap_index },
   { 10896, TexImage2DMultisample_remap_index },
   { 10920, TexImage3DMultisample_remap_index },
   { 10997, BlendEquationSeparateiARB_remap_index },
   { 10977, BlendEquationiARB_remap_index },
   { 11041, BlendFuncSeparateiARB_remap_index },
   { 11025, BlendFunciARB_remap_index },
   { 11214, BindFragDataLocationIndexed_remap_index },
   { 11244, GetFragDataIndex_remap_index },
   { 11306, BindSampler_remap_index },
   { 11277, DeleteSamplers_remap_index },
   { 11263, GenSamplers_remap_index },
   { 11495, GetSamplerParameterIiv_remap_index },
   { 11520, GetSamplerParameterIuiv_remap_index },
   { 11471, GetSamplerParameterfv_remap_index },
   { 11447, GetSamplerParameteriv_remap_index },
   { 11294, IsSampler_remap_index },
   { 11402, SamplerParameterIiv_remap_index },
   { 11424, SamplerParameterIuiv_remap_index },
   { 11340, SamplerParameterf_remap_index },
   { 11381, SamplerParameterfv_remap_index },
   { 11320, SamplerParameteri_remap_index },
   { 11360, SamplerParameteriv_remap_index },
   { 11546, GetQueryObjecti64v_remap_index },
   { 11567, GetQueryObjectui64v_remap_index },
   { 11589, QueryCounter_remap_index },
   { 12000, ColorP3ui_remap_index },
   { 12024, ColorP3uiv_remap_index },
   { 12012, ColorP4ui_remap_index },
   { 12037, ColorP4uiv_remap_index },
   { 11809, MultiTexCoordP1ui_remap_index },
   { 11889, MultiTexCoordP1uiv_remap_index },
   { 11829, MultiTexCoordP2ui_remap_index },
   { 11910, MultiTexCoordP2uiv_remap_index },
   { 11849, MultiTexCoordP3ui_remap_index },
   { 11931, MultiTexCoordP3uiv_remap_index },
   { 11869, MultiTexCoordP4ui_remap_index },
   { 11952, MultiTexCoordP4uiv_remap_index },
   { 11973, NormalP3ui_remap_index },
   { 11986, NormalP3uiv_remap_index },
   { 12050, SecondaryColorP3ui_remap_index },
   { 12071, SecondaryColorP3uiv_remap_index },
   { 11685, TexCoordP1ui_remap_index },
   { 11745, TexCoordP1uiv_remap_index },
   { 11700, TexCoordP2ui_remap_index },
   { 11761, TexCoordP2uiv_remap_index },
   { 11715, TexCoordP3ui_remap_index },
   { 11777, TexCoordP3uiv_remap_index },
   { 11730, TexCoordP4ui_remap_index },
   { 11793, TexCoordP4uiv_remap_index },
   { 12093, VertexAttribP1ui_remap_index },
   { 12169, VertexAttribP1uiv_remap_index },
   { 12112, VertexAttribP2ui_remap_index },
   { 12189, VertexAttribP2uiv_remap_index },
   { 12131, VertexAttribP3ui_remap_index },
   { 12209, VertexAttribP3uiv_remap_index },
   { 12150, VertexAttribP4ui_remap_index },
   { 12229, VertexAttribP4uiv_remap_index },
   { 11604, VertexP2ui_remap_index },
   { 11643, VertexP2uiv_remap_index },
   { 11617, VertexP3ui_remap_index },
   { 11657, VertexP3uiv_remap_index },
   { 11630, VertexP4ui_remap_index },
   { 11671, VertexP4uiv_remap_index },
   { 12498, DrawArraysIndirect_remap_index },
   { 12519, DrawElementsIndirect_remap_index },
   { 12879, GetUniformdv_remap_index },
   { 12596, Uniform1d_remap_index },
   { 12644, Uniform1dv_remap_index },
   { 12608, Uniform2d_remap_index },
   { 12657, Uniform2dv_remap_index },
   { 12620, Uniform3d_remap_index },
   { 12670, Uniform3dv_remap_index },
   { 12632, Uniform4d_remap_index },
   { 12683, Uniform4dv_remap_index },
   { 12696, UniformMatrix2dv_remap_index },
   { 12753, UniformMatrix2x3dv_remap_index },
   { 12774, UniformMatrix2x4dv_remap_index },
   { 12715, UniformMatrix3dv_remap_index },
   { 12795, UniformMatrix3x2dv_remap_index },
   { 12816, UniformMatrix3x4dv_remap_index },
   { 12734, UniformMatrix4dv_remap_index },
   { 12837, UniformMatrix4x2dv_remap_index },
   { 12858, UniformMatrix4x3dv_remap_index },
   { 12365, GetActiveSubroutineName_remap_index },
   { 12332, GetActiveSubroutineUniformName_remap_index },
   { 12301, GetActiveSubroutineUniformiv_remap_index },
   { 12441, GetProgramStageiv_remap_index },
   { 12280, GetSubroutineIndex_remap_index },
   { 12249, GetSubroutineUniformLocation_remap_index },
   { 12415, GetUniformSubroutineuiv_remap_index },
   { 12391, UniformSubroutinesuiv_remap_index },
   { 12479, PatchParameterfv_remap_index },
   { 12461, PatchParameteri_remap_index },
   { 36333, BindTransformFeedback_remap_index },
   { 36357, DeleteTransformFeedbacks_remap_index },
   { 36481, DrawTransformFeedback_remap_index },
   { 36384, GenTransformFeedbacks_remap_index },
   { 36408, IsTransformFeedback_remap_index },
   { 36430, PauseTransformFeedback_remap_index },
   { 36455, ResumeTransformFeedback_remap_index },
   { 13377, BeginQueryIndexed_remap_index },
   { 13347, DrawTransformFeedbackStream_remap_index },
   { 13397, EndQueryIndexed_remap_index },
   { 13415, GetQueryIndexediv_remap_index },
   { 14681, ClearDepthf_remap_index },
   { 14695, DepthRangef_remap_index },
   { 14615, GetShaderPrecisionFormat_remap_index },
   { 14642, ReleaseShaderCompiler_remap_index },
   { 14666, ShaderBinary_remap_index },
   { 14709, GetProgramBinary_remap_index },
   { 14728, ProgramBinary_remap_index },
   { 14744, ProgramParameteri_remap_index },
   { 14558, GetVertexAttribLdv_remap_index },
   { 14387, VertexAttribL1d_remap_index },
   { 14459, VertexAttribL1dv_remap_index },
   { 14405, VertexAttribL2d_remap_index },
   { 14478, VertexAttribL2dv_remap_index },
   { 14423, VertexAttribL3d_remap_index },
   { 14497, VertexAttribL3dv_remap_index },
   { 14441, VertexAttribL4d_remap_index },
   { 14516, VertexAttribL4dv_remap_index },
   { 14535, VertexAttribLPointer_remap_index },
   { 22480, DepthRangeArrayv_remap_index },
   { 22499, DepthRangeIndexed_remap_index },
   { 22533, GetDoublei_v_remap_index },
   { 22519, GetFloati_v_remap_index },
   { 22429, ScissorArrayv_remap_index },
   { 22445, ScissorIndexed_remap_index },
   { 22462, ScissorIndexedv_remap_index },
   { 22373, ViewportArrayv_remap_index },
   { 22390, ViewportIndexedf_remap_index },
   { 22409, ViewportIndexedfv_remap_index },
   { 14863, GetGraphicsResetStatusARB_remap_index },
   { 15057, GetnColorTableARB_remap_index },
   { 15164, GetnCompressedTexImageARB_remap_index },
   { 15077, GetnConvolutionFilterARB_remap_index },
   { 15129, GetnHistogramARB_remap_index },
   { 14891, GetnMapdvARB_remap_index },
   { 14906, GetnMapfvARB_remap_index },
   { 14921, GetnMapivARB_remap_index },
   { 15148, GetnMinmaxARB_remap_index },
   { 14936, GetnPixelMapfvARB_remap_index },
   { 14956, GetnPixelMapuivARB_remap_index },
   { 14977, GetnPixelMapusvARB_remap_index },
   { 14998, GetnPolygonStippleARB_remap_index },
   { 15104, GetnSeparableFilterARB_remap_index },
   { 15022, GetnTexImageARB_remap_index },
   { 15250, GetnUniformdvARB_remap_index },
   { 15192, GetnUniformfvARB_remap_index },
   { 15211, GetnUniformivARB_remap_index },
   { 15230, GetnUniformuivARB_remap_index },
   { 15040, ReadnPixelsARB_remap_index },
   { 15269, DrawArraysInstancedBaseInstance_remap_index },
   { 15303, DrawElementsInstancedBaseInstance_remap_index },
   { 15339, DrawElementsInstancedBaseVertexBaseInstance_remap_index },
   { 15385, DrawTransformFeedbackInstanced_remap_index },
   { 15418, DrawTransformFeedbackStreamInstanced_remap_index },
   { 15457, GetInternalformativ_remap_index },
   { 15479, GetActiveAtomicCounterBufferiv_remap_index },
   { 15512, BindImageTexture_remap_index },
   { 15531, MemoryBarrier_remap_index },
   { 15547, TexStorage1D_remap_index },
   { 15562, TexStorage2D_remap_index },
   { 15577, TexStorage3D_remap_index },
   { 34267, TextureStorage1DEXT_remap_index },
   { 34289, TextureStorage2DEXT_remap_index },
   { 34311, TextureStorage3DEXT_remap_index },
   { 15693, ClearBufferData_remap_index },
   { 15711, ClearBufferSubData_remap_index },
   { 15787, DispatchCompute_remap_index },
   { 15805, DispatchComputeIndirect_remap_index },
   { 15831, CopyImageSubData_remap_index },
   { 15850, TextureView_remap_index },
   { 15864, BindVertexBuffer_remap_index },
   { 15948, VertexAttribBinding_remap_index },
   { 15883, VertexAttribFormat_remap_index },
   { 15904, VertexAttribIFormat_remap_index },
   { 15926, VertexAttribLFormat_remap_index },
   { 15970, VertexBindingDivisor_remap_index },
   { 16206, FramebufferParameteri_remap_index },
   { 16230, GetFramebufferParameteriv_remap_index },
   { 16326, GetInternalformati64v_remap_index },
   { 12542, MultiDrawArraysIndirect_remap_index },
   { 12568, MultiDrawElementsIndirect_remap_index },
   { 16495, GetProgramInterfaceiv_remap_index },
   { 16519, GetProgramResourceIndex_remap_index },
   { 16593, GetProgramResourceLocation_remap_index },
   { 16622, GetProgramResourceLocationIndex_remap_index },
   { 16545, GetProgramResourceName_remap_index },
   { 16570, GetProgramResourceiv_remap_index },
   { 16656, ShaderStorageBlockBinding_remap_index },
   { 16684, TexBufferRange_remap_index },
   { 16725, TexStorage2DMultisample_remap_index },
   { 16751, TexStorage3DMultisample_remap_index },
   { 16843, BufferStorage_remap_index },
   { 16883, ClearTexImage_remap_index },
   { 16899, ClearTexSubImage_remap_index },
   { 16918, BindBuffersBase_remap_index },
   { 16936, BindBuffersRange_remap_index },
   { 16985, BindImageTextures_remap_index },
   { 16970, BindSamplers_remap_index },
   { 16955, BindTextures_remap_index },
   { 17005, BindVertexBuffers_remap_index },
   { 17141, GetImageHandleARB_remap_index },
   { 17025, GetTextureHandleARB_remap_index },
   { 17047, GetTextureSamplerHandleARB_remap_index },
   { 17435, GetVertexAttribLui64vARB_remap_index },
   { 17359, IsImageHandleResidentARB_remap_index },
   { 17330, IsTextureHandleResidentARB_remap_index },
   { 17190, MakeImageHandleNonResidentARB_remap_index },
   { 17161, MakeImageHandleResidentARB_remap_index },
   { 17107, MakeTextureHandleNonResidentARB_remap_index },
   { 17076, MakeTextureHandleResidentARB_remap_index },
   { 17269, ProgramUniformHandleui64ARB_remap_index },
   { 17299, ProgramUniformHandleui64vARB_remap_index },
   { 17222, UniformHandleui64ARB_remap_index },
   { 17245, UniformHandleui64vARB_remap_index },
   { 17386, VertexAttribL1ui64ARB_remap_index },
   { 17410, VertexAttribL1ui64vARB_remap_index },
   { 17462, DispatchComputeGroupSizeARB_remap_index },
   { 17492, MultiDrawArraysIndirectCountARB_remap_index },
   { 17526, MultiDrawElementsIndirectCountARB_remap_index },
   { 17612, ClipControl_remap_index },
   { 19332, BindTextureUnit_remap_index },
   { 18538, BlitNamedFramebuffer_remap_index },
   { 18561, CheckNamedFramebufferStatus_remap_index },
   { 17894, ClearNamedBufferData_remap_index },
   { 17917, ClearNamedBufferSubData_remap_index },
   { 18512, ClearNamedFramebufferfi_remap_index },
   { 18486, ClearNamedFramebufferfv_remap_index },
   { 18433, ClearNamedFramebufferiv_remap_index },
   { 18459, ClearNamedFramebufferuiv_remap_index },
   { 19019, CompressedTextureSubImage1D_remap_index },
   { 19049, CompressedTextureSubImage2D_remap_index },
   { 19079, CompressedTextureSubImage3D_remap_index },
   { 17869, CopyNamedBufferSubData_remap_index },
   { 19109, CopyTextureSubImage1D_remap_index },
   { 19133, CopyTextureSubImage2D_remap_index },
   { 19157, CopyTextureSubImage3D_remap_index },
   { 17793, CreateBuffers_remap_index },
   { 18138, CreateFramebuffers_remap_index },
   { 19932, CreateProgramPipelines_remap_index },
   { 19957, CreateQueries_remap_index },
   { 18667, CreateRenderbuffers_remap_index },
   { 19915, CreateSamplers_remap_index },
   { 18788, CreateTextures_remap_index },
   { 17626, CreateTransformFeedbacks_remap_index },
   { 19553, CreateVertexArrays_remap_index },
   { 19574, DisableVertexArrayAttrib_remap_index },
   { 19601, EnableVertexArrayAttrib_remap_index },
   { 18001, FlushMappedNamedBufferRange_remap_index },
   { 19308, GenerateTextureMipmap_remap_index },
   { 19368, GetCompressedTextureImage_remap_index },
   { 18059, GetNamedBufferParameteri64v_remap_index },
   { 18031, GetNamedBufferParameteriv_remap_index },
   { 18089, GetNamedBufferPointerv_remap_index },
   { 18114, GetNamedBufferSubData_remap_index },
   { 18624, GetNamedFramebufferAttachmentParameteriv_remap_index },
   { 18591, GetNamedFramebufferParameteriv_remap_index },
   { 18754, GetNamedRenderbufferParameteriv_remap_index },
   { 20024, GetQueryBufferObjecti64v_remap_index },
   { 19973, GetQueryBufferObjectiv_remap_index },
   { 20051, GetQueryBufferObjectui64v_remap_index },
   { 19998, GetQueryBufferObjectuiv_remap_index },
   { 19350, GetTextureImage_remap_index },
   { 19396, GetTextureLevelParameterfv_remap_index },
   { 19425, GetTextureLevelParameteriv_remap_index },
   { 19478, GetTextureParameterIiv_remap_index },
   { 19503, GetTextureParameterIuiv_remap_index },
   { 19454, GetTextureParameterfv_remap_index },
   { 19529, GetTextureParameteriv_remap_index },
   { 17765, GetTransformFeedbacki64_v_remap_index },
   { 17739, GetTransformFeedbacki_v_remap_index },
   { 17714, GetTransformFeedbackiv_remap_index },
   { 19887, GetVertexArrayIndexed64iv_remap_index },
   { 19861, GetVertexArrayIndexediv_remap_index },
   { 19842, GetVertexArrayiv_remap_index },
   { 18364, InvalidateNamedFramebufferData_remap_index },
   { 18397, InvalidateNamedFramebufferSubData_remap_index },
   { 17943, MapNamedBuffer_remap_index },
   { 17960, MapNamedBufferRange_remap_index },
   { 17830, NamedBufferData_remap_index },
   { 17809, NamedBufferStorage_remap_index },
   { 17848, NamedBufferSubData_remap_index },
   { 18276, NamedFramebufferDrawBuffer_remap_index },
   { 18305, NamedFramebufferDrawBuffers_remap_index },
   { 18190, NamedFramebufferParameteri_remap_index },
   { 18335, NamedFramebufferReadBuffer_remap_index },
   { 18159, NamedFramebufferRenderbuffer_remap_index },
   { 18219, NamedFramebufferTexture_remap_index },
   { 18245, NamedFramebufferTextureLayer_remap_index },
   { 18689, NamedRenderbufferStorage_remap_index },
   { 18716, NamedRenderbufferStorageMultisample_remap_index },
   { 18805, TextureBuffer_remap_index },
   { 18821, TextureBufferRange_remap_index },
   { 19242, TextureParameterIiv_remap_index },
   { 19264, TextureParameterIuiv_remap_index },
   { 19181, TextureParameterf_remap_index },
   { 19201, TextureParameterfv_remap_index },
   { 19222, TextureParameteri_remap_index },
   { 19287, TextureParameteriv_remap_index },
   { 18842, TextureStorage1D_remap_index },
   { 18861, TextureStorage2D_remap_index },
   { 18899, TextureStorage2DMultisample_remap_index },
   { 18880, TextureStorage3D_remap_index },
   { 18929, TextureStorage3DMultisample_remap_index },
   { 18959, TextureSubImage1D_remap_index },
   { 18979, TextureSubImage2D_remap_index },
   { 18999, TextureSubImage3D_remap_index },
   { 17653, TransformFeedbackBufferBase_remap_index },
   { 17683, TransformFeedbackBufferRange_remap_index },
   { 17982, UnmapNamedBufferEXT_remap_index },
   { 19787, VertexArrayAttribBinding_remap_index },
   { 19707, VertexArrayAttribFormat_remap_index },
   { 19733, VertexArrayAttribIFormat_remap_index },
   { 19760, VertexArrayAttribLFormat_remap_index },
   { 19814, VertexArrayBindingDivisor_remap_index },
   { 19627, VertexArrayElementBuffer_remap_index },
   { 19654, VertexArrayVertexBuffer_remap_index },
   { 19680, VertexArrayVertexBuffers_remap_index },
   { 20100, GetCompressedTextureSubImage_remap_index },
   { 20079, GetTextureSubImage_remap_index },
   { 20148, BufferPageCommitmentARB_remap_index },
   { 20205, NamedBufferPageCommitmentARB_remap_index },
   { 20562, GetUniformi64vARB_remap_index },
   { 20582, GetUniformui64vARB_remap_index },
   { 20603, GetnUniformi64vARB_remap_index },
   { 20624, GetnUniformui64vARB_remap_index },
   { 20646, ProgramUniform1i64ARB_remap_index },
   { 20742, ProgramUniform1i64vARB_remap_index },
   { 20842, ProgramUniform1ui64ARB_remap_index },
   { 20942, ProgramUniform1ui64vARB_remap_index },
   { 20670, ProgramUniform2i64ARB_remap_index },
   { 20767, ProgramUniform2i64vARB_remap_index },
   { 20867, ProgramUniform2ui64ARB_remap_index },
   { 20968, ProgramUniform2ui64vARB_remap_index },
   { 20694, ProgramUniform3i64ARB_remap_index },
   { 20792, ProgramUniform3i64vARB_remap_index },
   { 20892, ProgramUniform3ui64ARB_remap_index },
   { 20994, ProgramUniform3ui64vARB_remap_index },
   { 20718, ProgramUniform4i64ARB_remap_index },
   { 20817, ProgramUniform4i64vARB_remap_index },
   { 20917, ProgramUniform4ui64ARB_remap_index },
   { 21020, ProgramUniform4ui64vARB_remap_index },
   { 20274, Uniform1i64ARB_remap_index },
   { 20342, Uniform1i64vARB_remap_index },
   { 20414, Uniform1ui64ARB_remap_index },
   { 20486, Uniform1ui64vARB_remap_index },
   { 20291, Uniform2i64ARB_remap_index },
   { 20360, Uniform2i64vARB_remap_index },
   { 20432, Uniform2ui64ARB_remap_index },
   { 20505, Uniform2ui64vARB_remap_index },
   { 20308, Uniform3i64ARB_remap_index },
   { 20378, Uniform3i64vARB_remap_index },
   { 20450, Uniform3ui64ARB_remap_index },
   { 20524, Uniform3ui64vARB_remap_index },
   { 20325, Uniform4i64ARB_remap_index },
   { 20396, Uniform4i64vARB_remap_index },
   { 20468, Uniform4ui64ARB_remap_index },
   { 20543, Uniform4ui64vARB_remap_index },
   { 25602, EvaluateDepthValuesARB_remap_index },
   { 25529, FramebufferSampleLocationsfvARB_remap_index },
   { 25563, NamedFramebufferSampleLocationsfvARB_remap_index },
   { 21076, SpecializeShaderARB_remap_index },
   { 16421, InvalidateBufferData_remap_index },
   { 16395, InvalidateBufferSubData_remap_index },
   { 16471, InvalidateFramebuffer_remap_index },
   { 16444, InvalidateSubFramebuffer_remap_index },
   { 16374, InvalidateTexImage_remap_index },
   { 16350, InvalidateTexSubImage_remap_index },
   { 39120, DrawTexfOES_remap_index },
   { 39134, DrawTexfvOES_remap_index },
   { 39091, DrawTexiOES_remap_index },
   { 39105, DrawTexivOES_remap_index },
   { 39149, DrawTexsOES_remap_index },
   { 39163, DrawTexsvOES_remap_index },
   { 39178, DrawTexxOES_remap_index },
   { 39192, DrawTexxvOES_remap_index },
   { 39243, PointSizePointerOES_remap_index },
   { 39265, QueryMatrixxOES_remap_index },
   { 21444, SampleMaskSGIS_remap_index },
   { 21461, SamplePatternSGIS_remap_index },
   { 21481, ColorPointerEXT_remap_index },
   { 21499, EdgeFlagPointerEXT_remap_index },
   { 21520, IndexPointerEXT_remap_index },
   { 21538, NormalPointerEXT_remap_index },
   { 21557, TexCoordPointerEXT_remap_index },
   { 21578, VertexPointerEXT_remap_index },
   { 39283, DiscardFramebufferEXT_remap_index },
   { 13454, ActiveShaderProgram_remap_index },
   { 13499, BindProgramPipeline_remap_index },
   { 13476, CreateShaderProgramv_remap_index },
   { 13521, DeleteProgramPipelines_remap_index },
   { 13546, GenProgramPipelines_remap_index },
   { 14359, GetProgramPipelineInfoLog_remap_index },
   { 13588, GetProgramPipelineiv_remap_index },
   { 13568, IsProgramPipeline_remap_index },
   { 22297, LockArraysEXT_remap_index },
   { 12894, ProgramUniform1d_remap_index },
   { 12982, ProgramUniform1dv_remap_index },
   { 13767, ProgramUniform1f_remap_index },
   { 14007, ProgramUniform1fv_remap_index },
   { 13611, ProgramUniform1i_remap_index },
   { 13843, ProgramUniform1iv_remap_index },
   { 13687, ProgramUniform1ui_remap_index },
   { 13923, ProgramUniform1uiv_remap_index },
   { 12916, ProgramUniform2d_remap_index },
   { 13005, ProgramUniform2dv_remap_index },
   { 13786, ProgramUniform2f_remap_index },
   { 14027, ProgramUniform2fv_remap_index },
   { 13630, ProgramUniform2i_remap_index },
   { 13863, ProgramUniform2iv_remap_index },
   { 13707, ProgramUniform2ui_remap_index },
   { 13944, ProgramUniform2uiv_remap_index },
   { 12938, ProgramUniform3d_remap_index },
   { 13028, ProgramUniform3dv_remap_index },
   { 13805, ProgramUniform3f_remap_index },
   { 14047, ProgramUniform3fv_remap_index },
   { 13649, ProgramUniform3i_remap_index },
   { 13883, ProgramUniform3iv_remap_index },
   { 13727, ProgramUniform3ui_remap_index },
   { 13965, ProgramUniform3uiv_remap_index },
   { 12960, ProgramUniform4d_remap_index },
   { 13051, ProgramUniform4dv_remap_index },
   { 13824, ProgramUniform4f_remap_index },
   { 14067, ProgramUniform4fv_remap_index },
   { 13668, ProgramUniform4i_remap_index },
   { 13903, ProgramUniform4iv_remap_index },
   { 13747, ProgramUniform4ui_remap_index },
   { 13986, ProgramUniform4uiv_remap_index },
   { 13074, ProgramUniformMatrix2dv_remap_index },
   { 14087, ProgramUniformMatrix2fv_remap_index },
   { 13161, ProgramUniformMatrix2x3dv_remap_index },
   { 14165, ProgramUniformMatrix2x3fv_remap_index },
   { 13192, ProgramUniformMatrix2x4dv_remap_index },
   { 14221, ProgramUniformMatrix2x4fv_remap_index },
   { 13103, ProgramUniformMatrix3dv_remap_index },
   { 14113, ProgramUniformMatrix3fv_remap_index },
   { 13223, ProgramUniformMatrix3x2dv_remap_index },
   { 14193, ProgramUniformMatrix3x2fv_remap_index },
   { 13254, ProgramUniformMatrix3x4dv_remap_index },
   { 14277, ProgramUniformMatrix3x4fv_remap_index },
   { 13132, ProgramUniformMatrix4dv_remap_index },
   { 14139, ProgramUniformMatrix4fv_remap_index },
   { 13285, ProgramUniformMatrix4x2dv_remap_index },
   { 14249, ProgramUniformMatrix4x2fv_remap_index },
   { 13316, ProgramUniformMatrix4x3dv_remap_index },
   { 14305, ProgramUniformMatrix4x3fv_remap_index },
   { 22313, UnlockArraysEXT_remap_index },
   { 13435, UseProgramStages_remap_index },
   { 14333, ValidateProgramPipeline_remap_index },
   { 39307, FramebufferTexture2DMultisampleEXT_remap_index },
   { 14813, DebugMessageCallback_remap_index },
   { 14764, DebugMessageControl_remap_index },
   { 14789, DebugMessageInsert_remap_index },
   { 14839, GetDebugMessageLog_remap_index },
   { 15639, GetObjectLabel_remap_index },
   { 15673, GetObjectPtrLabel_remap_index },
   { 15625, ObjectLabel_remap_index },
   { 15656, ObjectPtrLabel_remap_index },
   { 15609, PopDebugGroup_remap_index },
   { 15592, PushDebugGroup_remap_index },
   {  6228, SecondaryColor3fEXT_remap_index },
   {  6247, SecondaryColor3fvEXT_remap_index },
   {  6053, MultiDrawElements_remap_index },
   {  5967, FogCoordfEXT_remap_index },
   {  5979, FogCoordfvEXT_remap_index },
   { 26096, ResizeBuffersMESA_remap_index },
   { 26116, WindowPos4dMESA_remap_index },
   { 26134, WindowPos4dvMESA_remap_index },
   { 26153, WindowPos4fMESA_remap_index },
   { 26171, WindowPos4fvMESA_remap_index },
   { 26190, WindowPos4iMESA_remap_index },
   { 26208, WindowPos4ivMESA_remap_index },
   { 26227, WindowPos4sMESA_remap_index },
   { 26245, WindowPos4svMESA_remap_index },
   { 26264, MultiModeDrawArraysIBM_remap_index },
   { 26289, MultiModeDrawElementsIBM_remap_index },
   { 26911, AreProgramsResidentNV_remap_index },
   { 26935, ExecuteProgramNV_remap_index },
   { 26954, GetProgramParameterdvNV_remap_index },
   { 26980, GetProgramParameterfvNV_remap_index },
   { 27023, GetProgramStringNV_remap_index },
   { 27006, GetProgramivNV_remap_index },
   { 27044, GetTrackMatrixivNV_remap_index },
   { 27065, GetVertexAttribdvNV_remap_index },
   { 27087, GetVertexAttribfvNV_remap_index },
   { 27109, GetVertexAttribivNV_remap_index },
   { 27131, LoadProgramNV_remap_index },
   { 27147, ProgramParameters4dvNV_remap_index },
   { 27172, ProgramParameters4fvNV_remap_index },
   { 27197, RequestResidentProgramsNV_remap_index },
   { 27225, TrackMatrixNV_remap_index },
   { 27577, VertexAttrib1dNV_remap_index },
   { 27596, VertexAttrib1dvNV_remap_index },
   { 27421, VertexAttrib1fNV_remap_index },
   { 27440, VertexAttrib1fvNV_remap_index },
   { 27265, VertexAttrib1sNV_remap_index },
   { 27284, VertexAttrib1svNV_remap_index },
   { 27616, VertexAttrib2dNV_remap_index },
   { 27635, VertexAttrib2dvNV_remap_index },
   { 27460, VertexAttrib2fNV_remap_index },
   { 27479, VertexAttrib2fvNV_remap_index },
   { 27304, VertexAttrib2sNV_remap_index },
   { 27323, VertexAttrib2svNV_remap_index },
   { 27655, VertexAttrib3dNV_remap_index },
   { 27674, VertexAttrib3dvNV_remap_index },
   { 27499, VertexAttrib3fNV_remap_index },
   { 27518, VertexAttrib3fvNV_remap_index },
   { 27343, VertexAttrib3sNV_remap_index },
   { 27362, VertexAttrib3svNV_remap_index },
   { 27694, VertexAttrib4dNV_remap_index },
   { 27713, VertexAttrib4dvNV_remap_index },
   { 27538, VertexAttrib4fNV_remap_index },
   { 27557, VertexAttrib4fvNV_remap_index },
   { 27382, VertexAttrib4sNV_remap_index },
   { 27401, VertexAttrib4svNV_remap_index },
   { 27733, VertexAttrib4ubNV_remap_index },
   { 27753, VertexAttrib4ubvNV_remap_index },
   { 27241, VertexAttribPointerNV_remap_index },
   { 27942, VertexAttribs1dvNV_remap_index },
   { 27858, VertexAttribs1fvNV_remap_index },
   { 27774, VertexAttribs1svNV_remap_index },
   { 27963, VertexAttribs2dvNV_remap_index },
   { 27879, VertexAttribs2fvNV_remap_index },
   { 27795, VertexAttribs2svNV_remap_index },
   { 27984, VertexAttribs3dvNV_remap_index },
   { 27900, VertexAttribs3fvNV_remap_index },
   { 27816, VertexAttribs3svNV_remap_index },
   { 28005, VertexAttribs4dvNV_remap_index },
   { 27921, VertexAttribs4fvNV_remap_index },
   { 27837, VertexAttribs4svNV_remap_index },
   { 28026, VertexAttribs4ubvNV_remap_index },
   { 28096, GetTexBumpParameterfvATI_remap_index },
   { 28123, GetTexBumpParameterivATI_remap_index },
   { 28048, TexBumpParameterfvATI_remap_index },
   { 28072, TexBumpParameterivATI_remap_index },
   { 28371, AlphaFragmentOp1ATI_remap_index },
   { 28393, AlphaFragmentOp2ATI_remap_index },
   { 28415, AlphaFragmentOp3ATI_remap_index },
   { 28224, BeginFragmentShaderATI_remap_index },
   { 28174, BindFragmentShaderATI_remap_index },
   { 28305, ColorFragmentOp1ATI_remap_index },
   { 28327, ColorFragmentOp2ATI_remap_index },
   { 28349, ColorFragmentOp3ATI_remap_index },
   { 28198, DeleteFragmentShaderATI_remap_index },
   { 28249, EndFragmentShaderATI_remap_index },
   { 28150, GenFragmentShadersATI_remap_index },
   { 28272, PassTexCoordATI_remap_index },
   { 28290, SampleMapATI_remap_index },
   { 28437, SetFragmentShaderConstantATI_remap_index },
   { 39344, DepthRangeArrayfvOES_remap_index },
   { 39367, DepthRangeIndexedfOES_remap_index },
   { 28488, ActiveStencilFaceEXT_remap_index },
   { 28825, GetProgramNamedParameterdvNV_remap_index },
   { 28794, GetProgramNamedParameterfvNV_remap_index },
   { 28708, ProgramNamedParameter4dNV_remap_index },
   { 28765, ProgramNamedParameter4dvNV_remap_index },
   { 28680, ProgramNamedParameter4fNV_remap_index },
   { 28736, ProgramNamedParameter4fvNV_remap_index },
   { 36290, PrimitiveRestartNV_remap_index },
   { 38986, GetTexGenxvOES_remap_index },
   { 39003, TexGenxOES_remap_index },
   { 39016, TexGenxvOES_remap_index },
   { 28856, DepthBoundsEXT_remap_index },
   { 28895, BindFramebufferEXT_remap_index },
   { 28873, BindRenderbufferEXT_remap_index },
   { 28916, StringMarkerGREMEDY_remap_index },
   { 29090, BufferParameteriAPPLE_remap_index },
   { 29114, FlushMappedBufferRangeAPPLE_remap_index },
   { 35216, VertexAttribI1iEXT_remap_index },
   { 35300, VertexAttribI1uiEXT_remap_index },
   { 35237, VertexAttribI2iEXT_remap_index },
   { 35410, VertexAttribI2ivEXT_remap_index },
   { 35322, VertexAttribI2uiEXT_remap_index },
   { 35499, VertexAttribI2uivEXT_remap_index },
   { 35258, VertexAttribI3iEXT_remap_index },
   { 35432, VertexAttribI3ivEXT_remap_index },
   { 35344, VertexAttribI3uiEXT_remap_index },
   { 35522, VertexAttribI3uivEXT_remap_index },
   { 35279, VertexAttribI4iEXT_remap_index },
   { 35454, VertexAttribI4ivEXT_remap_index },
   { 35366, VertexAttribI4uiEXT_remap_index },
   { 35545, VertexAttribI4uivEXT_remap_index },
   { 35087, ClearColorIiEXT_remap_index },
   { 35105, ClearColorIuiEXT_remap_index },
   { 36311, BindBufferOffsetEXT_remap_index },
   { 29369, BeginPerfMonitorAMD_remap_index },
   { 29314, DeletePerfMonitorsAMD_remap_index },
   { 29391, EndPerfMonitorAMD_remap_index },
   { 29293, GenPerfMonitorsAMD_remap_index },
   { 29411, GetPerfMonitorCounterDataAMD_remap_index },
   { 29262, GetPerfMonitorCounterInfoAMD_remap_index },
   { 29229, GetPerfMonitorCounterStringAMD_remap_index },
   { 29170, GetPerfMonitorCountersAMD_remap_index },
   { 29198, GetPerfMonitorGroupStringAMD_remap_index },
   { 29144, GetPerfMonitorGroupsAMD_remap_index },
   { 29338, SelectPerfMonitorCountersAMD_remap_index },
   { 28559, GetObjectParameterivAPPLE_remap_index },
   { 28511, ObjectPurgeableAPPLE_remap_index },
   { 28534, ObjectUnpurgeableAPPLE_remap_index },
   { 29515, ActiveProgramEXT_remap_index },
   { 29534, CreateShaderProgramEXT_remap_index },
   { 29493, UseShaderProgramEXT_remap_index },
   { 20131, TextureBarrierNV_remap_index },
   { 36519, VDPAUFiniNV_remap_index },
   { 36640, VDPAUGetSurfaceivNV_remap_index },
   { 36505, VDPAUInitNV_remap_index },
   { 36594, VDPAUIsSurfaceNV_remap_index },
   { 36685, VDPAUMapSurfacesNV_remap_index },
   { 36563, VDPAURegisterOutputSurfaceNV_remap_index },
   { 36533, VDPAURegisterVideoSurfaceNV_remap_index },
   { 36662, VDPAUSurfaceAccessNV_remap_index },
   { 36706, VDPAUUnmapSurfacesNV_remap_index },
   { 36613, VDPAUUnregisterSurfaceNV_remap_index },
   { 34093, BeginPerfQueryINTEL_remap_index },
   { 34047, CreatePerfQueryINTEL_remap_index },
   { 34070, DeletePerfQueryINTEL_remap_index },
   { 34115, EndPerfQueryINTEL_remap_index },
   { 33916, GetFirstPerfQueryIdINTEL_remap_index },
   { 33943, GetNextPerfQueryIdINTEL_remap_index },
   { 34021, GetPerfCounterInfoINTEL_remap_index },
   { 34135, GetPerfQueryDataINTEL_remap_index },
   { 33969, GetPerfQueryIdByNameINTEL_remap_index },
   { 33997, GetPerfQueryInfoINTEL_remap_index },
   { 34192, PolygonOffsetClampEXT_remap_index },
   { 33824, SubpixelPrecisionBiasNV_remap_index },
   { 33850, ConservativeRasterParameterfNV_remap_index },
   { 33883, ConservativeRasterParameteriNV_remap_index },
   { 34216, WindowRectanglesEXT_remap_index },
   { 37012, BufferStorageMemEXT_remap_index },
   { 36820, CreateMemoryObjectsEXT_remap_index },
   { 36775, DeleteMemoryObjectsEXT_remap_index },
   { 37248, DeleteSemaphoresEXT_remap_index },
   { 37229, GenSemaphoresEXT_remap_index },
   { 36874, GetMemoryObjectParameterivEXT_remap_index },
   { 37316, GetSemaphoreParameterui64vEXT_remap_index },
   { 36751, GetUnsignedBytei_vEXT_remap_index },
   { 36729, GetUnsignedBytevEXT_remap_index },
   { 36800, IsMemoryObjectEXT_remap_index },
   { 37270, IsSemaphoreEXT_remap_index },
   { 36845, MemoryObjectParameterivEXT_remap_index },
   { 37156, NamedBufferStorageMemEXT_remap_index },
   { 37287, SemaphoreParameterui64vEXT_remap_index },
   { 37367, SignalSemaphoreEXT_remap_index },
   { 37183, TexStorageMem1DEXT_remap_index },
   { 36906, TexStorageMem2DEXT_remap_index },
   { 36927, TexStorageMem2DMultisampleEXT_remap_index },
   { 36959, TexStorageMem3DEXT_remap_index },
   { 36980, TexStorageMem3DMultisampleEXT_remap_index },
   { 37204, TextureStorageMem1DEXT_remap_index },
   { 37034, TextureStorageMem2DEXT_remap_index },
   { 37059, TextureStorageMem2DMultisampleEXT_remap_index },
   { 37095, TextureStorageMem3DEXT_remap_index },
   { 37120, TextureStorageMem3DMultisampleEXT_remap_index },
   { 37348, WaitSemaphoreEXT_remap_index },
   { 37388, ImportMemoryFdEXT_remap_index },
   { 37408, ImportSemaphoreFdEXT_remap_index },
   { 34238, FramebufferFetchBarrierEXT_remap_index },
   { 34377, NamedRenderbufferStorageMultisampleAdvancedAMD_remap_index },
   { 34333, RenderbufferStorageMultisampleAdvancedAMD_remap_index },
   { 34426, StencilFuncSeparateATI_remap_index },
   { 34451, ProgramEnvParameters4fvEXT_remap_index },
   { 34480, ProgramLocalParameters4fvEXT_remap_index },
   { 34984, EGLImageTargetRenderbufferStorageOES_remap_index },
   { 34955, EGLImageTargetTexture2DOES_remap_index },
   { 38351, AlphaFuncx_remap_index },
   { 38367, ClearColorx_remap_index },
   { 38384, ClearDepthx_remap_index },
   { 38401, Color4x_remap_index },
   { 38414, DepthRangex_remap_index },
   { 38431, Fogx_remap_index },
   { 38441, Fogxv_remap_index },
   { 39065, Frustumf_remap_index },
   { 38452, Frustumx_remap_index },
   { 38466, LightModelx_remap_index },
   { 38483, LightModelxv_remap_index },
   { 38501, Lightx_remap_index },
   { 38513, Lightxv_remap_index },
   { 38526, LineWidthx_remap_index },
   { 38542, LoadMatrixx_remap_index },
   { 38559, Materialx_remap_index },
   { 38574, Materialxv_remap_index },
   { 38590, MultMatrixx_remap_index },
   { 38607, MultiTexCoord4x_remap_index },
   { 38628, Normal3x_remap_index },
   { 39079, Orthof_remap_index },
   { 38642, Orthox_remap_index },
   { 38654, PointSizex_remap_index },
   { 38670, PolygonOffsetx_remap_index },
   { 38690, Rotatex_remap_index },
   { 38703, SampleCoveragex_remap_index },
   { 38724, Scalex_remap_index },
   { 38736, TexEnvx_remap_index },
   { 38749, TexEnvxv_remap_index },
   { 38763, TexParameterx_remap_index },
   { 38782, Translatex_remap_index },
   { 39030, ClipPlanef_remap_index },
   { 38798, ClipPlanex_remap_index },
   { 39046, GetClipPlanef_remap_index },
   { 38814, GetClipPlanex_remap_index },
   { 38833, GetFixedv_remap_index },
   { 38848, GetLightxv_remap_index },
   { 38864, GetMaterialxv_remap_index },
   { 38883, GetTexEnvxv_remap_index },
   { 38900, GetTexParameterxv_remap_index },
   { 38923, PointParameterx_remap_index },
   { 38944, PointParameterxv_remap_index },
   { 38966, TexParameterxv_remap_index },
   { 20259, BlendBarrier_remap_index },
   { 20236, PrimitiveBoundingBox_remap_index },
   { 21046, MaxShaderCompilerThreadsKHR_remap_index },
   { 29580, MatrixLoadfEXT_remap_index },
   { 29597, MatrixLoaddEXT_remap_index },
   { 29614, MatrixMultfEXT_remap_index },
   { 29631, MatrixMultdEXT_remap_index },
   { 29648, MatrixLoadIdentityEXT_remap_index },
   { 29672, MatrixRotatefEXT_remap_index },
   { 29691, MatrixRotatedEXT_remap_index },
   { 29710, MatrixScalefEXT_remap_index },
   { 29728, MatrixScaledEXT_remap_index },
   { 29746, MatrixTranslatefEXT_remap_index },
   { 29768, MatrixTranslatedEXT_remap_index },
   { 29790, MatrixOrthoEXT_remap_index },
   { 29807, MatrixFrustumEXT_remap_index },
   { 29826, MatrixPushEXT_remap_index },
   { 29842, MatrixPopEXT_remap_index },
   { 31341, MatrixLoadTransposefEXT_remap_index },
   { 31367, MatrixLoadTransposedEXT_remap_index },
   { 31393, MatrixMultTransposefEXT_remap_index },
   { 31419, MatrixMultTransposedEXT_remap_index },
   { 30402, BindMultiTextureEXT_remap_index },
   { 31892, NamedBufferDataEXT_remap_index },
   { 31913, NamedBufferSubDataEXT_remap_index },
   { 16859, NamedBufferStorageEXT_remap_index },
   { 32076, MapNamedBufferRangeEXT_remap_index },
   { 30123, TextureImage1DEXT_remap_index },
   { 30143, TextureImage2DEXT_remap_index },
   { 30163, TextureImage3DEXT_remap_index },
   { 30183, TextureSubImage1DEXT_remap_index },
   { 30206, TextureSubImage2DEXT_remap_index },
   { 30229, TextureSubImage3DEXT_remap_index },
   { 30252, CopyTextureImage1DEXT_remap_index },
   { 30276, CopyTextureImage2DEXT_remap_index },
   { 30300, CopyTextureSubImage1DEXT_remap_index },
   { 30327, CopyTextureSubImage2DEXT_remap_index },
   { 30354, CopyTextureSubImage3DEXT_remap_index },
   { 31937, MapNamedBufferEXT_remap_index },
   { 29911, GetTextureParameterivEXT_remap_index },
   { 29938, GetTextureParameterfvEXT_remap_index },
   { 30029, TextureParameteriEXT_remap_index },
   { 30052, TextureParameterivEXT_remap_index },
   { 30076, TextureParameterfEXT_remap_index },
   { 30099, TextureParameterfvEXT_remap_index },
   { 30381, GetTextureImageEXT_remap_index },
   { 29965, GetTextureLevelParameterivEXT_remap_index },
   { 29997, GetTextureLevelParameterfvEXT_remap_index },
   { 31957, GetNamedBufferSubDataEXT_remap_index },
   { 31984, GetNamedBufferPointervEXT_remap_index },
   { 32012, GetNamedBufferParameterivEXT_remap_index },
   { 32043, FlushMappedNamedBufferRangeEXT_remap_index },
   { 32101, FramebufferDrawBufferEXT_remap_index },
   { 32128, FramebufferDrawBuffersEXT_remap_index },
   { 32156, FramebufferReadBufferEXT_remap_index },
   { 32183, GetFramebufferParameterivEXT_remap_index },
   { 32214, CheckNamedFramebufferStatusEXT_remap_index },
   { 32247, NamedFramebufferTexture1DEXT_remap_index },
   { 32278, NamedFramebufferTexture2DEXT_remap_index },
   { 32309, NamedFramebufferTexture3DEXT_remap_index },
   { 32340, NamedFramebufferRenderbufferEXT_remap_index },
   { 32374, GetNamedFramebufferAttachmentParameterivEXT_remap_index },
   { 30424, EnableClientStateiEXT_remap_index },
   { 30454, DisableClientStateiEXT_remap_index },
   { 30485, GetPointerIndexedvEXT_remap_index },
   { 30509, MultiTexEnviEXT_remap_index },
   { 30527, MultiTexEnvivEXT_remap_index },
   { 30546, MultiTexEnvfEXT_remap_index },
   { 30564, MultiTexEnvfvEXT_remap_index },
   { 30583, GetMultiTexEnvivEXT_remap_index },
   { 30605, GetMultiTexEnvfvEXT_remap_index },
   { 30627, MultiTexParameteriEXT_remap_index },
   { 30651, MultiTexParameterivEXT_remap_index },
   { 30676, MultiTexParameterfEXT_remap_index },
   { 30700, MultiTexParameterfvEXT_remap_index },
   { 30781, GetMultiTexImageEXT_remap_index },
   { 30869, MultiTexImage1DEXT_remap_index },
   { 30890, MultiTexImage2DEXT_remap_index },
   { 30911, MultiTexImage3DEXT_remap_index },
   { 30932, MultiTexSubImage1DEXT_remap_index },
   { 30956, MultiTexSubImage2DEXT_remap_index },
   { 30980, MultiTexSubImage3DEXT_remap_index },
   { 30725, GetMultiTexParameterivEXT_remap_index },
   { 30753, GetMultiTexParameterfvEXT_remap_index },
   { 31004, CopyMultiTexImage1DEXT_remap_index },
   { 31029, CopyMultiTexImage2DEXT_remap_index },
   { 31054, CopyMultiTexSubImage1DEXT_remap_index },
   { 31082, CopyMultiTexSubImage2DEXT_remap_index },
   { 31110, CopyMultiTexSubImage3DEXT_remap_index },
   { 31138, MultiTexGendEXT_remap_index },
   { 31156, MultiTexGendvEXT_remap_index },
   { 31175, MultiTexGenfEXT_remap_index },
   { 31193, MultiTexGenfvEXT_remap_index },
   { 31212, MultiTexGeniEXT_remap_index },
   { 31230, MultiTexGenivEXT_remap_index },
   { 31249, GetMultiTexGendvEXT_remap_index },
   { 31271, GetMultiTexGenfvEXT_remap_index },
   { 31293, GetMultiTexGenivEXT_remap_index },
   { 31315, MultiTexCoordPointerEXT_remap_index },
   { 33765, BindImageTextureEXT_remap_index },
   { 31445, CompressedTextureImage1DEXT_remap_index },
   { 31475, CompressedTextureImage2DEXT_remap_index },
   { 31505, CompressedTextureImage3DEXT_remap_index },
   { 31535, CompressedTextureSubImage1DEXT_remap_index },
   { 31568, CompressedTextureSubImage2DEXT_remap_index },
   { 31601, CompressedTextureSubImage3DEXT_remap_index },
   { 31634, GetCompressedTextureImageEXT_remap_index },
   { 31665, CompressedMultiTexImage1DEXT_remap_index },
   { 31696, CompressedMultiTexImage2DEXT_remap_index },
   { 31727, CompressedMultiTexImage3DEXT_remap_index },
   { 31758, CompressedMultiTexSubImage1DEXT_remap_index },
   { 31792, CompressedMultiTexSubImage2DEXT_remap_index },
   { 31826, CompressedMultiTexSubImage3DEXT_remap_index },
   { 31860, GetCompressedMultiTexImageEXT_remap_index },
   { 30803, GetMultiTexLevelParameterivEXT_remap_index },
   { 30836, GetMultiTexLevelParameterfvEXT_remap_index },
   { 39391, FramebufferParameteriMESA_remap_index },
   { 39419, GetFramebufferParameterivMESA_remap_index },
   { 32420, NamedRenderbufferStorageEXT_remap_index },
   { 32450, GetNamedRenderbufferParameterivEXT_remap_index },
   { 29857, ClientAttribDefaultEXT_remap_index },
   { 29882, PushClientAttribDefaultEXT_remap_index },
   { 33184, NamedProgramStringEXT_remap_index },
   { 33208, GetNamedProgramStringEXT_remap_index },
   { 33235, NamedProgramLocalParameter4fEXT_remap_index },
   { 33269, NamedProgramLocalParameter4fvEXT_remap_index },
   { 33304, GetNamedProgramLocalParameterfvEXT_remap_index },
   { 33341, NamedProgramLocalParameter4dEXT_remap_index },
   { 33375, NamedProgramLocalParameter4dvEXT_remap_index },
   { 33410, GetNamedProgramLocalParameterdvEXT_remap_index },
   { 33447, GetNamedProgramivEXT_remap_index },
   { 33470, TextureBufferEXT_remap_index },
   { 33489, MultiTexBufferEXT_remap_index },
   { 33509, TextureParameterIivEXT_remap_index },
   { 33534, TextureParameterIuivEXT_remap_index },
   { 33560, GetTextureParameterIivEXT_remap_index },
   { 33588, GetTextureParameterIuivEXT_remap_index },
   { 33617, MultiTexParameterIivEXT_remap_index },
   { 33643, MultiTexParameterIuivEXT_remap_index },
   { 33670, GetMultiTexParameterIivEXT_remap_index },
   { 33699, GetMultiTexParameterIuivEXT_remap_index },
   { 33729, NamedProgramLocalParameters4fvEXT_remap_index },
   { 32487, GenerateTextureMipmapEXT_remap_index },
   { 32514, GenerateMultiTexMipmapEXT_remap_index },
   { 32542, NamedRenderbufferStorageMultisampleEXT_remap_index },
   { 32583, NamedCopyBufferSubDataEXT_remap_index },
   { 32611, VertexArrayVertexOffsetEXT_remap_index },
   { 32640, VertexArrayColorOffsetEXT_remap_index },
   { 32668, VertexArrayEdgeFlagOffsetEXT_remap_index },
   { 32699, VertexArrayIndexOffsetEXT_remap_index },
   { 32727, VertexArrayNormalOffsetEXT_remap_index },
   { 32756, VertexArrayTexCoordOffsetEXT_remap_index },
   { 32787, VertexArrayMultiTexCoordOffsetEXT_remap_index },
   { 32823, VertexArrayFogCoordOffsetEXT_remap_index },
   { 32854, VertexArraySecondaryColorOffsetEXT_remap_index },
   { 32891, VertexArrayVertexAttribOffsetEXT_remap_index },
   { 32926, VertexArrayVertexAttribIOffsetEXT_remap_index },
   { 32962, EnableVertexArrayEXT_remap_index },
   { 32985, DisableVertexArrayEXT_remap_index },
   { 33009, EnableVertexArrayAttribEXT_remap_index },
   { 33038, DisableVertexArrayAttribEXT_remap_index },
   { 33068, GetVertexArrayIntegervEXT_remap_index },
   { 33096, GetVertexArrayPointervEXT_remap_index },
   { 33124, GetVertexArrayIntegeri_vEXT_remap_index },
   { 33154, GetVertexArrayPointeri_vEXT_remap_index },
   { 15732, ClearNamedBufferDataEXT_remap_index },
   { 15758, ClearNamedBufferSubDataEXT_remap_index },
   { 16258, NamedFramebufferParameteriEXT_remap_index },
   { 16290, GetNamedFramebufferParameterivEXT_remap_index },
   { 14579, VertexArrayVertexAttribLOffsetEXT_remap_index },
   { 10337, VertexArrayVertexAttribDivisorEXT_remap_index },
   { 16701, TextureBufferRangeEXT_remap_index },
   { 16777, TextureStorage2DMultisampleEXT_remap_index },
   { 16810, TextureStorage3DMultisampleEXT_remap_index },
   { 15993, VertexArrayBindVertexBufferEXT_remap_index },
   { 16026, VertexArrayVertexAttribFormatEXT_remap_index },
   { 16061, VertexArrayVertexAttribIFormatEXT_remap_index },
   { 16097, VertexArrayVertexAttribLFormatEXT_remap_index },
   { 16133, VertexArrayVertexAttribBindingEXT_remap_index },
   { 16169, VertexArrayVertexBindingDivisorEXT_remap_index },
   { 20174, NamedBufferPageCommitmentEXT_remap_index },
   { 11087, NamedStringARB_remap_index },
   { 11104, DeleteNamedStringARB_remap_index },
   { 11127, CompileShaderIncludeARB_remap_index },
   { 11153, IsNamedStringARB_remap_index },
   { 11172, GetNamedStringARB_remap_index },
   { 11192, GetNamedStringivARB_remap_index },
   { 35023, EGLImageTargetTexStorageEXT_remap_index },
   { 35053, EGLImageTargetTextureStorageEXT_remap_index },
   { 29559, CopyImageSubDataNV_remap_index },
   { 37549, ViewportSwizzleNV_remap_index },
   { 34159, AlphaToCoverageDitherControlNV_remap_index },
   { 34624, InternalBufferSubDataCopyMESA_remap_index },
   { 37569, Vertex2hNV_remap_index },
   { 37582, Vertex2hvNV_remap_index },
   { 37596, Vertex3hNV_remap_index },
   { 37609, Vertex3hvNV_remap_index },
   { 37623, Vertex4hNV_remap_index },
   { 37636, Vertex4hvNV_remap_index },
   { 37650, Normal3hNV_remap_index },
   { 37663, Normal3hvNV_remap_index },
   { 37677, Color3hNV_remap_index },
   { 37689, Color3hvNV_remap_index },
   { 37702, Color4hNV_remap_index },
   { 37714, Color4hvNV_remap_index },
   { 37727, TexCoord1hNV_remap_index },
   { 37742, TexCoord1hvNV_remap_index },
   { 37758, TexCoord2hNV_remap_index },
   { 37773, TexCoord2hvNV_remap_index },
   { 37789, TexCoord3hNV_remap_index },
   { 37804, TexCoord3hvNV_remap_index },
   { 37820, TexCoord4hNV_remap_index },
   { 37835, TexCoord4hvNV_remap_index },
   { 37851, MultiTexCoord1hNV_remap_index },
   { 37871, MultiTexCoord1hvNV_remap_index },
   { 37892, MultiTexCoord2hNV_remap_index },
   { 37912, MultiTexCoord2hvNV_remap_index },
   { 37933, MultiTexCoord3hNV_remap_index },
   { 37953, MultiTexCoord3hvNV_remap_index },
   { 37974, MultiTexCoord4hNV_remap_index },
   { 37994, MultiTexCoord4hvNV_remap_index },
   { 38255, FogCoordhNV_remap_index },
   { 38269, FogCoordhvNV_remap_index },
   { 38284, SecondaryColor3hNV_remap_index },
   { 38305, SecondaryColor3hvNV_remap_index },
   { 34656, InternalSetError_remap_index },
   { 38015, VertexAttrib1hNV_remap_index },
   { 38034, VertexAttrib1hvNV_remap_index },
   { 38054, VertexAttrib2hNV_remap_index },
   { 38073, VertexAttrib2hvNV_remap_index },
   { 38093, VertexAttrib3hNV_remap_index },
   { 38112, VertexAttrib3hvNV_remap_index },
   { 38132, VertexAttrib4hNV_remap_index },
   { 38151, VertexAttrib4hvNV_remap_index },
   { 38171, VertexAttribs1hvNV_remap_index },
   { 38192, VertexAttribs2hvNV_remap_index },
   { 38213, VertexAttribs3hvNV_remap_index },
   { 38234, VertexAttribs4hvNV_remap_index },
   { 17562, TexPageCommitmentARB_remap_index },
   { 17585, TexturePageCommitmentEXT_remap_index },
   { 37431, ImportMemoryWin32HandleEXT_remap_index },
   { 37487, ImportSemaphoreWin32HandleEXT_remap_index },
   { 37460, ImportMemoryWin32NameEXT_remap_index },
   { 37519, ImportSemaphoreWin32NameEXT_remap_index },
   { 33804, GetObjectLabelEXT_remap_index },
   { 33787, LabelObjectEXT_remap_index },
   { 34675, DrawArraysUserBuf_remap_index },
   { 34695, DrawElementsUserBuf_remap_index },
   { 34745, MultiDrawArraysUserBuf_remap_index },
   { 34770, MultiDrawElementsUserBuf_remap_index },
   { 34797, DrawArraysInstancedBaseInstanceDrawID_remap_index },
   { 34837, DrawElementsInstancedBaseVertexBaseInstanceDrawID_remap_index },
   { 34910, InternalInvalidateFramebufferAncillaryMESA_remap_index },
   { 34889, DrawElementsPacked_remap_index },
   { 34717, DrawElementsUserBufPacked_remap_index },
   { 39451, TexStorageAttribs2DEXT_remap_index },
   { 39476, TexStorageAttribs3DEXT_remap_index },
   { 10197, FramebufferTextureMultiviewOVR_remap_index },
   { 10230, NamedFramebufferTextureMultiviewOVR_remap_index },
   { 10268, FramebufferTextureMultisampleMultiviewOVR_remap_index },
   {    -1, -1 }
};

