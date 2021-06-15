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
   "ii\0"
   "glNewList\0"
   "\0"
   /* _mesa_function_pool[14]: EndList (offset 1) */
   "\0"
   "glEndList\0"
   "\0"
   /* _mesa_function_pool[26]: CallList (offset 2) */
   "i\0"
   "glCallList\0"
   "\0"
   /* _mesa_function_pool[40]: CallLists (offset 3) */
   "iip\0"
   "glCallLists\0"
   "\0"
   /* _mesa_function_pool[57]: DeleteLists (offset 4) */
   "ii\0"
   "glDeleteLists\0"
   "\0"
   /* _mesa_function_pool[75]: GenLists (offset 5) */
   "i\0"
   "glGenLists\0"
   "\0"
   /* _mesa_function_pool[89]: ListBase (offset 6) */
   "i\0"
   "glListBase\0"
   "\0"
   /* _mesa_function_pool[103]: Begin (offset 7) */
   "i\0"
   "glBegin\0"
   "\0"
   /* _mesa_function_pool[114]: Bitmap (offset 8) */
   "iiffffp\0"
   "glBitmap\0"
   "\0"
   /* _mesa_function_pool[132]: Color3b (offset 9) */
   "iii\0"
   "glColor3b\0"
   "\0"
   /* _mesa_function_pool[147]: Color3bv (offset 10) */
   "p\0"
   "glColor3bv\0"
   "\0"
   /* _mesa_function_pool[161]: Color3d (offset 11) */
   "ddd\0"
   "glColor3d\0"
   "\0"
   /* _mesa_function_pool[176]: Color3dv (offset 12) */
   "p\0"
   "glColor3dv\0"
   "\0"
   /* _mesa_function_pool[190]: Color3f (offset 13) */
   "fff\0"
   "glColor3f\0"
   "\0"
   /* _mesa_function_pool[205]: Color3fv (offset 14) */
   "p\0"
   "glColor3fv\0"
   "\0"
   /* _mesa_function_pool[219]: Color3i (offset 15) */
   "iii\0"
   "glColor3i\0"
   "\0"
   /* _mesa_function_pool[234]: Color3iv (offset 16) */
   "p\0"
   "glColor3iv\0"
   "\0"
   /* _mesa_function_pool[248]: Color3s (offset 17) */
   "iii\0"
   "glColor3s\0"
   "\0"
   /* _mesa_function_pool[263]: Color3sv (offset 18) */
   "p\0"
   "glColor3sv\0"
   "\0"
   /* _mesa_function_pool[277]: Color3ub (offset 19) */
   "iii\0"
   "glColor3ub\0"
   "\0"
   /* _mesa_function_pool[293]: Color3ubv (offset 20) */
   "p\0"
   "glColor3ubv\0"
   "\0"
   /* _mesa_function_pool[308]: Color3ui (offset 21) */
   "iii\0"
   "glColor3ui\0"
   "\0"
   /* _mesa_function_pool[324]: Color3uiv (offset 22) */
   "p\0"
   "glColor3uiv\0"
   "\0"
   /* _mesa_function_pool[339]: Color3us (offset 23) */
   "iii\0"
   "glColor3us\0"
   "\0"
   /* _mesa_function_pool[355]: Color3usv (offset 24) */
   "p\0"
   "glColor3usv\0"
   "\0"
   /* _mesa_function_pool[370]: Color4b (offset 25) */
   "iiii\0"
   "glColor4b\0"
   "\0"
   /* _mesa_function_pool[386]: Color4bv (offset 26) */
   "p\0"
   "glColor4bv\0"
   "\0"
   /* _mesa_function_pool[400]: Color4d (offset 27) */
   "dddd\0"
   "glColor4d\0"
   "\0"
   /* _mesa_function_pool[416]: Color4dv (offset 28) */
   "p\0"
   "glColor4dv\0"
   "\0"
   /* _mesa_function_pool[430]: Color4f (offset 29) */
   "ffff\0"
   "glColor4f\0"
   "\0"
   /* _mesa_function_pool[446]: Color4fv (offset 30) */
   "p\0"
   "glColor4fv\0"
   "\0"
   /* _mesa_function_pool[460]: Color4i (offset 31) */
   "iiii\0"
   "glColor4i\0"
   "\0"
   /* _mesa_function_pool[476]: Color4iv (offset 32) */
   "p\0"
   "glColor4iv\0"
   "\0"
   /* _mesa_function_pool[490]: Color4s (offset 33) */
   "iiii\0"
   "glColor4s\0"
   "\0"
   /* _mesa_function_pool[506]: Color4sv (offset 34) */
   "p\0"
   "glColor4sv\0"
   "\0"
   /* _mesa_function_pool[520]: Color4ub (offset 35) */
   "iiii\0"
   "glColor4ub\0"
   "\0"
   /* _mesa_function_pool[537]: Color4ubv (offset 36) */
   "p\0"
   "glColor4ubv\0"
   "\0"
   /* _mesa_function_pool[552]: Color4ui (offset 37) */
   "iiii\0"
   "glColor4ui\0"
   "\0"
   /* _mesa_function_pool[569]: Color4uiv (offset 38) */
   "p\0"
   "glColor4uiv\0"
   "\0"
   /* _mesa_function_pool[584]: Color4us (offset 39) */
   "iiii\0"
   "glColor4us\0"
   "\0"
   /* _mesa_function_pool[601]: Color4usv (offset 40) */
   "p\0"
   "glColor4usv\0"
   "\0"
   /* _mesa_function_pool[616]: EdgeFlag (offset 41) */
   "i\0"
   "glEdgeFlag\0"
   "\0"
   /* _mesa_function_pool[630]: EdgeFlagv (offset 42) */
   "p\0"
   "glEdgeFlagv\0"
   "\0"
   /* _mesa_function_pool[645]: End (offset 43) */
   "\0"
   "glEnd\0"
   "\0"
   /* _mesa_function_pool[653]: Indexd (offset 44) */
   "d\0"
   "glIndexd\0"
   "\0"
   /* _mesa_function_pool[665]: Indexdv (offset 45) */
   "p\0"
   "glIndexdv\0"
   "\0"
   /* _mesa_function_pool[678]: Indexf (offset 46) */
   "f\0"
   "glIndexf\0"
   "\0"
   /* _mesa_function_pool[690]: Indexfv (offset 47) */
   "p\0"
   "glIndexfv\0"
   "\0"
   /* _mesa_function_pool[703]: Indexi (offset 48) */
   "i\0"
   "glIndexi\0"
   "\0"
   /* _mesa_function_pool[715]: Indexiv (offset 49) */
   "p\0"
   "glIndexiv\0"
   "\0"
   /* _mesa_function_pool[728]: Indexs (offset 50) */
   "i\0"
   "glIndexs\0"
   "\0"
   /* _mesa_function_pool[740]: Indexsv (offset 51) */
   "p\0"
   "glIndexsv\0"
   "\0"
   /* _mesa_function_pool[753]: Normal3b (offset 52) */
   "iii\0"
   "glNormal3b\0"
   "\0"
   /* _mesa_function_pool[769]: Normal3bv (offset 53) */
   "p\0"
   "glNormal3bv\0"
   "\0"
   /* _mesa_function_pool[784]: Normal3d (offset 54) */
   "ddd\0"
   "glNormal3d\0"
   "\0"
   /* _mesa_function_pool[800]: Normal3dv (offset 55) */
   "p\0"
   "glNormal3dv\0"
   "\0"
   /* _mesa_function_pool[815]: Normal3f (offset 56) */
   "fff\0"
   "glNormal3f\0"
   "\0"
   /* _mesa_function_pool[831]: Normal3fv (offset 57) */
   "p\0"
   "glNormal3fv\0"
   "\0"
   /* _mesa_function_pool[846]: Normal3i (offset 58) */
   "iii\0"
   "glNormal3i\0"
   "\0"
   /* _mesa_function_pool[862]: Normal3iv (offset 59) */
   "p\0"
   "glNormal3iv\0"
   "\0"
   /* _mesa_function_pool[877]: Normal3s (offset 60) */
   "iii\0"
   "glNormal3s\0"
   "\0"
   /* _mesa_function_pool[893]: Normal3sv (offset 61) */
   "p\0"
   "glNormal3sv\0"
   "\0"
   /* _mesa_function_pool[908]: RasterPos2d (offset 62) */
   "dd\0"
   "glRasterPos2d\0"
   "\0"
   /* _mesa_function_pool[926]: RasterPos2dv (offset 63) */
   "p\0"
   "glRasterPos2dv\0"
   "\0"
   /* _mesa_function_pool[944]: RasterPos2f (offset 64) */
   "ff\0"
   "glRasterPos2f\0"
   "\0"
   /* _mesa_function_pool[962]: RasterPos2fv (offset 65) */
   "p\0"
   "glRasterPos2fv\0"
   "\0"
   /* _mesa_function_pool[980]: RasterPos2i (offset 66) */
   "ii\0"
   "glRasterPos2i\0"
   "\0"
   /* _mesa_function_pool[998]: RasterPos2iv (offset 67) */
   "p\0"
   "glRasterPos2iv\0"
   "\0"
   /* _mesa_function_pool[1016]: RasterPos2s (offset 68) */
   "ii\0"
   "glRasterPos2s\0"
   "\0"
   /* _mesa_function_pool[1034]: RasterPos2sv (offset 69) */
   "p\0"
   "glRasterPos2sv\0"
   "\0"
   /* _mesa_function_pool[1052]: RasterPos3d (offset 70) */
   "ddd\0"
   "glRasterPos3d\0"
   "\0"
   /* _mesa_function_pool[1071]: RasterPos3dv (offset 71) */
   "p\0"
   "glRasterPos3dv\0"
   "\0"
   /* _mesa_function_pool[1089]: RasterPos3f (offset 72) */
   "fff\0"
   "glRasterPos3f\0"
   "\0"
   /* _mesa_function_pool[1108]: RasterPos3fv (offset 73) */
   "p\0"
   "glRasterPos3fv\0"
   "\0"
   /* _mesa_function_pool[1126]: RasterPos3i (offset 74) */
   "iii\0"
   "glRasterPos3i\0"
   "\0"
   /* _mesa_function_pool[1145]: RasterPos3iv (offset 75) */
   "p\0"
   "glRasterPos3iv\0"
   "\0"
   /* _mesa_function_pool[1163]: RasterPos3s (offset 76) */
   "iii\0"
   "glRasterPos3s\0"
   "\0"
   /* _mesa_function_pool[1182]: RasterPos3sv (offset 77) */
   "p\0"
   "glRasterPos3sv\0"
   "\0"
   /* _mesa_function_pool[1200]: RasterPos4d (offset 78) */
   "dddd\0"
   "glRasterPos4d\0"
   "\0"
   /* _mesa_function_pool[1220]: RasterPos4dv (offset 79) */
   "p\0"
   "glRasterPos4dv\0"
   "\0"
   /* _mesa_function_pool[1238]: RasterPos4f (offset 80) */
   "ffff\0"
   "glRasterPos4f\0"
   "\0"
   /* _mesa_function_pool[1258]: RasterPos4fv (offset 81) */
   "p\0"
   "glRasterPos4fv\0"
   "\0"
   /* _mesa_function_pool[1276]: RasterPos4i (offset 82) */
   "iiii\0"
   "glRasterPos4i\0"
   "\0"
   /* _mesa_function_pool[1296]: RasterPos4iv (offset 83) */
   "p\0"
   "glRasterPos4iv\0"
   "\0"
   /* _mesa_function_pool[1314]: RasterPos4s (offset 84) */
   "iiii\0"
   "glRasterPos4s\0"
   "\0"
   /* _mesa_function_pool[1334]: RasterPos4sv (offset 85) */
   "p\0"
   "glRasterPos4sv\0"
   "\0"
   /* _mesa_function_pool[1352]: Rectd (offset 86) */
   "dddd\0"
   "glRectd\0"
   "\0"
   /* _mesa_function_pool[1366]: Rectdv (offset 87) */
   "pp\0"
   "glRectdv\0"
   "\0"
   /* _mesa_function_pool[1379]: Rectf (offset 88) */
   "ffff\0"
   "glRectf\0"
   "\0"
   /* _mesa_function_pool[1393]: Rectfv (offset 89) */
   "pp\0"
   "glRectfv\0"
   "\0"
   /* _mesa_function_pool[1406]: Recti (offset 90) */
   "iiii\0"
   "glRecti\0"
   "\0"
   /* _mesa_function_pool[1420]: Rectiv (offset 91) */
   "pp\0"
   "glRectiv\0"
   "\0"
   /* _mesa_function_pool[1433]: Rects (offset 92) */
   "iiii\0"
   "glRects\0"
   "\0"
   /* _mesa_function_pool[1447]: Rectsv (offset 93) */
   "pp\0"
   "glRectsv\0"
   "\0"
   /* _mesa_function_pool[1460]: TexCoord1d (offset 94) */
   "d\0"
   "glTexCoord1d\0"
   "\0"
   /* _mesa_function_pool[1476]: TexCoord1dv (offset 95) */
   "p\0"
   "glTexCoord1dv\0"
   "\0"
   /* _mesa_function_pool[1493]: TexCoord1f (offset 96) */
   "f\0"
   "glTexCoord1f\0"
   "\0"
   /* _mesa_function_pool[1509]: TexCoord1fv (offset 97) */
   "p\0"
   "glTexCoord1fv\0"
   "\0"
   /* _mesa_function_pool[1526]: TexCoord1i (offset 98) */
   "i\0"
   "glTexCoord1i\0"
   "\0"
   /* _mesa_function_pool[1542]: TexCoord1iv (offset 99) */
   "p\0"
   "glTexCoord1iv\0"
   "\0"
   /* _mesa_function_pool[1559]: TexCoord1s (offset 100) */
   "i\0"
   "glTexCoord1s\0"
   "\0"
   /* _mesa_function_pool[1575]: TexCoord1sv (offset 101) */
   "p\0"
   "glTexCoord1sv\0"
   "\0"
   /* _mesa_function_pool[1592]: TexCoord2d (offset 102) */
   "dd\0"
   "glTexCoord2d\0"
   "\0"
   /* _mesa_function_pool[1609]: TexCoord2dv (offset 103) */
   "p\0"
   "glTexCoord2dv\0"
   "\0"
   /* _mesa_function_pool[1626]: TexCoord2f (offset 104) */
   "ff\0"
   "glTexCoord2f\0"
   "\0"
   /* _mesa_function_pool[1643]: TexCoord2fv (offset 105) */
   "p\0"
   "glTexCoord2fv\0"
   "\0"
   /* _mesa_function_pool[1660]: TexCoord2i (offset 106) */
   "ii\0"
   "glTexCoord2i\0"
   "\0"
   /* _mesa_function_pool[1677]: TexCoord2iv (offset 107) */
   "p\0"
   "glTexCoord2iv\0"
   "\0"
   /* _mesa_function_pool[1694]: TexCoord2s (offset 108) */
   "ii\0"
   "glTexCoord2s\0"
   "\0"
   /* _mesa_function_pool[1711]: TexCoord2sv (offset 109) */
   "p\0"
   "glTexCoord2sv\0"
   "\0"
   /* _mesa_function_pool[1728]: TexCoord3d (offset 110) */
   "ddd\0"
   "glTexCoord3d\0"
   "\0"
   /* _mesa_function_pool[1746]: TexCoord3dv (offset 111) */
   "p\0"
   "glTexCoord3dv\0"
   "\0"
   /* _mesa_function_pool[1763]: TexCoord3f (offset 112) */
   "fff\0"
   "glTexCoord3f\0"
   "\0"
   /* _mesa_function_pool[1781]: TexCoord3fv (offset 113) */
   "p\0"
   "glTexCoord3fv\0"
   "\0"
   /* _mesa_function_pool[1798]: TexCoord3i (offset 114) */
   "iii\0"
   "glTexCoord3i\0"
   "\0"
   /* _mesa_function_pool[1816]: TexCoord3iv (offset 115) */
   "p\0"
   "glTexCoord3iv\0"
   "\0"
   /* _mesa_function_pool[1833]: TexCoord3s (offset 116) */
   "iii\0"
   "glTexCoord3s\0"
   "\0"
   /* _mesa_function_pool[1851]: TexCoord3sv (offset 117) */
   "p\0"
   "glTexCoord3sv\0"
   "\0"
   /* _mesa_function_pool[1868]: TexCoord4d (offset 118) */
   "dddd\0"
   "glTexCoord4d\0"
   "\0"
   /* _mesa_function_pool[1887]: TexCoord4dv (offset 119) */
   "p\0"
   "glTexCoord4dv\0"
   "\0"
   /* _mesa_function_pool[1904]: TexCoord4f (offset 120) */
   "ffff\0"
   "glTexCoord4f\0"
   "\0"
   /* _mesa_function_pool[1923]: TexCoord4fv (offset 121) */
   "p\0"
   "glTexCoord4fv\0"
   "\0"
   /* _mesa_function_pool[1940]: TexCoord4i (offset 122) */
   "iiii\0"
   "glTexCoord4i\0"
   "\0"
   /* _mesa_function_pool[1959]: TexCoord4iv (offset 123) */
   "p\0"
   "glTexCoord4iv\0"
   "\0"
   /* _mesa_function_pool[1976]: TexCoord4s (offset 124) */
   "iiii\0"
   "glTexCoord4s\0"
   "\0"
   /* _mesa_function_pool[1995]: TexCoord4sv (offset 125) */
   "p\0"
   "glTexCoord4sv\0"
   "\0"
   /* _mesa_function_pool[2012]: Vertex2d (offset 126) */
   "dd\0"
   "glVertex2d\0"
   "\0"
   /* _mesa_function_pool[2027]: Vertex2dv (offset 127) */
   "p\0"
   "glVertex2dv\0"
   "\0"
   /* _mesa_function_pool[2042]: Vertex2f (offset 128) */
   "ff\0"
   "glVertex2f\0"
   "\0"
   /* _mesa_function_pool[2057]: Vertex2fv (offset 129) */
   "p\0"
   "glVertex2fv\0"
   "\0"
   /* _mesa_function_pool[2072]: Vertex2i (offset 130) */
   "ii\0"
   "glVertex2i\0"
   "\0"
   /* _mesa_function_pool[2087]: Vertex2iv (offset 131) */
   "p\0"
   "glVertex2iv\0"
   "\0"
   /* _mesa_function_pool[2102]: Vertex2s (offset 132) */
   "ii\0"
   "glVertex2s\0"
   "\0"
   /* _mesa_function_pool[2117]: Vertex2sv (offset 133) */
   "p\0"
   "glVertex2sv\0"
   "\0"
   /* _mesa_function_pool[2132]: Vertex3d (offset 134) */
   "ddd\0"
   "glVertex3d\0"
   "\0"
   /* _mesa_function_pool[2148]: Vertex3dv (offset 135) */
   "p\0"
   "glVertex3dv\0"
   "\0"
   /* _mesa_function_pool[2163]: Vertex3f (offset 136) */
   "fff\0"
   "glVertex3f\0"
   "\0"
   /* _mesa_function_pool[2179]: Vertex3fv (offset 137) */
   "p\0"
   "glVertex3fv\0"
   "\0"
   /* _mesa_function_pool[2194]: Vertex3i (offset 138) */
   "iii\0"
   "glVertex3i\0"
   "\0"
   /* _mesa_function_pool[2210]: Vertex3iv (offset 139) */
   "p\0"
   "glVertex3iv\0"
   "\0"
   /* _mesa_function_pool[2225]: Vertex3s (offset 140) */
   "iii\0"
   "glVertex3s\0"
   "\0"
   /* _mesa_function_pool[2241]: Vertex3sv (offset 141) */
   "p\0"
   "glVertex3sv\0"
   "\0"
   /* _mesa_function_pool[2256]: Vertex4d (offset 142) */
   "dddd\0"
   "glVertex4d\0"
   "\0"
   /* _mesa_function_pool[2273]: Vertex4dv (offset 143) */
   "p\0"
   "glVertex4dv\0"
   "\0"
   /* _mesa_function_pool[2288]: Vertex4f (offset 144) */
   "ffff\0"
   "glVertex4f\0"
   "\0"
   /* _mesa_function_pool[2305]: Vertex4fv (offset 145) */
   "p\0"
   "glVertex4fv\0"
   "\0"
   /* _mesa_function_pool[2320]: Vertex4i (offset 146) */
   "iiii\0"
   "glVertex4i\0"
   "\0"
   /* _mesa_function_pool[2337]: Vertex4iv (offset 147) */
   "p\0"
   "glVertex4iv\0"
   "\0"
   /* _mesa_function_pool[2352]: Vertex4s (offset 148) */
   "iiii\0"
   "glVertex4s\0"
   "\0"
   /* _mesa_function_pool[2369]: Vertex4sv (offset 149) */
   "p\0"
   "glVertex4sv\0"
   "\0"
   /* _mesa_function_pool[2384]: ClipPlane (offset 150) */
   "ip\0"
   "glClipPlane\0"
   "\0"
   /* _mesa_function_pool[2400]: ColorMaterial (offset 151) */
   "ii\0"
   "glColorMaterial\0"
   "\0"
   /* _mesa_function_pool[2420]: CullFace (offset 152) */
   "i\0"
   "glCullFace\0"
   "\0"
   /* _mesa_function_pool[2434]: Fogf (offset 153) */
   "if\0"
   "glFogf\0"
   "\0"
   /* _mesa_function_pool[2445]: Fogfv (offset 154) */
   "ip\0"
   "glFogfv\0"
   "\0"
   /* _mesa_function_pool[2457]: Fogi (offset 155) */
   "ii\0"
   "glFogi\0"
   "\0"
   /* _mesa_function_pool[2468]: Fogiv (offset 156) */
   "ip\0"
   "glFogiv\0"
   "\0"
   /* _mesa_function_pool[2480]: FrontFace (offset 157) */
   "i\0"
   "glFrontFace\0"
   "\0"
   /* _mesa_function_pool[2495]: Hint (offset 158) */
   "ii\0"
   "glHint\0"
   "\0"
   /* _mesa_function_pool[2506]: Lightf (offset 159) */
   "iif\0"
   "glLightf\0"
   "\0"
   /* _mesa_function_pool[2520]: Lightfv (offset 160) */
   "iip\0"
   "glLightfv\0"
   "\0"
   /* _mesa_function_pool[2535]: Lighti (offset 161) */
   "iii\0"
   "glLighti\0"
   "\0"
   /* _mesa_function_pool[2549]: Lightiv (offset 162) */
   "iip\0"
   "glLightiv\0"
   "\0"
   /* _mesa_function_pool[2564]: LightModelf (offset 163) */
   "if\0"
   "glLightModelf\0"
   "\0"
   /* _mesa_function_pool[2582]: LightModelfv (offset 164) */
   "ip\0"
   "glLightModelfv\0"
   "\0"
   /* _mesa_function_pool[2601]: LightModeli (offset 165) */
   "ii\0"
   "glLightModeli\0"
   "\0"
   /* _mesa_function_pool[2619]: LightModeliv (offset 166) */
   "ip\0"
   "glLightModeliv\0"
   "\0"
   /* _mesa_function_pool[2638]: LineStipple (offset 167) */
   "ii\0"
   "glLineStipple\0"
   "\0"
   /* _mesa_function_pool[2656]: LineWidth (offset 168) */
   "f\0"
   "glLineWidth\0"
   "\0"
   /* _mesa_function_pool[2671]: Materialf (offset 169) */
   "iif\0"
   "glMaterialf\0"
   "\0"
   /* _mesa_function_pool[2688]: Materialfv (offset 170) */
   "iip\0"
   "glMaterialfv\0"
   "\0"
   /* _mesa_function_pool[2706]: Materiali (offset 171) */
   "iii\0"
   "glMateriali\0"
   "\0"
   /* _mesa_function_pool[2723]: Materialiv (offset 172) */
   "iip\0"
   "glMaterialiv\0"
   "\0"
   /* _mesa_function_pool[2741]: PointSize (offset 173) */
   "f\0"
   "glPointSize\0"
   "\0"
   /* _mesa_function_pool[2756]: PolygonMode (offset 174) */
   "ii\0"
   "glPolygonMode\0"
   "\0"
   /* _mesa_function_pool[2774]: PolygonStipple (offset 175) */
   "p\0"
   "glPolygonStipple\0"
   "\0"
   /* _mesa_function_pool[2794]: Scissor (offset 176) */
   "iiii\0"
   "glScissor\0"
   "\0"
   /* _mesa_function_pool[2810]: ShadeModel (offset 177) */
   "i\0"
   "glShadeModel\0"
   "\0"
   /* _mesa_function_pool[2826]: TexParameterf (offset 178) */
   "iif\0"
   "glTexParameterf\0"
   "\0"
   /* _mesa_function_pool[2847]: TexParameterfv (offset 179) */
   "iip\0"
   "glTexParameterfv\0"
   "\0"
   /* _mesa_function_pool[2869]: TexParameteri (offset 180) */
   "iii\0"
   "glTexParameteri\0"
   "\0"
   /* _mesa_function_pool[2890]: TexParameteriv (offset 181) */
   "iip\0"
   "glTexParameteriv\0"
   "\0"
   /* _mesa_function_pool[2912]: TexImage1D (offset 182) */
   "iiiiiiip\0"
   "glTexImage1D\0"
   "\0"
   /* _mesa_function_pool[2935]: TexImage2D (offset 183) */
   "iiiiiiiip\0"
   "glTexImage2D\0"
   "\0"
   /* _mesa_function_pool[2959]: TexEnvf (offset 184) */
   "iif\0"
   "glTexEnvf\0"
   "\0"
   /* _mesa_function_pool[2974]: TexEnvfv (offset 185) */
   "iip\0"
   "glTexEnvfv\0"
   "\0"
   /* _mesa_function_pool[2990]: TexEnvi (offset 186) */
   "iii\0"
   "glTexEnvi\0"
   "\0"
   /* _mesa_function_pool[3005]: TexEnviv (offset 187) */
   "iip\0"
   "glTexEnviv\0"
   "\0"
   /* _mesa_function_pool[3021]: TexGend (offset 188) */
   "iid\0"
   "glTexGend\0"
   "\0"
   /* _mesa_function_pool[3036]: TexGendv (offset 189) */
   "iip\0"
   "glTexGendv\0"
   "\0"
   /* _mesa_function_pool[3052]: TexGenf (offset 190) */
   "iif\0"
   "glTexGenf\0"
   "glTexGenfOES\0"
   "\0"
   /* _mesa_function_pool[3080]: TexGenfv (offset 191) */
   "iip\0"
   "glTexGenfv\0"
   "glTexGenfvOES\0"
   "\0"
   /* _mesa_function_pool[3110]: TexGeni (offset 192) */
   "iii\0"
   "glTexGeni\0"
   "glTexGeniOES\0"
   "\0"
   /* _mesa_function_pool[3138]: TexGeniv (offset 193) */
   "iip\0"
   "glTexGeniv\0"
   "glTexGenivOES\0"
   "\0"
   /* _mesa_function_pool[3168]: FeedbackBuffer (offset 194) */
   "iip\0"
   "glFeedbackBuffer\0"
   "\0"
   /* _mesa_function_pool[3190]: SelectBuffer (offset 195) */
   "ip\0"
   "glSelectBuffer\0"
   "\0"
   /* _mesa_function_pool[3209]: RenderMode (offset 196) */
   "i\0"
   "glRenderMode\0"
   "\0"
   /* _mesa_function_pool[3225]: InitNames (offset 197) */
   "\0"
   "glInitNames\0"
   "\0"
   /* _mesa_function_pool[3239]: LoadName (offset 198) */
   "i\0"
   "glLoadName\0"
   "\0"
   /* _mesa_function_pool[3253]: PassThrough (offset 199) */
   "f\0"
   "glPassThrough\0"
   "\0"
   /* _mesa_function_pool[3270]: PopName (offset 200) */
   "\0"
   "glPopName\0"
   "\0"
   /* _mesa_function_pool[3282]: PushName (offset 201) */
   "i\0"
   "glPushName\0"
   "\0"
   /* _mesa_function_pool[3296]: DrawBuffer (offset 202) */
   "i\0"
   "glDrawBuffer\0"
   "\0"
   /* _mesa_function_pool[3312]: Clear (offset 203) */
   "i\0"
   "glClear\0"
   "\0"
   /* _mesa_function_pool[3323]: ClearAccum (offset 204) */
   "ffff\0"
   "glClearAccum\0"
   "\0"
   /* _mesa_function_pool[3342]: ClearIndex (offset 205) */
   "f\0"
   "glClearIndex\0"
   "\0"
   /* _mesa_function_pool[3358]: ClearColor (offset 206) */
   "ffff\0"
   "glClearColor\0"
   "\0"
   /* _mesa_function_pool[3377]: ClearStencil (offset 207) */
   "i\0"
   "glClearStencil\0"
   "\0"
   /* _mesa_function_pool[3395]: ClearDepth (offset 208) */
   "d\0"
   "glClearDepth\0"
   "\0"
   /* _mesa_function_pool[3411]: StencilMask (offset 209) */
   "i\0"
   "glStencilMask\0"
   "\0"
   /* _mesa_function_pool[3428]: ColorMask (offset 210) */
   "iiii\0"
   "glColorMask\0"
   "\0"
   /* _mesa_function_pool[3446]: DepthMask (offset 211) */
   "i\0"
   "glDepthMask\0"
   "\0"
   /* _mesa_function_pool[3461]: IndexMask (offset 212) */
   "i\0"
   "glIndexMask\0"
   "\0"
   /* _mesa_function_pool[3476]: Accum (offset 213) */
   "if\0"
   "glAccum\0"
   "\0"
   /* _mesa_function_pool[3488]: Disable (offset 214) */
   "i\0"
   "glDisable\0"
   "\0"
   /* _mesa_function_pool[3501]: Enable (offset 215) */
   "i\0"
   "glEnable\0"
   "\0"
   /* _mesa_function_pool[3513]: Finish (offset 216) */
   "\0"
   "glFinish\0"
   "\0"
   /* _mesa_function_pool[3524]: Flush (offset 217) */
   "\0"
   "glFlush\0"
   "\0"
   /* _mesa_function_pool[3534]: PopAttrib (offset 218) */
   "\0"
   "glPopAttrib\0"
   "\0"
   /* _mesa_function_pool[3548]: PushAttrib (offset 219) */
   "i\0"
   "glPushAttrib\0"
   "\0"
   /* _mesa_function_pool[3564]: Map1d (offset 220) */
   "iddiip\0"
   "glMap1d\0"
   "\0"
   /* _mesa_function_pool[3580]: Map1f (offset 221) */
   "iffiip\0"
   "glMap1f\0"
   "\0"
   /* _mesa_function_pool[3596]: Map2d (offset 222) */
   "iddiiddiip\0"
   "glMap2d\0"
   "\0"
   /* _mesa_function_pool[3616]: Map2f (offset 223) */
   "iffiiffiip\0"
   "glMap2f\0"
   "\0"
   /* _mesa_function_pool[3636]: MapGrid1d (offset 224) */
   "idd\0"
   "glMapGrid1d\0"
   "\0"
   /* _mesa_function_pool[3653]: MapGrid1f (offset 225) */
   "iff\0"
   "glMapGrid1f\0"
   "\0"
   /* _mesa_function_pool[3670]: MapGrid2d (offset 226) */
   "iddidd\0"
   "glMapGrid2d\0"
   "\0"
   /* _mesa_function_pool[3690]: MapGrid2f (offset 227) */
   "iffiff\0"
   "glMapGrid2f\0"
   "\0"
   /* _mesa_function_pool[3710]: EvalCoord1d (offset 228) */
   "d\0"
   "glEvalCoord1d\0"
   "\0"
   /* _mesa_function_pool[3727]: EvalCoord1dv (offset 229) */
   "p\0"
   "glEvalCoord1dv\0"
   "\0"
   /* _mesa_function_pool[3745]: EvalCoord1f (offset 230) */
   "f\0"
   "glEvalCoord1f\0"
   "\0"
   /* _mesa_function_pool[3762]: EvalCoord1fv (offset 231) */
   "p\0"
   "glEvalCoord1fv\0"
   "\0"
   /* _mesa_function_pool[3780]: EvalCoord2d (offset 232) */
   "dd\0"
   "glEvalCoord2d\0"
   "\0"
   /* _mesa_function_pool[3798]: EvalCoord2dv (offset 233) */
   "p\0"
   "glEvalCoord2dv\0"
   "\0"
   /* _mesa_function_pool[3816]: EvalCoord2f (offset 234) */
   "ff\0"
   "glEvalCoord2f\0"
   "\0"
   /* _mesa_function_pool[3834]: EvalCoord2fv (offset 235) */
   "p\0"
   "glEvalCoord2fv\0"
   "\0"
   /* _mesa_function_pool[3852]: EvalMesh1 (offset 236) */
   "iii\0"
   "glEvalMesh1\0"
   "\0"
   /* _mesa_function_pool[3869]: EvalPoint1 (offset 237) */
   "i\0"
   "glEvalPoint1\0"
   "\0"
   /* _mesa_function_pool[3885]: EvalMesh2 (offset 238) */
   "iiiii\0"
   "glEvalMesh2\0"
   "\0"
   /* _mesa_function_pool[3904]: EvalPoint2 (offset 239) */
   "ii\0"
   "glEvalPoint2\0"
   "\0"
   /* _mesa_function_pool[3921]: AlphaFunc (offset 240) */
   "if\0"
   "glAlphaFunc\0"
   "\0"
   /* _mesa_function_pool[3937]: BlendFunc (offset 241) */
   "ii\0"
   "glBlendFunc\0"
   "\0"
   /* _mesa_function_pool[3953]: LogicOp (offset 242) */
   "i\0"
   "glLogicOp\0"
   "\0"
   /* _mesa_function_pool[3966]: StencilFunc (offset 243) */
   "iii\0"
   "glStencilFunc\0"
   "\0"
   /* _mesa_function_pool[3985]: StencilOp (offset 244) */
   "iii\0"
   "glStencilOp\0"
   "\0"
   /* _mesa_function_pool[4002]: DepthFunc (offset 245) */
   "i\0"
   "glDepthFunc\0"
   "\0"
   /* _mesa_function_pool[4017]: PixelZoom (offset 246) */
   "ff\0"
   "glPixelZoom\0"
   "\0"
   /* _mesa_function_pool[4033]: PixelTransferf (offset 247) */
   "if\0"
   "glPixelTransferf\0"
   "\0"
   /* _mesa_function_pool[4054]: PixelTransferi (offset 248) */
   "ii\0"
   "glPixelTransferi\0"
   "\0"
   /* _mesa_function_pool[4075]: PixelStoref (offset 249) */
   "if\0"
   "glPixelStoref\0"
   "\0"
   /* _mesa_function_pool[4093]: PixelStorei (offset 250) */
   "ii\0"
   "glPixelStorei\0"
   "\0"
   /* _mesa_function_pool[4111]: PixelMapfv (offset 251) */
   "iip\0"
   "glPixelMapfv\0"
   "\0"
   /* _mesa_function_pool[4129]: PixelMapuiv (offset 252) */
   "iip\0"
   "glPixelMapuiv\0"
   "\0"
   /* _mesa_function_pool[4148]: PixelMapusv (offset 253) */
   "iip\0"
   "glPixelMapusv\0"
   "\0"
   /* _mesa_function_pool[4167]: ReadBuffer (offset 254) */
   "i\0"
   "glReadBuffer\0"
   "glReadBufferNV\0"
   "\0"
   /* _mesa_function_pool[4198]: CopyPixels (offset 255) */
   "iiiii\0"
   "glCopyPixels\0"
   "\0"
   /* _mesa_function_pool[4218]: ReadPixels (offset 256) */
   "iiiiiip\0"
   "glReadPixels\0"
   "\0"
   /* _mesa_function_pool[4240]: DrawPixels (offset 257) */
   "iiiip\0"
   "glDrawPixels\0"
   "\0"
   /* _mesa_function_pool[4260]: GetBooleanv (offset 258) */
   "ip\0"
   "glGetBooleanv\0"
   "\0"
   /* _mesa_function_pool[4278]: GetClipPlane (offset 259) */
   "ip\0"
   "glGetClipPlane\0"
   "\0"
   /* _mesa_function_pool[4297]: GetDoublev (offset 260) */
   "ip\0"
   "glGetDoublev\0"
   "\0"
   /* _mesa_function_pool[4314]: GetError (offset 261) */
   "\0"
   "glGetError\0"
   "\0"
   /* _mesa_function_pool[4327]: GetFloatv (offset 262) */
   "ip\0"
   "glGetFloatv\0"
   "\0"
   /* _mesa_function_pool[4343]: GetIntegerv (offset 263) */
   "ip\0"
   "glGetIntegerv\0"
   "\0"
   /* _mesa_function_pool[4361]: GetLightfv (offset 264) */
   "iip\0"
   "glGetLightfv\0"
   "\0"
   /* _mesa_function_pool[4379]: GetLightiv (offset 265) */
   "iip\0"
   "glGetLightiv\0"
   "\0"
   /* _mesa_function_pool[4397]: GetMapdv (offset 266) */
   "iip\0"
   "glGetMapdv\0"
   "\0"
   /* _mesa_function_pool[4413]: GetMapfv (offset 267) */
   "iip\0"
   "glGetMapfv\0"
   "\0"
   /* _mesa_function_pool[4429]: GetMapiv (offset 268) */
   "iip\0"
   "glGetMapiv\0"
   "\0"
   /* _mesa_function_pool[4445]: GetMaterialfv (offset 269) */
   "iip\0"
   "glGetMaterialfv\0"
   "\0"
   /* _mesa_function_pool[4466]: GetMaterialiv (offset 270) */
   "iip\0"
   "glGetMaterialiv\0"
   "\0"
   /* _mesa_function_pool[4487]: GetPixelMapfv (offset 271) */
   "ip\0"
   "glGetPixelMapfv\0"
   "\0"
   /* _mesa_function_pool[4507]: GetPixelMapuiv (offset 272) */
   "ip\0"
   "glGetPixelMapuiv\0"
   "\0"
   /* _mesa_function_pool[4528]: GetPixelMapusv (offset 273) */
   "ip\0"
   "glGetPixelMapusv\0"
   "\0"
   /* _mesa_function_pool[4549]: GetPolygonStipple (offset 274) */
   "p\0"
   "glGetPolygonStipple\0"
   "\0"
   /* _mesa_function_pool[4572]: GetString (offset 275) */
   "i\0"
   "glGetString\0"
   "\0"
   /* _mesa_function_pool[4587]: GetTexEnvfv (offset 276) */
   "iip\0"
   "glGetTexEnvfv\0"
   "\0"
   /* _mesa_function_pool[4606]: GetTexEnviv (offset 277) */
   "iip\0"
   "glGetTexEnviv\0"
   "\0"
   /* _mesa_function_pool[4625]: GetTexGendv (offset 278) */
   "iip\0"
   "glGetTexGendv\0"
   "\0"
   /* _mesa_function_pool[4644]: GetTexGenfv (offset 279) */
   "iip\0"
   "glGetTexGenfv\0"
   "glGetTexGenfvOES\0"
   "\0"
   /* _mesa_function_pool[4680]: GetTexGeniv (offset 280) */
   "iip\0"
   "glGetTexGeniv\0"
   "glGetTexGenivOES\0"
   "\0"
   /* _mesa_function_pool[4716]: GetTexImage (offset 281) */
   "iiiip\0"
   "glGetTexImage\0"
   "\0"
   /* _mesa_function_pool[4737]: GetTexParameterfv (offset 282) */
   "iip\0"
   "glGetTexParameterfv\0"
   "\0"
   /* _mesa_function_pool[4762]: GetTexParameteriv (offset 283) */
   "iip\0"
   "glGetTexParameteriv\0"
   "\0"
   /* _mesa_function_pool[4787]: GetTexLevelParameterfv (offset 284) */
   "iiip\0"
   "glGetTexLevelParameterfv\0"
   "\0"
   /* _mesa_function_pool[4818]: GetTexLevelParameteriv (offset 285) */
   "iiip\0"
   "glGetTexLevelParameteriv\0"
   "\0"
   /* _mesa_function_pool[4849]: IsEnabled (offset 286) */
   "i\0"
   "glIsEnabled\0"
   "\0"
   /* _mesa_function_pool[4864]: IsList (offset 287) */
   "i\0"
   "glIsList\0"
   "\0"
   /* _mesa_function_pool[4876]: DepthRange (offset 288) */
   "dd\0"
   "glDepthRange\0"
   "\0"
   /* _mesa_function_pool[4893]: Frustum (offset 289) */
   "dddddd\0"
   "glFrustum\0"
   "\0"
   /* _mesa_function_pool[4911]: LoadIdentity (offset 290) */
   "\0"
   "glLoadIdentity\0"
   "\0"
   /* _mesa_function_pool[4928]: LoadMatrixf (offset 291) */
   "p\0"
   "glLoadMatrixf\0"
   "\0"
   /* _mesa_function_pool[4945]: LoadMatrixd (offset 292) */
   "p\0"
   "glLoadMatrixd\0"
   "\0"
   /* _mesa_function_pool[4962]: MatrixMode (offset 293) */
   "i\0"
   "glMatrixMode\0"
   "\0"
   /* _mesa_function_pool[4978]: MultMatrixf (offset 294) */
   "p\0"
   "glMultMatrixf\0"
   "\0"
   /* _mesa_function_pool[4995]: MultMatrixd (offset 295) */
   "p\0"
   "glMultMatrixd\0"
   "\0"
   /* _mesa_function_pool[5012]: Ortho (offset 296) */
   "dddddd\0"
   "glOrtho\0"
   "\0"
   /* _mesa_function_pool[5028]: PopMatrix (offset 297) */
   "\0"
   "glPopMatrix\0"
   "\0"
   /* _mesa_function_pool[5042]: PushMatrix (offset 298) */
   "\0"
   "glPushMatrix\0"
   "\0"
   /* _mesa_function_pool[5057]: Rotated (offset 299) */
   "dddd\0"
   "glRotated\0"
   "\0"
   /* _mesa_function_pool[5073]: Rotatef (offset 300) */
   "ffff\0"
   "glRotatef\0"
   "\0"
   /* _mesa_function_pool[5089]: Scaled (offset 301) */
   "ddd\0"
   "glScaled\0"
   "\0"
   /* _mesa_function_pool[5103]: Scalef (offset 302) */
   "fff\0"
   "glScalef\0"
   "\0"
   /* _mesa_function_pool[5117]: Translated (offset 303) */
   "ddd\0"
   "glTranslated\0"
   "\0"
   /* _mesa_function_pool[5135]: Translatef (offset 304) */
   "fff\0"
   "glTranslatef\0"
   "\0"
   /* _mesa_function_pool[5153]: Viewport (offset 305) */
   "iiii\0"
   "glViewport\0"
   "\0"
   /* _mesa_function_pool[5170]: ArrayElement (offset 306) */
   "i\0"
   "glArrayElement\0"
   "glArrayElementEXT\0"
   "\0"
   /* _mesa_function_pool[5206]: ColorPointer (offset 308) */
   "iiip\0"
   "glColorPointer\0"
   "\0"
   /* _mesa_function_pool[5227]: DisableClientState (offset 309) */
   "i\0"
   "glDisableClientState\0"
   "\0"
   /* _mesa_function_pool[5251]: DrawArrays (offset 310) */
   "iii\0"
   "glDrawArrays\0"
   "glDrawArraysEXT\0"
   "\0"
   /* _mesa_function_pool[5285]: DrawElements (offset 311) */
   "iiip\0"
   "glDrawElements\0"
   "\0"
   /* _mesa_function_pool[5306]: EdgeFlagPointer (offset 312) */
   "ip\0"
   "glEdgeFlagPointer\0"
   "\0"
   /* _mesa_function_pool[5328]: EnableClientState (offset 313) */
   "i\0"
   "glEnableClientState\0"
   "\0"
   /* _mesa_function_pool[5351]: GetPointerv (offset 329) */
   "ip\0"
   "glGetPointerv\0"
   "glGetPointervKHR\0"
   "glGetPointervEXT\0"
   "\0"
   /* _mesa_function_pool[5403]: IndexPointer (offset 314) */
   "iip\0"
   "glIndexPointer\0"
   "\0"
   /* _mesa_function_pool[5423]: InterleavedArrays (offset 317) */
   "iip\0"
   "glInterleavedArrays\0"
   "\0"
   /* _mesa_function_pool[5448]: NormalPointer (offset 318) */
   "iip\0"
   "glNormalPointer\0"
   "\0"
   /* _mesa_function_pool[5469]: TexCoordPointer (offset 320) */
   "iiip\0"
   "glTexCoordPointer\0"
   "\0"
   /* _mesa_function_pool[5493]: VertexPointer (offset 321) */
   "iiip\0"
   "glVertexPointer\0"
   "\0"
   /* _mesa_function_pool[5515]: PolygonOffset (offset 319) */
   "ff\0"
   "glPolygonOffset\0"
   "\0"
   /* _mesa_function_pool[5535]: CopyTexImage1D (offset 323) */
   "iiiiiii\0"
   "glCopyTexImage1D\0"
   "glCopyTexImage1DEXT\0"
   "\0"
   /* _mesa_function_pool[5581]: CopyTexImage2D (offset 324) */
   "iiiiiiii\0"
   "glCopyTexImage2D\0"
   "glCopyTexImage2DEXT\0"
   "\0"
   /* _mesa_function_pool[5628]: CopyTexSubImage1D (offset 325) */
   "iiiiii\0"
   "glCopyTexSubImage1D\0"
   "glCopyTexSubImage1DEXT\0"
   "\0"
   /* _mesa_function_pool[5679]: CopyTexSubImage2D (offset 326) */
   "iiiiiiii\0"
   "glCopyTexSubImage2D\0"
   "glCopyTexSubImage2DEXT\0"
   "\0"
   /* _mesa_function_pool[5732]: TexSubImage1D (offset 332) */
   "iiiiiip\0"
   "glTexSubImage1D\0"
   "glTexSubImage1DEXT\0"
   "\0"
   /* _mesa_function_pool[5776]: TexSubImage2D (offset 333) */
   "iiiiiiiip\0"
   "glTexSubImage2D\0"
   "glTexSubImage2DEXT\0"
   "\0"
   /* _mesa_function_pool[5822]: AreTexturesResident (offset 322) */
   "ipp\0"
   "glAreTexturesResident\0"
   "glAreTexturesResidentEXT\0"
   "\0"
   /* _mesa_function_pool[5874]: BindTexture (offset 307) */
   "ii\0"
   "glBindTexture\0"
   "glBindTextureEXT\0"
   "\0"
   /* _mesa_function_pool[5909]: DeleteTextures (offset 327) */
   "ip\0"
   "glDeleteTextures\0"
   "glDeleteTexturesEXT\0"
   "\0"
   /* _mesa_function_pool[5950]: GenTextures (offset 328) */
   "ip\0"
   "glGenTextures\0"
   "glGenTexturesEXT\0"
   "\0"
   /* _mesa_function_pool[5985]: IsTexture (offset 330) */
   "i\0"
   "glIsTexture\0"
   "glIsTextureEXT\0"
   "\0"
   /* _mesa_function_pool[6015]: PrioritizeTextures (offset 331) */
   "ipp\0"
   "glPrioritizeTextures\0"
   "glPrioritizeTexturesEXT\0"
   "\0"
   /* _mesa_function_pool[6065]: Indexub (offset 315) */
   "i\0"
   "glIndexub\0"
   "\0"
   /* _mesa_function_pool[6078]: Indexubv (offset 316) */
   "p\0"
   "glIndexubv\0"
   "\0"
   /* _mesa_function_pool[6092]: PopClientAttrib (offset 334) */
   "\0"
   "glPopClientAttrib\0"
   "\0"
   /* _mesa_function_pool[6112]: PushClientAttrib (offset 335) */
   "i\0"
   "glPushClientAttrib\0"
   "\0"
   /* _mesa_function_pool[6134]: BlendColor (offset 336) */
   "ffff\0"
   "glBlendColor\0"
   "glBlendColorEXT\0"
   "\0"
   /* _mesa_function_pool[6169]: BlendEquation (offset 337) */
   "i\0"
   "glBlendEquation\0"
   "glBlendEquationEXT\0"
   "glBlendEquationOES\0"
   "\0"
   /* _mesa_function_pool[6226]: DrawRangeElements (offset 338) */
   "iiiiip\0"
   "glDrawRangeElements\0"
   "glDrawRangeElementsEXT\0"
   "\0"
   /* _mesa_function_pool[6277]: ColorTable (offset 339) */
   "iiiiip\0"
   "glColorTable\0"
   "glColorTableSGI\0"
   "glColorTableEXT\0"
   "\0"
   /* _mesa_function_pool[6330]: ColorTableParameterfv (offset 340) */
   "iip\0"
   "glColorTableParameterfv\0"
   "glColorTableParameterfvSGI\0"
   "\0"
   /* _mesa_function_pool[6386]: ColorTableParameteriv (offset 341) */
   "iip\0"
   "glColorTableParameteriv\0"
   "glColorTableParameterivSGI\0"
   "\0"
   /* _mesa_function_pool[6442]: CopyColorTable (offset 342) */
   "iiiii\0"
   "glCopyColorTable\0"
   "glCopyColorTableSGI\0"
   "\0"
   /* _mesa_function_pool[6486]: GetColorTable (offset 343) */
   "iiip\0"
   "glGetColorTable\0"
   "glGetColorTableSGI\0"
   "glGetColorTableEXT\0"
   "\0"
   /* _mesa_function_pool[6546]: GetColorTableParameterfv (offset 344) */
   "iip\0"
   "glGetColorTableParameterfv\0"
   "glGetColorTableParameterfvSGI\0"
   "glGetColorTableParameterfvEXT\0"
   "\0"
   /* _mesa_function_pool[6638]: GetColorTableParameteriv (offset 345) */
   "iip\0"
   "glGetColorTableParameteriv\0"
   "glGetColorTableParameterivSGI\0"
   "glGetColorTableParameterivEXT\0"
   "\0"
   /* _mesa_function_pool[6730]: ColorSubTable (offset 346) */
   "iiiiip\0"
   "glColorSubTable\0"
   "glColorSubTableEXT\0"
   "\0"
   /* _mesa_function_pool[6773]: CopyColorSubTable (offset 347) */
   "iiiii\0"
   "glCopyColorSubTable\0"
   "glCopyColorSubTableEXT\0"
   "\0"
   /* _mesa_function_pool[6823]: ConvolutionFilter1D (offset 348) */
   "iiiiip\0"
   "glConvolutionFilter1D\0"
   "glConvolutionFilter1DEXT\0"
   "\0"
   /* _mesa_function_pool[6878]: ConvolutionFilter2D (offset 349) */
   "iiiiiip\0"
   "glConvolutionFilter2D\0"
   "glConvolutionFilter2DEXT\0"
   "\0"
   /* _mesa_function_pool[6934]: ConvolutionParameterf (offset 350) */
   "iif\0"
   "glConvolutionParameterf\0"
   "glConvolutionParameterfEXT\0"
   "\0"
   /* _mesa_function_pool[6990]: ConvolutionParameterfv (offset 351) */
   "iip\0"
   "glConvolutionParameterfv\0"
   "glConvolutionParameterfvEXT\0"
   "\0"
   /* _mesa_function_pool[7048]: ConvolutionParameteri (offset 352) */
   "iii\0"
   "glConvolutionParameteri\0"
   "glConvolutionParameteriEXT\0"
   "\0"
   /* _mesa_function_pool[7104]: ConvolutionParameteriv (offset 353) */
   "iip\0"
   "glConvolutionParameteriv\0"
   "glConvolutionParameterivEXT\0"
   "\0"
   /* _mesa_function_pool[7162]: CopyConvolutionFilter1D (offset 354) */
   "iiiii\0"
   "glCopyConvolutionFilter1D\0"
   "glCopyConvolutionFilter1DEXT\0"
   "\0"
   /* _mesa_function_pool[7224]: CopyConvolutionFilter2D (offset 355) */
   "iiiiii\0"
   "glCopyConvolutionFilter2D\0"
   "glCopyConvolutionFilter2DEXT\0"
   "\0"
   /* _mesa_function_pool[7287]: GetConvolutionFilter (offset 356) */
   "iiip\0"
   "glGetConvolutionFilter\0"
   "glGetConvolutionFilterEXT\0"
   "\0"
   /* _mesa_function_pool[7342]: GetConvolutionParameterfv (offset 357) */
   "iip\0"
   "glGetConvolutionParameterfv\0"
   "glGetConvolutionParameterfvEXT\0"
   "\0"
   /* _mesa_function_pool[7406]: GetConvolutionParameteriv (offset 358) */
   "iip\0"
   "glGetConvolutionParameteriv\0"
   "glGetConvolutionParameterivEXT\0"
   "\0"
   /* _mesa_function_pool[7470]: GetSeparableFilter (offset 359) */
   "iiippp\0"
   "glGetSeparableFilter\0"
   "glGetSeparableFilterEXT\0"
   "\0"
   /* _mesa_function_pool[7523]: SeparableFilter2D (offset 360) */
   "iiiiiipp\0"
   "glSeparableFilter2D\0"
   "glSeparableFilter2DEXT\0"
   "\0"
   /* _mesa_function_pool[7576]: GetHistogram (offset 361) */
   "iiiip\0"
   "glGetHistogram\0"
   "glGetHistogramEXT\0"
   "\0"
   /* _mesa_function_pool[7616]: GetHistogramParameterfv (offset 362) */
   "iip\0"
   "glGetHistogramParameterfv\0"
   "glGetHistogramParameterfvEXT\0"
   "\0"
   /* _mesa_function_pool[7676]: GetHistogramParameteriv (offset 363) */
   "iip\0"
   "glGetHistogramParameteriv\0"
   "glGetHistogramParameterivEXT\0"
   "\0"
   /* _mesa_function_pool[7736]: GetMinmax (offset 364) */
   "iiiip\0"
   "glGetMinmax\0"
   "glGetMinmaxEXT\0"
   "\0"
   /* _mesa_function_pool[7770]: GetMinmaxParameterfv (offset 365) */
   "iip\0"
   "glGetMinmaxParameterfv\0"
   "glGetMinmaxParameterfvEXT\0"
   "\0"
   /* _mesa_function_pool[7824]: GetMinmaxParameteriv (offset 366) */
   "iip\0"
   "glGetMinmaxParameteriv\0"
   "glGetMinmaxParameterivEXT\0"
   "\0"
   /* _mesa_function_pool[7878]: Histogram (offset 367) */
   "iiii\0"
   "glHistogram\0"
   "glHistogramEXT\0"
   "\0"
   /* _mesa_function_pool[7911]: Minmax (offset 368) */
   "iii\0"
   "glMinmax\0"
   "glMinmaxEXT\0"
   "\0"
   /* _mesa_function_pool[7937]: ResetHistogram (offset 369) */
   "i\0"
   "glResetHistogram\0"
   "glResetHistogramEXT\0"
   "\0"
   /* _mesa_function_pool[7977]: ResetMinmax (offset 370) */
   "i\0"
   "glResetMinmax\0"
   "glResetMinmaxEXT\0"
   "\0"
   /* _mesa_function_pool[8011]: TexImage3D (offset 371) */
   "iiiiiiiiip\0"
   "glTexImage3D\0"
   "glTexImage3DEXT\0"
   "glTexImage3DOES\0"
   "\0"
   /* _mesa_function_pool[8068]: TexSubImage3D (offset 372) */
   "iiiiiiiiiip\0"
   "glTexSubImage3D\0"
   "glTexSubImage3DEXT\0"
   "glTexSubImage3DOES\0"
   "\0"
   /* _mesa_function_pool[8135]: CopyTexSubImage3D (offset 373) */
   "iiiiiiiii\0"
   "glCopyTexSubImage3D\0"
   "glCopyTexSubImage3DEXT\0"
   "glCopyTexSubImage3DOES\0"
   "\0"
   /* _mesa_function_pool[8212]: ActiveTexture (offset 374) */
   "i\0"
   "glActiveTexture\0"
   "glActiveTextureARB\0"
   "\0"
   /* _mesa_function_pool[8250]: ClientActiveTexture (offset 375) */
   "i\0"
   "glClientActiveTexture\0"
   "glClientActiveTextureARB\0"
   "\0"
   /* _mesa_function_pool[8300]: MultiTexCoord1d (offset 376) */
   "id\0"
   "glMultiTexCoord1d\0"
   "glMultiTexCoord1dARB\0"
   "\0"
   /* _mesa_function_pool[8343]: MultiTexCoord1dv (offset 377) */
   "ip\0"
   "glMultiTexCoord1dv\0"
   "glMultiTexCoord1dvARB\0"
   "\0"
   /* _mesa_function_pool[8388]: MultiTexCoord1fARB (offset 378) */
   "if\0"
   "glMultiTexCoord1f\0"
   "glMultiTexCoord1fARB\0"
   "\0"
   /* _mesa_function_pool[8431]: MultiTexCoord1fvARB (offset 379) */
   "ip\0"
   "glMultiTexCoord1fv\0"
   "glMultiTexCoord1fvARB\0"
   "\0"
   /* _mesa_function_pool[8476]: MultiTexCoord1i (offset 380) */
   "ii\0"
   "glMultiTexCoord1i\0"
   "glMultiTexCoord1iARB\0"
   "\0"
   /* _mesa_function_pool[8519]: MultiTexCoord1iv (offset 381) */
   "ip\0"
   "glMultiTexCoord1iv\0"
   "glMultiTexCoord1ivARB\0"
   "\0"
   /* _mesa_function_pool[8564]: MultiTexCoord1s (offset 382) */
   "ii\0"
   "glMultiTexCoord1s\0"
   "glMultiTexCoord1sARB\0"
   "\0"
   /* _mesa_function_pool[8607]: MultiTexCoord1sv (offset 383) */
   "ip\0"
   "glMultiTexCoord1sv\0"
   "glMultiTexCoord1svARB\0"
   "\0"
   /* _mesa_function_pool[8652]: MultiTexCoord2d (offset 384) */
   "idd\0"
   "glMultiTexCoord2d\0"
   "glMultiTexCoord2dARB\0"
   "\0"
   /* _mesa_function_pool[8696]: MultiTexCoord2dv (offset 385) */
   "ip\0"
   "glMultiTexCoord2dv\0"
   "glMultiTexCoord2dvARB\0"
   "\0"
   /* _mesa_function_pool[8741]: MultiTexCoord2fARB (offset 386) */
   "iff\0"
   "glMultiTexCoord2f\0"
   "glMultiTexCoord2fARB\0"
   "\0"
   /* _mesa_function_pool[8785]: MultiTexCoord2fvARB (offset 387) */
   "ip\0"
   "glMultiTexCoord2fv\0"
   "glMultiTexCoord2fvARB\0"
   "\0"
   /* _mesa_function_pool[8830]: MultiTexCoord2i (offset 388) */
   "iii\0"
   "glMultiTexCoord2i\0"
   "glMultiTexCoord2iARB\0"
   "\0"
   /* _mesa_function_pool[8874]: MultiTexCoord2iv (offset 389) */
   "ip\0"
   "glMultiTexCoord2iv\0"
   "glMultiTexCoord2ivARB\0"
   "\0"
   /* _mesa_function_pool[8919]: MultiTexCoord2s (offset 390) */
   "iii\0"
   "glMultiTexCoord2s\0"
   "glMultiTexCoord2sARB\0"
   "\0"
   /* _mesa_function_pool[8963]: MultiTexCoord2sv (offset 391) */
   "ip\0"
   "glMultiTexCoord2sv\0"
   "glMultiTexCoord2svARB\0"
   "\0"
   /* _mesa_function_pool[9008]: MultiTexCoord3d (offset 392) */
   "iddd\0"
   "glMultiTexCoord3d\0"
   "glMultiTexCoord3dARB\0"
   "\0"
   /* _mesa_function_pool[9053]: MultiTexCoord3dv (offset 393) */
   "ip\0"
   "glMultiTexCoord3dv\0"
   "glMultiTexCoord3dvARB\0"
   "\0"
   /* _mesa_function_pool[9098]: MultiTexCoord3fARB (offset 394) */
   "ifff\0"
   "glMultiTexCoord3f\0"
   "glMultiTexCoord3fARB\0"
   "\0"
   /* _mesa_function_pool[9143]: MultiTexCoord3fvARB (offset 395) */
   "ip\0"
   "glMultiTexCoord3fv\0"
   "glMultiTexCoord3fvARB\0"
   "\0"
   /* _mesa_function_pool[9188]: MultiTexCoord3i (offset 396) */
   "iiii\0"
   "glMultiTexCoord3i\0"
   "glMultiTexCoord3iARB\0"
   "\0"
   /* _mesa_function_pool[9233]: MultiTexCoord3iv (offset 397) */
   "ip\0"
   "glMultiTexCoord3iv\0"
   "glMultiTexCoord3ivARB\0"
   "\0"
   /* _mesa_function_pool[9278]: MultiTexCoord3s (offset 398) */
   "iiii\0"
   "glMultiTexCoord3s\0"
   "glMultiTexCoord3sARB\0"
   "\0"
   /* _mesa_function_pool[9323]: MultiTexCoord3sv (offset 399) */
   "ip\0"
   "glMultiTexCoord3sv\0"
   "glMultiTexCoord3svARB\0"
   "\0"
   /* _mesa_function_pool[9368]: MultiTexCoord4d (offset 400) */
   "idddd\0"
   "glMultiTexCoord4d\0"
   "glMultiTexCoord4dARB\0"
   "\0"
   /* _mesa_function_pool[9414]: MultiTexCoord4dv (offset 401) */
   "ip\0"
   "glMultiTexCoord4dv\0"
   "glMultiTexCoord4dvARB\0"
   "\0"
   /* _mesa_function_pool[9459]: MultiTexCoord4fARB (offset 402) */
   "iffff\0"
   "glMultiTexCoord4f\0"
   "glMultiTexCoord4fARB\0"
   "\0"
   /* _mesa_function_pool[9505]: MultiTexCoord4fvARB (offset 403) */
   "ip\0"
   "glMultiTexCoord4fv\0"
   "glMultiTexCoord4fvARB\0"
   "\0"
   /* _mesa_function_pool[9550]: MultiTexCoord4i (offset 404) */
   "iiiii\0"
   "glMultiTexCoord4i\0"
   "glMultiTexCoord4iARB\0"
   "\0"
   /* _mesa_function_pool[9596]: MultiTexCoord4iv (offset 405) */
   "ip\0"
   "glMultiTexCoord4iv\0"
   "glMultiTexCoord4ivARB\0"
   "\0"
   /* _mesa_function_pool[9641]: MultiTexCoord4s (offset 406) */
   "iiiii\0"
   "glMultiTexCoord4s\0"
   "glMultiTexCoord4sARB\0"
   "\0"
   /* _mesa_function_pool[9687]: MultiTexCoord4sv (offset 407) */
   "ip\0"
   "glMultiTexCoord4sv\0"
   "glMultiTexCoord4svARB\0"
   "\0"
   /* _mesa_function_pool[9732]: LoadTransposeMatrixf (will be remapped) */
   "p\0"
   "glLoadTransposeMatrixf\0"
   "glLoadTransposeMatrixfARB\0"
   "\0"
   /* _mesa_function_pool[9784]: LoadTransposeMatrixd (will be remapped) */
   "p\0"
   "glLoadTransposeMatrixd\0"
   "glLoadTransposeMatrixdARB\0"
   "\0"
   /* _mesa_function_pool[9836]: MultTransposeMatrixf (will be remapped) */
   "p\0"
   "glMultTransposeMatrixf\0"
   "glMultTransposeMatrixfARB\0"
   "\0"
   /* _mesa_function_pool[9888]: MultTransposeMatrixd (will be remapped) */
   "p\0"
   "glMultTransposeMatrixd\0"
   "glMultTransposeMatrixdARB\0"
   "\0"
   /* _mesa_function_pool[9940]: SampleCoverage (will be remapped) */
   "fi\0"
   "glSampleCoverage\0"
   "glSampleCoverageARB\0"
   "\0"
   /* _mesa_function_pool[9981]: CompressedTexImage3D (will be remapped) */
   "iiiiiiiip\0"
   "glCompressedTexImage3D\0"
   "glCompressedTexImage3DARB\0"
   "glCompressedTexImage3DOES\0"
   "\0"
   /* _mesa_function_pool[10067]: CompressedTexImage2D (will be remapped) */
   "iiiiiiip\0"
   "glCompressedTexImage2D\0"
   "glCompressedTexImage2DARB\0"
   "\0"
   /* _mesa_function_pool[10126]: CompressedTexImage1D (will be remapped) */
   "iiiiiip\0"
   "glCompressedTexImage1D\0"
   "glCompressedTexImage1DARB\0"
   "\0"
   /* _mesa_function_pool[10184]: CompressedTexSubImage3D (will be remapped) */
   "iiiiiiiiiip\0"
   "glCompressedTexSubImage3D\0"
   "glCompressedTexSubImage3DARB\0"
   "glCompressedTexSubImage3DOES\0"
   "\0"
   /* _mesa_function_pool[10281]: CompressedTexSubImage2D (will be remapped) */
   "iiiiiiiip\0"
   "glCompressedTexSubImage2D\0"
   "glCompressedTexSubImage2DARB\0"
   "\0"
   /* _mesa_function_pool[10347]: CompressedTexSubImage1D (will be remapped) */
   "iiiiiip\0"
   "glCompressedTexSubImage1D\0"
   "glCompressedTexSubImage1DARB\0"
   "\0"
   /* _mesa_function_pool[10411]: GetCompressedTexImage (will be remapped) */
   "iip\0"
   "glGetCompressedTexImage\0"
   "glGetCompressedTexImageARB\0"
   "\0"
   /* _mesa_function_pool[10467]: BlendFuncSeparate (will be remapped) */
   "iiii\0"
   "glBlendFuncSeparate\0"
   "glBlendFuncSeparateEXT\0"
   "glBlendFuncSeparateINGR\0"
   "glBlendFuncSeparateOES\0"
   "\0"
   /* _mesa_function_pool[10563]: FogCoordfEXT (will be remapped) */
   "f\0"
   "glFogCoordf\0"
   "glFogCoordfEXT\0"
   "\0"
   /* _mesa_function_pool[10593]: FogCoordfvEXT (will be remapped) */
   "p\0"
   "glFogCoordfv\0"
   "glFogCoordfvEXT\0"
   "\0"
   /* _mesa_function_pool[10625]: FogCoordd (will be remapped) */
   "d\0"
   "glFogCoordd\0"
   "glFogCoorddEXT\0"
   "\0"
   /* _mesa_function_pool[10655]: FogCoorddv (will be remapped) */
   "p\0"
   "glFogCoorddv\0"
   "glFogCoorddvEXT\0"
   "\0"
   /* _mesa_function_pool[10687]: FogCoordPointer (will be remapped) */
   "iip\0"
   "glFogCoordPointer\0"
   "glFogCoordPointerEXT\0"
   "\0"
   /* _mesa_function_pool[10731]: MultiDrawArrays (will be remapped) */
   "ippi\0"
   "glMultiDrawArrays\0"
   "glMultiDrawArraysEXT\0"
   "\0"
   /* _mesa_function_pool[10776]: MultiDrawElementsEXT (will be remapped) */
   "ipipi\0"
   "glMultiDrawElements\0"
   "glMultiDrawElementsEXT\0"
   "\0"
   /* _mesa_function_pool[10826]: PointParameterf (will be remapped) */
   "if\0"
   "glPointParameterf\0"
   "glPointParameterfARB\0"
   "glPointParameterfEXT\0"
   "glPointParameterfSGIS\0"
   "\0"
   /* _mesa_function_pool[10912]: PointParameterfv (will be remapped) */
   "ip\0"
   "glPointParameterfv\0"
   "glPointParameterfvARB\0"
   "glPointParameterfvEXT\0"
   "glPointParameterfvSGIS\0"
   "\0"
   /* _mesa_function_pool[11002]: PointParameteri (will be remapped) */
   "ii\0"
   "glPointParameteri\0"
   "glPointParameteriNV\0"
   "\0"
   /* _mesa_function_pool[11044]: PointParameteriv (will be remapped) */
   "ip\0"
   "glPointParameteriv\0"
   "glPointParameterivNV\0"
   "\0"
   /* _mesa_function_pool[11088]: SecondaryColor3b (will be remapped) */
   "iii\0"
   "glSecondaryColor3b\0"
   "glSecondaryColor3bEXT\0"
   "\0"
   /* _mesa_function_pool[11134]: SecondaryColor3bv (will be remapped) */
   "p\0"
   "glSecondaryColor3bv\0"
   "glSecondaryColor3bvEXT\0"
   "\0"
   /* _mesa_function_pool[11180]: SecondaryColor3d (will be remapped) */
   "ddd\0"
   "glSecondaryColor3d\0"
   "glSecondaryColor3dEXT\0"
   "\0"
   /* _mesa_function_pool[11226]: SecondaryColor3dv (will be remapped) */
   "p\0"
   "glSecondaryColor3dv\0"
   "glSecondaryColor3dvEXT\0"
   "\0"
   /* _mesa_function_pool[11272]: SecondaryColor3fEXT (will be remapped) */
   "fff\0"
   "glSecondaryColor3f\0"
   "glSecondaryColor3fEXT\0"
   "\0"
   /* _mesa_function_pool[11318]: SecondaryColor3fvEXT (will be remapped) */
   "p\0"
   "glSecondaryColor3fv\0"
   "glSecondaryColor3fvEXT\0"
   "\0"
   /* _mesa_function_pool[11364]: SecondaryColor3i (will be remapped) */
   "iii\0"
   "glSecondaryColor3i\0"
   "glSecondaryColor3iEXT\0"
   "\0"
   /* _mesa_function_pool[11410]: SecondaryColor3iv (will be remapped) */
   "p\0"
   "glSecondaryColor3iv\0"
   "glSecondaryColor3ivEXT\0"
   "\0"
   /* _mesa_function_pool[11456]: SecondaryColor3s (will be remapped) */
   "iii\0"
   "glSecondaryColor3s\0"
   "glSecondaryColor3sEXT\0"
   "\0"
   /* _mesa_function_pool[11502]: SecondaryColor3sv (will be remapped) */
   "p\0"
   "glSecondaryColor3sv\0"
   "glSecondaryColor3svEXT\0"
   "\0"
   /* _mesa_function_pool[11548]: SecondaryColor3ub (will be remapped) */
   "iii\0"
   "glSecondaryColor3ub\0"
   "glSecondaryColor3ubEXT\0"
   "\0"
   /* _mesa_function_pool[11596]: SecondaryColor3ubv (will be remapped) */
   "p\0"
   "glSecondaryColor3ubv\0"
   "glSecondaryColor3ubvEXT\0"
   "\0"
   /* _mesa_function_pool[11644]: SecondaryColor3ui (will be remapped) */
   "iii\0"
   "glSecondaryColor3ui\0"
   "glSecondaryColor3uiEXT\0"
   "\0"
   /* _mesa_function_pool[11692]: SecondaryColor3uiv (will be remapped) */
   "p\0"
   "glSecondaryColor3uiv\0"
   "glSecondaryColor3uivEXT\0"
   "\0"
   /* _mesa_function_pool[11740]: SecondaryColor3us (will be remapped) */
   "iii\0"
   "glSecondaryColor3us\0"
   "glSecondaryColor3usEXT\0"
   "\0"
   /* _mesa_function_pool[11788]: SecondaryColor3usv (will be remapped) */
   "p\0"
   "glSecondaryColor3usv\0"
   "glSecondaryColor3usvEXT\0"
   "\0"
   /* _mesa_function_pool[11836]: SecondaryColorPointer (will be remapped) */
   "iiip\0"
   "glSecondaryColorPointer\0"
   "glSecondaryColorPointerEXT\0"
   "\0"
   /* _mesa_function_pool[11893]: WindowPos2d (will be remapped) */
   "dd\0"
   "glWindowPos2d\0"
   "glWindowPos2dARB\0"
   "glWindowPos2dMESA\0"
   "\0"
   /* _mesa_function_pool[11946]: WindowPos2dv (will be remapped) */
   "p\0"
   "glWindowPos2dv\0"
   "glWindowPos2dvARB\0"
   "glWindowPos2dvMESA\0"
   "\0"
   /* _mesa_function_pool[12001]: WindowPos2f (will be remapped) */
   "ff\0"
   "glWindowPos2f\0"
   "glWindowPos2fARB\0"
   "glWindowPos2fMESA\0"
   "\0"
   /* _mesa_function_pool[12054]: WindowPos2fv (will be remapped) */
   "p\0"
   "glWindowPos2fv\0"
   "glWindowPos2fvARB\0"
   "glWindowPos2fvMESA\0"
   "\0"
   /* _mesa_function_pool[12109]: WindowPos2i (will be remapped) */
   "ii\0"
   "glWindowPos2i\0"
   "glWindowPos2iARB\0"
   "glWindowPos2iMESA\0"
   "\0"
   /* _mesa_function_pool[12162]: WindowPos2iv (will be remapped) */
   "p\0"
   "glWindowPos2iv\0"
   "glWindowPos2ivARB\0"
   "glWindowPos2ivMESA\0"
   "\0"
   /* _mesa_function_pool[12217]: WindowPos2s (will be remapped) */
   "ii\0"
   "glWindowPos2s\0"
   "glWindowPos2sARB\0"
   "glWindowPos2sMESA\0"
   "\0"
   /* _mesa_function_pool[12270]: WindowPos2sv (will be remapped) */
   "p\0"
   "glWindowPos2sv\0"
   "glWindowPos2svARB\0"
   "glWindowPos2svMESA\0"
   "\0"
   /* _mesa_function_pool[12325]: WindowPos3d (will be remapped) */
   "ddd\0"
   "glWindowPos3d\0"
   "glWindowPos3dARB\0"
   "glWindowPos3dMESA\0"
   "\0"
   /* _mesa_function_pool[12379]: WindowPos3dv (will be remapped) */
   "p\0"
   "glWindowPos3dv\0"
   "glWindowPos3dvARB\0"
   "glWindowPos3dvMESA\0"
   "\0"
   /* _mesa_function_pool[12434]: WindowPos3f (will be remapped) */
   "fff\0"
   "glWindowPos3f\0"
   "glWindowPos3fARB\0"
   "glWindowPos3fMESA\0"
   "\0"
   /* _mesa_function_pool[12488]: WindowPos3fv (will be remapped) */
   "p\0"
   "glWindowPos3fv\0"
   "glWindowPos3fvARB\0"
   "glWindowPos3fvMESA\0"
   "\0"
   /* _mesa_function_pool[12543]: WindowPos3i (will be remapped) */
   "iii\0"
   "glWindowPos3i\0"
   "glWindowPos3iARB\0"
   "glWindowPos3iMESA\0"
   "\0"
   /* _mesa_function_pool[12597]: WindowPos3iv (will be remapped) */
   "p\0"
   "glWindowPos3iv\0"
   "glWindowPos3ivARB\0"
   "glWindowPos3ivMESA\0"
   "\0"
   /* _mesa_function_pool[12652]: WindowPos3s (will be remapped) */
   "iii\0"
   "glWindowPos3s\0"
   "glWindowPos3sARB\0"
   "glWindowPos3sMESA\0"
   "\0"
   /* _mesa_function_pool[12706]: WindowPos3sv (will be remapped) */
   "p\0"
   "glWindowPos3sv\0"
   "glWindowPos3svARB\0"
   "glWindowPos3svMESA\0"
   "\0"
   /* _mesa_function_pool[12761]: BindBuffer (will be remapped) */
   "ii\0"
   "glBindBuffer\0"
   "glBindBufferARB\0"
   "\0"
   /* _mesa_function_pool[12794]: BufferData (will be remapped) */
   "iipi\0"
   "glBufferData\0"
   "glBufferDataARB\0"
   "\0"
   /* _mesa_function_pool[12829]: BufferSubData (will be remapped) */
   "iiip\0"
   "glBufferSubData\0"
   "glBufferSubDataARB\0"
   "\0"
   /* _mesa_function_pool[12870]: DeleteBuffers (will be remapped) */
   "ip\0"
   "glDeleteBuffers\0"
   "glDeleteBuffersARB\0"
   "\0"
   /* _mesa_function_pool[12909]: GenBuffers (will be remapped) */
   "ip\0"
   "glGenBuffers\0"
   "glGenBuffersARB\0"
   "\0"
   /* _mesa_function_pool[12942]: GetBufferParameteriv (will be remapped) */
   "iip\0"
   "glGetBufferParameteriv\0"
   "glGetBufferParameterivARB\0"
   "\0"
   /* _mesa_function_pool[12996]: GetBufferPointerv (will be remapped) */
   "iip\0"
   "glGetBufferPointerv\0"
   "glGetBufferPointervARB\0"
   "glGetBufferPointervOES\0"
   "\0"
   /* _mesa_function_pool[13067]: GetBufferSubData (will be remapped) */
   "iiip\0"
   "glGetBufferSubData\0"
   "glGetBufferSubDataARB\0"
   "\0"
   /* _mesa_function_pool[13114]: IsBuffer (will be remapped) */
   "i\0"
   "glIsBuffer\0"
   "glIsBufferARB\0"
   "\0"
   /* _mesa_function_pool[13142]: MapBuffer (will be remapped) */
   "ii\0"
   "glMapBuffer\0"
   "glMapBufferARB\0"
   "glMapBufferOES\0"
   "\0"
   /* _mesa_function_pool[13188]: UnmapBuffer (will be remapped) */
   "i\0"
   "glUnmapBuffer\0"
   "glUnmapBufferARB\0"
   "glUnmapBufferOES\0"
   "\0"
   /* _mesa_function_pool[13239]: GenQueries (will be remapped) */
   "ip\0"
   "glGenQueries\0"
   "glGenQueriesARB\0"
   "glGenQueriesEXT\0"
   "\0"
   /* _mesa_function_pool[13288]: DeleteQueries (will be remapped) */
   "ip\0"
   "glDeleteQueries\0"
   "glDeleteQueriesARB\0"
   "glDeleteQueriesEXT\0"
   "\0"
   /* _mesa_function_pool[13346]: IsQuery (will be remapped) */
   "i\0"
   "glIsQuery\0"
   "glIsQueryARB\0"
   "glIsQueryEXT\0"
   "\0"
   /* _mesa_function_pool[13385]: BeginQuery (will be remapped) */
   "ii\0"
   "glBeginQuery\0"
   "glBeginQueryARB\0"
   "glBeginQueryEXT\0"
   "\0"
   /* _mesa_function_pool[13434]: EndQuery (will be remapped) */
   "i\0"
   "glEndQuery\0"
   "glEndQueryARB\0"
   "glEndQueryEXT\0"
   "\0"
   /* _mesa_function_pool[13476]: GetQueryiv (will be remapped) */
   "iip\0"
   "glGetQueryiv\0"
   "glGetQueryivARB\0"
   "glGetQueryivEXT\0"
   "\0"
   /* _mesa_function_pool[13526]: GetQueryObjectiv (will be remapped) */
   "iip\0"
   "glGetQueryObjectiv\0"
   "glGetQueryObjectivARB\0"
   "glGetQueryObjectivEXT\0"
   "\0"
   /* _mesa_function_pool[13594]: GetQueryObjectuiv (will be remapped) */
   "iip\0"
   "glGetQueryObjectuiv\0"
   "glGetQueryObjectuivARB\0"
   "glGetQueryObjectuivEXT\0"
   "\0"
   /* _mesa_function_pool[13665]: BlendEquationSeparate (will be remapped) */
   "ii\0"
   "glBlendEquationSeparate\0"
   "glBlendEquationSeparateEXT\0"
   "glBlendEquationSeparateATI\0"
   "glBlendEquationSeparateOES\0"
   "\0"
   /* _mesa_function_pool[13774]: DrawBuffers (will be remapped) */
   "ip\0"
   "glDrawBuffers\0"
   "glDrawBuffersARB\0"
   "glDrawBuffersATI\0"
   "glDrawBuffersNV\0"
   "glDrawBuffersEXT\0"
   "\0"
   /* _mesa_function_pool[13859]: StencilFuncSeparate (will be remapped) */
   "iiii\0"
   "glStencilFuncSeparate\0"
   "\0"
   /* _mesa_function_pool[13887]: StencilOpSeparate (will be remapped) */
   "iiii\0"
   "glStencilOpSeparate\0"
   "glStencilOpSeparateATI\0"
   "\0"
   /* _mesa_function_pool[13936]: StencilMaskSeparate (will be remapped) */
   "ii\0"
   "glStencilMaskSeparate\0"
   "\0"
   /* _mesa_function_pool[13962]: AttachShader (will be remapped) */
   "ii\0"
   "glAttachShader\0"
   "\0"
   /* _mesa_function_pool[13981]: BindAttribLocation (will be remapped) */
   "iip\0"
   "glBindAttribLocation\0"
   "glBindAttribLocationARB\0"
   "\0"
   /* _mesa_function_pool[14031]: CompileShader (will be remapped) */
   "i\0"
   "glCompileShader\0"
   "glCompileShaderARB\0"
   "\0"
   /* _mesa_function_pool[14069]: CreateProgram (will be remapped) */
   "\0"
   "glCreateProgram\0"
   "\0"
   /* _mesa_function_pool[14087]: CreateShader (will be remapped) */
   "i\0"
   "glCreateShader\0"
   "\0"
   /* _mesa_function_pool[14105]: DeleteProgram (will be remapped) */
   "i\0"
   "glDeleteProgram\0"
   "\0"
   /* _mesa_function_pool[14124]: DeleteShader (will be remapped) */
   "i\0"
   "glDeleteShader\0"
   "\0"
   /* _mesa_function_pool[14142]: DetachShader (will be remapped) */
   "ii\0"
   "glDetachShader\0"
   "\0"
   /* _mesa_function_pool[14161]: DisableVertexAttribArray (will be remapped) */
   "i\0"
   "glDisableVertexAttribArray\0"
   "glDisableVertexAttribArrayARB\0"
   "\0"
   /* _mesa_function_pool[14221]: EnableVertexAttribArray (will be remapped) */
   "i\0"
   "glEnableVertexAttribArray\0"
   "glEnableVertexAttribArrayARB\0"
   "\0"
   /* _mesa_function_pool[14279]: GetActiveAttrib (will be remapped) */
   "iiipppp\0"
   "glGetActiveAttrib\0"
   "glGetActiveAttribARB\0"
   "\0"
   /* _mesa_function_pool[14327]: GetActiveUniform (will be remapped) */
   "iiipppp\0"
   "glGetActiveUniform\0"
   "glGetActiveUniformARB\0"
   "\0"
   /* _mesa_function_pool[14377]: GetAttachedShaders (will be remapped) */
   "iipp\0"
   "glGetAttachedShaders\0"
   "\0"
   /* _mesa_function_pool[14404]: GetAttribLocation (will be remapped) */
   "ip\0"
   "glGetAttribLocation\0"
   "glGetAttribLocationARB\0"
   "\0"
   /* _mesa_function_pool[14451]: GetProgramiv (will be remapped) */
   "iip\0"
   "glGetProgramiv\0"
   "\0"
   /* _mesa_function_pool[14471]: GetProgramInfoLog (will be remapped) */
   "iipp\0"
   "glGetProgramInfoLog\0"
   "\0"
   /* _mesa_function_pool[14497]: GetShaderiv (will be remapped) */
   "iip\0"
   "glGetShaderiv\0"
   "\0"
   /* _mesa_function_pool[14516]: GetShaderInfoLog (will be remapped) */
   "iipp\0"
   "glGetShaderInfoLog\0"
   "\0"
   /* _mesa_function_pool[14541]: GetShaderSource (will be remapped) */
   "iipp\0"
   "glGetShaderSource\0"
   "glGetShaderSourceARB\0"
   "\0"
   /* _mesa_function_pool[14586]: GetUniformLocation (will be remapped) */
   "ip\0"
   "glGetUniformLocation\0"
   "glGetUniformLocationARB\0"
   "\0"
   /* _mesa_function_pool[14635]: GetUniformfv (will be remapped) */
   "iip\0"
   "glGetUniformfv\0"
   "glGetUniformfvARB\0"
   "\0"
   /* _mesa_function_pool[14673]: GetUniformiv (will be remapped) */
   "iip\0"
   "glGetUniformiv\0"
   "glGetUniformivARB\0"
   "\0"
   /* _mesa_function_pool[14711]: GetVertexAttribdv (will be remapped) */
   "iip\0"
   "glGetVertexAttribdv\0"
   "glGetVertexAttribdvARB\0"
   "\0"
   /* _mesa_function_pool[14759]: GetVertexAttribfv (will be remapped) */
   "iip\0"
   "glGetVertexAttribfv\0"
   "glGetVertexAttribfvARB\0"
   "\0"
   /* _mesa_function_pool[14807]: GetVertexAttribiv (will be remapped) */
   "iip\0"
   "glGetVertexAttribiv\0"
   "glGetVertexAttribivARB\0"
   "\0"
   /* _mesa_function_pool[14855]: GetVertexAttribPointerv (will be remapped) */
   "iip\0"
   "glGetVertexAttribPointerv\0"
   "glGetVertexAttribPointervARB\0"
   "glGetVertexAttribPointervNV\0"
   "\0"
   /* _mesa_function_pool[14943]: IsProgram (will be remapped) */
   "i\0"
   "glIsProgram\0"
   "\0"
   /* _mesa_function_pool[14958]: IsShader (will be remapped) */
   "i\0"
   "glIsShader\0"
   "\0"
   /* _mesa_function_pool[14972]: LinkProgram (will be remapped) */
   "i\0"
   "glLinkProgram\0"
   "glLinkProgramARB\0"
   "\0"
   /* _mesa_function_pool[15006]: ShaderSource (will be remapped) */
   "iipp\0"
   "glShaderSource\0"
   "glShaderSourceARB\0"
   "\0"
   /* _mesa_function_pool[15045]: UseProgram (will be remapped) */
   "i\0"
   "glUseProgram\0"
   "glUseProgramObjectARB\0"
   "\0"
   /* _mesa_function_pool[15083]: Uniform1f (will be remapped) */
   "if\0"
   "glUniform1f\0"
   "glUniform1fARB\0"
   "\0"
   /* _mesa_function_pool[15114]: Uniform2f (will be remapped) */
   "iff\0"
   "glUniform2f\0"
   "glUniform2fARB\0"
   "\0"
   /* _mesa_function_pool[15146]: Uniform3f (will be remapped) */
   "ifff\0"
   "glUniform3f\0"
   "glUniform3fARB\0"
   "\0"
   /* _mesa_function_pool[15179]: Uniform4f (will be remapped) */
   "iffff\0"
   "glUniform4f\0"
   "glUniform4fARB\0"
   "\0"
   /* _mesa_function_pool[15213]: Uniform1i (will be remapped) */
   "ii\0"
   "glUniform1i\0"
   "glUniform1iARB\0"
   "\0"
   /* _mesa_function_pool[15244]: Uniform2i (will be remapped) */
   "iii\0"
   "glUniform2i\0"
   "glUniform2iARB\0"
   "\0"
   /* _mesa_function_pool[15276]: Uniform3i (will be remapped) */
   "iiii\0"
   "glUniform3i\0"
   "glUniform3iARB\0"
   "\0"
   /* _mesa_function_pool[15309]: Uniform4i (will be remapped) */
   "iiiii\0"
   "glUniform4i\0"
   "glUniform4iARB\0"
   "\0"
   /* _mesa_function_pool[15343]: Uniform1fv (will be remapped) */
   "iip\0"
   "glUniform1fv\0"
   "glUniform1fvARB\0"
   "\0"
   /* _mesa_function_pool[15377]: Uniform2fv (will be remapped) */
   "iip\0"
   "glUniform2fv\0"
   "glUniform2fvARB\0"
   "\0"
   /* _mesa_function_pool[15411]: Uniform3fv (will be remapped) */
   "iip\0"
   "glUniform3fv\0"
   "glUniform3fvARB\0"
   "\0"
   /* _mesa_function_pool[15445]: Uniform4fv (will be remapped) */
   "iip\0"
   "glUniform4fv\0"
   "glUniform4fvARB\0"
   "\0"
   /* _mesa_function_pool[15479]: Uniform1iv (will be remapped) */
   "iip\0"
   "glUniform1iv\0"
   "glUniform1ivARB\0"
   "\0"
   /* _mesa_function_pool[15513]: Uniform2iv (will be remapped) */
   "iip\0"
   "glUniform2iv\0"
   "glUniform2ivARB\0"
   "\0"
   /* _mesa_function_pool[15547]: Uniform3iv (will be remapped) */
   "iip\0"
   "glUniform3iv\0"
   "glUniform3ivARB\0"
   "\0"
   /* _mesa_function_pool[15581]: Uniform4iv (will be remapped) */
   "iip\0"
   "glUniform4iv\0"
   "glUniform4ivARB\0"
   "\0"
   /* _mesa_function_pool[15615]: UniformMatrix2fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix2fv\0"
   "glUniformMatrix2fvARB\0"
   "\0"
   /* _mesa_function_pool[15662]: UniformMatrix3fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix3fv\0"
   "glUniformMatrix3fvARB\0"
   "\0"
   /* _mesa_function_pool[15709]: UniformMatrix4fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix4fv\0"
   "glUniformMatrix4fvARB\0"
   "\0"
   /* _mesa_function_pool[15756]: ValidateProgram (will be remapped) */
   "i\0"
   "glValidateProgram\0"
   "glValidateProgramARB\0"
   "\0"
   /* _mesa_function_pool[15798]: VertexAttrib1d (will be remapped) */
   "id\0"
   "glVertexAttrib1d\0"
   "glVertexAttrib1dARB\0"
   "\0"
   /* _mesa_function_pool[15839]: VertexAttrib1dv (will be remapped) */
   "ip\0"
   "glVertexAttrib1dv\0"
   "glVertexAttrib1dvARB\0"
   "\0"
   /* _mesa_function_pool[15882]: VertexAttrib1fARB (will be remapped) */
   "if\0"
   "glVertexAttrib1f\0"
   "glVertexAttrib1fARB\0"
   "\0"
   /* _mesa_function_pool[15923]: VertexAttrib1fvARB (will be remapped) */
   "ip\0"
   "glVertexAttrib1fv\0"
   "glVertexAttrib1fvARB\0"
   "\0"
   /* _mesa_function_pool[15966]: VertexAttrib1s (will be remapped) */
   "ii\0"
   "glVertexAttrib1s\0"
   "glVertexAttrib1sARB\0"
   "\0"
   /* _mesa_function_pool[16007]: VertexAttrib1sv (will be remapped) */
   "ip\0"
   "glVertexAttrib1sv\0"
   "glVertexAttrib1svARB\0"
   "\0"
   /* _mesa_function_pool[16050]: VertexAttrib2d (will be remapped) */
   "idd\0"
   "glVertexAttrib2d\0"
   "glVertexAttrib2dARB\0"
   "\0"
   /* _mesa_function_pool[16092]: VertexAttrib2dv (will be remapped) */
   "ip\0"
   "glVertexAttrib2dv\0"
   "glVertexAttrib2dvARB\0"
   "\0"
   /* _mesa_function_pool[16135]: VertexAttrib2fARB (will be remapped) */
   "iff\0"
   "glVertexAttrib2f\0"
   "glVertexAttrib2fARB\0"
   "\0"
   /* _mesa_function_pool[16177]: VertexAttrib2fvARB (will be remapped) */
   "ip\0"
   "glVertexAttrib2fv\0"
   "glVertexAttrib2fvARB\0"
   "\0"
   /* _mesa_function_pool[16220]: VertexAttrib2s (will be remapped) */
   "iii\0"
   "glVertexAttrib2s\0"
   "glVertexAttrib2sARB\0"
   "\0"
   /* _mesa_function_pool[16262]: VertexAttrib2sv (will be remapped) */
   "ip\0"
   "glVertexAttrib2sv\0"
   "glVertexAttrib2svARB\0"
   "\0"
   /* _mesa_function_pool[16305]: VertexAttrib3d (will be remapped) */
   "iddd\0"
   "glVertexAttrib3d\0"
   "glVertexAttrib3dARB\0"
   "\0"
   /* _mesa_function_pool[16348]: VertexAttrib3dv (will be remapped) */
   "ip\0"
   "glVertexAttrib3dv\0"
   "glVertexAttrib3dvARB\0"
   "\0"
   /* _mesa_function_pool[16391]: VertexAttrib3fARB (will be remapped) */
   "ifff\0"
   "glVertexAttrib3f\0"
   "glVertexAttrib3fARB\0"
   "\0"
   /* _mesa_function_pool[16434]: VertexAttrib3fvARB (will be remapped) */
   "ip\0"
   "glVertexAttrib3fv\0"
   "glVertexAttrib3fvARB\0"
   "\0"
   /* _mesa_function_pool[16477]: VertexAttrib3s (will be remapped) */
   "iiii\0"
   "glVertexAttrib3s\0"
   "glVertexAttrib3sARB\0"
   "\0"
   /* _mesa_function_pool[16520]: VertexAttrib3sv (will be remapped) */
   "ip\0"
   "glVertexAttrib3sv\0"
   "glVertexAttrib3svARB\0"
   "\0"
   /* _mesa_function_pool[16563]: VertexAttrib4Nbv (will be remapped) */
   "ip\0"
   "glVertexAttrib4Nbv\0"
   "glVertexAttrib4NbvARB\0"
   "\0"
   /* _mesa_function_pool[16608]: VertexAttrib4Niv (will be remapped) */
   "ip\0"
   "glVertexAttrib4Niv\0"
   "glVertexAttrib4NivARB\0"
   "\0"
   /* _mesa_function_pool[16653]: VertexAttrib4Nsv (will be remapped) */
   "ip\0"
   "glVertexAttrib4Nsv\0"
   "glVertexAttrib4NsvARB\0"
   "\0"
   /* _mesa_function_pool[16698]: VertexAttrib4Nub (will be remapped) */
   "iiiii\0"
   "glVertexAttrib4Nub\0"
   "glVertexAttrib4NubARB\0"
   "\0"
   /* _mesa_function_pool[16746]: VertexAttrib4Nubv (will be remapped) */
   "ip\0"
   "glVertexAttrib4Nubv\0"
   "glVertexAttrib4NubvARB\0"
   "\0"
   /* _mesa_function_pool[16793]: VertexAttrib4Nuiv (will be remapped) */
   "ip\0"
   "glVertexAttrib4Nuiv\0"
   "glVertexAttrib4NuivARB\0"
   "\0"
   /* _mesa_function_pool[16840]: VertexAttrib4Nusv (will be remapped) */
   "ip\0"
   "glVertexAttrib4Nusv\0"
   "glVertexAttrib4NusvARB\0"
   "\0"
   /* _mesa_function_pool[16887]: VertexAttrib4bv (will be remapped) */
   "ip\0"
   "glVertexAttrib4bv\0"
   "glVertexAttrib4bvARB\0"
   "\0"
   /* _mesa_function_pool[16930]: VertexAttrib4d (will be remapped) */
   "idddd\0"
   "glVertexAttrib4d\0"
   "glVertexAttrib4dARB\0"
   "\0"
   /* _mesa_function_pool[16974]: VertexAttrib4dv (will be remapped) */
   "ip\0"
   "glVertexAttrib4dv\0"
   "glVertexAttrib4dvARB\0"
   "\0"
   /* _mesa_function_pool[17017]: VertexAttrib4fARB (will be remapped) */
   "iffff\0"
   "glVertexAttrib4f\0"
   "glVertexAttrib4fARB\0"
   "\0"
   /* _mesa_function_pool[17061]: VertexAttrib4fvARB (will be remapped) */
   "ip\0"
   "glVertexAttrib4fv\0"
   "glVertexAttrib4fvARB\0"
   "\0"
   /* _mesa_function_pool[17104]: VertexAttrib4iv (will be remapped) */
   "ip\0"
   "glVertexAttrib4iv\0"
   "glVertexAttrib4ivARB\0"
   "\0"
   /* _mesa_function_pool[17147]: VertexAttrib4s (will be remapped) */
   "iiiii\0"
   "glVertexAttrib4s\0"
   "glVertexAttrib4sARB\0"
   "\0"
   /* _mesa_function_pool[17191]: VertexAttrib4sv (will be remapped) */
   "ip\0"
   "glVertexAttrib4sv\0"
   "glVertexAttrib4svARB\0"
   "\0"
   /* _mesa_function_pool[17234]: VertexAttrib4ubv (will be remapped) */
   "ip\0"
   "glVertexAttrib4ubv\0"
   "glVertexAttrib4ubvARB\0"
   "\0"
   /* _mesa_function_pool[17279]: VertexAttrib4uiv (will be remapped) */
   "ip\0"
   "glVertexAttrib4uiv\0"
   "glVertexAttrib4uivARB\0"
   "\0"
   /* _mesa_function_pool[17324]: VertexAttrib4usv (will be remapped) */
   "ip\0"
   "glVertexAttrib4usv\0"
   "glVertexAttrib4usvARB\0"
   "\0"
   /* _mesa_function_pool[17369]: VertexAttribPointer (will be remapped) */
   "iiiiip\0"
   "glVertexAttribPointer\0"
   "glVertexAttribPointerARB\0"
   "\0"
   /* _mesa_function_pool[17424]: UniformMatrix2x3fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix2x3fv\0"
   "\0"
   /* _mesa_function_pool[17451]: UniformMatrix3x2fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix3x2fv\0"
   "\0"
   /* _mesa_function_pool[17478]: UniformMatrix2x4fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix2x4fv\0"
   "\0"
   /* _mesa_function_pool[17505]: UniformMatrix4x2fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix4x2fv\0"
   "\0"
   /* _mesa_function_pool[17532]: UniformMatrix3x4fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix3x4fv\0"
   "\0"
   /* _mesa_function_pool[17559]: UniformMatrix4x3fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix4x3fv\0"
   "\0"
   /* _mesa_function_pool[17586]: WeightbvARB (dynamic) */
   "ip\0"
   "glWeightbvARB\0"
   "\0"
   /* _mesa_function_pool[17604]: WeightsvARB (dynamic) */
   "ip\0"
   "glWeightsvARB\0"
   "\0"
   /* _mesa_function_pool[17622]: WeightivARB (dynamic) */
   "ip\0"
   "glWeightivARB\0"
   "\0"
   /* _mesa_function_pool[17640]: WeightfvARB (dynamic) */
   "ip\0"
   "glWeightfvARB\0"
   "\0"
   /* _mesa_function_pool[17658]: WeightdvARB (dynamic) */
   "ip\0"
   "glWeightdvARB\0"
   "\0"
   /* _mesa_function_pool[17676]: WeightubvARB (dynamic) */
   "ip\0"
   "glWeightubvARB\0"
   "\0"
   /* _mesa_function_pool[17695]: WeightusvARB (dynamic) */
   "ip\0"
   "glWeightusvARB\0"
   "\0"
   /* _mesa_function_pool[17714]: WeightuivARB (dynamic) */
   "ip\0"
   "glWeightuivARB\0"
   "\0"
   /* _mesa_function_pool[17733]: WeightPointerARB (dynamic) */
   "iiip\0"
   "glWeightPointerARB\0"
   "glWeightPointerOES\0"
   "\0"
   /* _mesa_function_pool[17777]: VertexBlendARB (dynamic) */
   "i\0"
   "glVertexBlendARB\0"
   "\0"
   /* _mesa_function_pool[17797]: CurrentPaletteMatrixARB (dynamic) */
   "i\0"
   "glCurrentPaletteMatrixARB\0"
   "glCurrentPaletteMatrixOES\0"
   "\0"
   /* _mesa_function_pool[17852]: MatrixIndexubvARB (dynamic) */
   "ip\0"
   "glMatrixIndexubvARB\0"
   "\0"
   /* _mesa_function_pool[17876]: MatrixIndexusvARB (dynamic) */
   "ip\0"
   "glMatrixIndexusvARB\0"
   "\0"
   /* _mesa_function_pool[17900]: MatrixIndexuivARB (dynamic) */
   "ip\0"
   "glMatrixIndexuivARB\0"
   "\0"
   /* _mesa_function_pool[17924]: MatrixIndexPointerARB (dynamic) */
   "iiip\0"
   "glMatrixIndexPointerARB\0"
   "glMatrixIndexPointerOES\0"
   "\0"
   /* _mesa_function_pool[17978]: ProgramStringARB (will be remapped) */
   "iiip\0"
   "glProgramStringARB\0"
   "\0"
   /* _mesa_function_pool[18003]: BindProgramARB (will be remapped) */
   "ii\0"
   "glBindProgramARB\0"
   "glBindProgramNV\0"
   "\0"
   /* _mesa_function_pool[18040]: DeleteProgramsARB (will be remapped) */
   "ip\0"
   "glDeleteProgramsARB\0"
   "glDeleteProgramsNV\0"
   "\0"
   /* _mesa_function_pool[18083]: GenProgramsARB (will be remapped) */
   "ip\0"
   "glGenProgramsARB\0"
   "glGenProgramsNV\0"
   "\0"
   /* _mesa_function_pool[18120]: IsProgramARB (will be remapped) */
   "i\0"
   "glIsProgramARB\0"
   "glIsProgramNV\0"
   "\0"
   /* _mesa_function_pool[18152]: ProgramEnvParameter4dARB (will be remapped) */
   "iidddd\0"
   "glProgramEnvParameter4dARB\0"
   "glProgramParameter4dNV\0"
   "\0"
   /* _mesa_function_pool[18210]: ProgramEnvParameter4dvARB (will be remapped) */
   "iip\0"
   "glProgramEnvParameter4dvARB\0"
   "glProgramParameter4dvNV\0"
   "\0"
   /* _mesa_function_pool[18267]: ProgramEnvParameter4fARB (will be remapped) */
   "iiffff\0"
   "glProgramEnvParameter4fARB\0"
   "glProgramParameter4fNV\0"
   "\0"
   /* _mesa_function_pool[18325]: ProgramEnvParameter4fvARB (will be remapped) */
   "iip\0"
   "glProgramEnvParameter4fvARB\0"
   "glProgramParameter4fvNV\0"
   "\0"
   /* _mesa_function_pool[18382]: ProgramLocalParameter4dARB (will be remapped) */
   "iidddd\0"
   "glProgramLocalParameter4dARB\0"
   "\0"
   /* _mesa_function_pool[18419]: ProgramLocalParameter4dvARB (will be remapped) */
   "iip\0"
   "glProgramLocalParameter4dvARB\0"
   "\0"
   /* _mesa_function_pool[18454]: ProgramLocalParameter4fARB (will be remapped) */
   "iiffff\0"
   "glProgramLocalParameter4fARB\0"
   "\0"
   /* _mesa_function_pool[18491]: ProgramLocalParameter4fvARB (will be remapped) */
   "iip\0"
   "glProgramLocalParameter4fvARB\0"
   "\0"
   /* _mesa_function_pool[18526]: GetProgramEnvParameterdvARB (will be remapped) */
   "iip\0"
   "glGetProgramEnvParameterdvARB\0"
   "\0"
   /* _mesa_function_pool[18561]: GetProgramEnvParameterfvARB (will be remapped) */
   "iip\0"
   "glGetProgramEnvParameterfvARB\0"
   "\0"
   /* _mesa_function_pool[18596]: GetProgramLocalParameterdvARB (will be remapped) */
   "iip\0"
   "glGetProgramLocalParameterdvARB\0"
   "\0"
   /* _mesa_function_pool[18633]: GetProgramLocalParameterfvARB (will be remapped) */
   "iip\0"
   "glGetProgramLocalParameterfvARB\0"
   "\0"
   /* _mesa_function_pool[18670]: GetProgramivARB (will be remapped) */
   "iip\0"
   "glGetProgramivARB\0"
   "\0"
   /* _mesa_function_pool[18693]: GetProgramStringARB (will be remapped) */
   "iip\0"
   "glGetProgramStringARB\0"
   "\0"
   /* _mesa_function_pool[18720]: DeleteObjectARB (will be remapped) */
   "i\0"
   "glDeleteObjectARB\0"
   "\0"
   /* _mesa_function_pool[18741]: GetHandleARB (will be remapped) */
   "i\0"
   "glGetHandleARB\0"
   "\0"
   /* _mesa_function_pool[18759]: DetachObjectARB (will be remapped) */
   "ii\0"
   "glDetachObjectARB\0"
   "\0"
   /* _mesa_function_pool[18781]: CreateShaderObjectARB (will be remapped) */
   "i\0"
   "glCreateShaderObjectARB\0"
   "\0"
   /* _mesa_function_pool[18808]: CreateProgramObjectARB (will be remapped) */
   "\0"
   "glCreateProgramObjectARB\0"
   "\0"
   /* _mesa_function_pool[18835]: AttachObjectARB (will be remapped) */
   "ii\0"
   "glAttachObjectARB\0"
   "\0"
   /* _mesa_function_pool[18857]: GetObjectParameterfvARB (will be remapped) */
   "iip\0"
   "glGetObjectParameterfvARB\0"
   "\0"
   /* _mesa_function_pool[18888]: GetObjectParameterivARB (will be remapped) */
   "iip\0"
   "glGetObjectParameterivARB\0"
   "\0"
   /* _mesa_function_pool[18919]: GetInfoLogARB (will be remapped) */
   "iipp\0"
   "glGetInfoLogARB\0"
   "\0"
   /* _mesa_function_pool[18941]: GetAttachedObjectsARB (will be remapped) */
   "iipp\0"
   "glGetAttachedObjectsARB\0"
   "\0"
   /* _mesa_function_pool[18971]: ClampColor (will be remapped) */
   "ii\0"
   "glClampColorARB\0"
   "glClampColor\0"
   "\0"
   /* _mesa_function_pool[19004]: DrawArraysInstancedARB (will be remapped) */
   "iiii\0"
   "glDrawArraysInstancedARB\0"
   "glDrawArraysInstancedEXT\0"
   "glDrawArraysInstanced\0"
   "\0"
   /* _mesa_function_pool[19082]: DrawElementsInstancedARB (will be remapped) */
   "iiipi\0"
   "glDrawElementsInstancedARB\0"
   "glDrawElementsInstancedEXT\0"
   "glDrawElementsInstanced\0"
   "\0"
   /* _mesa_function_pool[19167]: IsRenderbuffer (will be remapped) */
   "i\0"
   "glIsRenderbuffer\0"
   "glIsRenderbufferEXT\0"
   "glIsRenderbufferOES\0"
   "\0"
   /* _mesa_function_pool[19227]: BindRenderbuffer (will be remapped) */
   "ii\0"
   "glBindRenderbuffer\0"
   "glBindRenderbufferOES\0"
   "\0"
   /* _mesa_function_pool[19272]: DeleteRenderbuffers (will be remapped) */
   "ip\0"
   "glDeleteRenderbuffers\0"
   "glDeleteRenderbuffersEXT\0"
   "glDeleteRenderbuffersOES\0"
   "\0"
   /* _mesa_function_pool[19348]: GenRenderbuffers (will be remapped) */
   "ip\0"
   "glGenRenderbuffers\0"
   "glGenRenderbuffersEXT\0"
   "glGenRenderbuffersOES\0"
   "\0"
   /* _mesa_function_pool[19415]: RenderbufferStorage (will be remapped) */
   "iiii\0"
   "glRenderbufferStorage\0"
   "glRenderbufferStorageEXT\0"
   "glRenderbufferStorageOES\0"
   "\0"
   /* _mesa_function_pool[19493]: RenderbufferStorageMultisample (will be remapped) */
   "iiiii\0"
   "glRenderbufferStorageMultisample\0"
   "glRenderbufferStorageMultisampleEXT\0"
   "\0"
   /* _mesa_function_pool[19569]: GetRenderbufferParameteriv (will be remapped) */
   "iip\0"
   "glGetRenderbufferParameteriv\0"
   "glGetRenderbufferParameterivEXT\0"
   "glGetRenderbufferParameterivOES\0"
   "\0"
   /* _mesa_function_pool[19667]: IsFramebuffer (will be remapped) */
   "i\0"
   "glIsFramebuffer\0"
   "glIsFramebufferEXT\0"
   "glIsFramebufferOES\0"
   "\0"
   /* _mesa_function_pool[19724]: BindFramebuffer (will be remapped) */
   "ii\0"
   "glBindFramebuffer\0"
   "glBindFramebufferOES\0"
   "\0"
   /* _mesa_function_pool[19767]: DeleteFramebuffers (will be remapped) */
   "ip\0"
   "glDeleteFramebuffers\0"
   "glDeleteFramebuffersEXT\0"
   "glDeleteFramebuffersOES\0"
   "\0"
   /* _mesa_function_pool[19840]: GenFramebuffers (will be remapped) */
   "ip\0"
   "glGenFramebuffers\0"
   "glGenFramebuffersEXT\0"
   "glGenFramebuffersOES\0"
   "\0"
   /* _mesa_function_pool[19904]: CheckFramebufferStatus (will be remapped) */
   "i\0"
   "glCheckFramebufferStatus\0"
   "glCheckFramebufferStatusEXT\0"
   "glCheckFramebufferStatusOES\0"
   "\0"
   /* _mesa_function_pool[19988]: FramebufferTexture1D (will be remapped) */
   "iiiii\0"
   "glFramebufferTexture1D\0"
   "glFramebufferTexture1DEXT\0"
   "\0"
   /* _mesa_function_pool[20044]: FramebufferTexture2D (will be remapped) */
   "iiiii\0"
   "glFramebufferTexture2D\0"
   "glFramebufferTexture2DEXT\0"
   "glFramebufferTexture2DOES\0"
   "\0"
   /* _mesa_function_pool[20126]: FramebufferTexture3D (will be remapped) */
   "iiiiii\0"
   "glFramebufferTexture3D\0"
   "glFramebufferTexture3DEXT\0"
   "glFramebufferTexture3DOES\0"
   "\0"
   /* _mesa_function_pool[20209]: FramebufferTextureLayer (will be remapped) */
   "iiiii\0"
   "glFramebufferTextureLayer\0"
   "glFramebufferTextureLayerEXT\0"
   "\0"
   /* _mesa_function_pool[20271]: FramebufferRenderbuffer (will be remapped) */
   "iiii\0"
   "glFramebufferRenderbuffer\0"
   "glFramebufferRenderbufferEXT\0"
   "glFramebufferRenderbufferOES\0"
   "\0"
   /* _mesa_function_pool[20361]: GetFramebufferAttachmentParameteriv (will be remapped) */
   "iiip\0"
   "glGetFramebufferAttachmentParameteriv\0"
   "glGetFramebufferAttachmentParameterivEXT\0"
   "glGetFramebufferAttachmentParameterivOES\0"
   "\0"
   /* _mesa_function_pool[20487]: BlitFramebuffer (will be remapped) */
   "iiiiiiiiii\0"
   "glBlitFramebuffer\0"
   "glBlitFramebufferEXT\0"
   "\0"
   /* _mesa_function_pool[20538]: GenerateMipmap (will be remapped) */
   "i\0"
   "glGenerateMipmap\0"
   "glGenerateMipmapEXT\0"
   "glGenerateMipmapOES\0"
   "\0"
   /* _mesa_function_pool[20598]: VertexAttribDivisor (will be remapped) */
   "ii\0"
   "glVertexAttribDivisorARB\0"
   "glVertexAttribDivisor\0"
   "\0"
   /* _mesa_function_pool[20649]: VertexArrayVertexAttribDivisorEXT (will be remapped) */
   "iii\0"
   "glVertexArrayVertexAttribDivisorEXT\0"
   "\0"
   /* _mesa_function_pool[20690]: MapBufferRange (will be remapped) */
   "iiii\0"
   "glMapBufferRange\0"
   "glMapBufferRangeEXT\0"
   "\0"
   /* _mesa_function_pool[20733]: FlushMappedBufferRange (will be remapped) */
   "iii\0"
   "glFlushMappedBufferRange\0"
   "glFlushMappedBufferRangeEXT\0"
   "\0"
   /* _mesa_function_pool[20791]: TexBuffer (will be remapped) */
   "iii\0"
   "glTexBufferARB\0"
   "glTexBuffer\0"
   "glTexBufferEXT\0"
   "glTexBufferOES\0"
   "\0"
   /* _mesa_function_pool[20853]: BindVertexArray (will be remapped) */
   "i\0"
   "glBindVertexArray\0"
   "glBindVertexArrayOES\0"
   "\0"
   /* _mesa_function_pool[20895]: DeleteVertexArrays (will be remapped) */
   "ip\0"
   "glDeleteVertexArrays\0"
   "glDeleteVertexArraysOES\0"
   "\0"
   /* _mesa_function_pool[20944]: GenVertexArrays (will be remapped) */
   "ip\0"
   "glGenVertexArrays\0"
   "glGenVertexArraysOES\0"
   "\0"
   /* _mesa_function_pool[20987]: IsVertexArray (will be remapped) */
   "i\0"
   "glIsVertexArray\0"
   "glIsVertexArrayOES\0"
   "\0"
   /* _mesa_function_pool[21025]: GetUniformIndices (will be remapped) */
   "iipp\0"
   "glGetUniformIndices\0"
   "\0"
   /* _mesa_function_pool[21051]: GetActiveUniformsiv (will be remapped) */
   "iipip\0"
   "glGetActiveUniformsiv\0"
   "\0"
   /* _mesa_function_pool[21080]: GetActiveUniformName (will be remapped) */
   "iiipp\0"
   "glGetActiveUniformName\0"
   "\0"
   /* _mesa_function_pool[21110]: GetUniformBlockIndex (will be remapped) */
   "ip\0"
   "glGetUniformBlockIndex\0"
   "\0"
   /* _mesa_function_pool[21137]: GetActiveUniformBlockiv (will be remapped) */
   "iiip\0"
   "glGetActiveUniformBlockiv\0"
   "\0"
   /* _mesa_function_pool[21169]: GetActiveUniformBlockName (will be remapped) */
   "iiipp\0"
   "glGetActiveUniformBlockName\0"
   "\0"
   /* _mesa_function_pool[21204]: UniformBlockBinding (will be remapped) */
   "iii\0"
   "glUniformBlockBinding\0"
   "\0"
   /* _mesa_function_pool[21231]: CopyBufferSubData (will be remapped) */
   "iiiii\0"
   "glCopyBufferSubData\0"
   "\0"
   /* _mesa_function_pool[21258]: DrawElementsBaseVertex (will be remapped) */
   "iiipi\0"
   "glDrawElementsBaseVertex\0"
   "glDrawElementsBaseVertexEXT\0"
   "glDrawElementsBaseVertexOES\0"
   "\0"
   /* _mesa_function_pool[21346]: DrawRangeElementsBaseVertex (will be remapped) */
   "iiiiipi\0"
   "glDrawRangeElementsBaseVertex\0"
   "glDrawRangeElementsBaseVertexEXT\0"
   "glDrawRangeElementsBaseVertexOES\0"
   "\0"
   /* _mesa_function_pool[21451]: MultiDrawElementsBaseVertex (will be remapped) */
   "ipipip\0"
   "glMultiDrawElementsBaseVertex\0"
   "glMultiDrawElementsBaseVertexEXT\0"
   "\0"
   /* _mesa_function_pool[21522]: DrawElementsInstancedBaseVertex (will be remapped) */
   "iiipii\0"
   "glDrawElementsInstancedBaseVertex\0"
   "glDrawElementsInstancedBaseVertexEXT\0"
   "glDrawElementsInstancedBaseVertexOES\0"
   "\0"
   /* _mesa_function_pool[21638]: FenceSync (will be remapped) */
   "ii\0"
   "glFenceSync\0"
   "\0"
   /* _mesa_function_pool[21654]: IsSync (will be remapped) */
   "i\0"
   "glIsSync\0"
   "\0"
   /* _mesa_function_pool[21666]: DeleteSync (will be remapped) */
   "i\0"
   "glDeleteSync\0"
   "\0"
   /* _mesa_function_pool[21682]: ClientWaitSync (will be remapped) */
   "iii\0"
   "glClientWaitSync\0"
   "\0"
   /* _mesa_function_pool[21704]: WaitSync (will be remapped) */
   "iii\0"
   "glWaitSync\0"
   "\0"
   /* _mesa_function_pool[21720]: GetInteger64v (will be remapped) */
   "ip\0"
   "glGetInteger64v\0"
   "glGetInteger64vEXT\0"
   "\0"
   /* _mesa_function_pool[21759]: GetSynciv (will be remapped) */
   "iiipp\0"
   "glGetSynciv\0"
   "\0"
   /* _mesa_function_pool[21778]: TexImage2DMultisample (will be remapped) */
   "iiiiii\0"
   "glTexImage2DMultisample\0"
   "\0"
   /* _mesa_function_pool[21810]: TexImage3DMultisample (will be remapped) */
   "iiiiiii\0"
   "glTexImage3DMultisample\0"
   "\0"
   /* _mesa_function_pool[21843]: GetMultisamplefv (will be remapped) */
   "iip\0"
   "glGetMultisamplefv\0"
   "\0"
   /* _mesa_function_pool[21867]: SampleMaski (will be remapped) */
   "ii\0"
   "glSampleMaski\0"
   "\0"
   /* _mesa_function_pool[21885]: BlendEquationiARB (will be remapped) */
   "ii\0"
   "glBlendEquationiARB\0"
   "glBlendEquationIndexedAMD\0"
   "glBlendEquationi\0"
   "glBlendEquationiEXT\0"
   "glBlendEquationiOES\0"
   "\0"
   /* _mesa_function_pool[21992]: BlendEquationSeparateiARB (will be remapped) */
   "iii\0"
   "glBlendEquationSeparateiARB\0"
   "glBlendEquationSeparateIndexedAMD\0"
   "glBlendEquationSeparatei\0"
   "glBlendEquationSeparateiEXT\0"
   "glBlendEquationSeparateiOES\0"
   "\0"
   /* _mesa_function_pool[22140]: BlendFunciARB (will be remapped) */
   "iii\0"
   "glBlendFunciARB\0"
   "glBlendFuncIndexedAMD\0"
   "glBlendFunci\0"
   "glBlendFunciEXT\0"
   "glBlendFunciOES\0"
   "\0"
   /* _mesa_function_pool[22228]: BlendFuncSeparateiARB (will be remapped) */
   "iiiii\0"
   "glBlendFuncSeparateiARB\0"
   "glBlendFuncSeparateIndexedAMD\0"
   "glBlendFuncSeparatei\0"
   "glBlendFuncSeparateiEXT\0"
   "glBlendFuncSeparateiOES\0"
   "\0"
   /* _mesa_function_pool[22358]: MinSampleShading (will be remapped) */
   "f\0"
   "glMinSampleShadingARB\0"
   "glMinSampleShading\0"
   "glMinSampleShadingOES\0"
   "\0"
   /* _mesa_function_pool[22424]: NamedStringARB (will be remapped) */
   "iipip\0"
   "glNamedStringARB\0"
   "\0"
   /* _mesa_function_pool[22448]: DeleteNamedStringARB (will be remapped) */
   "ip\0"
   "glDeleteNamedStringARB\0"
   "\0"
   /* _mesa_function_pool[22475]: CompileShaderIncludeARB (will be remapped) */
   "iipp\0"
   "glCompileShaderIncludeARB\0"
   "\0"
   /* _mesa_function_pool[22507]: IsNamedStringARB (will be remapped) */
   "ip\0"
   "glIsNamedStringARB\0"
   "\0"
   /* _mesa_function_pool[22530]: GetNamedStringARB (will be remapped) */
   "ipipp\0"
   "glGetNamedStringARB\0"
   "\0"
   /* _mesa_function_pool[22557]: GetNamedStringivARB (will be remapped) */
   "ipip\0"
   "glGetNamedStringivARB\0"
   "\0"
   /* _mesa_function_pool[22585]: BindFragDataLocationIndexed (will be remapped) */
   "iiip\0"
   "glBindFragDataLocationIndexed\0"
   "glBindFragDataLocationIndexedEXT\0"
   "\0"
   /* _mesa_function_pool[22654]: GetFragDataIndex (will be remapped) */
   "ip\0"
   "glGetFragDataIndex\0"
   "glGetFragDataIndexEXT\0"
   "\0"
   /* _mesa_function_pool[22699]: GenSamplers (will be remapped) */
   "ip\0"
   "glGenSamplers\0"
   "\0"
   /* _mesa_function_pool[22717]: DeleteSamplers (will be remapped) */
   "ip\0"
   "glDeleteSamplers\0"
   "\0"
   /* _mesa_function_pool[22738]: IsSampler (will be remapped) */
   "i\0"
   "glIsSampler\0"
   "\0"
   /* _mesa_function_pool[22753]: BindSampler (will be remapped) */
   "ii\0"
   "glBindSampler\0"
   "\0"
   /* _mesa_function_pool[22771]: SamplerParameteri (will be remapped) */
   "iii\0"
   "glSamplerParameteri\0"
   "\0"
   /* _mesa_function_pool[22796]: SamplerParameterf (will be remapped) */
   "iif\0"
   "glSamplerParameterf\0"
   "\0"
   /* _mesa_function_pool[22821]: SamplerParameteriv (will be remapped) */
   "iip\0"
   "glSamplerParameteriv\0"
   "\0"
   /* _mesa_function_pool[22847]: SamplerParameterfv (will be remapped) */
   "iip\0"
   "glSamplerParameterfv\0"
   "\0"
   /* _mesa_function_pool[22873]: SamplerParameterIiv (will be remapped) */
   "iip\0"
   "glSamplerParameterIiv\0"
   "glSamplerParameterIivEXT\0"
   "glSamplerParameterIivOES\0"
   "\0"
   /* _mesa_function_pool[22950]: SamplerParameterIuiv (will be remapped) */
   "iip\0"
   "glSamplerParameterIuiv\0"
   "glSamplerParameterIuivEXT\0"
   "glSamplerParameterIuivOES\0"
   "\0"
   /* _mesa_function_pool[23030]: GetSamplerParameteriv (will be remapped) */
   "iip\0"
   "glGetSamplerParameteriv\0"
   "\0"
   /* _mesa_function_pool[23059]: GetSamplerParameterfv (will be remapped) */
   "iip\0"
   "glGetSamplerParameterfv\0"
   "\0"
   /* _mesa_function_pool[23088]: GetSamplerParameterIiv (will be remapped) */
   "iip\0"
   "glGetSamplerParameterIiv\0"
   "glGetSamplerParameterIivEXT\0"
   "glGetSamplerParameterIivOES\0"
   "\0"
   /* _mesa_function_pool[23174]: GetSamplerParameterIuiv (will be remapped) */
   "iip\0"
   "glGetSamplerParameterIuiv\0"
   "glGetSamplerParameterIuivEXT\0"
   "glGetSamplerParameterIuivOES\0"
   "\0"
   /* _mesa_function_pool[23263]: GetQueryObjecti64v (will be remapped) */
   "iip\0"
   "glGetQueryObjecti64v\0"
   "glGetQueryObjecti64vEXT\0"
   "\0"
   /* _mesa_function_pool[23313]: GetQueryObjectui64v (will be remapped) */
   "iip\0"
   "glGetQueryObjectui64v\0"
   "glGetQueryObjectui64vEXT\0"
   "\0"
   /* _mesa_function_pool[23365]: QueryCounter (will be remapped) */
   "ii\0"
   "glQueryCounter\0"
   "glQueryCounterEXT\0"
   "\0"
   /* _mesa_function_pool[23402]: VertexP2ui (will be remapped) */
   "ii\0"
   "glVertexP2ui\0"
   "\0"
   /* _mesa_function_pool[23419]: VertexP3ui (will be remapped) */
   "ii\0"
   "glVertexP3ui\0"
   "\0"
   /* _mesa_function_pool[23436]: VertexP4ui (will be remapped) */
   "ii\0"
   "glVertexP4ui\0"
   "\0"
   /* _mesa_function_pool[23453]: VertexP2uiv (will be remapped) */
   "ip\0"
   "glVertexP2uiv\0"
   "\0"
   /* _mesa_function_pool[23471]: VertexP3uiv (will be remapped) */
   "ip\0"
   "glVertexP3uiv\0"
   "\0"
   /* _mesa_function_pool[23489]: VertexP4uiv (will be remapped) */
   "ip\0"
   "glVertexP4uiv\0"
   "\0"
   /* _mesa_function_pool[23507]: TexCoordP1ui (will be remapped) */
   "ii\0"
   "glTexCoordP1ui\0"
   "\0"
   /* _mesa_function_pool[23526]: TexCoordP2ui (will be remapped) */
   "ii\0"
   "glTexCoordP2ui\0"
   "\0"
   /* _mesa_function_pool[23545]: TexCoordP3ui (will be remapped) */
   "ii\0"
   "glTexCoordP3ui\0"
   "\0"
   /* _mesa_function_pool[23564]: TexCoordP4ui (will be remapped) */
   "ii\0"
   "glTexCoordP4ui\0"
   "\0"
   /* _mesa_function_pool[23583]: TexCoordP1uiv (will be remapped) */
   "ip\0"
   "glTexCoordP1uiv\0"
   "\0"
   /* _mesa_function_pool[23603]: TexCoordP2uiv (will be remapped) */
   "ip\0"
   "glTexCoordP2uiv\0"
   "\0"
   /* _mesa_function_pool[23623]: TexCoordP3uiv (will be remapped) */
   "ip\0"
   "glTexCoordP3uiv\0"
   "\0"
   /* _mesa_function_pool[23643]: TexCoordP4uiv (will be remapped) */
   "ip\0"
   "glTexCoordP4uiv\0"
   "\0"
   /* _mesa_function_pool[23663]: MultiTexCoordP1ui (will be remapped) */
   "iii\0"
   "glMultiTexCoordP1ui\0"
   "\0"
   /* _mesa_function_pool[23688]: MultiTexCoordP2ui (will be remapped) */
   "iii\0"
   "glMultiTexCoordP2ui\0"
   "\0"
   /* _mesa_function_pool[23713]: MultiTexCoordP3ui (will be remapped) */
   "iii\0"
   "glMultiTexCoordP3ui\0"
   "\0"
   /* _mesa_function_pool[23738]: MultiTexCoordP4ui (will be remapped) */
   "iii\0"
   "glMultiTexCoordP4ui\0"
   "\0"
   /* _mesa_function_pool[23763]: MultiTexCoordP1uiv (will be remapped) */
   "iip\0"
   "glMultiTexCoordP1uiv\0"
   "\0"
   /* _mesa_function_pool[23789]: MultiTexCoordP2uiv (will be remapped) */
   "iip\0"
   "glMultiTexCoordP2uiv\0"
   "\0"
   /* _mesa_function_pool[23815]: MultiTexCoordP3uiv (will be remapped) */
   "iip\0"
   "glMultiTexCoordP3uiv\0"
   "\0"
   /* _mesa_function_pool[23841]: MultiTexCoordP4uiv (will be remapped) */
   "iip\0"
   "glMultiTexCoordP4uiv\0"
   "\0"
   /* _mesa_function_pool[23867]: NormalP3ui (will be remapped) */
   "ii\0"
   "glNormalP3ui\0"
   "\0"
   /* _mesa_function_pool[23884]: NormalP3uiv (will be remapped) */
   "ip\0"
   "glNormalP3uiv\0"
   "\0"
   /* _mesa_function_pool[23902]: ColorP3ui (will be remapped) */
   "ii\0"
   "glColorP3ui\0"
   "\0"
   /* _mesa_function_pool[23918]: ColorP4ui (will be remapped) */
   "ii\0"
   "glColorP4ui\0"
   "\0"
   /* _mesa_function_pool[23934]: ColorP3uiv (will be remapped) */
   "ip\0"
   "glColorP3uiv\0"
   "\0"
   /* _mesa_function_pool[23951]: ColorP4uiv (will be remapped) */
   "ip\0"
   "glColorP4uiv\0"
   "\0"
   /* _mesa_function_pool[23968]: SecondaryColorP3ui (will be remapped) */
   "ii\0"
   "glSecondaryColorP3ui\0"
   "\0"
   /* _mesa_function_pool[23993]: SecondaryColorP3uiv (will be remapped) */
   "ip\0"
   "glSecondaryColorP3uiv\0"
   "\0"
   /* _mesa_function_pool[24019]: VertexAttribP1ui (will be remapped) */
   "iiii\0"
   "glVertexAttribP1ui\0"
   "\0"
   /* _mesa_function_pool[24044]: VertexAttribP2ui (will be remapped) */
   "iiii\0"
   "glVertexAttribP2ui\0"
   "\0"
   /* _mesa_function_pool[24069]: VertexAttribP3ui (will be remapped) */
   "iiii\0"
   "glVertexAttribP3ui\0"
   "\0"
   /* _mesa_function_pool[24094]: VertexAttribP4ui (will be remapped) */
   "iiii\0"
   "glVertexAttribP4ui\0"
   "\0"
   /* _mesa_function_pool[24119]: VertexAttribP1uiv (will be remapped) */
   "iiip\0"
   "glVertexAttribP1uiv\0"
   "\0"
   /* _mesa_function_pool[24145]: VertexAttribP2uiv (will be remapped) */
   "iiip\0"
   "glVertexAttribP2uiv\0"
   "\0"
   /* _mesa_function_pool[24171]: VertexAttribP3uiv (will be remapped) */
   "iiip\0"
   "glVertexAttribP3uiv\0"
   "\0"
   /* _mesa_function_pool[24197]: VertexAttribP4uiv (will be remapped) */
   "iiip\0"
   "glVertexAttribP4uiv\0"
   "\0"
   /* _mesa_function_pool[24223]: GetSubroutineUniformLocation (will be remapped) */
   "iip\0"
   "glGetSubroutineUniformLocation\0"
   "\0"
   /* _mesa_function_pool[24259]: GetSubroutineIndex (will be remapped) */
   "iip\0"
   "glGetSubroutineIndex\0"
   "\0"
   /* _mesa_function_pool[24285]: GetActiveSubroutineUniformiv (will be remapped) */
   "iiiip\0"
   "glGetActiveSubroutineUniformiv\0"
   "\0"
   /* _mesa_function_pool[24323]: GetActiveSubroutineUniformName (will be remapped) */
   "iiiipp\0"
   "glGetActiveSubroutineUniformName\0"
   "\0"
   /* _mesa_function_pool[24364]: GetActiveSubroutineName (will be remapped) */
   "iiiipp\0"
   "glGetActiveSubroutineName\0"
   "\0"
   /* _mesa_function_pool[24398]: UniformSubroutinesuiv (will be remapped) */
   "iip\0"
   "glUniformSubroutinesuiv\0"
   "\0"
   /* _mesa_function_pool[24427]: GetUniformSubroutineuiv (will be remapped) */
   "iip\0"
   "glGetUniformSubroutineuiv\0"
   "\0"
   /* _mesa_function_pool[24458]: GetProgramStageiv (will be remapped) */
   "iiip\0"
   "glGetProgramStageiv\0"
   "\0"
   /* _mesa_function_pool[24484]: PatchParameteri (will be remapped) */
   "ii\0"
   "glPatchParameteri\0"
   "glPatchParameteriEXT\0"
   "glPatchParameteriOES\0"
   "\0"
   /* _mesa_function_pool[24548]: PatchParameterfv (will be remapped) */
   "ip\0"
   "glPatchParameterfv\0"
   "\0"
   /* _mesa_function_pool[24571]: DrawArraysIndirect (will be remapped) */
   "ip\0"
   "glDrawArraysIndirect\0"
   "\0"
   /* _mesa_function_pool[24596]: DrawElementsIndirect (will be remapped) */
   "iip\0"
   "glDrawElementsIndirect\0"
   "\0"
   /* _mesa_function_pool[24624]: MultiDrawArraysIndirect (will be remapped) */
   "ipii\0"
   "glMultiDrawArraysIndirect\0"
   "glMultiDrawArraysIndirectAMD\0"
   "\0"
   /* _mesa_function_pool[24685]: MultiDrawElementsIndirect (will be remapped) */
   "iipii\0"
   "glMultiDrawElementsIndirect\0"
   "glMultiDrawElementsIndirectAMD\0"
   "\0"
   /* _mesa_function_pool[24751]: Uniform1d (will be remapped) */
   "id\0"
   "glUniform1d\0"
   "\0"
   /* _mesa_function_pool[24767]: Uniform2d (will be remapped) */
   "idd\0"
   "glUniform2d\0"
   "\0"
   /* _mesa_function_pool[24784]: Uniform3d (will be remapped) */
   "iddd\0"
   "glUniform3d\0"
   "\0"
   /* _mesa_function_pool[24802]: Uniform4d (will be remapped) */
   "idddd\0"
   "glUniform4d\0"
   "\0"
   /* _mesa_function_pool[24821]: Uniform1dv (will be remapped) */
   "iip\0"
   "glUniform1dv\0"
   "\0"
   /* _mesa_function_pool[24839]: Uniform2dv (will be remapped) */
   "iip\0"
   "glUniform2dv\0"
   "\0"
   /* _mesa_function_pool[24857]: Uniform3dv (will be remapped) */
   "iip\0"
   "glUniform3dv\0"
   "\0"
   /* _mesa_function_pool[24875]: Uniform4dv (will be remapped) */
   "iip\0"
   "glUniform4dv\0"
   "\0"
   /* _mesa_function_pool[24893]: UniformMatrix2dv (will be remapped) */
   "iiip\0"
   "glUniformMatrix2dv\0"
   "\0"
   /* _mesa_function_pool[24918]: UniformMatrix3dv (will be remapped) */
   "iiip\0"
   "glUniformMatrix3dv\0"
   "\0"
   /* _mesa_function_pool[24943]: UniformMatrix4dv (will be remapped) */
   "iiip\0"
   "glUniformMatrix4dv\0"
   "\0"
   /* _mesa_function_pool[24968]: UniformMatrix2x3dv (will be remapped) */
   "iiip\0"
   "glUniformMatrix2x3dv\0"
   "\0"
   /* _mesa_function_pool[24995]: UniformMatrix2x4dv (will be remapped) */
   "iiip\0"
   "glUniformMatrix2x4dv\0"
   "\0"
   /* _mesa_function_pool[25022]: UniformMatrix3x2dv (will be remapped) */
   "iiip\0"
   "glUniformMatrix3x2dv\0"
   "\0"
   /* _mesa_function_pool[25049]: UniformMatrix3x4dv (will be remapped) */
   "iiip\0"
   "glUniformMatrix3x4dv\0"
   "\0"
   /* _mesa_function_pool[25076]: UniformMatrix4x2dv (will be remapped) */
   "iiip\0"
   "glUniformMatrix4x2dv\0"
   "\0"
   /* _mesa_function_pool[25103]: UniformMatrix4x3dv (will be remapped) */
   "iiip\0"
   "glUniformMatrix4x3dv\0"
   "\0"
   /* _mesa_function_pool[25130]: GetUniformdv (will be remapped) */
   "iip\0"
   "glGetUniformdv\0"
   "\0"
   /* _mesa_function_pool[25150]: ProgramUniform1d (will be remapped) */
   "iid\0"
   "glProgramUniform1dEXT\0"
   "glProgramUniform1d\0"
   "\0"
   /* _mesa_function_pool[25196]: ProgramUniform2d (will be remapped) */
   "iidd\0"
   "glProgramUniform2dEXT\0"
   "glProgramUniform2d\0"
   "\0"
   /* _mesa_function_pool[25243]: ProgramUniform3d (will be remapped) */
   "iiddd\0"
   "glProgramUniform3dEXT\0"
   "glProgramUniform3d\0"
   "\0"
   /* _mesa_function_pool[25291]: ProgramUniform4d (will be remapped) */
   "iidddd\0"
   "glProgramUniform4dEXT\0"
   "glProgramUniform4d\0"
   "\0"
   /* _mesa_function_pool[25340]: ProgramUniform1dv (will be remapped) */
   "iiip\0"
   "glProgramUniform1dvEXT\0"
   "glProgramUniform1dv\0"
   "\0"
   /* _mesa_function_pool[25389]: ProgramUniform2dv (will be remapped) */
   "iiip\0"
   "glProgramUniform2dvEXT\0"
   "glProgramUniform2dv\0"
   "\0"
   /* _mesa_function_pool[25438]: ProgramUniform3dv (will be remapped) */
   "iiip\0"
   "glProgramUniform3dvEXT\0"
   "glProgramUniform3dv\0"
   "\0"
   /* _mesa_function_pool[25487]: ProgramUniform4dv (will be remapped) */
   "iiip\0"
   "glProgramUniform4dvEXT\0"
   "glProgramUniform4dv\0"
   "\0"
   /* _mesa_function_pool[25536]: ProgramUniformMatrix2dv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix2dvEXT\0"
   "glProgramUniformMatrix2dv\0"
   "\0"
   /* _mesa_function_pool[25598]: ProgramUniformMatrix3dv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix3dvEXT\0"
   "glProgramUniformMatrix3dv\0"
   "\0"
   /* _mesa_function_pool[25660]: ProgramUniformMatrix4dv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix4dvEXT\0"
   "glProgramUniformMatrix4dv\0"
   "\0"
   /* _mesa_function_pool[25722]: ProgramUniformMatrix2x3dv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix2x3dvEXT\0"
   "glProgramUniformMatrix2x3dv\0"
   "\0"
   /* _mesa_function_pool[25788]: ProgramUniformMatrix2x4dv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix2x4dvEXT\0"
   "glProgramUniformMatrix2x4dv\0"
   "\0"
   /* _mesa_function_pool[25854]: ProgramUniformMatrix3x2dv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix3x2dvEXT\0"
   "glProgramUniformMatrix3x2dv\0"
   "\0"
   /* _mesa_function_pool[25920]: ProgramUniformMatrix3x4dv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix3x4dvEXT\0"
   "glProgramUniformMatrix3x4dv\0"
   "\0"
   /* _mesa_function_pool[25986]: ProgramUniformMatrix4x2dv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix4x2dvEXT\0"
   "glProgramUniformMatrix4x2dv\0"
   "\0"
   /* _mesa_function_pool[26052]: ProgramUniformMatrix4x3dv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix4x3dvEXT\0"
   "glProgramUniformMatrix4x3dv\0"
   "\0"
   /* _mesa_function_pool[26118]: DrawTransformFeedbackStream (will be remapped) */
   "iii\0"
   "glDrawTransformFeedbackStream\0"
   "\0"
   /* _mesa_function_pool[26153]: BeginQueryIndexed (will be remapped) */
   "iii\0"
   "glBeginQueryIndexed\0"
   "\0"
   /* _mesa_function_pool[26178]: EndQueryIndexed (will be remapped) */
   "ii\0"
   "glEndQueryIndexed\0"
   "\0"
   /* _mesa_function_pool[26200]: GetQueryIndexediv (will be remapped) */
   "iiip\0"
   "glGetQueryIndexediv\0"
   "\0"
   /* _mesa_function_pool[26226]: UseProgramStages (will be remapped) */
   "iii\0"
   "glUseProgramStages\0"
   "glUseProgramStagesEXT\0"
   "\0"
   /* _mesa_function_pool[26272]: ActiveShaderProgram (will be remapped) */
   "ii\0"
   "glActiveShaderProgram\0"
   "glActiveShaderProgramEXT\0"
   "\0"
   /* _mesa_function_pool[26323]: CreateShaderProgramv (will be remapped) */
   "iip\0"
   "glCreateShaderProgramv\0"
   "glCreateShaderProgramvEXT\0"
   "\0"
   /* _mesa_function_pool[26377]: BindProgramPipeline (will be remapped) */
   "i\0"
   "glBindProgramPipeline\0"
   "glBindProgramPipelineEXT\0"
   "\0"
   /* _mesa_function_pool[26427]: DeleteProgramPipelines (will be remapped) */
   "ip\0"
   "glDeleteProgramPipelines\0"
   "glDeleteProgramPipelinesEXT\0"
   "\0"
   /* _mesa_function_pool[26484]: GenProgramPipelines (will be remapped) */
   "ip\0"
   "glGenProgramPipelines\0"
   "glGenProgramPipelinesEXT\0"
   "\0"
   /* _mesa_function_pool[26535]: IsProgramPipeline (will be remapped) */
   "i\0"
   "glIsProgramPipeline\0"
   "glIsProgramPipelineEXT\0"
   "\0"
   /* _mesa_function_pool[26581]: GetProgramPipelineiv (will be remapped) */
   "iip\0"
   "glGetProgramPipelineiv\0"
   "glGetProgramPipelineivEXT\0"
   "\0"
   /* _mesa_function_pool[26635]: ProgramUniform1i (will be remapped) */
   "iii\0"
   "glProgramUniform1i\0"
   "glProgramUniform1iEXT\0"
   "\0"
   /* _mesa_function_pool[26681]: ProgramUniform2i (will be remapped) */
   "iiii\0"
   "glProgramUniform2i\0"
   "glProgramUniform2iEXT\0"
   "\0"
   /* _mesa_function_pool[26728]: ProgramUniform3i (will be remapped) */
   "iiiii\0"
   "glProgramUniform3i\0"
   "glProgramUniform3iEXT\0"
   "\0"
   /* _mesa_function_pool[26776]: ProgramUniform4i (will be remapped) */
   "iiiiii\0"
   "glProgramUniform4i\0"
   "glProgramUniform4iEXT\0"
   "\0"
   /* _mesa_function_pool[26825]: ProgramUniform1ui (will be remapped) */
   "iii\0"
   "glProgramUniform1ui\0"
   "glProgramUniform1uiEXT\0"
   "\0"
   /* _mesa_function_pool[26873]: ProgramUniform2ui (will be remapped) */
   "iiii\0"
   "glProgramUniform2ui\0"
   "glProgramUniform2uiEXT\0"
   "\0"
   /* _mesa_function_pool[26922]: ProgramUniform3ui (will be remapped) */
   "iiiii\0"
   "glProgramUniform3ui\0"
   "glProgramUniform3uiEXT\0"
   "\0"
   /* _mesa_function_pool[26972]: ProgramUniform4ui (will be remapped) */
   "iiiiii\0"
   "glProgramUniform4ui\0"
   "glProgramUniform4uiEXT\0"
   "\0"
   /* _mesa_function_pool[27023]: ProgramUniform1f (will be remapped) */
   "iif\0"
   "glProgramUniform1f\0"
   "glProgramUniform1fEXT\0"
   "\0"
   /* _mesa_function_pool[27069]: ProgramUniform2f (will be remapped) */
   "iiff\0"
   "glProgramUniform2f\0"
   "glProgramUniform2fEXT\0"
   "\0"
   /* _mesa_function_pool[27116]: ProgramUniform3f (will be remapped) */
   "iifff\0"
   "glProgramUniform3f\0"
   "glProgramUniform3fEXT\0"
   "\0"
   /* _mesa_function_pool[27164]: ProgramUniform4f (will be remapped) */
   "iiffff\0"
   "glProgramUniform4f\0"
   "glProgramUniform4fEXT\0"
   "\0"
   /* _mesa_function_pool[27213]: ProgramUniform1iv (will be remapped) */
   "iiip\0"
   "glProgramUniform1iv\0"
   "glProgramUniform1ivEXT\0"
   "\0"
   /* _mesa_function_pool[27262]: ProgramUniform2iv (will be remapped) */
   "iiip\0"
   "glProgramUniform2iv\0"
   "glProgramUniform2ivEXT\0"
   "\0"
   /* _mesa_function_pool[27311]: ProgramUniform3iv (will be remapped) */
   "iiip\0"
   "glProgramUniform3iv\0"
   "glProgramUniform3ivEXT\0"
   "\0"
   /* _mesa_function_pool[27360]: ProgramUniform4iv (will be remapped) */
   "iiip\0"
   "glProgramUniform4iv\0"
   "glProgramUniform4ivEXT\0"
   "\0"
   /* _mesa_function_pool[27409]: ProgramUniform1uiv (will be remapped) */
   "iiip\0"
   "glProgramUniform1uiv\0"
   "glProgramUniform1uivEXT\0"
   "\0"
   /* _mesa_function_pool[27460]: ProgramUniform2uiv (will be remapped) */
   "iiip\0"
   "glProgramUniform2uiv\0"
   "glProgramUniform2uivEXT\0"
   "\0"
   /* _mesa_function_pool[27511]: ProgramUniform3uiv (will be remapped) */
   "iiip\0"
   "glProgramUniform3uiv\0"
   "glProgramUniform3uivEXT\0"
   "\0"
   /* _mesa_function_pool[27562]: ProgramUniform4uiv (will be remapped) */
   "iiip\0"
   "glProgramUniform4uiv\0"
   "glProgramUniform4uivEXT\0"
   "\0"
   /* _mesa_function_pool[27613]: ProgramUniform1fv (will be remapped) */
   "iiip\0"
   "glProgramUniform1fv\0"
   "glProgramUniform1fvEXT\0"
   "\0"
   /* _mesa_function_pool[27662]: ProgramUniform2fv (will be remapped) */
   "iiip\0"
   "glProgramUniform2fv\0"
   "glProgramUniform2fvEXT\0"
   "\0"
   /* _mesa_function_pool[27711]: ProgramUniform3fv (will be remapped) */
   "iiip\0"
   "glProgramUniform3fv\0"
   "glProgramUniform3fvEXT\0"
   "\0"
   /* _mesa_function_pool[27760]: ProgramUniform4fv (will be remapped) */
   "iiip\0"
   "glProgramUniform4fv\0"
   "glProgramUniform4fvEXT\0"
   "\0"
   /* _mesa_function_pool[27809]: ProgramUniformMatrix2fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix2fv\0"
   "glProgramUniformMatrix2fvEXT\0"
   "\0"
   /* _mesa_function_pool[27871]: ProgramUniformMatrix3fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix3fv\0"
   "glProgramUniformMatrix3fvEXT\0"
   "\0"
   /* _mesa_function_pool[27933]: ProgramUniformMatrix4fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix4fv\0"
   "glProgramUniformMatrix4fvEXT\0"
   "\0"
   /* _mesa_function_pool[27995]: ProgramUniformMatrix2x3fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix2x3fv\0"
   "glProgramUniformMatrix2x3fvEXT\0"
   "\0"
   /* _mesa_function_pool[28061]: ProgramUniformMatrix3x2fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix3x2fv\0"
   "glProgramUniformMatrix3x2fvEXT\0"
   "\0"
   /* _mesa_function_pool[28127]: ProgramUniformMatrix2x4fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix2x4fv\0"
   "glProgramUniformMatrix2x4fvEXT\0"
   "\0"
   /* _mesa_function_pool[28193]: ProgramUniformMatrix4x2fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix4x2fv\0"
   "glProgramUniformMatrix4x2fvEXT\0"
   "\0"
   /* _mesa_function_pool[28259]: ProgramUniformMatrix3x4fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix3x4fv\0"
   "glProgramUniformMatrix3x4fvEXT\0"
   "\0"
   /* _mesa_function_pool[28325]: ProgramUniformMatrix4x3fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix4x3fv\0"
   "glProgramUniformMatrix4x3fvEXT\0"
   "\0"
   /* _mesa_function_pool[28391]: ValidateProgramPipeline (will be remapped) */
   "i\0"
   "glValidateProgramPipeline\0"
   "glValidateProgramPipelineEXT\0"
   "\0"
   /* _mesa_function_pool[28449]: GetProgramPipelineInfoLog (will be remapped) */
   "iipp\0"
   "glGetProgramPipelineInfoLog\0"
   "glGetProgramPipelineInfoLogEXT\0"
   "\0"
   /* _mesa_function_pool[28514]: VertexAttribL1d (will be remapped) */
   "id\0"
   "glVertexAttribL1d\0"
   "glVertexAttribL1dEXT\0"
   "\0"
   /* _mesa_function_pool[28557]: VertexAttribL2d (will be remapped) */
   "idd\0"
   "glVertexAttribL2d\0"
   "glVertexAttribL2dEXT\0"
   "\0"
   /* _mesa_function_pool[28601]: VertexAttribL3d (will be remapped) */
   "iddd\0"
   "glVertexAttribL3d\0"
   "glVertexAttribL3dEXT\0"
   "\0"
   /* _mesa_function_pool[28646]: VertexAttribL4d (will be remapped) */
   "idddd\0"
   "glVertexAttribL4d\0"
   "glVertexAttribL4dEXT\0"
   "\0"
   /* _mesa_function_pool[28692]: VertexAttribL1dv (will be remapped) */
   "ip\0"
   "glVertexAttribL1dv\0"
   "glVertexAttribL1dvEXT\0"
   "\0"
   /* _mesa_function_pool[28737]: VertexAttribL2dv (will be remapped) */
   "ip\0"
   "glVertexAttribL2dv\0"
   "glVertexAttribL2dvEXT\0"
   "\0"
   /* _mesa_function_pool[28782]: VertexAttribL3dv (will be remapped) */
   "ip\0"
   "glVertexAttribL3dv\0"
   "glVertexAttribL3dvEXT\0"
   "\0"
   /* _mesa_function_pool[28827]: VertexAttribL4dv (will be remapped) */
   "ip\0"
   "glVertexAttribL4dv\0"
   "glVertexAttribL4dvEXT\0"
   "\0"
   /* _mesa_function_pool[28872]: VertexAttribLPointer (will be remapped) */
   "iiiip\0"
   "glVertexAttribLPointer\0"
   "glVertexAttribLPointerEXT\0"
   "\0"
   /* _mesa_function_pool[28928]: GetVertexAttribLdv (will be remapped) */
   "iip\0"
   "glGetVertexAttribLdv\0"
   "glGetVertexAttribLdvEXT\0"
   "\0"
   /* _mesa_function_pool[28978]: VertexArrayVertexAttribLOffsetEXT (will be remapped) */
   "iiiiiii\0"
   "glVertexArrayVertexAttribLOffsetEXT\0"
   "\0"
   /* _mesa_function_pool[29023]: GetShaderPrecisionFormat (will be remapped) */
   "iipp\0"
   "glGetShaderPrecisionFormat\0"
   "\0"
   /* _mesa_function_pool[29056]: ReleaseShaderCompiler (will be remapped) */
   "\0"
   "glReleaseShaderCompiler\0"
   "\0"
   /* _mesa_function_pool[29082]: ShaderBinary (will be remapped) */
   "ipipi\0"
   "glShaderBinary\0"
   "\0"
   /* _mesa_function_pool[29104]: ClearDepthf (will be remapped) */
   "f\0"
   "glClearDepthf\0"
   "glClearDepthfOES\0"
   "\0"
   /* _mesa_function_pool[29138]: DepthRangef (will be remapped) */
   "ff\0"
   "glDepthRangef\0"
   "glDepthRangefOES\0"
   "\0"
   /* _mesa_function_pool[29173]: GetProgramBinary (will be remapped) */
   "iippp\0"
   "glGetProgramBinary\0"
   "glGetProgramBinaryOES\0"
   "\0"
   /* _mesa_function_pool[29221]: ProgramBinary (will be remapped) */
   "iipi\0"
   "glProgramBinary\0"
   "glProgramBinaryOES\0"
   "\0"
   /* _mesa_function_pool[29262]: ProgramParameteri (will be remapped) */
   "iii\0"
   "glProgramParameteri\0"
   "glProgramParameteriEXT\0"
   "\0"
   /* _mesa_function_pool[29310]: DebugMessageControl (will be remapped) */
   "iiiipi\0"
   "glDebugMessageControlARB\0"
   "glDebugMessageControl\0"
   "glDebugMessageControlKHR\0"
   "\0"
   /* _mesa_function_pool[29390]: DebugMessageInsert (will be remapped) */
   "iiiiip\0"
   "glDebugMessageInsertARB\0"
   "glDebugMessageInsert\0"
   "glDebugMessageInsertKHR\0"
   "\0"
   /* _mesa_function_pool[29467]: DebugMessageCallback (will be remapped) */
   "pp\0"
   "glDebugMessageCallbackARB\0"
   "glDebugMessageCallback\0"
   "glDebugMessageCallbackKHR\0"
   "\0"
   /* _mesa_function_pool[29546]: GetDebugMessageLog (will be remapped) */
   "iipppppp\0"
   "glGetDebugMessageLogARB\0"
   "glGetDebugMessageLog\0"
   "glGetDebugMessageLogKHR\0"
   "\0"
   /* _mesa_function_pool[29625]: GetGraphicsResetStatusARB (will be remapped) */
   "\0"
   "glGetGraphicsResetStatusARB\0"
   "glGetGraphicsResetStatus\0"
   "glGetGraphicsResetStatusKHR\0"
   "glGetGraphicsResetStatusEXT\0"
   "\0"
   /* _mesa_function_pool[29736]: GetnMapdvARB (will be remapped) */
   "iiip\0"
   "glGetnMapdvARB\0"
   "\0"
   /* _mesa_function_pool[29757]: GetnMapfvARB (will be remapped) */
   "iiip\0"
   "glGetnMapfvARB\0"
   "\0"
   /* _mesa_function_pool[29778]: GetnMapivARB (will be remapped) */
   "iiip\0"
   "glGetnMapivARB\0"
   "\0"
   /* _mesa_function_pool[29799]: GetnPixelMapfvARB (will be remapped) */
   "iip\0"
   "glGetnPixelMapfvARB\0"
   "\0"
   /* _mesa_function_pool[29824]: GetnPixelMapuivARB (will be remapped) */
   "iip\0"
   "glGetnPixelMapuivARB\0"
   "\0"
   /* _mesa_function_pool[29850]: GetnPixelMapusvARB (will be remapped) */
   "iip\0"
   "glGetnPixelMapusvARB\0"
   "\0"
   /* _mesa_function_pool[29876]: GetnPolygonStippleARB (will be remapped) */
   "ip\0"
   "glGetnPolygonStippleARB\0"
   "\0"
   /* _mesa_function_pool[29904]: GetnTexImageARB (will be remapped) */
   "iiiiip\0"
   "glGetnTexImageARB\0"
   "\0"
   /* _mesa_function_pool[29930]: ReadnPixelsARB (will be remapped) */
   "iiiiiiip\0"
   "glReadnPixelsARB\0"
   "glReadnPixels\0"
   "glReadnPixelsKHR\0"
   "glReadnPixelsEXT\0"
   "\0"
   /* _mesa_function_pool[30005]: GetnColorTableARB (will be remapped) */
   "iiiip\0"
   "glGetnColorTableARB\0"
   "\0"
   /* _mesa_function_pool[30032]: GetnConvolutionFilterARB (will be remapped) */
   "iiiip\0"
   "glGetnConvolutionFilterARB\0"
   "\0"
   /* _mesa_function_pool[30066]: GetnSeparableFilterARB (will be remapped) */
   "iiiipipp\0"
   "glGetnSeparableFilterARB\0"
   "\0"
   /* _mesa_function_pool[30101]: GetnHistogramARB (will be remapped) */
   "iiiiip\0"
   "glGetnHistogramARB\0"
   "\0"
   /* _mesa_function_pool[30128]: GetnMinmaxARB (will be remapped) */
   "iiiiip\0"
   "glGetnMinmaxARB\0"
   "\0"
   /* _mesa_function_pool[30152]: GetnCompressedTexImageARB (will be remapped) */
   "iiip\0"
   "glGetnCompressedTexImageARB\0"
   "\0"
   /* _mesa_function_pool[30186]: GetnUniformfvARB (will be remapped) */
   "iiip\0"
   "glGetnUniformfvARB\0"
   "glGetnUniformfv\0"
   "glGetnUniformfvKHR\0"
   "glGetnUniformfvEXT\0"
   "\0"
   /* _mesa_function_pool[30265]: GetnUniformivARB (will be remapped) */
   "iiip\0"
   "glGetnUniformivARB\0"
   "glGetnUniformiv\0"
   "glGetnUniformivKHR\0"
   "glGetnUniformivEXT\0"
   "\0"
   /* _mesa_function_pool[30344]: GetnUniformuivARB (will be remapped) */
   "iiip\0"
   "glGetnUniformuivARB\0"
   "glGetnUniformuiv\0"
   "glGetnUniformuivKHR\0"
   "\0"
   /* _mesa_function_pool[30407]: GetnUniformdvARB (will be remapped) */
   "iiip\0"
   "glGetnUniformdvARB\0"
   "\0"
   /* _mesa_function_pool[30432]: DrawArraysInstancedBaseInstance (will be remapped) */
   "iiiii\0"
   "glDrawArraysInstancedBaseInstance\0"
   "glDrawArraysInstancedBaseInstanceEXT\0"
   "\0"
   /* _mesa_function_pool[30510]: DrawElementsInstancedBaseInstance (will be remapped) */
   "iiipii\0"
   "glDrawElementsInstancedBaseInstance\0"
   "glDrawElementsInstancedBaseInstanceEXT\0"
   "\0"
   /* _mesa_function_pool[30593]: DrawElementsInstancedBaseVertexBaseInstance (will be remapped) */
   "iiipiii\0"
   "glDrawElementsInstancedBaseVertexBaseInstance\0"
   "glDrawElementsInstancedBaseVertexBaseInstanceEXT\0"
   "\0"
   /* _mesa_function_pool[30697]: DrawTransformFeedbackInstanced (will be remapped) */
   "iii\0"
   "glDrawTransformFeedbackInstanced\0"
   "\0"
   /* _mesa_function_pool[30735]: DrawTransformFeedbackStreamInstanced (will be remapped) */
   "iiii\0"
   "glDrawTransformFeedbackStreamInstanced\0"
   "\0"
   /* _mesa_function_pool[30780]: GetInternalformativ (will be remapped) */
   "iiiip\0"
   "glGetInternalformativ\0"
   "\0"
   /* _mesa_function_pool[30809]: GetActiveAtomicCounterBufferiv (will be remapped) */
   "iiip\0"
   "glGetActiveAtomicCounterBufferiv\0"
   "\0"
   /* _mesa_function_pool[30848]: BindImageTexture (will be remapped) */
   "iiiiiii\0"
   "glBindImageTexture\0"
   "\0"
   /* _mesa_function_pool[30876]: MemoryBarrier (will be remapped) */
   "i\0"
   "glMemoryBarrier\0"
   "glMemoryBarrierEXT\0"
   "\0"
   /* _mesa_function_pool[30914]: TexStorage1D (will be remapped) */
   "iiii\0"
   "glTexStorage1D\0"
   "\0"
   /* _mesa_function_pool[30935]: TexStorage2D (will be remapped) */
   "iiiii\0"
   "glTexStorage2D\0"
   "\0"
   /* _mesa_function_pool[30957]: TexStorage3D (will be remapped) */
   "iiiiii\0"
   "glTexStorage3D\0"
   "\0"
   /* _mesa_function_pool[30980]: TextureStorage1DEXT (will be remapped) */
   "iiiii\0"
   "glTextureStorage1DEXT\0"
   "\0"
   /* _mesa_function_pool[31009]: TextureStorage2DEXT (will be remapped) */
   "iiiiii\0"
   "glTextureStorage2DEXT\0"
   "\0"
   /* _mesa_function_pool[31039]: TextureStorage3DEXT (will be remapped) */
   "iiiiiii\0"
   "glTextureStorage3DEXT\0"
   "\0"
   /* _mesa_function_pool[31070]: PushDebugGroup (will be remapped) */
   "iiip\0"
   "glPushDebugGroup\0"
   "glPushDebugGroupKHR\0"
   "\0"
   /* _mesa_function_pool[31113]: PopDebugGroup (will be remapped) */
   "\0"
   "glPopDebugGroup\0"
   "glPopDebugGroupKHR\0"
   "\0"
   /* _mesa_function_pool[31150]: ObjectLabel (will be remapped) */
   "iiip\0"
   "glObjectLabel\0"
   "glObjectLabelKHR\0"
   "\0"
   /* _mesa_function_pool[31187]: GetObjectLabel (will be remapped) */
   "iiipp\0"
   "glGetObjectLabel\0"
   "glGetObjectLabelKHR\0"
   "\0"
   /* _mesa_function_pool[31231]: ObjectPtrLabel (will be remapped) */
   "pip\0"
   "glObjectPtrLabel\0"
   "glObjectPtrLabelKHR\0"
   "\0"
   /* _mesa_function_pool[31273]: GetObjectPtrLabel (will be remapped) */
   "pipp\0"
   "glGetObjectPtrLabel\0"
   "glGetObjectPtrLabelKHR\0"
   "\0"
   /* _mesa_function_pool[31322]: ClearBufferData (will be remapped) */
   "iiiip\0"
   "glClearBufferData\0"
   "\0"
   /* _mesa_function_pool[31347]: ClearBufferSubData (will be remapped) */
   "iiiiiip\0"
   "glClearBufferSubData\0"
   "\0"
   /* _mesa_function_pool[31377]: ClearNamedBufferDataEXT (will be remapped) */
   "iiiip\0"
   "glClearNamedBufferDataEXT\0"
   "\0"
   /* _mesa_function_pool[31410]: ClearNamedBufferSubDataEXT (will be remapped) */
   "iiiiiip\0"
   "glClearNamedBufferSubDataEXT\0"
   "\0"
   /* _mesa_function_pool[31448]: DispatchCompute (will be remapped) */
   "iii\0"
   "glDispatchCompute\0"
   "\0"
   /* _mesa_function_pool[31471]: DispatchComputeIndirect (will be remapped) */
   "i\0"
   "glDispatchComputeIndirect\0"
   "\0"
   /* _mesa_function_pool[31500]: CopyImageSubData (will be remapped) */
   "iiiiiiiiiiiiiii\0"
   "glCopyImageSubData\0"
   "glCopyImageSubDataEXT\0"
   "glCopyImageSubDataOES\0"
   "\0"
   /* _mesa_function_pool[31580]: TextureView (will be remapped) */
   "iiiiiiii\0"
   "glTextureView\0"
   "glTextureViewOES\0"
   "glTextureViewEXT\0"
   "\0"
   /* _mesa_function_pool[31638]: BindVertexBuffer (will be remapped) */
   "iiii\0"
   "glBindVertexBuffer\0"
   "\0"
   /* _mesa_function_pool[31663]: VertexAttribFormat (will be remapped) */
   "iiiii\0"
   "glVertexAttribFormat\0"
   "\0"
   /* _mesa_function_pool[31691]: VertexAttribIFormat (will be remapped) */
   "iiii\0"
   "glVertexAttribIFormat\0"
   "\0"
   /* _mesa_function_pool[31719]: VertexAttribLFormat (will be remapped) */
   "iiii\0"
   "glVertexAttribLFormat\0"
   "\0"
   /* _mesa_function_pool[31747]: VertexAttribBinding (will be remapped) */
   "ii\0"
   "glVertexAttribBinding\0"
   "\0"
   /* _mesa_function_pool[31773]: VertexBindingDivisor (will be remapped) */
   "ii\0"
   "glVertexBindingDivisor\0"
   "\0"
   /* _mesa_function_pool[31800]: VertexArrayBindVertexBufferEXT (will be remapped) */
   "iiiii\0"
   "glVertexArrayBindVertexBufferEXT\0"
   "\0"
   /* _mesa_function_pool[31840]: VertexArrayVertexAttribFormatEXT (will be remapped) */
   "iiiiii\0"
   "glVertexArrayVertexAttribFormatEXT\0"
   "\0"
   /* _mesa_function_pool[31883]: VertexArrayVertexAttribIFormatEXT (will be remapped) */
   "iiiii\0"
   "glVertexArrayVertexAttribIFormatEXT\0"
   "\0"
   /* _mesa_function_pool[31926]: VertexArrayVertexAttribLFormatEXT (will be remapped) */
   "iiiii\0"
   "glVertexArrayVertexAttribLFormatEXT\0"
   "\0"
   /* _mesa_function_pool[31969]: VertexArrayVertexAttribBindingEXT (will be remapped) */
   "iii\0"
   "glVertexArrayVertexAttribBindingEXT\0"
   "\0"
   /* _mesa_function_pool[32010]: VertexArrayVertexBindingDivisorEXT (will be remapped) */
   "iii\0"
   "glVertexArrayVertexBindingDivisorEXT\0"
   "\0"
   /* _mesa_function_pool[32052]: FramebufferParameteri (will be remapped) */
   "iii\0"
   "glFramebufferParameteri\0"
   "\0"
   /* _mesa_function_pool[32081]: GetFramebufferParameteriv (will be remapped) */
   "iip\0"
   "glGetFramebufferParameteriv\0"
   "\0"
   /* _mesa_function_pool[32114]: NamedFramebufferParameteriEXT (will be remapped) */
   "iii\0"
   "glNamedFramebufferParameteriEXT\0"
   "\0"
   /* _mesa_function_pool[32151]: GetNamedFramebufferParameterivEXT (will be remapped) */
   "iip\0"
   "glGetNamedFramebufferParameterivEXT\0"
   "\0"
   /* _mesa_function_pool[32192]: GetInternalformati64v (will be remapped) */
   "iiiip\0"
   "glGetInternalformati64v\0"
   "\0"
   /* _mesa_function_pool[32223]: InvalidateTexSubImage (will be remapped) */
   "iiiiiiii\0"
   "glInvalidateTexSubImage\0"
   "\0"
   /* _mesa_function_pool[32257]: InvalidateTexImage (will be remapped) */
   "ii\0"
   "glInvalidateTexImage\0"
   "\0"
   /* _mesa_function_pool[32282]: InvalidateBufferSubData (will be remapped) */
   "iii\0"
   "glInvalidateBufferSubData\0"
   "\0"
   /* _mesa_function_pool[32313]: InvalidateBufferData (will be remapped) */
   "i\0"
   "glInvalidateBufferData\0"
   "\0"
   /* _mesa_function_pool[32339]: InvalidateSubFramebuffer (will be remapped) */
   "iipiiii\0"
   "glInvalidateSubFramebuffer\0"
   "\0"
   /* _mesa_function_pool[32375]: InvalidateFramebuffer (will be remapped) */
   "iip\0"
   "glInvalidateFramebuffer\0"
   "\0"
   /* _mesa_function_pool[32404]: GetProgramInterfaceiv (will be remapped) */
   "iiip\0"
   "glGetProgramInterfaceiv\0"
   "\0"
   /* _mesa_function_pool[32434]: GetProgramResourceIndex (will be remapped) */
   "iip\0"
   "glGetProgramResourceIndex\0"
   "\0"
   /* _mesa_function_pool[32465]: GetProgramResourceName (will be remapped) */
   "iiiipp\0"
   "glGetProgramResourceName\0"
   "\0"
   /* _mesa_function_pool[32498]: GetProgramResourceiv (will be remapped) */
   "iiiipipp\0"
   "glGetProgramResourceiv\0"
   "\0"
   /* _mesa_function_pool[32531]: GetProgramResourceLocation (will be remapped) */
   "iip\0"
   "glGetProgramResourceLocation\0"
   "\0"
   /* _mesa_function_pool[32565]: GetProgramResourceLocationIndex (will be remapped) */
   "iip\0"
   "glGetProgramResourceLocationIndex\0"
   "glGetProgramResourceLocationIndexEXT\0"
   "\0"
   /* _mesa_function_pool[32641]: ShaderStorageBlockBinding (will be remapped) */
   "iii\0"
   "glShaderStorageBlockBinding\0"
   "\0"
   /* _mesa_function_pool[32674]: TexBufferRange (will be remapped) */
   "iiiii\0"
   "glTexBufferRange\0"
   "glTexBufferRangeEXT\0"
   "glTexBufferRangeOES\0"
   "\0"
   /* _mesa_function_pool[32738]: TextureBufferRangeEXT (will be remapped) */
   "iiiiii\0"
   "glTextureBufferRangeEXT\0"
   "\0"
   /* _mesa_function_pool[32770]: TexStorage2DMultisample (will be remapped) */
   "iiiiii\0"
   "glTexStorage2DMultisample\0"
   "\0"
   /* _mesa_function_pool[32804]: TexStorage3DMultisample (will be remapped) */
   "iiiiiii\0"
   "glTexStorage3DMultisample\0"
   "glTexStorage3DMultisampleOES\0"
   "\0"
   /* _mesa_function_pool[32868]: TextureStorage2DMultisampleEXT (will be remapped) */
   "iiiiiii\0"
   "glTextureStorage2DMultisampleEXT\0"
   "\0"
   /* _mesa_function_pool[32910]: TextureStorage3DMultisampleEXT (will be remapped) */
   "iiiiiiii\0"
   "glTextureStorage3DMultisampleEXT\0"
   "\0"
   /* _mesa_function_pool[32953]: BufferStorage (will be remapped) */
   "iipi\0"
   "glBufferStorage\0"
   "glBufferStorageEXT\0"
   "\0"
   /* _mesa_function_pool[32994]: NamedBufferStorageEXT (will be remapped) */
   "iipi\0"
   "glNamedBufferStorageEXT\0"
   "\0"
   /* _mesa_function_pool[33024]: ClearTexImage (will be remapped) */
   "iiiip\0"
   "glClearTexImage\0"
   "\0"
   /* _mesa_function_pool[33047]: ClearTexSubImage (will be remapped) */
   "iiiiiiiiiip\0"
   "glClearTexSubImage\0"
   "\0"
   /* _mesa_function_pool[33079]: BindBuffersBase (will be remapped) */
   "iiip\0"
   "glBindBuffersBase\0"
   "\0"
   /* _mesa_function_pool[33103]: BindBuffersRange (will be remapped) */
   "iiippp\0"
   "glBindBuffersRange\0"
   "\0"
   /* _mesa_function_pool[33130]: BindTextures (will be remapped) */
   "iip\0"
   "glBindTextures\0"
   "\0"
   /* _mesa_function_pool[33150]: BindSamplers (will be remapped) */
   "iip\0"
   "glBindSamplers\0"
   "\0"
   /* _mesa_function_pool[33170]: BindImageTextures (will be remapped) */
   "iip\0"
   "glBindImageTextures\0"
   "\0"
   /* _mesa_function_pool[33195]: BindVertexBuffers (will be remapped) */
   "iippp\0"
   "glBindVertexBuffers\0"
   "\0"
   /* _mesa_function_pool[33222]: GetTextureHandleARB (will be remapped) */
   "i\0"
   "glGetTextureHandleARB\0"
   "\0"
   /* _mesa_function_pool[33247]: GetTextureSamplerHandleARB (will be remapped) */
   "ii\0"
   "glGetTextureSamplerHandleARB\0"
   "\0"
   /* _mesa_function_pool[33280]: MakeTextureHandleResidentARB (will be remapped) */
   "i\0"
   "glMakeTextureHandleResidentARB\0"
   "\0"
   /* _mesa_function_pool[33314]: MakeTextureHandleNonResidentARB (will be remapped) */
   "i\0"
   "glMakeTextureHandleNonResidentARB\0"
   "\0"
   /* _mesa_function_pool[33351]: GetImageHandleARB (will be remapped) */
   "iiiii\0"
   "glGetImageHandleARB\0"
   "\0"
   /* _mesa_function_pool[33378]: MakeImageHandleResidentARB (will be remapped) */
   "ii\0"
   "glMakeImageHandleResidentARB\0"
   "\0"
   /* _mesa_function_pool[33411]: MakeImageHandleNonResidentARB (will be remapped) */
   "i\0"
   "glMakeImageHandleNonResidentARB\0"
   "\0"
   /* _mesa_function_pool[33446]: UniformHandleui64ARB (will be remapped) */
   "ii\0"
   "glUniformHandleui64ARB\0"
   "\0"
   /* _mesa_function_pool[33473]: UniformHandleui64vARB (will be remapped) */
   "iip\0"
   "glUniformHandleui64vARB\0"
   "\0"
   /* _mesa_function_pool[33502]: ProgramUniformHandleui64ARB (will be remapped) */
   "iii\0"
   "glProgramUniformHandleui64ARB\0"
   "\0"
   /* _mesa_function_pool[33537]: ProgramUniformHandleui64vARB (will be remapped) */
   "iiip\0"
   "glProgramUniformHandleui64vARB\0"
   "\0"
   /* _mesa_function_pool[33574]: IsTextureHandleResidentARB (will be remapped) */
   "i\0"
   "glIsTextureHandleResidentARB\0"
   "\0"
   /* _mesa_function_pool[33606]: IsImageHandleResidentARB (will be remapped) */
   "i\0"
   "glIsImageHandleResidentARB\0"
   "\0"
   /* _mesa_function_pool[33636]: VertexAttribL1ui64ARB (will be remapped) */
   "ii\0"
   "glVertexAttribL1ui64ARB\0"
   "\0"
   /* _mesa_function_pool[33664]: VertexAttribL1ui64vARB (will be remapped) */
   "ip\0"
   "glVertexAttribL1ui64vARB\0"
   "\0"
   /* _mesa_function_pool[33693]: GetVertexAttribLui64vARB (will be remapped) */
   "iip\0"
   "glGetVertexAttribLui64vARB\0"
   "\0"
   /* _mesa_function_pool[33725]: DispatchComputeGroupSizeARB (will be remapped) */
   "iiiiii\0"
   "glDispatchComputeGroupSizeARB\0"
   "\0"
   /* _mesa_function_pool[33763]: MultiDrawArraysIndirectCountARB (will be remapped) */
   "iiiii\0"
   "glMultiDrawArraysIndirectCountARB\0"
   "glMultiDrawArraysIndirectCount\0"
   "\0"
   /* _mesa_function_pool[33835]: MultiDrawElementsIndirectCountARB (will be remapped) */
   "iiiiii\0"
   "glMultiDrawElementsIndirectCountARB\0"
   "glMultiDrawElementsIndirectCount\0"
   "\0"
   /* _mesa_function_pool[33912]: ClipControl (will be remapped) */
   "ii\0"
   "glClipControl\0"
   "glClipControlEXT\0"
   "\0"
   /* _mesa_function_pool[33947]: CreateTransformFeedbacks (will be remapped) */
   "ip\0"
   "glCreateTransformFeedbacks\0"
   "\0"
   /* _mesa_function_pool[33978]: TransformFeedbackBufferBase (will be remapped) */
   "iii\0"
   "glTransformFeedbackBufferBase\0"
   "\0"
   /* _mesa_function_pool[34013]: TransformFeedbackBufferRange (will be remapped) */
   "iiiii\0"
   "glTransformFeedbackBufferRange\0"
   "\0"
   /* _mesa_function_pool[34051]: GetTransformFeedbackiv (will be remapped) */
   "iip\0"
   "glGetTransformFeedbackiv\0"
   "\0"
   /* _mesa_function_pool[34081]: GetTransformFeedbacki_v (will be remapped) */
   "iiip\0"
   "glGetTransformFeedbacki_v\0"
   "\0"
   /* _mesa_function_pool[34113]: GetTransformFeedbacki64_v (will be remapped) */
   "iiip\0"
   "glGetTransformFeedbacki64_v\0"
   "\0"
   /* _mesa_function_pool[34147]: CreateBuffers (will be remapped) */
   "ip\0"
   "glCreateBuffers\0"
   "\0"
   /* _mesa_function_pool[34167]: NamedBufferStorage (will be remapped) */
   "iipi\0"
   "glNamedBufferStorage\0"
   "\0"
   /* _mesa_function_pool[34194]: NamedBufferData (will be remapped) */
   "iipi\0"
   "glNamedBufferData\0"
   "\0"
   /* _mesa_function_pool[34218]: NamedBufferSubData (will be remapped) */
   "iiip\0"
   "glNamedBufferSubData\0"
   "\0"
   /* _mesa_function_pool[34245]: CopyNamedBufferSubData (will be remapped) */
   "iiiii\0"
   "glCopyNamedBufferSubData\0"
   "\0"
   /* _mesa_function_pool[34277]: ClearNamedBufferData (will be remapped) */
   "iiiip\0"
   "glClearNamedBufferData\0"
   "\0"
   /* _mesa_function_pool[34307]: ClearNamedBufferSubData (will be remapped) */
   "iiiiiip\0"
   "glClearNamedBufferSubData\0"
   "\0"
   /* _mesa_function_pool[34342]: MapNamedBuffer (will be remapped) */
   "ii\0"
   "glMapNamedBuffer\0"
   "\0"
   /* _mesa_function_pool[34363]: MapNamedBufferRange (will be remapped) */
   "iiii\0"
   "glMapNamedBufferRange\0"
   "\0"
   /* _mesa_function_pool[34391]: UnmapNamedBufferEXT (will be remapped) */
   "i\0"
   "glUnmapNamedBuffer\0"
   "glUnmapNamedBufferEXT\0"
   "\0"
   /* _mesa_function_pool[34435]: FlushMappedNamedBufferRange (will be remapped) */
   "iii\0"
   "glFlushMappedNamedBufferRange\0"
   "\0"
   /* _mesa_function_pool[34470]: GetNamedBufferParameteriv (will be remapped) */
   "iip\0"
   "glGetNamedBufferParameteriv\0"
   "\0"
   /* _mesa_function_pool[34503]: GetNamedBufferParameteri64v (will be remapped) */
   "iip\0"
   "glGetNamedBufferParameteri64v\0"
   "\0"
   /* _mesa_function_pool[34538]: GetNamedBufferPointerv (will be remapped) */
   "iip\0"
   "glGetNamedBufferPointerv\0"
   "\0"
   /* _mesa_function_pool[34568]: GetNamedBufferSubData (will be remapped) */
   "iiip\0"
   "glGetNamedBufferSubData\0"
   "\0"
   /* _mesa_function_pool[34598]: CreateFramebuffers (will be remapped) */
   "ip\0"
   "glCreateFramebuffers\0"
   "\0"
   /* _mesa_function_pool[34623]: NamedFramebufferRenderbuffer (will be remapped) */
   "iiii\0"
   "glNamedFramebufferRenderbuffer\0"
   "\0"
   /* _mesa_function_pool[34660]: NamedFramebufferParameteri (will be remapped) */
   "iii\0"
   "glNamedFramebufferParameteri\0"
   "\0"
   /* _mesa_function_pool[34694]: NamedFramebufferTexture (will be remapped) */
   "iiii\0"
   "glNamedFramebufferTexture\0"
   "\0"
   /* _mesa_function_pool[34726]: NamedFramebufferTextureLayer (will be remapped) */
   "iiiii\0"
   "glNamedFramebufferTextureLayer\0"
   "\0"
   /* _mesa_function_pool[34764]: NamedFramebufferDrawBuffer (will be remapped) */
   "ii\0"
   "glNamedFramebufferDrawBuffer\0"
   "\0"
   /* _mesa_function_pool[34797]: NamedFramebufferDrawBuffers (will be remapped) */
   "iip\0"
   "glNamedFramebufferDrawBuffers\0"
   "\0"
   /* _mesa_function_pool[34832]: NamedFramebufferReadBuffer (will be remapped) */
   "ii\0"
   "glNamedFramebufferReadBuffer\0"
   "\0"
   /* _mesa_function_pool[34865]: InvalidateNamedFramebufferData (will be remapped) */
   "iip\0"
   "glInvalidateNamedFramebufferData\0"
   "\0"
   /* _mesa_function_pool[34903]: InvalidateNamedFramebufferSubData (will be remapped) */
   "iipiiii\0"
   "glInvalidateNamedFramebufferSubData\0"
   "\0"
   /* _mesa_function_pool[34948]: ClearNamedFramebufferiv (will be remapped) */
   "iiip\0"
   "glClearNamedFramebufferiv\0"
   "\0"
   /* _mesa_function_pool[34980]: ClearNamedFramebufferuiv (will be remapped) */
   "iiip\0"
   "glClearNamedFramebufferuiv\0"
   "\0"
   /* _mesa_function_pool[35013]: ClearNamedFramebufferfv (will be remapped) */
   "iiip\0"
   "glClearNamedFramebufferfv\0"
   "\0"
   /* _mesa_function_pool[35045]: ClearNamedFramebufferfi (will be remapped) */
   "iiifi\0"
   "glClearNamedFramebufferfi\0"
   "\0"
   /* _mesa_function_pool[35078]: BlitNamedFramebuffer (will be remapped) */
   "iiiiiiiiiiii\0"
   "glBlitNamedFramebuffer\0"
   "\0"
   /* _mesa_function_pool[35115]: CheckNamedFramebufferStatus (will be remapped) */
   "ii\0"
   "glCheckNamedFramebufferStatus\0"
   "\0"
   /* _mesa_function_pool[35149]: GetNamedFramebufferParameteriv (will be remapped) */
   "iip\0"
   "glGetNamedFramebufferParameteriv\0"
   "\0"
   /* _mesa_function_pool[35187]: GetNamedFramebufferAttachmentParameteriv (will be remapped) */
   "iiip\0"
   "glGetNamedFramebufferAttachmentParameteriv\0"
   "\0"
   /* _mesa_function_pool[35236]: CreateRenderbuffers (will be remapped) */
   "ip\0"
   "glCreateRenderbuffers\0"
   "\0"
   /* _mesa_function_pool[35262]: NamedRenderbufferStorage (will be remapped) */
   "iiii\0"
   "glNamedRenderbufferStorage\0"
   "\0"
   /* _mesa_function_pool[35295]: NamedRenderbufferStorageMultisample (will be remapped) */
   "iiiii\0"
   "glNamedRenderbufferStorageMultisample\0"
   "\0"
   /* _mesa_function_pool[35340]: GetNamedRenderbufferParameteriv (will be remapped) */
   "iip\0"
   "glGetNamedRenderbufferParameteriv\0"
   "\0"
   /* _mesa_function_pool[35379]: CreateTextures (will be remapped) */
   "iip\0"
   "glCreateTextures\0"
   "\0"
   /* _mesa_function_pool[35401]: TextureBuffer (will be remapped) */
   "iii\0"
   "glTextureBuffer\0"
   "\0"
   /* _mesa_function_pool[35422]: TextureBufferRange (will be remapped) */
   "iiiii\0"
   "glTextureBufferRange\0"
   "\0"
   /* _mesa_function_pool[35450]: TextureStorage1D (will be remapped) */
   "iiii\0"
   "glTextureStorage1D\0"
   "\0"
   /* _mesa_function_pool[35475]: TextureStorage2D (will be remapped) */
   "iiiii\0"
   "glTextureStorage2D\0"
   "\0"
   /* _mesa_function_pool[35501]: TextureStorage3D (will be remapped) */
   "iiiiii\0"
   "glTextureStorage3D\0"
   "\0"
   /* _mesa_function_pool[35528]: TextureStorage2DMultisample (will be remapped) */
   "iiiiii\0"
   "glTextureStorage2DMultisample\0"
   "\0"
   /* _mesa_function_pool[35566]: TextureStorage3DMultisample (will be remapped) */
   "iiiiiii\0"
   "glTextureStorage3DMultisample\0"
   "\0"
   /* _mesa_function_pool[35605]: TextureSubImage1D (will be remapped) */
   "iiiiiip\0"
   "glTextureSubImage1D\0"
   "\0"
   /* _mesa_function_pool[35634]: TextureSubImage2D (will be remapped) */
   "iiiiiiiip\0"
   "glTextureSubImage2D\0"
   "\0"
   /* _mesa_function_pool[35665]: TextureSubImage3D (will be remapped) */
   "iiiiiiiiiip\0"
   "glTextureSubImage3D\0"
   "\0"
   /* _mesa_function_pool[35698]: CompressedTextureSubImage1D (will be remapped) */
   "iiiiiip\0"
   "glCompressedTextureSubImage1D\0"
   "\0"
   /* _mesa_function_pool[35737]: CompressedTextureSubImage2D (will be remapped) */
   "iiiiiiiip\0"
   "glCompressedTextureSubImage2D\0"
   "\0"
   /* _mesa_function_pool[35778]: CompressedTextureSubImage3D (will be remapped) */
   "iiiiiiiiiip\0"
   "glCompressedTextureSubImage3D\0"
   "\0"
   /* _mesa_function_pool[35821]: CopyTextureSubImage1D (will be remapped) */
   "iiiiii\0"
   "glCopyTextureSubImage1D\0"
   "\0"
   /* _mesa_function_pool[35853]: CopyTextureSubImage2D (will be remapped) */
   "iiiiiiii\0"
   "glCopyTextureSubImage2D\0"
   "\0"
   /* _mesa_function_pool[35887]: CopyTextureSubImage3D (will be remapped) */
   "iiiiiiiii\0"
   "glCopyTextureSubImage3D\0"
   "\0"
   /* _mesa_function_pool[35922]: TextureParameterf (will be remapped) */
   "iif\0"
   "glTextureParameterf\0"
   "\0"
   /* _mesa_function_pool[35947]: TextureParameterfv (will be remapped) */
   "iip\0"
   "glTextureParameterfv\0"
   "\0"
   /* _mesa_function_pool[35973]: TextureParameteri (will be remapped) */
   "iii\0"
   "glTextureParameteri\0"
   "\0"
   /* _mesa_function_pool[35998]: TextureParameterIiv (will be remapped) */
   "iip\0"
   "glTextureParameterIiv\0"
   "\0"
   /* _mesa_function_pool[36025]: TextureParameterIuiv (will be remapped) */
   "iip\0"
   "glTextureParameterIuiv\0"
   "\0"
   /* _mesa_function_pool[36053]: TextureParameteriv (will be remapped) */
   "iip\0"
   "glTextureParameteriv\0"
   "\0"
   /* _mesa_function_pool[36079]: GenerateTextureMipmap (will be remapped) */
   "i\0"
   "glGenerateTextureMipmap\0"
   "\0"
   /* _mesa_function_pool[36106]: BindTextureUnit (will be remapped) */
   "ii\0"
   "glBindTextureUnit\0"
   "\0"
   /* _mesa_function_pool[36128]: GetTextureImage (will be remapped) */
   "iiiiip\0"
   "glGetTextureImage\0"
   "\0"
   /* _mesa_function_pool[36154]: GetCompressedTextureImage (will be remapped) */
   "iiip\0"
   "glGetCompressedTextureImage\0"
   "\0"
   /* _mesa_function_pool[36188]: GetTextureLevelParameterfv (will be remapped) */
   "iiip\0"
   "glGetTextureLevelParameterfv\0"
   "\0"
   /* _mesa_function_pool[36223]: GetTextureLevelParameteriv (will be remapped) */
   "iiip\0"
   "glGetTextureLevelParameteriv\0"
   "\0"
   /* _mesa_function_pool[36258]: GetTextureParameterfv (will be remapped) */
   "iip\0"
   "glGetTextureParameterfv\0"
   "\0"
   /* _mesa_function_pool[36287]: GetTextureParameterIiv (will be remapped) */
   "iip\0"
   "glGetTextureParameterIiv\0"
   "\0"
   /* _mesa_function_pool[36317]: GetTextureParameterIuiv (will be remapped) */
   "iip\0"
   "glGetTextureParameterIuiv\0"
   "\0"
   /* _mesa_function_pool[36348]: GetTextureParameteriv (will be remapped) */
   "iip\0"
   "glGetTextureParameteriv\0"
   "\0"
   /* _mesa_function_pool[36377]: CreateVertexArrays (will be remapped) */
   "ip\0"
   "glCreateVertexArrays\0"
   "\0"
   /* _mesa_function_pool[36402]: DisableVertexArrayAttrib (will be remapped) */
   "ii\0"
   "glDisableVertexArrayAttrib\0"
   "\0"
   /* _mesa_function_pool[36433]: EnableVertexArrayAttrib (will be remapped) */
   "ii\0"
   "glEnableVertexArrayAttrib\0"
   "\0"
   /* _mesa_function_pool[36463]: VertexArrayElementBuffer (will be remapped) */
   "ii\0"
   "glVertexArrayElementBuffer\0"
   "\0"
   /* _mesa_function_pool[36494]: VertexArrayVertexBuffer (will be remapped) */
   "iiiii\0"
   "glVertexArrayVertexBuffer\0"
   "\0"
   /* _mesa_function_pool[36527]: VertexArrayVertexBuffers (will be remapped) */
   "iiippp\0"
   "glVertexArrayVertexBuffers\0"
   "\0"
   /* _mesa_function_pool[36562]: VertexArrayAttribFormat (will be remapped) */
   "iiiiii\0"
   "glVertexArrayAttribFormat\0"
   "\0"
   /* _mesa_function_pool[36596]: VertexArrayAttribIFormat (will be remapped) */
   "iiiii\0"
   "glVertexArrayAttribIFormat\0"
   "\0"
   /* _mesa_function_pool[36630]: VertexArrayAttribLFormat (will be remapped) */
   "iiiii\0"
   "glVertexArrayAttribLFormat\0"
   "\0"
   /* _mesa_function_pool[36664]: VertexArrayAttribBinding (will be remapped) */
   "iii\0"
   "glVertexArrayAttribBinding\0"
   "\0"
   /* _mesa_function_pool[36696]: VertexArrayBindingDivisor (will be remapped) */
   "iii\0"
   "glVertexArrayBindingDivisor\0"
   "\0"
   /* _mesa_function_pool[36729]: GetVertexArrayiv (will be remapped) */
   "iip\0"
   "glGetVertexArrayiv\0"
   "\0"
   /* _mesa_function_pool[36753]: GetVertexArrayIndexediv (will be remapped) */
   "iiip\0"
   "glGetVertexArrayIndexediv\0"
   "\0"
   /* _mesa_function_pool[36785]: GetVertexArrayIndexed64iv (will be remapped) */
   "iiip\0"
   "glGetVertexArrayIndexed64iv\0"
   "\0"
   /* _mesa_function_pool[36819]: CreateSamplers (will be remapped) */
   "ip\0"
   "glCreateSamplers\0"
   "\0"
   /* _mesa_function_pool[36840]: CreateProgramPipelines (will be remapped) */
   "ip\0"
   "glCreateProgramPipelines\0"
   "\0"
   /* _mesa_function_pool[36869]: CreateQueries (will be remapped) */
   "iip\0"
   "glCreateQueries\0"
   "\0"
   /* _mesa_function_pool[36890]: GetQueryBufferObjectiv (will be remapped) */
   "iiii\0"
   "glGetQueryBufferObjectiv\0"
   "\0"
   /* _mesa_function_pool[36921]: GetQueryBufferObjectuiv (will be remapped) */
   "iiii\0"
   "glGetQueryBufferObjectuiv\0"
   "\0"
   /* _mesa_function_pool[36953]: GetQueryBufferObjecti64v (will be remapped) */
   "iiii\0"
   "glGetQueryBufferObjecti64v\0"
   "\0"
   /* _mesa_function_pool[36986]: GetQueryBufferObjectui64v (will be remapped) */
   "iiii\0"
   "glGetQueryBufferObjectui64v\0"
   "\0"
   /* _mesa_function_pool[37020]: GetTextureSubImage (will be remapped) */
   "iiiiiiiiiiip\0"
   "glGetTextureSubImage\0"
   "\0"
   /* _mesa_function_pool[37055]: GetCompressedTextureSubImage (will be remapped) */
   "iiiiiiiiip\0"
   "glGetCompressedTextureSubImage\0"
   "\0"
   /* _mesa_function_pool[37098]: TextureBarrierNV (will be remapped) */
   "\0"
   "glTextureBarrier\0"
   "glTextureBarrierNV\0"
   "\0"
   /* _mesa_function_pool[37136]: BufferPageCommitmentARB (will be remapped) */
   "iiii\0"
   "glBufferPageCommitmentARB\0"
   "\0"
   /* _mesa_function_pool[37168]: NamedBufferPageCommitmentEXT (will be remapped) */
   "iiii\0"
   "glNamedBufferPageCommitmentEXT\0"
   "\0"
   /* _mesa_function_pool[37205]: NamedBufferPageCommitmentARB (will be remapped) */
   "iiii\0"
   "glNamedBufferPageCommitmentARB\0"
   "\0"
   /* _mesa_function_pool[37242]: PrimitiveBoundingBox (will be remapped) */
   "ffffffff\0"
   "glPrimitiveBoundingBox\0"
   "glPrimitiveBoundingBoxARB\0"
   "glPrimitiveBoundingBoxEXT\0"
   "glPrimitiveBoundingBoxOES\0"
   "\0"
   /* _mesa_function_pool[37353]: BlendBarrier (will be remapped) */
   "\0"
   "glBlendBarrier\0"
   "glBlendBarrierKHR\0"
   "\0"
   /* _mesa_function_pool[37388]: Uniform1i64ARB (will be remapped) */
   "ii\0"
   "glUniform1i64ARB\0"
   "glUniform1i64NV\0"
   "\0"
   /* _mesa_function_pool[37425]: Uniform2i64ARB (will be remapped) */
   "iii\0"
   "glUniform2i64ARB\0"
   "glUniform2i64NV\0"
   "\0"
   /* _mesa_function_pool[37463]: Uniform3i64ARB (will be remapped) */
   "iiii\0"
   "glUniform3i64ARB\0"
   "glUniform3i64NV\0"
   "\0"
   /* _mesa_function_pool[37502]: Uniform4i64ARB (will be remapped) */
   "iiiii\0"
   "glUniform4i64ARB\0"
   "glUniform4i64NV\0"
   "\0"
   /* _mesa_function_pool[37542]: Uniform1i64vARB (will be remapped) */
   "iip\0"
   "glUniform1i64vARB\0"
   "glUniform1i64vNV\0"
   "\0"
   /* _mesa_function_pool[37582]: Uniform2i64vARB (will be remapped) */
   "iip\0"
   "glUniform2i64vARB\0"
   "glUniform2i64vNV\0"
   "\0"
   /* _mesa_function_pool[37622]: Uniform3i64vARB (will be remapped) */
   "iip\0"
   "glUniform3i64vARB\0"
   "glUniform3i64vNV\0"
   "\0"
   /* _mesa_function_pool[37662]: Uniform4i64vARB (will be remapped) */
   "iip\0"
   "glUniform4i64vARB\0"
   "glUniform4i64vNV\0"
   "\0"
   /* _mesa_function_pool[37702]: Uniform1ui64ARB (will be remapped) */
   "ii\0"
   "glUniform1ui64ARB\0"
   "glUniform1ui64NV\0"
   "\0"
   /* _mesa_function_pool[37741]: Uniform2ui64ARB (will be remapped) */
   "iii\0"
   "glUniform2ui64ARB\0"
   "glUniform2ui64NV\0"
   "\0"
   /* _mesa_function_pool[37781]: Uniform3ui64ARB (will be remapped) */
   "iiii\0"
   "glUniform3ui64ARB\0"
   "glUniform3ui64NV\0"
   "\0"
   /* _mesa_function_pool[37822]: Uniform4ui64ARB (will be remapped) */
   "iiiii\0"
   "glUniform4ui64ARB\0"
   "glUniform4ui64NV\0"
   "\0"
   /* _mesa_function_pool[37864]: Uniform1ui64vARB (will be remapped) */
   "iip\0"
   "glUniform1ui64vARB\0"
   "glUniform1ui64vNV\0"
   "\0"
   /* _mesa_function_pool[37906]: Uniform2ui64vARB (will be remapped) */
   "iip\0"
   "glUniform2ui64vARB\0"
   "glUniform2ui64vNV\0"
   "\0"
   /* _mesa_function_pool[37948]: Uniform3ui64vARB (will be remapped) */
   "iip\0"
   "glUniform3ui64vARB\0"
   "glUniform3ui64vNV\0"
   "\0"
   /* _mesa_function_pool[37990]: Uniform4ui64vARB (will be remapped) */
   "iip\0"
   "glUniform4ui64vARB\0"
   "glUniform4ui64vNV\0"
   "\0"
   /* _mesa_function_pool[38032]: GetUniformi64vARB (will be remapped) */
   "iip\0"
   "glGetUniformi64vARB\0"
   "glGetUniformi64vNV\0"
   "\0"
   /* _mesa_function_pool[38076]: GetUniformui64vARB (will be remapped) */
   "iip\0"
   "glGetUniformui64vARB\0"
   "glGetUniformui64vNV\0"
   "\0"
   /* _mesa_function_pool[38122]: GetnUniformi64vARB (will be remapped) */
   "iiip\0"
   "glGetnUniformi64vARB\0"
   "\0"
   /* _mesa_function_pool[38149]: GetnUniformui64vARB (will be remapped) */
   "iiip\0"
   "glGetnUniformui64vARB\0"
   "\0"
   /* _mesa_function_pool[38177]: ProgramUniform1i64ARB (will be remapped) */
   "iii\0"
   "glProgramUniform1i64ARB\0"
   "glProgramUniform1i64NV\0"
   "\0"
   /* _mesa_function_pool[38229]: ProgramUniform2i64ARB (will be remapped) */
   "iiii\0"
   "glProgramUniform2i64ARB\0"
   "glProgramUniform2i64NV\0"
   "\0"
   /* _mesa_function_pool[38282]: ProgramUniform3i64ARB (will be remapped) */
   "iiiii\0"
   "glProgramUniform3i64ARB\0"
   "glProgramUniform3i64NV\0"
   "\0"
   /* _mesa_function_pool[38336]: ProgramUniform4i64ARB (will be remapped) */
   "iiiiii\0"
   "glProgramUniform4i64ARB\0"
   "glProgramUniform4i64NV\0"
   "\0"
   /* _mesa_function_pool[38391]: ProgramUniform1i64vARB (will be remapped) */
   "iiip\0"
   "glProgramUniform1i64vARB\0"
   "glProgramUniform1i64vNV\0"
   "\0"
   /* _mesa_function_pool[38446]: ProgramUniform2i64vARB (will be remapped) */
   "iiip\0"
   "glProgramUniform2i64vARB\0"
   "glProgramUniform2i64vNV\0"
   "\0"
   /* _mesa_function_pool[38501]: ProgramUniform3i64vARB (will be remapped) */
   "iiip\0"
   "glProgramUniform3i64vARB\0"
   "glProgramUniform3i64vNV\0"
   "\0"
   /* _mesa_function_pool[38556]: ProgramUniform4i64vARB (will be remapped) */
   "iiip\0"
   "glProgramUniform4i64vARB\0"
   "glProgramUniform4i64vNV\0"
   "\0"
   /* _mesa_function_pool[38611]: ProgramUniform1ui64ARB (will be remapped) */
   "iii\0"
   "glProgramUniform1ui64ARB\0"
   "glProgramUniform1ui64NV\0"
   "\0"
   /* _mesa_function_pool[38665]: ProgramUniform2ui64ARB (will be remapped) */
   "iiii\0"
   "glProgramUniform2ui64ARB\0"
   "glProgramUniform2ui64NV\0"
   "\0"
   /* _mesa_function_pool[38720]: ProgramUniform3ui64ARB (will be remapped) */
   "iiiii\0"
   "glProgramUniform3ui64ARB\0"
   "glProgramUniform3ui64NV\0"
   "\0"
   /* _mesa_function_pool[38776]: ProgramUniform4ui64ARB (will be remapped) */
   "iiiiii\0"
   "glProgramUniform4ui64ARB\0"
   "glProgramUniform4ui64NV\0"
   "\0"
   /* _mesa_function_pool[38833]: ProgramUniform1ui64vARB (will be remapped) */
   "iiip\0"
   "glProgramUniform1ui64vARB\0"
   "glProgramUniform1ui64vNV\0"
   "\0"
   /* _mesa_function_pool[38890]: ProgramUniform2ui64vARB (will be remapped) */
   "iiip\0"
   "glProgramUniform2ui64vARB\0"
   "glProgramUniform2ui64vNV\0"
   "\0"
   /* _mesa_function_pool[38947]: ProgramUniform3ui64vARB (will be remapped) */
   "iiip\0"
   "glProgramUniform3ui64vARB\0"
   "glProgramUniform3ui64vNV\0"
   "\0"
   /* _mesa_function_pool[39004]: ProgramUniform4ui64vARB (will be remapped) */
   "iiip\0"
   "glProgramUniform4ui64vARB\0"
   "glProgramUniform4ui64vNV\0"
   "\0"
   /* _mesa_function_pool[39061]: MaxShaderCompilerThreadsKHR (will be remapped) */
   "i\0"
   "glMaxShaderCompilerThreadsKHR\0"
   "glMaxShaderCompilerThreadsARB\0"
   "\0"
   /* _mesa_function_pool[39124]: SpecializeShaderARB (will be remapped) */
   "ipipp\0"
   "glSpecializeShaderARB\0"
   "glSpecializeShader\0"
   "\0"
   /* _mesa_function_pool[39172]: GetTexFilterFuncSGIS (dynamic) */
   "iip\0"
   "glGetTexFilterFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[39200]: TexFilterFuncSGIS (dynamic) */
   "iiip\0"
   "glTexFilterFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[39226]: PixelTexGenParameteriSGIS (dynamic) */
   "ii\0"
   "glPixelTexGenParameteriSGIS\0"
   "\0"
   /* _mesa_function_pool[39258]: PixelTexGenParameterivSGIS (dynamic) */
   "ip\0"
   "glPixelTexGenParameterivSGIS\0"
   "\0"
   /* _mesa_function_pool[39291]: PixelTexGenParameterfSGIS (dynamic) */
   "if\0"
   "glPixelTexGenParameterfSGIS\0"
   "\0"
   /* _mesa_function_pool[39323]: PixelTexGenParameterfvSGIS (dynamic) */
   "ip\0"
   "glPixelTexGenParameterfvSGIS\0"
   "\0"
   /* _mesa_function_pool[39356]: GetPixelTexGenParameterivSGIS (dynamic) */
   "ip\0"
   "glGetPixelTexGenParameterivSGIS\0"
   "\0"
   /* _mesa_function_pool[39392]: GetPixelTexGenParameterfvSGIS (dynamic) */
   "ip\0"
   "glGetPixelTexGenParameterfvSGIS\0"
   "\0"
   /* _mesa_function_pool[39428]: TexImage4DSGIS (dynamic) */
   "iiiiiiiiiip\0"
   "glTexImage4DSGIS\0"
   "\0"
   /* _mesa_function_pool[39458]: TexSubImage4DSGIS (dynamic) */
   "iiiiiiiiiiiip\0"
   "glTexSubImage4DSGIS\0"
   "\0"
   /* _mesa_function_pool[39493]: DetailTexFuncSGIS (dynamic) */
   "iip\0"
   "glDetailTexFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[39518]: GetDetailTexFuncSGIS (dynamic) */
   "ip\0"
   "glGetDetailTexFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[39545]: SharpenTexFuncSGIS (dynamic) */
   "iip\0"
   "glSharpenTexFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[39571]: GetSharpenTexFuncSGIS (dynamic) */
   "ip\0"
   "glGetSharpenTexFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[39599]: SampleMaskSGIS (will be remapped) */
   "fi\0"
   "glSampleMaskSGIS\0"
   "glSampleMaskEXT\0"
   "\0"
   /* _mesa_function_pool[39636]: SamplePatternSGIS (will be remapped) */
   "i\0"
   "glSamplePatternSGIS\0"
   "glSamplePatternEXT\0"
   "\0"
   /* _mesa_function_pool[39678]: ColorPointerEXT (will be remapped) */
   "iiiip\0"
   "glColorPointerEXT\0"
   "\0"
   /* _mesa_function_pool[39703]: EdgeFlagPointerEXT (will be remapped) */
   "iip\0"
   "glEdgeFlagPointerEXT\0"
   "\0"
   /* _mesa_function_pool[39729]: IndexPointerEXT (will be remapped) */
   "iiip\0"
   "glIndexPointerEXT\0"
   "\0"
   /* _mesa_function_pool[39753]: NormalPointerEXT (will be remapped) */
   "iiip\0"
   "glNormalPointerEXT\0"
   "\0"
   /* _mesa_function_pool[39778]: TexCoordPointerEXT (will be remapped) */
   "iiiip\0"
   "glTexCoordPointerEXT\0"
   "\0"
   /* _mesa_function_pool[39806]: VertexPointerEXT (will be remapped) */
   "iiiip\0"
   "glVertexPointerEXT\0"
   "\0"
   /* _mesa_function_pool[39832]: SpriteParameterfSGIX (dynamic) */
   "if\0"
   "glSpriteParameterfSGIX\0"
   "\0"
   /* _mesa_function_pool[39859]: SpriteParameterfvSGIX (dynamic) */
   "ip\0"
   "glSpriteParameterfvSGIX\0"
   "\0"
   /* _mesa_function_pool[39887]: SpriteParameteriSGIX (dynamic) */
   "ii\0"
   "glSpriteParameteriSGIX\0"
   "\0"
   /* _mesa_function_pool[39914]: SpriteParameterivSGIX (dynamic) */
   "ip\0"
   "glSpriteParameterivSGIX\0"
   "\0"
   /* _mesa_function_pool[39942]: GetInstrumentsSGIX (dynamic) */
   "\0"
   "glGetInstrumentsSGIX\0"
   "\0"
   /* _mesa_function_pool[39965]: InstrumentsBufferSGIX (dynamic) */
   "ip\0"
   "glInstrumentsBufferSGIX\0"
   "\0"
   /* _mesa_function_pool[39993]: PollInstrumentsSGIX (dynamic) */
   "p\0"
   "glPollInstrumentsSGIX\0"
   "\0"
   /* _mesa_function_pool[40018]: ReadInstrumentsSGIX (dynamic) */
   "i\0"
   "glReadInstrumentsSGIX\0"
   "\0"
   /* _mesa_function_pool[40043]: StartInstrumentsSGIX (dynamic) */
   "\0"
   "glStartInstrumentsSGIX\0"
   "\0"
   /* _mesa_function_pool[40068]: StopInstrumentsSGIX (dynamic) */
   "i\0"
   "glStopInstrumentsSGIX\0"
   "\0"
   /* _mesa_function_pool[40093]: FrameZoomSGIX (dynamic) */
   "i\0"
   "glFrameZoomSGIX\0"
   "\0"
   /* _mesa_function_pool[40112]: TagSampleBufferSGIX (dynamic) */
   "\0"
   "glTagSampleBufferSGIX\0"
   "\0"
   /* _mesa_function_pool[40136]: ReferencePlaneSGIX (dynamic) */
   "p\0"
   "glReferencePlaneSGIX\0"
   "\0"
   /* _mesa_function_pool[40160]: FlushRasterSGIX (dynamic) */
   "\0"
   "glFlushRasterSGIX\0"
   "\0"
   /* _mesa_function_pool[40180]: FogFuncSGIS (dynamic) */
   "ip\0"
   "glFogFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[40198]: GetFogFuncSGIS (dynamic) */
   "p\0"
   "glGetFogFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[40218]: ImageTransformParameteriHP (dynamic) */
   "iii\0"
   "glImageTransformParameteriHP\0"
   "\0"
   /* _mesa_function_pool[40252]: ImageTransformParameterfHP (dynamic) */
   "iif\0"
   "glImageTransformParameterfHP\0"
   "\0"
   /* _mesa_function_pool[40286]: ImageTransformParameterivHP (dynamic) */
   "iip\0"
   "glImageTransformParameterivHP\0"
   "\0"
   /* _mesa_function_pool[40321]: ImageTransformParameterfvHP (dynamic) */
   "iip\0"
   "glImageTransformParameterfvHP\0"
   "\0"
   /* _mesa_function_pool[40356]: GetImageTransformParameterivHP (dynamic) */
   "iip\0"
   "glGetImageTransformParameterivHP\0"
   "\0"
   /* _mesa_function_pool[40394]: GetImageTransformParameterfvHP (dynamic) */
   "iip\0"
   "glGetImageTransformParameterfvHP\0"
   "\0"
   /* _mesa_function_pool[40432]: HintPGI (dynamic) */
   "ii\0"
   "glHintPGI\0"
   "\0"
   /* _mesa_function_pool[40446]: GetListParameterfvSGIX (dynamic) */
   "iip\0"
   "glGetListParameterfvSGIX\0"
   "\0"
   /* _mesa_function_pool[40476]: GetListParameterivSGIX (dynamic) */
   "iip\0"
   "glGetListParameterivSGIX\0"
   "\0"
   /* _mesa_function_pool[40506]: ListParameterfSGIX (dynamic) */
   "iif\0"
   "glListParameterfSGIX\0"
   "\0"
   /* _mesa_function_pool[40532]: ListParameterfvSGIX (dynamic) */
   "iip\0"
   "glListParameterfvSGIX\0"
   "\0"
   /* _mesa_function_pool[40559]: ListParameteriSGIX (dynamic) */
   "iii\0"
   "glListParameteriSGIX\0"
   "\0"
   /* _mesa_function_pool[40585]: ListParameterivSGIX (dynamic) */
   "iip\0"
   "glListParameterivSGIX\0"
   "\0"
   /* _mesa_function_pool[40612]: IndexMaterialEXT (dynamic) */
   "ii\0"
   "glIndexMaterialEXT\0"
   "\0"
   /* _mesa_function_pool[40635]: IndexFuncEXT (dynamic) */
   "if\0"
   "glIndexFuncEXT\0"
   "\0"
   /* _mesa_function_pool[40654]: LockArraysEXT (will be remapped) */
   "ii\0"
   "glLockArraysEXT\0"
   "\0"
   /* _mesa_function_pool[40674]: UnlockArraysEXT (will be remapped) */
   "\0"
   "glUnlockArraysEXT\0"
   "\0"
   /* _mesa_function_pool[40694]: CullParameterdvEXT (dynamic) */
   "ip\0"
   "glCullParameterdvEXT\0"
   "\0"
   /* _mesa_function_pool[40719]: CullParameterfvEXT (dynamic) */
   "ip\0"
   "glCullParameterfvEXT\0"
   "\0"
   /* _mesa_function_pool[40744]: ViewportArrayv (will be remapped) */
   "iip\0"
   "glViewportArrayv\0"
   "glViewportArrayvOES\0"
   "\0"
   /* _mesa_function_pool[40786]: ViewportIndexedf (will be remapped) */
   "iffff\0"
   "glViewportIndexedf\0"
   "glViewportIndexedfOES\0"
   "\0"
   /* _mesa_function_pool[40834]: ViewportIndexedfv (will be remapped) */
   "ip\0"
   "glViewportIndexedfv\0"
   "glViewportIndexedfvOES\0"
   "\0"
   /* _mesa_function_pool[40881]: ScissorArrayv (will be remapped) */
   "iip\0"
   "glScissorArrayv\0"
   "glScissorArrayvOES\0"
   "\0"
   /* _mesa_function_pool[40921]: ScissorIndexed (will be remapped) */
   "iiiii\0"
   "glScissorIndexed\0"
   "glScissorIndexedOES\0"
   "\0"
   /* _mesa_function_pool[40965]: ScissorIndexedv (will be remapped) */
   "ip\0"
   "glScissorIndexedv\0"
   "glScissorIndexedvOES\0"
   "\0"
   /* _mesa_function_pool[41008]: DepthRangeArrayv (will be remapped) */
   "iip\0"
   "glDepthRangeArrayv\0"
   "\0"
   /* _mesa_function_pool[41032]: DepthRangeIndexed (will be remapped) */
   "idd\0"
   "glDepthRangeIndexed\0"
   "\0"
   /* _mesa_function_pool[41057]: GetFloati_v (will be remapped) */
   "iip\0"
   "glGetFloati_v\0"
   "glGetFloatIndexedvEXT\0"
   "glGetFloati_vEXT\0"
   "glGetFloati_vOES\0"
   "\0"
   /* _mesa_function_pool[41132]: GetDoublei_v (will be remapped) */
   "iip\0"
   "glGetDoublei_v\0"
   "glGetDoubleIndexedvEXT\0"
   "glGetDoublei_vEXT\0"
   "\0"
   /* _mesa_function_pool[41193]: FragmentColorMaterialSGIX (dynamic) */
   "ii\0"
   "glFragmentColorMaterialSGIX\0"
   "\0"
   /* _mesa_function_pool[41225]: FragmentLightfSGIX (dynamic) */
   "iif\0"
   "glFragmentLightfSGIX\0"
   "\0"
   /* _mesa_function_pool[41251]: FragmentLightfvSGIX (dynamic) */
   "iip\0"
   "glFragmentLightfvSGIX\0"
   "\0"
   /* _mesa_function_pool[41278]: FragmentLightiSGIX (dynamic) */
   "iii\0"
   "glFragmentLightiSGIX\0"
   "\0"
   /* _mesa_function_pool[41304]: FragmentLightivSGIX (dynamic) */
   "iip\0"
   "glFragmentLightivSGIX\0"
   "\0"
   /* _mesa_function_pool[41331]: FragmentLightModelfSGIX (dynamic) */
   "if\0"
   "glFragmentLightModelfSGIX\0"
   "\0"
   /* _mesa_function_pool[41361]: FragmentLightModelfvSGIX (dynamic) */
   "ip\0"
   "glFragmentLightModelfvSGIX\0"
   "\0"
   /* _mesa_function_pool[41392]: FragmentLightModeliSGIX (dynamic) */
   "ii\0"
   "glFragmentLightModeliSGIX\0"
   "\0"
   /* _mesa_function_pool[41422]: FragmentLightModelivSGIX (dynamic) */
   "ip\0"
   "glFragmentLightModelivSGIX\0"
   "\0"
   /* _mesa_function_pool[41453]: FragmentMaterialfSGIX (dynamic) */
   "iif\0"
   "glFragmentMaterialfSGIX\0"
   "\0"
   /* _mesa_function_pool[41482]: FragmentMaterialfvSGIX (dynamic) */
   "iip\0"
   "glFragmentMaterialfvSGIX\0"
   "\0"
   /* _mesa_function_pool[41512]: FragmentMaterialiSGIX (dynamic) */
   "iii\0"
   "glFragmentMaterialiSGIX\0"
   "\0"
   /* _mesa_function_pool[41541]: FragmentMaterialivSGIX (dynamic) */
   "iip\0"
   "glFragmentMaterialivSGIX\0"
   "\0"
   /* _mesa_function_pool[41571]: GetFragmentLightfvSGIX (dynamic) */
   "iip\0"
   "glGetFragmentLightfvSGIX\0"
   "\0"
   /* _mesa_function_pool[41601]: GetFragmentLightivSGIX (dynamic) */
   "iip\0"
   "glGetFragmentLightivSGIX\0"
   "\0"
   /* _mesa_function_pool[41631]: GetFragmentMaterialfvSGIX (dynamic) */
   "iip\0"
   "glGetFragmentMaterialfvSGIX\0"
   "\0"
   /* _mesa_function_pool[41664]: GetFragmentMaterialivSGIX (dynamic) */
   "iip\0"
   "glGetFragmentMaterialivSGIX\0"
   "\0"
   /* _mesa_function_pool[41697]: LightEnviSGIX (dynamic) */
   "ii\0"
   "glLightEnviSGIX\0"
   "\0"
   /* _mesa_function_pool[41717]: ApplyTextureEXT (dynamic) */
   "i\0"
   "glApplyTextureEXT\0"
   "\0"
   /* _mesa_function_pool[41738]: TextureLightEXT (dynamic) */
   "i\0"
   "glTextureLightEXT\0"
   "\0"
   /* _mesa_function_pool[41759]: TextureMaterialEXT (dynamic) */
   "ii\0"
   "glTextureMaterialEXT\0"
   "\0"
   /* _mesa_function_pool[41784]: AsyncMarkerSGIX (dynamic) */
   "i\0"
   "glAsyncMarkerSGIX\0"
   "\0"
   /* _mesa_function_pool[41805]: FinishAsyncSGIX (dynamic) */
   "p\0"
   "glFinishAsyncSGIX\0"
   "\0"
   /* _mesa_function_pool[41826]: PollAsyncSGIX (dynamic) */
   "p\0"
   "glPollAsyncSGIX\0"
   "\0"
   /* _mesa_function_pool[41845]: GenAsyncMarkersSGIX (dynamic) */
   "i\0"
   "glGenAsyncMarkersSGIX\0"
   "\0"
   /* _mesa_function_pool[41870]: DeleteAsyncMarkersSGIX (dynamic) */
   "ii\0"
   "glDeleteAsyncMarkersSGIX\0"
   "\0"
   /* _mesa_function_pool[41899]: IsAsyncMarkerSGIX (dynamic) */
   "i\0"
   "glIsAsyncMarkerSGIX\0"
   "\0"
   /* _mesa_function_pool[41922]: VertexPointervINTEL (dynamic) */
   "iip\0"
   "glVertexPointervINTEL\0"
   "\0"
   /* _mesa_function_pool[41949]: NormalPointervINTEL (dynamic) */
   "ip\0"
   "glNormalPointervINTEL\0"
   "\0"
   /* _mesa_function_pool[41975]: ColorPointervINTEL (dynamic) */
   "iip\0"
   "glColorPointervINTEL\0"
   "\0"
   /* _mesa_function_pool[42001]: TexCoordPointervINTEL (dynamic) */
   "iip\0"
   "glTexCoordPointervINTEL\0"
   "\0"
   /* _mesa_function_pool[42030]: PixelTransformParameteriEXT (dynamic) */
   "iii\0"
   "glPixelTransformParameteriEXT\0"
   "\0"
   /* _mesa_function_pool[42065]: PixelTransformParameterfEXT (dynamic) */
   "iif\0"
   "glPixelTransformParameterfEXT\0"
   "\0"
   /* _mesa_function_pool[42100]: PixelTransformParameterivEXT (dynamic) */
   "iip\0"
   "glPixelTransformParameterivEXT\0"
   "\0"
   /* _mesa_function_pool[42136]: PixelTransformParameterfvEXT (dynamic) */
   "iip\0"
   "glPixelTransformParameterfvEXT\0"
   "\0"
   /* _mesa_function_pool[42172]: TextureNormalEXT (dynamic) */
   "i\0"
   "glTextureNormalEXT\0"
   "\0"
   /* _mesa_function_pool[42194]: Tangent3bEXT (dynamic) */
   "iii\0"
   "glTangent3bEXT\0"
   "\0"
   /* _mesa_function_pool[42214]: Tangent3bvEXT (dynamic) */
   "p\0"
   "glTangent3bvEXT\0"
   "\0"
   /* _mesa_function_pool[42233]: Tangent3dEXT (dynamic) */
   "ddd\0"
   "glTangent3dEXT\0"
   "\0"
   /* _mesa_function_pool[42253]: Tangent3dvEXT (dynamic) */
   "p\0"
   "glTangent3dvEXT\0"
   "\0"
   /* _mesa_function_pool[42272]: Tangent3fEXT (dynamic) */
   "fff\0"
   "glTangent3fEXT\0"
   "\0"
   /* _mesa_function_pool[42292]: Tangent3fvEXT (dynamic) */
   "p\0"
   "glTangent3fvEXT\0"
   "\0"
   /* _mesa_function_pool[42311]: Tangent3iEXT (dynamic) */
   "iii\0"
   "glTangent3iEXT\0"
   "\0"
   /* _mesa_function_pool[42331]: Tangent3ivEXT (dynamic) */
   "p\0"
   "glTangent3ivEXT\0"
   "\0"
   /* _mesa_function_pool[42350]: Tangent3sEXT (dynamic) */
   "iii\0"
   "glTangent3sEXT\0"
   "\0"
   /* _mesa_function_pool[42370]: Tangent3svEXT (dynamic) */
   "p\0"
   "glTangent3svEXT\0"
   "\0"
   /* _mesa_function_pool[42389]: Binormal3bEXT (dynamic) */
   "iii\0"
   "glBinormal3bEXT\0"
   "\0"
   /* _mesa_function_pool[42410]: Binormal3bvEXT (dynamic) */
   "p\0"
   "glBinormal3bvEXT\0"
   "\0"
   /* _mesa_function_pool[42430]: Binormal3dEXT (dynamic) */
   "ddd\0"
   "glBinormal3dEXT\0"
   "\0"
   /* _mesa_function_pool[42451]: Binormal3dvEXT (dynamic) */
   "p\0"
   "glBinormal3dvEXT\0"
   "\0"
   /* _mesa_function_pool[42471]: Binormal3fEXT (dynamic) */
   "fff\0"
   "glBinormal3fEXT\0"
   "\0"
   /* _mesa_function_pool[42492]: Binormal3fvEXT (dynamic) */
   "p\0"
   "glBinormal3fvEXT\0"
   "\0"
   /* _mesa_function_pool[42512]: Binormal3iEXT (dynamic) */
   "iii\0"
   "glBinormal3iEXT\0"
   "\0"
   /* _mesa_function_pool[42533]: Binormal3ivEXT (dynamic) */
   "p\0"
   "glBinormal3ivEXT\0"
   "\0"
   /* _mesa_function_pool[42553]: Binormal3sEXT (dynamic) */
   "iii\0"
   "glBinormal3sEXT\0"
   "\0"
   /* _mesa_function_pool[42574]: Binormal3svEXT (dynamic) */
   "p\0"
   "glBinormal3svEXT\0"
   "\0"
   /* _mesa_function_pool[42594]: TangentPointerEXT (dynamic) */
   "iip\0"
   "glTangentPointerEXT\0"
   "\0"
   /* _mesa_function_pool[42619]: BinormalPointerEXT (dynamic) */
   "iip\0"
   "glBinormalPointerEXT\0"
   "\0"
   /* _mesa_function_pool[42645]: PixelTexGenSGIX (dynamic) */
   "i\0"
   "glPixelTexGenSGIX\0"
   "\0"
   /* _mesa_function_pool[42666]: FinishTextureSUNX (dynamic) */
   "\0"
   "glFinishTextureSUNX\0"
   "\0"
   /* _mesa_function_pool[42688]: GlobalAlphaFactorbSUN (dynamic) */
   "i\0"
   "glGlobalAlphaFactorbSUN\0"
   "\0"
   /* _mesa_function_pool[42715]: GlobalAlphaFactorsSUN (dynamic) */
   "i\0"
   "glGlobalAlphaFactorsSUN\0"
   "\0"
   /* _mesa_function_pool[42742]: GlobalAlphaFactoriSUN (dynamic) */
   "i\0"
   "glGlobalAlphaFactoriSUN\0"
   "\0"
   /* _mesa_function_pool[42769]: GlobalAlphaFactorfSUN (dynamic) */
   "f\0"
   "glGlobalAlphaFactorfSUN\0"
   "\0"
   /* _mesa_function_pool[42796]: GlobalAlphaFactordSUN (dynamic) */
   "d\0"
   "glGlobalAlphaFactordSUN\0"
   "\0"
   /* _mesa_function_pool[42823]: GlobalAlphaFactorubSUN (dynamic) */
   "i\0"
   "glGlobalAlphaFactorubSUN\0"
   "\0"
   /* _mesa_function_pool[42851]: GlobalAlphaFactorusSUN (dynamic) */
   "i\0"
   "glGlobalAlphaFactorusSUN\0"
   "\0"
   /* _mesa_function_pool[42879]: GlobalAlphaFactoruiSUN (dynamic) */
   "i\0"
   "glGlobalAlphaFactoruiSUN\0"
   "\0"
   /* _mesa_function_pool[42907]: ReplacementCodeuiSUN (dynamic) */
   "i\0"
   "glReplacementCodeuiSUN\0"
   "\0"
   /* _mesa_function_pool[42933]: ReplacementCodeusSUN (dynamic) */
   "i\0"
   "glReplacementCodeusSUN\0"
   "\0"
   /* _mesa_function_pool[42959]: ReplacementCodeubSUN (dynamic) */
   "i\0"
   "glReplacementCodeubSUN\0"
   "\0"
   /* _mesa_function_pool[42985]: ReplacementCodeuivSUN (dynamic) */
   "p\0"
   "glReplacementCodeuivSUN\0"
   "\0"
   /* _mesa_function_pool[43012]: ReplacementCodeusvSUN (dynamic) */
   "p\0"
   "glReplacementCodeusvSUN\0"
   "\0"
   /* _mesa_function_pool[43039]: ReplacementCodeubvSUN (dynamic) */
   "p\0"
   "glReplacementCodeubvSUN\0"
   "\0"
   /* _mesa_function_pool[43066]: ReplacementCodePointerSUN (dynamic) */
   "iip\0"
   "glReplacementCodePointerSUN\0"
   "\0"
   /* _mesa_function_pool[43099]: Color4ubVertex2fSUN (dynamic) */
   "iiiiff\0"
   "glColor4ubVertex2fSUN\0"
   "\0"
   /* _mesa_function_pool[43129]: Color4ubVertex2fvSUN (dynamic) */
   "pp\0"
   "glColor4ubVertex2fvSUN\0"
   "\0"
   /* _mesa_function_pool[43156]: Color4ubVertex3fSUN (dynamic) */
   "iiiifff\0"
   "glColor4ubVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[43187]: Color4ubVertex3fvSUN (dynamic) */
   "pp\0"
   "glColor4ubVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[43214]: Color3fVertex3fSUN (dynamic) */
   "ffffff\0"
   "glColor3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[43243]: Color3fVertex3fvSUN (dynamic) */
   "pp\0"
   "glColor3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[43269]: Normal3fVertex3fSUN (dynamic) */
   "ffffff\0"
   "glNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[43299]: Normal3fVertex3fvSUN (dynamic) */
   "pp\0"
   "glNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[43326]: Color4fNormal3fVertex3fSUN (dynamic) */
   "ffffffffff\0"
   "glColor4fNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[43367]: Color4fNormal3fVertex3fvSUN (dynamic) */
   "ppp\0"
   "glColor4fNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[43402]: TexCoord2fVertex3fSUN (dynamic) */
   "fffff\0"
   "glTexCoord2fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[43433]: TexCoord2fVertex3fvSUN (dynamic) */
   "pp\0"
   "glTexCoord2fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[43462]: TexCoord4fVertex4fSUN (dynamic) */
   "ffffffff\0"
   "glTexCoord4fVertex4fSUN\0"
   "\0"
   /* _mesa_function_pool[43496]: TexCoord4fVertex4fvSUN (dynamic) */
   "pp\0"
   "glTexCoord4fVertex4fvSUN\0"
   "\0"
   /* _mesa_function_pool[43525]: TexCoord2fColor4ubVertex3fSUN (dynamic) */
   "ffiiiifff\0"
   "glTexCoord2fColor4ubVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[43568]: TexCoord2fColor4ubVertex3fvSUN (dynamic) */
   "ppp\0"
   "glTexCoord2fColor4ubVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[43606]: TexCoord2fColor3fVertex3fSUN (dynamic) */
   "ffffffff\0"
   "glTexCoord2fColor3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[43647]: TexCoord2fColor3fVertex3fvSUN (dynamic) */
   "ppp\0"
   "glTexCoord2fColor3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[43684]: TexCoord2fNormal3fVertex3fSUN (dynamic) */
   "ffffffff\0"
   "glTexCoord2fNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[43726]: TexCoord2fNormal3fVertex3fvSUN (dynamic) */
   "ppp\0"
   "glTexCoord2fNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[43764]: TexCoord2fColor4fNormal3fVertex3fSUN (dynamic) */
   "ffffffffffff\0"
   "glTexCoord2fColor4fNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[43817]: TexCoord2fColor4fNormal3fVertex3fvSUN (dynamic) */
   "pppp\0"
   "glTexCoord2fColor4fNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[43863]: TexCoord4fColor4fNormal3fVertex4fSUN (dynamic) */
   "fffffffffffffff\0"
   "glTexCoord4fColor4fNormal3fVertex4fSUN\0"
   "\0"
   /* _mesa_function_pool[43919]: TexCoord4fColor4fNormal3fVertex4fvSUN (dynamic) */
   "pppp\0"
   "glTexCoord4fColor4fNormal3fVertex4fvSUN\0"
   "\0"
   /* _mesa_function_pool[43965]: ReplacementCodeuiVertex3fSUN (dynamic) */
   "ifff\0"
   "glReplacementCodeuiVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[44002]: ReplacementCodeuiVertex3fvSUN (dynamic) */
   "pp\0"
   "glReplacementCodeuiVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[44038]: ReplacementCodeuiColor4ubVertex3fSUN (dynamic) */
   "iiiiifff\0"
   "glReplacementCodeuiColor4ubVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[44087]: ReplacementCodeuiColor4ubVertex3fvSUN (dynamic) */
   "ppp\0"
   "glReplacementCodeuiColor4ubVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[44132]: ReplacementCodeuiColor3fVertex3fSUN (dynamic) */
   "iffffff\0"
   "glReplacementCodeuiColor3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[44179]: ReplacementCodeuiColor3fVertex3fvSUN (dynamic) */
   "ppp\0"
   "glReplacementCodeuiColor3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[44223]: ReplacementCodeuiNormal3fVertex3fSUN (dynamic) */
   "iffffff\0"
   "glReplacementCodeuiNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[44271]: ReplacementCodeuiNormal3fVertex3fvSUN (dynamic) */
   "ppp\0"
   "glReplacementCodeuiNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[44316]: ReplacementCodeuiColor4fNormal3fVertex3fSUN (dynamic) */
   "iffffffffff\0"
   "glReplacementCodeuiColor4fNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[44375]: ReplacementCodeuiColor4fNormal3fVertex3fvSUN (dynamic) */
   "pppp\0"
   "glReplacementCodeuiColor4fNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[44428]: ReplacementCodeuiTexCoord2fVertex3fSUN (dynamic) */
   "ifffff\0"
   "glReplacementCodeuiTexCoord2fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[44477]: ReplacementCodeuiTexCoord2fVertex3fvSUN (dynamic) */
   "ppp\0"
   "glReplacementCodeuiTexCoord2fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[44524]: ReplacementCodeuiTexCoord2fNormal3fVertex3fSUN (dynamic) */
   "iffffffff\0"
   "glReplacementCodeuiTexCoord2fNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[44584]: ReplacementCodeuiTexCoord2fNormal3fVertex3fvSUN (dynamic) */
   "pppp\0"
   "glReplacementCodeuiTexCoord2fNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[44640]: ReplacementCodeuiTexCoord2fColor4fNormal3fVertex3fSUN (dynamic) */
   "iffffffffffff\0"
   "glReplacementCodeuiTexCoord2fColor4fNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[44711]: ReplacementCodeuiTexCoord2fColor4fNormal3fVertex3fvSUN (dynamic) */
   "ppppp\0"
   "glReplacementCodeuiTexCoord2fColor4fNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[44775]: FramebufferSampleLocationsfvARB (will be remapped) */
   "iiip\0"
   "glFramebufferSampleLocationsfvARB\0"
   "glFramebufferSampleLocationsfvNV\0"
   "\0"
   /* _mesa_function_pool[44848]: NamedFramebufferSampleLocationsfvARB (will be remapped) */
   "iiip\0"
   "glNamedFramebufferSampleLocationsfvARB\0"
   "glNamedFramebufferSampleLocationsfvNV\0"
   "\0"
   /* _mesa_function_pool[44931]: EvaluateDepthValuesARB (will be remapped) */
   "\0"
   "glEvaluateDepthValuesARB\0"
   "glResolveDepthValuesNV\0"
   "\0"
   /* _mesa_function_pool[44981]: VertexWeightfEXT (dynamic) */
   "f\0"
   "glVertexWeightfEXT\0"
   "\0"
   /* _mesa_function_pool[45003]: VertexWeightfvEXT (dynamic) */
   "p\0"
   "glVertexWeightfvEXT\0"
   "\0"
   /* _mesa_function_pool[45026]: VertexWeightPointerEXT (dynamic) */
   "iiip\0"
   "glVertexWeightPointerEXT\0"
   "\0"
   /* _mesa_function_pool[45057]: FlushVertexArrayRangeNV (dynamic) */
   "\0"
   "glFlushVertexArrayRangeNV\0"
   "\0"
   /* _mesa_function_pool[45085]: VertexArrayRangeNV (dynamic) */
   "ip\0"
   "glVertexArrayRangeNV\0"
   "\0"
   /* _mesa_function_pool[45110]: CombinerParameterfvNV (dynamic) */
   "ip\0"
   "glCombinerParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[45138]: CombinerParameterfNV (dynamic) */
   "if\0"
   "glCombinerParameterfNV\0"
   "\0"
   /* _mesa_function_pool[45165]: CombinerParameterivNV (dynamic) */
   "ip\0"
   "glCombinerParameterivNV\0"
   "\0"
   /* _mesa_function_pool[45193]: CombinerParameteriNV (dynamic) */
   "ii\0"
   "glCombinerParameteriNV\0"
   "\0"
   /* _mesa_function_pool[45220]: CombinerInputNV (dynamic) */
   "iiiiii\0"
   "glCombinerInputNV\0"
   "\0"
   /* _mesa_function_pool[45246]: CombinerOutputNV (dynamic) */
   "iiiiiiiiii\0"
   "glCombinerOutputNV\0"
   "\0"
   /* _mesa_function_pool[45277]: FinalCombinerInputNV (dynamic) */
   "iiii\0"
   "glFinalCombinerInputNV\0"
   "\0"
   /* _mesa_function_pool[45306]: GetCombinerInputParameterfvNV (dynamic) */
   "iiiip\0"
   "glGetCombinerInputParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[45345]: GetCombinerInputParameterivNV (dynamic) */
   "iiiip\0"
   "glGetCombinerInputParameterivNV\0"
   "\0"
   /* _mesa_function_pool[45384]: GetCombinerOutputParameterfvNV (dynamic) */
   "iiip\0"
   "glGetCombinerOutputParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[45423]: GetCombinerOutputParameterivNV (dynamic) */
   "iiip\0"
   "glGetCombinerOutputParameterivNV\0"
   "\0"
   /* _mesa_function_pool[45462]: GetFinalCombinerInputParameterfvNV (dynamic) */
   "iip\0"
   "glGetFinalCombinerInputParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[45504]: GetFinalCombinerInputParameterivNV (dynamic) */
   "iip\0"
   "glGetFinalCombinerInputParameterivNV\0"
   "\0"
   /* _mesa_function_pool[45546]: ResizeBuffersMESA (will be remapped) */
   "\0"
   "glResizeBuffersMESA\0"
   "\0"
   /* _mesa_function_pool[45568]: WindowPos4dMESA (will be remapped) */
   "dddd\0"
   "glWindowPos4dMESA\0"
   "\0"
   /* _mesa_function_pool[45592]: WindowPos4dvMESA (will be remapped) */
   "p\0"
   "glWindowPos4dvMESA\0"
   "\0"
   /* _mesa_function_pool[45614]: WindowPos4fMESA (will be remapped) */
   "ffff\0"
   "glWindowPos4fMESA\0"
   "\0"
   /* _mesa_function_pool[45638]: WindowPos4fvMESA (will be remapped) */
   "p\0"
   "glWindowPos4fvMESA\0"
   "\0"
   /* _mesa_function_pool[45660]: WindowPos4iMESA (will be remapped) */
   "iiii\0"
   "glWindowPos4iMESA\0"
   "\0"
   /* _mesa_function_pool[45684]: WindowPos4ivMESA (will be remapped) */
   "p\0"
   "glWindowPos4ivMESA\0"
   "\0"
   /* _mesa_function_pool[45706]: WindowPos4sMESA (will be remapped) */
   "iiii\0"
   "glWindowPos4sMESA\0"
   "\0"
   /* _mesa_function_pool[45730]: WindowPos4svMESA (will be remapped) */
   "p\0"
   "glWindowPos4svMESA\0"
   "\0"
   /* _mesa_function_pool[45752]: MultiModeDrawArraysIBM (will be remapped) */
   "pppii\0"
   "glMultiModeDrawArraysIBM\0"
   "\0"
   /* _mesa_function_pool[45784]: MultiModeDrawElementsIBM (will be remapped) */
   "ppipii\0"
   "glMultiModeDrawElementsIBM\0"
   "\0"
   /* _mesa_function_pool[45819]: ColorPointerListIBM (dynamic) */
   "iiipi\0"
   "glColorPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[45848]: SecondaryColorPointerListIBM (dynamic) */
   "iiipi\0"
   "glSecondaryColorPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[45886]: EdgeFlagPointerListIBM (dynamic) */
   "ipi\0"
   "glEdgeFlagPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[45916]: FogCoordPointerListIBM (dynamic) */
   "iipi\0"
   "glFogCoordPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[45947]: IndexPointerListIBM (dynamic) */
   "iipi\0"
   "glIndexPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[45975]: NormalPointerListIBM (dynamic) */
   "iipi\0"
   "glNormalPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[46004]: TexCoordPointerListIBM (dynamic) */
   "iiipi\0"
   "glTexCoordPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[46036]: VertexPointerListIBM (dynamic) */
   "iiipi\0"
   "glVertexPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[46066]: TbufferMask3DFX (dynamic) */
   "i\0"
   "glTbufferMask3DFX\0"
   "\0"
   /* _mesa_function_pool[46087]: TextureColorMaskSGIS (dynamic) */
   "iiii\0"
   "glTextureColorMaskSGIS\0"
   "\0"
   /* _mesa_function_pool[46116]: DeleteFencesNV (dynamic) */
   "ip\0"
   "glDeleteFencesNV\0"
   "\0"
   /* _mesa_function_pool[46137]: GenFencesNV (dynamic) */
   "ip\0"
   "glGenFencesNV\0"
   "\0"
   /* _mesa_function_pool[46155]: IsFenceNV (dynamic) */
   "i\0"
   "glIsFenceNV\0"
   "\0"
   /* _mesa_function_pool[46170]: TestFenceNV (dynamic) */
   "i\0"
   "glTestFenceNV\0"
   "\0"
   /* _mesa_function_pool[46187]: GetFenceivNV (dynamic) */
   "iip\0"
   "glGetFenceivNV\0"
   "\0"
   /* _mesa_function_pool[46207]: FinishFenceNV (dynamic) */
   "i\0"
   "glFinishFenceNV\0"
   "\0"
   /* _mesa_function_pool[46226]: SetFenceNV (dynamic) */
   "ii\0"
   "glSetFenceNV\0"
   "\0"
   /* _mesa_function_pool[46243]: MapControlPointsNV (dynamic) */
   "iiiiiiiip\0"
   "glMapControlPointsNV\0"
   "\0"
   /* _mesa_function_pool[46275]: MapParameterivNV (dynamic) */
   "iip\0"
   "glMapParameterivNV\0"
   "\0"
   /* _mesa_function_pool[46299]: MapParameterfvNV (dynamic) */
   "iip\0"
   "glMapParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[46323]: GetMapControlPointsNV (dynamic) */
   "iiiiiip\0"
   "glGetMapControlPointsNV\0"
   "\0"
   /* _mesa_function_pool[46356]: GetMapParameterivNV (dynamic) */
   "iip\0"
   "glGetMapParameterivNV\0"
   "\0"
   /* _mesa_function_pool[46383]: GetMapParameterfvNV (dynamic) */
   "iip\0"
   "glGetMapParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[46410]: GetMapAttribParameterivNV (dynamic) */
   "iiip\0"
   "glGetMapAttribParameterivNV\0"
   "\0"
   /* _mesa_function_pool[46444]: GetMapAttribParameterfvNV (dynamic) */
   "iiip\0"
   "glGetMapAttribParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[46478]: EvalMapsNV (dynamic) */
   "ii\0"
   "glEvalMapsNV\0"
   "\0"
   /* _mesa_function_pool[46495]: CombinerStageParameterfvNV (dynamic) */
   "iip\0"
   "glCombinerStageParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[46529]: GetCombinerStageParameterfvNV (dynamic) */
   "iip\0"
   "glGetCombinerStageParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[46566]: AreProgramsResidentNV (will be remapped) */
   "ipp\0"
   "glAreProgramsResidentNV\0"
   "\0"
   /* _mesa_function_pool[46595]: ExecuteProgramNV (will be remapped) */
   "iip\0"
   "glExecuteProgramNV\0"
   "\0"
   /* _mesa_function_pool[46619]: GetProgramParameterdvNV (will be remapped) */
   "iiip\0"
   "glGetProgramParameterdvNV\0"
   "\0"
   /* _mesa_function_pool[46651]: GetProgramParameterfvNV (will be remapped) */
   "iiip\0"
   "glGetProgramParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[46683]: GetProgramivNV (will be remapped) */
   "iip\0"
   "glGetProgramivNV\0"
   "\0"
   /* _mesa_function_pool[46705]: GetProgramStringNV (will be remapped) */
   "iip\0"
   "glGetProgramStringNV\0"
   "\0"
   /* _mesa_function_pool[46731]: GetTrackMatrixivNV (will be remapped) */
   "iiip\0"
   "glGetTrackMatrixivNV\0"
   "\0"
   /* _mesa_function_pool[46758]: GetVertexAttribdvNV (will be remapped) */
   "iip\0"
   "glGetVertexAttribdvNV\0"
   "\0"
   /* _mesa_function_pool[46785]: GetVertexAttribfvNV (will be remapped) */
   "iip\0"
   "glGetVertexAttribfvNV\0"
   "\0"
   /* _mesa_function_pool[46812]: GetVertexAttribivNV (will be remapped) */
   "iip\0"
   "glGetVertexAttribivNV\0"
   "\0"
   /* _mesa_function_pool[46839]: LoadProgramNV (will be remapped) */
   "iiip\0"
   "glLoadProgramNV\0"
   "\0"
   /* _mesa_function_pool[46861]: ProgramParameters4dvNV (will be remapped) */
   "iiip\0"
   "glProgramParameters4dvNV\0"
   "\0"
   /* _mesa_function_pool[46892]: ProgramParameters4fvNV (will be remapped) */
   "iiip\0"
   "glProgramParameters4fvNV\0"
   "\0"
   /* _mesa_function_pool[46923]: RequestResidentProgramsNV (will be remapped) */
   "ip\0"
   "glRequestResidentProgramsNV\0"
   "\0"
   /* _mesa_function_pool[46955]: TrackMatrixNV (will be remapped) */
   "iiii\0"
   "glTrackMatrixNV\0"
   "\0"
   /* _mesa_function_pool[46977]: VertexAttribPointerNV (will be remapped) */
   "iiiip\0"
   "glVertexAttribPointerNV\0"
   "\0"
   /* _mesa_function_pool[47008]: VertexAttrib1sNV (will be remapped) */
   "ii\0"
   "glVertexAttrib1sNV\0"
   "\0"
   /* _mesa_function_pool[47031]: VertexAttrib1svNV (will be remapped) */
   "ip\0"
   "glVertexAttrib1svNV\0"
   "\0"
   /* _mesa_function_pool[47055]: VertexAttrib2sNV (will be remapped) */
   "iii\0"
   "glVertexAttrib2sNV\0"
   "\0"
   /* _mesa_function_pool[47079]: VertexAttrib2svNV (will be remapped) */
   "ip\0"
   "glVertexAttrib2svNV\0"
   "\0"
   /* _mesa_function_pool[47103]: VertexAttrib3sNV (will be remapped) */
   "iiii\0"
   "glVertexAttrib3sNV\0"
   "\0"
   /* _mesa_function_pool[47128]: VertexAttrib3svNV (will be remapped) */
   "ip\0"
   "glVertexAttrib3svNV\0"
   "\0"
   /* _mesa_function_pool[47152]: VertexAttrib4sNV (will be remapped) */
   "iiiii\0"
   "glVertexAttrib4sNV\0"
   "\0"
   /* _mesa_function_pool[47178]: VertexAttrib4svNV (will be remapped) */
   "ip\0"
   "glVertexAttrib4svNV\0"
   "\0"
   /* _mesa_function_pool[47202]: VertexAttrib1fNV (will be remapped) */
   "if\0"
   "glVertexAttrib1fNV\0"
   "\0"
   /* _mesa_function_pool[47225]: VertexAttrib1fvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib1fvNV\0"
   "\0"
   /* _mesa_function_pool[47249]: VertexAttrib2fNV (will be remapped) */
   "iff\0"
   "glVertexAttrib2fNV\0"
   "\0"
   /* _mesa_function_pool[47273]: VertexAttrib2fvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib2fvNV\0"
   "\0"
   /* _mesa_function_pool[47297]: VertexAttrib3fNV (will be remapped) */
   "ifff\0"
   "glVertexAttrib3fNV\0"
   "\0"
   /* _mesa_function_pool[47322]: VertexAttrib3fvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib3fvNV\0"
   "\0"
   /* _mesa_function_pool[47346]: VertexAttrib4fNV (will be remapped) */
   "iffff\0"
   "glVertexAttrib4fNV\0"
   "\0"
   /* _mesa_function_pool[47372]: VertexAttrib4fvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib4fvNV\0"
   "\0"
   /* _mesa_function_pool[47396]: VertexAttrib1dNV (will be remapped) */
   "id\0"
   "glVertexAttrib1dNV\0"
   "\0"
   /* _mesa_function_pool[47419]: VertexAttrib1dvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib1dvNV\0"
   "\0"
   /* _mesa_function_pool[47443]: VertexAttrib2dNV (will be remapped) */
   "idd\0"
   "glVertexAttrib2dNV\0"
   "\0"
   /* _mesa_function_pool[47467]: VertexAttrib2dvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib2dvNV\0"
   "\0"
   /* _mesa_function_pool[47491]: VertexAttrib3dNV (will be remapped) */
   "iddd\0"
   "glVertexAttrib3dNV\0"
   "\0"
   /* _mesa_function_pool[47516]: VertexAttrib3dvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib3dvNV\0"
   "\0"
   /* _mesa_function_pool[47540]: VertexAttrib4dNV (will be remapped) */
   "idddd\0"
   "glVertexAttrib4dNV\0"
   "\0"
   /* _mesa_function_pool[47566]: VertexAttrib4dvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib4dvNV\0"
   "\0"
   /* _mesa_function_pool[47590]: VertexAttrib4ubNV (will be remapped) */
   "iiiii\0"
   "glVertexAttrib4ubNV\0"
   "\0"
   /* _mesa_function_pool[47617]: VertexAttrib4ubvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib4ubvNV\0"
   "\0"
   /* _mesa_function_pool[47642]: VertexAttribs1svNV (will be remapped) */
   "iip\0"
   "glVertexAttribs1svNV\0"
   "\0"
   /* _mesa_function_pool[47668]: VertexAttribs2svNV (will be remapped) */
   "iip\0"
   "glVertexAttribs2svNV\0"
   "\0"
   /* _mesa_function_pool[47694]: VertexAttribs3svNV (will be remapped) */
   "iip\0"
   "glVertexAttribs3svNV\0"
   "\0"
   /* _mesa_function_pool[47720]: VertexAttribs4svNV (will be remapped) */
   "iip\0"
   "glVertexAttribs4svNV\0"
   "\0"
   /* _mesa_function_pool[47746]: VertexAttribs1fvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs1fvNV\0"
   "\0"
   /* _mesa_function_pool[47772]: VertexAttribs2fvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs2fvNV\0"
   "\0"
   /* _mesa_function_pool[47798]: VertexAttribs3fvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs3fvNV\0"
   "\0"
   /* _mesa_function_pool[47824]: VertexAttribs4fvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs4fvNV\0"
   "\0"
   /* _mesa_function_pool[47850]: VertexAttribs1dvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs1dvNV\0"
   "\0"
   /* _mesa_function_pool[47876]: VertexAttribs2dvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs2dvNV\0"
   "\0"
   /* _mesa_function_pool[47902]: VertexAttribs3dvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs3dvNV\0"
   "\0"
   /* _mesa_function_pool[47928]: VertexAttribs4dvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs4dvNV\0"
   "\0"
   /* _mesa_function_pool[47954]: VertexAttribs4ubvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs4ubvNV\0"
   "\0"
   /* _mesa_function_pool[47981]: TexBumpParameterfvATI (will be remapped) */
   "ip\0"
   "glTexBumpParameterfvATI\0"
   "\0"
   /* _mesa_function_pool[48009]: TexBumpParameterivATI (will be remapped) */
   "ip\0"
   "glTexBumpParameterivATI\0"
   "\0"
   /* _mesa_function_pool[48037]: GetTexBumpParameterfvATI (will be remapped) */
   "ip\0"
   "glGetTexBumpParameterfvATI\0"
   "\0"
   /* _mesa_function_pool[48068]: GetTexBumpParameterivATI (will be remapped) */
   "ip\0"
   "glGetTexBumpParameterivATI\0"
   "\0"
   /* _mesa_function_pool[48099]: GenFragmentShadersATI (will be remapped) */
   "i\0"
   "glGenFragmentShadersATI\0"
   "\0"
   /* _mesa_function_pool[48126]: BindFragmentShaderATI (will be remapped) */
   "i\0"
   "glBindFragmentShaderATI\0"
   "\0"
   /* _mesa_function_pool[48153]: DeleteFragmentShaderATI (will be remapped) */
   "i\0"
   "glDeleteFragmentShaderATI\0"
   "\0"
   /* _mesa_function_pool[48182]: BeginFragmentShaderATI (will be remapped) */
   "\0"
   "glBeginFragmentShaderATI\0"
   "\0"
   /* _mesa_function_pool[48209]: EndFragmentShaderATI (will be remapped) */
   "\0"
   "glEndFragmentShaderATI\0"
   "\0"
   /* _mesa_function_pool[48234]: PassTexCoordATI (will be remapped) */
   "iii\0"
   "glPassTexCoordATI\0"
   "\0"
   /* _mesa_function_pool[48257]: SampleMapATI (will be remapped) */
   "iii\0"
   "glSampleMapATI\0"
   "\0"
   /* _mesa_function_pool[48277]: ColorFragmentOp1ATI (will be remapped) */
   "iiiiiii\0"
   "glColorFragmentOp1ATI\0"
   "\0"
   /* _mesa_function_pool[48308]: ColorFragmentOp2ATI (will be remapped) */
   "iiiiiiiiii\0"
   "glColorFragmentOp2ATI\0"
   "\0"
   /* _mesa_function_pool[48342]: ColorFragmentOp3ATI (will be remapped) */
   "iiiiiiiiiiiii\0"
   "glColorFragmentOp3ATI\0"
   "\0"
   /* _mesa_function_pool[48379]: AlphaFragmentOp1ATI (will be remapped) */
   "iiiiii\0"
   "glAlphaFragmentOp1ATI\0"
   "\0"
   /* _mesa_function_pool[48409]: AlphaFragmentOp2ATI (will be remapped) */
   "iiiiiiiii\0"
   "glAlphaFragmentOp2ATI\0"
   "\0"
   /* _mesa_function_pool[48442]: AlphaFragmentOp3ATI (will be remapped) */
   "iiiiiiiiiiii\0"
   "glAlphaFragmentOp3ATI\0"
   "\0"
   /* _mesa_function_pool[48478]: SetFragmentShaderConstantATI (will be remapped) */
   "ip\0"
   "glSetFragmentShaderConstantATI\0"
   "\0"
   /* _mesa_function_pool[48513]: DrawMeshArraysSUN (dynamic) */
   "iiii\0"
   "glDrawMeshArraysSUN\0"
   "\0"
   /* _mesa_function_pool[48539]: ActiveStencilFaceEXT (will be remapped) */
   "i\0"
   "glActiveStencilFaceEXT\0"
   "\0"
   /* _mesa_function_pool[48565]: ObjectPurgeableAPPLE (will be remapped) */
   "iii\0"
   "glObjectPurgeableAPPLE\0"
   "\0"
   /* _mesa_function_pool[48593]: ObjectUnpurgeableAPPLE (will be remapped) */
   "iii\0"
   "glObjectUnpurgeableAPPLE\0"
   "\0"
   /* _mesa_function_pool[48623]: GetObjectParameterivAPPLE (will be remapped) */
   "iiip\0"
   "glGetObjectParameterivAPPLE\0"
   "\0"
   /* _mesa_function_pool[48657]: BindVertexArrayAPPLE (dynamic) */
   "i\0"
   "glBindVertexArrayAPPLE\0"
   "\0"
   /* _mesa_function_pool[48683]: DeleteVertexArraysAPPLE (dynamic) */
   "ip\0"
   "glDeleteVertexArraysAPPLE\0"
   "\0"
   /* _mesa_function_pool[48713]: GenVertexArraysAPPLE (dynamic) */
   "ip\0"
   "glGenVertexArraysAPPLE\0"
   "\0"
   /* _mesa_function_pool[48740]: IsVertexArrayAPPLE (dynamic) */
   "i\0"
   "glIsVertexArrayAPPLE\0"
   "\0"
   /* _mesa_function_pool[48764]: ProgramNamedParameter4fNV (will be remapped) */
   "iipffff\0"
   "glProgramNamedParameter4fNV\0"
   "\0"
   /* _mesa_function_pool[48801]: ProgramNamedParameter4dNV (will be remapped) */
   "iipdddd\0"
   "glProgramNamedParameter4dNV\0"
   "\0"
   /* _mesa_function_pool[48838]: ProgramNamedParameter4fvNV (will be remapped) */
   "iipp\0"
   "glProgramNamedParameter4fvNV\0"
   "\0"
   /* _mesa_function_pool[48873]: ProgramNamedParameter4dvNV (will be remapped) */
   "iipp\0"
   "glProgramNamedParameter4dvNV\0"
   "\0"
   /* _mesa_function_pool[48908]: GetProgramNamedParameterfvNV (will be remapped) */
   "iipp\0"
   "glGetProgramNamedParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[48945]: GetProgramNamedParameterdvNV (will be remapped) */
   "iipp\0"
   "glGetProgramNamedParameterdvNV\0"
   "\0"
   /* _mesa_function_pool[48982]: DepthBoundsEXT (will be remapped) */
   "dd\0"
   "glDepthBoundsEXT\0"
   "\0"
   /* _mesa_function_pool[49003]: BindRenderbufferEXT (will be remapped) */
   "ii\0"
   "glBindRenderbufferEXT\0"
   "\0"
   /* _mesa_function_pool[49029]: BindFramebufferEXT (will be remapped) */
   "ii\0"
   "glBindFramebufferEXT\0"
   "\0"
   /* _mesa_function_pool[49054]: StringMarkerGREMEDY (will be remapped) */
   "ip\0"
   "glStringMarkerGREMEDY\0"
   "\0"
   /* _mesa_function_pool[49080]: ProvokingVertex (will be remapped) */
   "i\0"
   "glProvokingVertexEXT\0"
   "glProvokingVertex\0"
   "\0"
   /* _mesa_function_pool[49122]: ColorMaski (will be remapped) */
   "iiiii\0"
   "glColorMaskIndexedEXT\0"
   "glColorMaski\0"
   "glColorMaskiEXT\0"
   "glColorMaskiOES\0"
   "\0"
   /* _mesa_function_pool[49196]: GetBooleani_v (will be remapped) */
   "iip\0"
   "glGetBooleanIndexedvEXT\0"
   "glGetBooleani_v\0"
   "\0"
   /* _mesa_function_pool[49241]: GetIntegeri_v (will be remapped) */
   "iip\0"
   "glGetIntegerIndexedvEXT\0"
   "glGetIntegeri_v\0"
   "\0"
   /* _mesa_function_pool[49286]: Enablei (will be remapped) */
   "ii\0"
   "glEnableIndexedEXT\0"
   "glEnablei\0"
   "glEnableiEXT\0"
   "glEnableiOES\0"
   "\0"
   /* _mesa_function_pool[49345]: Disablei (will be remapped) */
   "ii\0"
   "glDisableIndexedEXT\0"
   "glDisablei\0"
   "glDisableiEXT\0"
   "glDisableiOES\0"
   "\0"
   /* _mesa_function_pool[49408]: IsEnabledi (will be remapped) */
   "ii\0"
   "glIsEnabledIndexedEXT\0"
   "glIsEnabledi\0"
   "glIsEnablediEXT\0"
   "glIsEnablediOES\0"
   "\0"
   /* _mesa_function_pool[49479]: BufferParameteriAPPLE (will be remapped) */
   "iii\0"
   "glBufferParameteriAPPLE\0"
   "\0"
   /* _mesa_function_pool[49508]: FlushMappedBufferRangeAPPLE (will be remapped) */
   "iii\0"
   "glFlushMappedBufferRangeAPPLE\0"
   "\0"
   /* _mesa_function_pool[49543]: GetPerfMonitorGroupsAMD (will be remapped) */
   "pip\0"
   "glGetPerfMonitorGroupsAMD\0"
   "\0"
   /* _mesa_function_pool[49574]: GetPerfMonitorCountersAMD (will be remapped) */
   "ippip\0"
   "glGetPerfMonitorCountersAMD\0"
   "\0"
   /* _mesa_function_pool[49609]: GetPerfMonitorGroupStringAMD (will be remapped) */
   "iipp\0"
   "glGetPerfMonitorGroupStringAMD\0"
   "\0"
   /* _mesa_function_pool[49646]: GetPerfMonitorCounterStringAMD (will be remapped) */
   "iiipp\0"
   "glGetPerfMonitorCounterStringAMD\0"
   "\0"
   /* _mesa_function_pool[49686]: GetPerfMonitorCounterInfoAMD (will be remapped) */
   "iiip\0"
   "glGetPerfMonitorCounterInfoAMD\0"
   "\0"
   /* _mesa_function_pool[49723]: GenPerfMonitorsAMD (will be remapped) */
   "ip\0"
   "glGenPerfMonitorsAMD\0"
   "\0"
   /* _mesa_function_pool[49748]: DeletePerfMonitorsAMD (will be remapped) */
   "ip\0"
   "glDeletePerfMonitorsAMD\0"
   "\0"
   /* _mesa_function_pool[49776]: SelectPerfMonitorCountersAMD (will be remapped) */
   "iiiip\0"
   "glSelectPerfMonitorCountersAMD\0"
   "\0"
   /* _mesa_function_pool[49814]: BeginPerfMonitorAMD (will be remapped) */
   "i\0"
   "glBeginPerfMonitorAMD\0"
   "\0"
   /* _mesa_function_pool[49839]: EndPerfMonitorAMD (will be remapped) */
   "i\0"
   "glEndPerfMonitorAMD\0"
   "\0"
   /* _mesa_function_pool[49862]: GetPerfMonitorCounterDataAMD (will be remapped) */
   "iiipp\0"
   "glGetPerfMonitorCounterDataAMD\0"
   "\0"
   /* _mesa_function_pool[49900]: TextureRangeAPPLE (dynamic) */
   "iip\0"
   "glTextureRangeAPPLE\0"
   "\0"
   /* _mesa_function_pool[49925]: GetTexParameterPointervAPPLE (dynamic) */
   "iip\0"
   "glGetTexParameterPointervAPPLE\0"
   "\0"
   /* _mesa_function_pool[49961]: UseShaderProgramEXT (will be remapped) */
   "ii\0"
   "glUseShaderProgramEXT\0"
   "\0"
   /* _mesa_function_pool[49987]: ActiveProgramEXT (will be remapped) */
   "i\0"
   "glActiveProgramEXT\0"
   "\0"
   /* _mesa_function_pool[50009]: CreateShaderProgramEXT (will be remapped) */
   "ip\0"
   "glCreateShaderProgramEXT\0"
   "\0"
   /* _mesa_function_pool[50038]: CopyImageSubDataNV (will be remapped) */
   "iiiiiiiiiiiiiii\0"
   "glCopyImageSubDataNV\0"
   "\0"
   /* _mesa_function_pool[50076]: MatrixLoadfEXT (will be remapped) */
   "ip\0"
   "glMatrixLoadfEXT\0"
   "\0"
   /* _mesa_function_pool[50097]: MatrixLoaddEXT (will be remapped) */
   "ip\0"
   "glMatrixLoaddEXT\0"
   "\0"
   /* _mesa_function_pool[50118]: MatrixMultfEXT (will be remapped) */
   "ip\0"
   "glMatrixMultfEXT\0"
   "\0"
   /* _mesa_function_pool[50139]: MatrixMultdEXT (will be remapped) */
   "ip\0"
   "glMatrixMultdEXT\0"
   "\0"
   /* _mesa_function_pool[50160]: MatrixLoadIdentityEXT (will be remapped) */
   "i\0"
   "glMatrixLoadIdentityEXT\0"
   "\0"
   /* _mesa_function_pool[50187]: MatrixRotatefEXT (will be remapped) */
   "iffff\0"
   "glMatrixRotatefEXT\0"
   "\0"
   /* _mesa_function_pool[50213]: MatrixRotatedEXT (will be remapped) */
   "idddd\0"
   "glMatrixRotatedEXT\0"
   "\0"
   /* _mesa_function_pool[50239]: MatrixScalefEXT (will be remapped) */
   "ifff\0"
   "glMatrixScalefEXT\0"
   "\0"
   /* _mesa_function_pool[50263]: MatrixScaledEXT (will be remapped) */
   "iddd\0"
   "glMatrixScaledEXT\0"
   "\0"
   /* _mesa_function_pool[50287]: MatrixTranslatefEXT (will be remapped) */
   "ifff\0"
   "glMatrixTranslatefEXT\0"
   "\0"
   /* _mesa_function_pool[50315]: MatrixTranslatedEXT (will be remapped) */
   "iddd\0"
   "glMatrixTranslatedEXT\0"
   "\0"
   /* _mesa_function_pool[50343]: MatrixOrthoEXT (will be remapped) */
   "idddddd\0"
   "glMatrixOrthoEXT\0"
   "\0"
   /* _mesa_function_pool[50369]: MatrixFrustumEXT (will be remapped) */
   "idddddd\0"
   "glMatrixFrustumEXT\0"
   "\0"
   /* _mesa_function_pool[50397]: MatrixPushEXT (will be remapped) */
   "i\0"
   "glMatrixPushEXT\0"
   "\0"
   /* _mesa_function_pool[50416]: MatrixPopEXT (will be remapped) */
   "i\0"
   "glMatrixPopEXT\0"
   "\0"
   /* _mesa_function_pool[50434]: ClientAttribDefaultEXT (will be remapped) */
   "i\0"
   "glClientAttribDefaultEXT\0"
   "\0"
   /* _mesa_function_pool[50462]: PushClientAttribDefaultEXT (will be remapped) */
   "i\0"
   "glPushClientAttribDefaultEXT\0"
   "\0"
   /* _mesa_function_pool[50494]: GetTextureParameterivEXT (will be remapped) */
   "iiip\0"
   "glGetTextureParameterivEXT\0"
   "\0"
   /* _mesa_function_pool[50527]: GetTextureParameterfvEXT (will be remapped) */
   "iiip\0"
   "glGetTextureParameterfvEXT\0"
   "\0"
   /* _mesa_function_pool[50560]: GetTextureLevelParameterivEXT (will be remapped) */
   "iiiip\0"
   "glGetTextureLevelParameterivEXT\0"
   "\0"
   /* _mesa_function_pool[50599]: GetTextureLevelParameterfvEXT (will be remapped) */
   "iiiip\0"
   "glGetTextureLevelParameterfvEXT\0"
   "\0"
   /* _mesa_function_pool[50638]: TextureParameteriEXT (will be remapped) */
   "iiii\0"
   "glTextureParameteriEXT\0"
   "\0"
   /* _mesa_function_pool[50667]: TextureParameterivEXT (will be remapped) */
   "iiip\0"
   "glTextureParameterivEXT\0"
   "\0"
   /* _mesa_function_pool[50697]: TextureParameterfEXT (will be remapped) */
   "iiif\0"
   "glTextureParameterfEXT\0"
   "\0"
   /* _mesa_function_pool[50726]: TextureParameterfvEXT (will be remapped) */
   "iiip\0"
   "glTextureParameterfvEXT\0"
   "\0"
   /* _mesa_function_pool[50756]: TextureImage1DEXT (will be remapped) */
   "iiiiiiiip\0"
   "glTextureImage1DEXT\0"
   "\0"
   /* _mesa_function_pool[50787]: TextureImage2DEXT (will be remapped) */
   "iiiiiiiiip\0"
   "glTextureImage2DEXT\0"
   "\0"
   /* _mesa_function_pool[50819]: TextureImage3DEXT (will be remapped) */
   "iiiiiiiiiip\0"
   "glTextureImage3DEXT\0"
   "\0"
   /* _mesa_function_pool[50852]: TextureSubImage1DEXT (will be remapped) */
   "iiiiiiip\0"
   "glTextureSubImage1DEXT\0"
   "\0"
   /* _mesa_function_pool[50885]: TextureSubImage2DEXT (will be remapped) */
   "iiiiiiiiip\0"
   "glTextureSubImage2DEXT\0"
   "\0"
   /* _mesa_function_pool[50920]: TextureSubImage3DEXT (will be remapped) */
   "iiiiiiiiiiip\0"
   "glTextureSubImage3DEXT\0"
   "\0"
   /* _mesa_function_pool[50957]: CopyTextureImage1DEXT (will be remapped) */
   "iiiiiiii\0"
   "glCopyTextureImage1DEXT\0"
   "\0"
   /* _mesa_function_pool[50991]: CopyTextureImage2DEXT (will be remapped) */
   "iiiiiiiii\0"
   "glCopyTextureImage2DEXT\0"
   "\0"
   /* _mesa_function_pool[51026]: CopyTextureSubImage1DEXT (will be remapped) */
   "iiiiiii\0"
   "glCopyTextureSubImage1DEXT\0"
   "\0"
   /* _mesa_function_pool[51062]: CopyTextureSubImage2DEXT (will be remapped) */
   "iiiiiiiii\0"
   "glCopyTextureSubImage2DEXT\0"
   "\0"
   /* _mesa_function_pool[51100]: CopyTextureSubImage3DEXT (will be remapped) */
   "iiiiiiiiii\0"
   "glCopyTextureSubImage3DEXT\0"
   "\0"
   /* _mesa_function_pool[51139]: GetTextureImageEXT (will be remapped) */
   "iiiiip\0"
   "glGetTextureImageEXT\0"
   "\0"
   /* _mesa_function_pool[51168]: BindMultiTextureEXT (will be remapped) */
   "iii\0"
   "glBindMultiTextureEXT\0"
   "\0"
   /* _mesa_function_pool[51195]: EnableClientStateiEXT (will be remapped) */
   "ii\0"
   "glEnableClientStateIndexedEXT\0"
   "glEnableClientStateiEXT\0"
   "\0"
   /* _mesa_function_pool[51253]: DisableClientStateiEXT (will be remapped) */
   "ii\0"
   "glDisableClientStateIndexedEXT\0"
   "glDisableClientStateiEXT\0"
   "\0"
   /* _mesa_function_pool[51313]: GetPointerIndexedvEXT (will be remapped) */
   "iip\0"
   "glGetPointerIndexedvEXT\0"
   "glGetPointeri_vEXT\0"
   "\0"
   /* _mesa_function_pool[51361]: MultiTexEnviEXT (will be remapped) */
   "iiii\0"
   "glMultiTexEnviEXT\0"
   "\0"
   /* _mesa_function_pool[51385]: MultiTexEnvivEXT (will be remapped) */
   "iiip\0"
   "glMultiTexEnvivEXT\0"
   "\0"
   /* _mesa_function_pool[51410]: MultiTexEnvfEXT (will be remapped) */
   "iiif\0"
   "glMultiTexEnvfEXT\0"
   "\0"
   /* _mesa_function_pool[51434]: MultiTexEnvfvEXT (will be remapped) */
   "iiip\0"
   "glMultiTexEnvfvEXT\0"
   "\0"
   /* _mesa_function_pool[51459]: GetMultiTexEnvivEXT (will be remapped) */
   "iiip\0"
   "glGetMultiTexEnvivEXT\0"
   "\0"
   /* _mesa_function_pool[51487]: GetMultiTexEnvfvEXT (will be remapped) */
   "iiip\0"
   "glGetMultiTexEnvfvEXT\0"
   "\0"
   /* _mesa_function_pool[51515]: MultiTexParameteriEXT (will be remapped) */
   "iiii\0"
   "glMultiTexParameteriEXT\0"
   "\0"
   /* _mesa_function_pool[51545]: MultiTexParameterivEXT (will be remapped) */
   "iiip\0"
   "glMultiTexParameterivEXT\0"
   "\0"
   /* _mesa_function_pool[51576]: MultiTexParameterfEXT (will be remapped) */
   "iiif\0"
   "glMultiTexParameterfEXT\0"
   "\0"
   /* _mesa_function_pool[51606]: MultiTexParameterfvEXT (will be remapped) */
   "iiip\0"
   "glMultiTexParameterfvEXT\0"
   "\0"
   /* _mesa_function_pool[51637]: GetMultiTexParameterivEXT (will be remapped) */
   "iiip\0"
   "glGetMultiTexParameterivEXT\0"
   "\0"
   /* _mesa_function_pool[51671]: GetMultiTexParameterfvEXT (will be remapped) */
   "iiip\0"
   "glGetMultiTexParameterfvEXT\0"
   "\0"
   /* _mesa_function_pool[51705]: GetMultiTexImageEXT (will be remapped) */
   "iiiiip\0"
   "glGetMultiTexImageEXT\0"
   "\0"
   /* _mesa_function_pool[51735]: GetMultiTexLevelParameterivEXT (will be remapped) */
   "iiiip\0"
   "glGetMultiTexLevelParameterivEXT\0"
   "\0"
   /* _mesa_function_pool[51775]: GetMultiTexLevelParameterfvEXT (will be remapped) */
   "iiiip\0"
   "glGetMultiTexLevelParameterfvEXT\0"
   "\0"
   /* _mesa_function_pool[51815]: MultiTexImage1DEXT (will be remapped) */
   "iiiiiiiip\0"
   "glMultiTexImage1DEXT\0"
   "\0"
   /* _mesa_function_pool[51847]: MultiTexImage2DEXT (will be remapped) */
   "iiiiiiiiip\0"
   "glMultiTexImage2DEXT\0"
   "\0"
   /* _mesa_function_pool[51880]: MultiTexImage3DEXT (will be remapped) */
   "iiiiiiiiiip\0"
   "glMultiTexImage3DEXT\0"
   "\0"
   /* _mesa_function_pool[51914]: MultiTexSubImage1DEXT (will be remapped) */
   "iiiiiiip\0"
   "glMultiTexSubImage1DEXT\0"
   "\0"
   /* _mesa_function_pool[51948]: MultiTexSubImage2DEXT (will be remapped) */
   "iiiiiiiiip\0"
   "glMultiTexSubImage2DEXT\0"
   "\0"
   /* _mesa_function_pool[51984]: MultiTexSubImage3DEXT (will be remapped) */
   "iiiiiiiiiiip\0"
   "glMultiTexSubImage3DEXT\0"
   "\0"
   /* _mesa_function_pool[52022]: CopyMultiTexImage1DEXT (will be remapped) */
   "iiiiiiii\0"
   "glCopyMultiTexImage1DEXT\0"
   "\0"
   /* _mesa_function_pool[52057]: CopyMultiTexImage2DEXT (will be remapped) */
   "iiiiiiiii\0"
   "glCopyMultiTexImage2DEXT\0"
   "\0"
   /* _mesa_function_pool[52093]: CopyMultiTexSubImage1DEXT (will be remapped) */
   "iiiiiii\0"
   "glCopyMultiTexSubImage1DEXT\0"
   "\0"
   /* _mesa_function_pool[52130]: CopyMultiTexSubImage2DEXT (will be remapped) */
   "iiiiiiiii\0"
   "glCopyMultiTexSubImage2DEXT\0"
   "\0"
   /* _mesa_function_pool[52169]: CopyMultiTexSubImage3DEXT (will be remapped) */
   "iiiiiiiiii\0"
   "glCopyMultiTexSubImage3DEXT\0"
   "\0"
   /* _mesa_function_pool[52209]: MultiTexGendEXT (will be remapped) */
   "iiid\0"
   "glMultiTexGendEXT\0"
   "\0"
   /* _mesa_function_pool[52233]: MultiTexGendvEXT (will be remapped) */
   "iiip\0"
   "glMultiTexGendvEXT\0"
   "\0"
   /* _mesa_function_pool[52258]: MultiTexGenfEXT (will be remapped) */
   "iiif\0"
   "glMultiTexGenfEXT\0"
   "\0"
   /* _mesa_function_pool[52282]: MultiTexGenfvEXT (will be remapped) */
   "iiip\0"
   "glMultiTexGenfvEXT\0"
   "\0"
   /* _mesa_function_pool[52307]: MultiTexGeniEXT (will be remapped) */
   "iiii\0"
   "glMultiTexGeniEXT\0"
   "\0"
   /* _mesa_function_pool[52331]: MultiTexGenivEXT (will be remapped) */
   "iiip\0"
   "glMultiTexGenivEXT\0"
   "\0"
   /* _mesa_function_pool[52356]: GetMultiTexGendvEXT (will be remapped) */
   "iiip\0"
   "glGetMultiTexGendvEXT\0"
   "\0"
   /* _mesa_function_pool[52384]: GetMultiTexGenfvEXT (will be remapped) */
   "iiip\0"
   "glGetMultiTexGenfvEXT\0"
   "\0"
   /* _mesa_function_pool[52412]: GetMultiTexGenivEXT (will be remapped) */
   "iiip\0"
   "glGetMultiTexGenivEXT\0"
   "\0"
   /* _mesa_function_pool[52440]: MultiTexCoordPointerEXT (will be remapped) */
   "iiiip\0"
   "glMultiTexCoordPointerEXT\0"
   "\0"
   /* _mesa_function_pool[52473]: MatrixLoadTransposefEXT (will be remapped) */
   "ip\0"
   "glMatrixLoadTransposefEXT\0"
   "\0"
   /* _mesa_function_pool[52503]: MatrixLoadTransposedEXT (will be remapped) */
   "ip\0"
   "glMatrixLoadTransposedEXT\0"
   "\0"
   /* _mesa_function_pool[52533]: MatrixMultTransposefEXT (will be remapped) */
   "ip\0"
   "glMatrixMultTransposefEXT\0"
   "\0"
   /* _mesa_function_pool[52563]: MatrixMultTransposedEXT (will be remapped) */
   "ip\0"
   "glMatrixMultTransposedEXT\0"
   "\0"
   /* _mesa_function_pool[52593]: CompressedTextureImage1DEXT (will be remapped) */
   "iiiiiiip\0"
   "glCompressedTextureImage1DEXT\0"
   "\0"
   /* _mesa_function_pool[52633]: CompressedTextureImage2DEXT (will be remapped) */
   "iiiiiiiip\0"
   "glCompressedTextureImage2DEXT\0"
   "\0"
   /* _mesa_function_pool[52674]: CompressedTextureImage3DEXT (will be remapped) */
   "iiiiiiiiip\0"
   "glCompressedTextureImage3DEXT\0"
   "\0"
   /* _mesa_function_pool[52716]: CompressedTextureSubImage1DEXT (will be remapped) */
   "iiiiiiip\0"
   "glCompressedTextureSubImage1DEXT\0"
   "\0"
   /* _mesa_function_pool[52759]: CompressedTextureSubImage2DEXT (will be remapped) */
   "iiiiiiiiip\0"
   "glCompressedTextureSubImage2DEXT\0"
   "\0"
   /* _mesa_function_pool[52804]: CompressedTextureSubImage3DEXT (will be remapped) */
   "iiiiiiiiiiip\0"
   "glCompressedTextureSubImage3DEXT\0"
   "\0"
   /* _mesa_function_pool[52851]: GetCompressedTextureImageEXT (will be remapped) */
   "iiip\0"
   "glGetCompressedTextureImageEXT\0"
   "\0"
   /* _mesa_function_pool[52888]: CompressedMultiTexImage1DEXT (will be remapped) */
   "iiiiiiip\0"
   "glCompressedMultiTexImage1DEXT\0"
   "\0"
   /* _mesa_function_pool[52929]: CompressedMultiTexImage2DEXT (will be remapped) */
   "iiiiiiiip\0"
   "glCompressedMultiTexImage2DEXT\0"
   "\0"
   /* _mesa_function_pool[52971]: CompressedMultiTexImage3DEXT (will be remapped) */
   "iiiiiiiiip\0"
   "glCompressedMultiTexImage3DEXT\0"
   "\0"
   /* _mesa_function_pool[53014]: CompressedMultiTexSubImage1DEXT (will be remapped) */
   "iiiiiiip\0"
   "glCompressedMultiTexSubImage1DEXT\0"
   "\0"
   /* _mesa_function_pool[53058]: CompressedMultiTexSubImage2DEXT (will be remapped) */
   "iiiiiiiiip\0"
   "glCompressedMultiTexSubImage2DEXT\0"
   "\0"
   /* _mesa_function_pool[53104]: CompressedMultiTexSubImage3DEXT (will be remapped) */
   "iiiiiiiiiiip\0"
   "glCompressedMultiTexSubImage3DEXT\0"
   "\0"
   /* _mesa_function_pool[53152]: GetCompressedMultiTexImageEXT (will be remapped) */
   "iiip\0"
   "glGetCompressedMultiTexImageEXT\0"
   "\0"
   /* _mesa_function_pool[53190]: NamedBufferDataEXT (will be remapped) */
   "iipi\0"
   "glNamedBufferDataEXT\0"
   "\0"
   /* _mesa_function_pool[53217]: NamedBufferSubDataEXT (will be remapped) */
   "iiip\0"
   "glNamedBufferSubDataEXT\0"
   "\0"
   /* _mesa_function_pool[53247]: MapNamedBufferEXT (will be remapped) */
   "ii\0"
   "glMapNamedBufferEXT\0"
   "\0"
   /* _mesa_function_pool[53271]: GetNamedBufferSubDataEXT (will be remapped) */
   "iiip\0"
   "glGetNamedBufferSubDataEXT\0"
   "\0"
   /* _mesa_function_pool[53304]: GetNamedBufferPointervEXT (will be remapped) */
   "iip\0"
   "glGetNamedBufferPointervEXT\0"
   "\0"
   /* _mesa_function_pool[53337]: GetNamedBufferParameterivEXT (will be remapped) */
   "iip\0"
   "glGetNamedBufferParameterivEXT\0"
   "\0"
   /* _mesa_function_pool[53373]: FlushMappedNamedBufferRangeEXT (will be remapped) */
   "iii\0"
   "glFlushMappedNamedBufferRangeEXT\0"
   "\0"
   /* _mesa_function_pool[53411]: MapNamedBufferRangeEXT (will be remapped) */
   "iiii\0"
   "glMapNamedBufferRangeEXT\0"
   "\0"
   /* _mesa_function_pool[53442]: FramebufferDrawBufferEXT (will be remapped) */
   "ii\0"
   "glFramebufferDrawBufferEXT\0"
   "\0"
   /* _mesa_function_pool[53473]: FramebufferDrawBuffersEXT (will be remapped) */
   "iip\0"
   "glFramebufferDrawBuffersEXT\0"
   "\0"
   /* _mesa_function_pool[53506]: FramebufferReadBufferEXT (will be remapped) */
   "ii\0"
   "glFramebufferReadBufferEXT\0"
   "\0"
   /* _mesa_function_pool[53537]: GetFramebufferParameterivEXT (will be remapped) */
   "iip\0"
   "glGetFramebufferParameterivEXT\0"
   "\0"
   /* _mesa_function_pool[53573]: CheckNamedFramebufferStatusEXT (will be remapped) */
   "ii\0"
   "glCheckNamedFramebufferStatusEXT\0"
   "\0"
   /* _mesa_function_pool[53610]: NamedFramebufferTexture1DEXT (will be remapped) */
   "iiiii\0"
   "glNamedFramebufferTexture1DEXT\0"
   "\0"
   /* _mesa_function_pool[53648]: NamedFramebufferTexture2DEXT (will be remapped) */
   "iiiii\0"
   "glNamedFramebufferTexture2DEXT\0"
   "\0"
   /* _mesa_function_pool[53686]: NamedFramebufferTexture3DEXT (will be remapped) */
   "iiiiii\0"
   "glNamedFramebufferTexture3DEXT\0"
   "\0"
   /* _mesa_function_pool[53725]: NamedFramebufferRenderbufferEXT (will be remapped) */
   "iiii\0"
   "glNamedFramebufferRenderbufferEXT\0"
   "\0"
   /* _mesa_function_pool[53765]: GetNamedFramebufferAttachmentParameterivEXT (will be remapped) */
   "iiip\0"
   "glGetNamedFramebufferAttachmentParameterivEXT\0"
   "\0"
   /* _mesa_function_pool[53817]: NamedRenderbufferStorageEXT (will be remapped) */
   "iiii\0"
   "glNamedRenderbufferStorageEXT\0"
   "\0"
   /* _mesa_function_pool[53853]: GetNamedRenderbufferParameterivEXT (will be remapped) */
   "iip\0"
   "glGetNamedRenderbufferParameterivEXT\0"
   "\0"
   /* _mesa_function_pool[53895]: GenerateTextureMipmapEXT (will be remapped) */
   "ii\0"
   "glGenerateTextureMipmapEXT\0"
   "\0"
   /* _mesa_function_pool[53926]: GenerateMultiTexMipmapEXT (will be remapped) */
   "ii\0"
   "glGenerateMultiTexMipmapEXT\0"
   "\0"
   /* _mesa_function_pool[53958]: NamedRenderbufferStorageMultisampleEXT (will be remapped) */
   "iiiii\0"
   "glNamedRenderbufferStorageMultisampleEXT\0"
   "\0"
   /* _mesa_function_pool[54006]: NamedCopyBufferSubDataEXT (will be remapped) */
   "iiiii\0"
   "glNamedCopyBufferSubDataEXT\0"
   "\0"
   /* _mesa_function_pool[54041]: VertexArrayVertexOffsetEXT (will be remapped) */
   "iiiiii\0"
   "glVertexArrayVertexOffsetEXT\0"
   "\0"
   /* _mesa_function_pool[54078]: VertexArrayColorOffsetEXT (will be remapped) */
   "iiiiii\0"
   "glVertexArrayColorOffsetEXT\0"
   "\0"
   /* _mesa_function_pool[54114]: VertexArrayEdgeFlagOffsetEXT (will be remapped) */
   "iiii\0"
   "glVertexArrayEdgeFlagOffsetEXT\0"
   "\0"
   /* _mesa_function_pool[54151]: VertexArrayIndexOffsetEXT (will be remapped) */
   "iiiii\0"
   "glVertexArrayIndexOffsetEXT\0"
   "\0"
   /* _mesa_function_pool[54186]: VertexArrayNormalOffsetEXT (will be remapped) */
   "iiiii\0"
   "glVertexArrayNormalOffsetEXT\0"
   "\0"
   /* _mesa_function_pool[54222]: VertexArrayTexCoordOffsetEXT (will be remapped) */
   "iiiiii\0"
   "glVertexArrayTexCoordOffsetEXT\0"
   "\0"
   /* _mesa_function_pool[54261]: VertexArrayMultiTexCoordOffsetEXT (will be remapped) */
   "iiiiiii\0"
   "glVertexArrayMultiTexCoordOffsetEXT\0"
   "\0"
   /* _mesa_function_pool[54306]: VertexArrayFogCoordOffsetEXT (will be remapped) */
   "iiiii\0"
   "glVertexArrayFogCoordOffsetEXT\0"
   "\0"
   /* _mesa_function_pool[54344]: VertexArraySecondaryColorOffsetEXT (will be remapped) */
   "iiiiii\0"
   "glVertexArraySecondaryColorOffsetEXT\0"
   "\0"
   /* _mesa_function_pool[54389]: VertexArrayVertexAttribOffsetEXT (will be remapped) */
   "iiiiiiii\0"
   "glVertexArrayVertexAttribOffsetEXT\0"
   "\0"
   /* _mesa_function_pool[54434]: VertexArrayVertexAttribIOffsetEXT (will be remapped) */
   "iiiiiii\0"
   "glVertexArrayVertexAttribIOffsetEXT\0"
   "\0"
   /* _mesa_function_pool[54479]: EnableVertexArrayEXT (will be remapped) */
   "ii\0"
   "glEnableVertexArrayEXT\0"
   "\0"
   /* _mesa_function_pool[54506]: DisableVertexArrayEXT (will be remapped) */
   "ii\0"
   "glDisableVertexArrayEXT\0"
   "\0"
   /* _mesa_function_pool[54534]: EnableVertexArrayAttribEXT (will be remapped) */
   "ii\0"
   "glEnableVertexArrayAttribEXT\0"
   "\0"
   /* _mesa_function_pool[54567]: DisableVertexArrayAttribEXT (will be remapped) */
   "ii\0"
   "glDisableVertexArrayAttribEXT\0"
   "\0"
   /* _mesa_function_pool[54601]: GetVertexArrayIntegervEXT (will be remapped) */
   "iip\0"
   "glGetVertexArrayIntegervEXT\0"
   "\0"
   /* _mesa_function_pool[54634]: GetVertexArrayPointervEXT (will be remapped) */
   "iip\0"
   "glGetVertexArrayPointervEXT\0"
   "\0"
   /* _mesa_function_pool[54667]: GetVertexArrayIntegeri_vEXT (will be remapped) */
   "iiip\0"
   "glGetVertexArrayIntegeri_vEXT\0"
   "\0"
   /* _mesa_function_pool[54703]: GetVertexArrayPointeri_vEXT (will be remapped) */
   "iiip\0"
   "glGetVertexArrayPointeri_vEXT\0"
   "\0"
   /* _mesa_function_pool[54739]: NamedProgramStringEXT (will be remapped) */
   "iiiip\0"
   "glNamedProgramStringEXT\0"
   "\0"
   /* _mesa_function_pool[54770]: GetNamedProgramStringEXT (will be remapped) */
   "iiip\0"
   "glGetNamedProgramStringEXT\0"
   "\0"
   /* _mesa_function_pool[54803]: NamedProgramLocalParameter4fEXT (will be remapped) */
   "iiiffff\0"
   "glNamedProgramLocalParameter4fEXT\0"
   "\0"
   /* _mesa_function_pool[54846]: NamedProgramLocalParameter4fvEXT (will be remapped) */
   "iiip\0"
   "glNamedProgramLocalParameter4fvEXT\0"
   "\0"
   /* _mesa_function_pool[54887]: GetNamedProgramLocalParameterfvEXT (will be remapped) */
   "iiip\0"
   "glGetNamedProgramLocalParameterfvEXT\0"
   "\0"
   /* _mesa_function_pool[54930]: NamedProgramLocalParameter4dEXT (will be remapped) */
   "iiidddd\0"
   "glNamedProgramLocalParameter4dEXT\0"
   "\0"
   /* _mesa_function_pool[54973]: NamedProgramLocalParameter4dvEXT (will be remapped) */
   "iiip\0"
   "glNamedProgramLocalParameter4dvEXT\0"
   "\0"
   /* _mesa_function_pool[55014]: GetNamedProgramLocalParameterdvEXT (will be remapped) */
   "iiip\0"
   "glGetNamedProgramLocalParameterdvEXT\0"
   "\0"
   /* _mesa_function_pool[55057]: GetNamedProgramivEXT (will be remapped) */
   "iiip\0"
   "glGetNamedProgramivEXT\0"
   "\0"
   /* _mesa_function_pool[55086]: TextureBufferEXT (will be remapped) */
   "iiii\0"
   "glTextureBufferEXT\0"
   "\0"
   /* _mesa_function_pool[55111]: MultiTexBufferEXT (will be remapped) */
   "iiii\0"
   "glMultiTexBufferEXT\0"
   "\0"
   /* _mesa_function_pool[55137]: TextureParameterIivEXT (will be remapped) */
   "iiip\0"
   "glTextureParameterIivEXT\0"
   "\0"
   /* _mesa_function_pool[55168]: TextureParameterIuivEXT (will be remapped) */
   "iiip\0"
   "glTextureParameterIuivEXT\0"
   "\0"
   /* _mesa_function_pool[55200]: GetTextureParameterIivEXT (will be remapped) */
   "iiip\0"
   "glGetTextureParameterIivEXT\0"
   "\0"
   /* _mesa_function_pool[55234]: GetTextureParameterIuivEXT (will be remapped) */
   "iiip\0"
   "glGetTextureParameterIuivEXT\0"
   "\0"
   /* _mesa_function_pool[55269]: MultiTexParameterIivEXT (will be remapped) */
   "iiip\0"
   "glMultiTexParameterIivEXT\0"
   "\0"
   /* _mesa_function_pool[55301]: MultiTexParameterIuivEXT (will be remapped) */
   "iiip\0"
   "glMultiTexParameterIuivEXT\0"
   "\0"
   /* _mesa_function_pool[55334]: GetMultiTexParameterIivEXT (will be remapped) */
   "iiip\0"
   "glGetMultiTexParameterIivEXT\0"
   "\0"
   /* _mesa_function_pool[55369]: GetMultiTexParameterIuivEXT (will be remapped) */
   "iiip\0"
   "glGetMultiTexParameterIuivEXT\0"
   "\0"
   /* _mesa_function_pool[55405]: NamedProgramLocalParameters4fvEXT (will be remapped) */
   "iiiip\0"
   "glNamedProgramLocalParameters4fvEXT\0"
   "\0"
   /* _mesa_function_pool[55448]: BindImageTextureEXT (will be remapped) */
   "iiiiiii\0"
   "glBindImageTextureEXT\0"
   "\0"
   /* _mesa_function_pool[55479]: SubpixelPrecisionBiasNV (will be remapped) */
   "ii\0"
   "glSubpixelPrecisionBiasNV\0"
   "\0"
   /* _mesa_function_pool[55509]: ConservativeRasterParameterfNV (will be remapped) */
   "if\0"
   "glConservativeRasterParameterfNV\0"
   "\0"
   /* _mesa_function_pool[55546]: ConservativeRasterParameteriNV (will be remapped) */
   "ii\0"
   "glConservativeRasterParameteriNV\0"
   "\0"
   /* _mesa_function_pool[55583]: GetFirstPerfQueryIdINTEL (will be remapped) */
   "p\0"
   "glGetFirstPerfQueryIdINTEL\0"
   "\0"
   /* _mesa_function_pool[55613]: GetNextPerfQueryIdINTEL (will be remapped) */
   "ip\0"
   "glGetNextPerfQueryIdINTEL\0"
   "\0"
   /* _mesa_function_pool[55643]: GetPerfQueryIdByNameINTEL (will be remapped) */
   "pp\0"
   "glGetPerfQueryIdByNameINTEL\0"
   "\0"
   /* _mesa_function_pool[55675]: GetPerfQueryInfoINTEL (will be remapped) */
   "iippppp\0"
   "glGetPerfQueryInfoINTEL\0"
   "\0"
   /* _mesa_function_pool[55708]: GetPerfCounterInfoINTEL (will be remapped) */
   "iiipipppppp\0"
   "glGetPerfCounterInfoINTEL\0"
   "\0"
   /* _mesa_function_pool[55747]: CreatePerfQueryINTEL (will be remapped) */
   "ip\0"
   "glCreatePerfQueryINTEL\0"
   "\0"
   /* _mesa_function_pool[55774]: DeletePerfQueryINTEL (will be remapped) */
   "i\0"
   "glDeletePerfQueryINTEL\0"
   "\0"
   /* _mesa_function_pool[55800]: BeginPerfQueryINTEL (will be remapped) */
   "i\0"
   "glBeginPerfQueryINTEL\0"
   "\0"
   /* _mesa_function_pool[55825]: EndPerfQueryINTEL (will be remapped) */
   "i\0"
   "glEndPerfQueryINTEL\0"
   "\0"
   /* _mesa_function_pool[55848]: GetPerfQueryDataINTEL (will be remapped) */
   "iiipp\0"
   "glGetPerfQueryDataINTEL\0"
   "\0"
   /* _mesa_function_pool[55879]: AlphaToCoverageDitherControlNV (will be remapped) */
   "i\0"
   "glAlphaToCoverageDitherControlNV\0"
   "\0"
   /* _mesa_function_pool[55915]: PolygonOffsetClampEXT (will be remapped) */
   "fff\0"
   "glPolygonOffsetClampEXT\0"
   "glPolygonOffsetClamp\0"
   "\0"
   /* _mesa_function_pool[55965]: WindowRectanglesEXT (will be remapped) */
   "iip\0"
   "glWindowRectanglesEXT\0"
   "\0"
   /* _mesa_function_pool[55992]: FramebufferFetchBarrierEXT (will be remapped) */
   "\0"
   "glFramebufferFetchBarrierEXT\0"
   "\0"
   /* _mesa_function_pool[56023]: RenderbufferStorageMultisampleAdvancedAMD (will be remapped) */
   "iiiiii\0"
   "glRenderbufferStorageMultisampleAdvancedAMD\0"
   "\0"
   /* _mesa_function_pool[56075]: NamedRenderbufferStorageMultisampleAdvancedAMD (will be remapped) */
   "iiiiii\0"
   "glNamedRenderbufferStorageMultisampleAdvancedAMD\0"
   "\0"
   /* _mesa_function_pool[56132]: StencilFuncSeparateATI (will be remapped) */
   "iiii\0"
   "glStencilFuncSeparateATI\0"
   "\0"
   /* _mesa_function_pool[56163]: ProgramEnvParameters4fvEXT (will be remapped) */
   "iiip\0"
   "glProgramEnvParameters4fvEXT\0"
   "\0"
   /* _mesa_function_pool[56198]: ProgramLocalParameters4fvEXT (will be remapped) */
   "iiip\0"
   "glProgramLocalParameters4fvEXT\0"
   "\0"
   /* _mesa_function_pool[56235]: IglooInterfaceSGIX (dynamic) */
   "ip\0"
   "glIglooInterfaceSGIX\0"
   "\0"
   /* _mesa_function_pool[56260]: DeformationMap3dSGIX (dynamic) */
   "iddiiddiiddiip\0"
   "glDeformationMap3dSGIX\0"
   "\0"
   /* _mesa_function_pool[56299]: DeformationMap3fSGIX (dynamic) */
   "iffiiffiiffiip\0"
   "glDeformationMap3fSGIX\0"
   "\0"
   /* _mesa_function_pool[56338]: DeformSGIX (dynamic) */
   "i\0"
   "glDeformSGIX\0"
   "\0"
   /* _mesa_function_pool[56354]: LoadIdentityDeformationMapSGIX (dynamic) */
   "i\0"
   "glLoadIdentityDeformationMapSGIX\0"
   "\0"
   /* _mesa_function_pool[56390]: InternalBufferSubDataCopyMESA (will be remapped) */
   "iiiiiii\0"
   "glInternalBufferSubDataCopyMESA\0"
   "\0"
   /* _mesa_function_pool[56431]: InternalSetError (will be remapped) */
   "i\0"
   "glInternalSetError\0"
   "\0"
   /* _mesa_function_pool[56453]: EGLImageTargetTexture2DOES (will be remapped) */
   "ip\0"
   "glEGLImageTargetTexture2DOES\0"
   "\0"
   /* _mesa_function_pool[56486]: EGLImageTargetRenderbufferStorageOES (will be remapped) */
   "ip\0"
   "glEGLImageTargetRenderbufferStorageOES\0"
   "\0"
   /* _mesa_function_pool[56529]: EGLImageTargetTexStorageEXT (will be remapped) */
   "ipp\0"
   "glEGLImageTargetTexStorageEXT\0"
   "\0"
   /* _mesa_function_pool[56564]: EGLImageTargetTextureStorageEXT (will be remapped) */
   "ipp\0"
   "glEGLImageTargetTextureStorageEXT\0"
   "\0"
   /* _mesa_function_pool[56603]: ClearColorIiEXT (will be remapped) */
   "iiii\0"
   "glClearColorIiEXT\0"
   "\0"
   /* _mesa_function_pool[56627]: ClearColorIuiEXT (will be remapped) */
   "iiii\0"
   "glClearColorIuiEXT\0"
   "\0"
   /* _mesa_function_pool[56652]: TexParameterIiv (will be remapped) */
   "iip\0"
   "glTexParameterIivEXT\0"
   "glTexParameterIiv\0"
   "glTexParameterIivOES\0"
   "\0"
   /* _mesa_function_pool[56717]: TexParameterIuiv (will be remapped) */
   "iip\0"
   "glTexParameterIuivEXT\0"
   "glTexParameterIuiv\0"
   "glTexParameterIuivOES\0"
   "\0"
   /* _mesa_function_pool[56785]: GetTexParameterIiv (will be remapped) */
   "iip\0"
   "glGetTexParameterIivEXT\0"
   "glGetTexParameterIiv\0"
   "glGetTexParameterIivOES\0"
   "\0"
   /* _mesa_function_pool[56859]: GetTexParameterIuiv (will be remapped) */
   "iip\0"
   "glGetTexParameterIuivEXT\0"
   "glGetTexParameterIuiv\0"
   "glGetTexParameterIuivOES\0"
   "\0"
   /* _mesa_function_pool[56936]: VertexAttribI1iEXT (will be remapped) */
   "ii\0"
   "glVertexAttribI1iEXT\0"
   "glVertexAttribI1i\0"
   "\0"
   /* _mesa_function_pool[56979]: VertexAttribI2iEXT (will be remapped) */
   "iii\0"
   "glVertexAttribI2iEXT\0"
   "glVertexAttribI2i\0"
   "\0"
   /* _mesa_function_pool[57023]: VertexAttribI3iEXT (will be remapped) */
   "iiii\0"
   "glVertexAttribI3iEXT\0"
   "glVertexAttribI3i\0"
   "\0"
   /* _mesa_function_pool[57068]: VertexAttribI4iEXT (will be remapped) */
   "iiiii\0"
   "glVertexAttribI4iEXT\0"
   "glVertexAttribI4i\0"
   "\0"
   /* _mesa_function_pool[57114]: VertexAttribI1uiEXT (will be remapped) */
   "ii\0"
   "glVertexAttribI1uiEXT\0"
   "glVertexAttribI1ui\0"
   "\0"
   /* _mesa_function_pool[57159]: VertexAttribI2uiEXT (will be remapped) */
   "iii\0"
   "glVertexAttribI2uiEXT\0"
   "glVertexAttribI2ui\0"
   "\0"
   /* _mesa_function_pool[57205]: VertexAttribI3uiEXT (will be remapped) */
   "iiii\0"
   "glVertexAttribI3uiEXT\0"
   "glVertexAttribI3ui\0"
   "\0"
   /* _mesa_function_pool[57252]: VertexAttribI4uiEXT (will be remapped) */
   "iiiii\0"
   "glVertexAttribI4uiEXT\0"
   "glVertexAttribI4ui\0"
   "\0"
   /* _mesa_function_pool[57300]: VertexAttribI1iv (will be remapped) */
   "ip\0"
   "glVertexAttribI1ivEXT\0"
   "glVertexAttribI1iv\0"
   "\0"
   /* _mesa_function_pool[57345]: VertexAttribI2ivEXT (will be remapped) */
   "ip\0"
   "glVertexAttribI2ivEXT\0"
   "glVertexAttribI2iv\0"
   "\0"
   /* _mesa_function_pool[57390]: VertexAttribI3ivEXT (will be remapped) */
   "ip\0"
   "glVertexAttribI3ivEXT\0"
   "glVertexAttribI3iv\0"
   "\0"
   /* _mesa_function_pool[57435]: VertexAttribI4ivEXT (will be remapped) */
   "ip\0"
   "glVertexAttribI4ivEXT\0"
   "glVertexAttribI4iv\0"
   "\0"
   /* _mesa_function_pool[57480]: VertexAttribI1uiv (will be remapped) */
   "ip\0"
   "glVertexAttribI1uivEXT\0"
   "glVertexAttribI1uiv\0"
   "\0"
   /* _mesa_function_pool[57527]: VertexAttribI2uivEXT (will be remapped) */
   "ip\0"
   "glVertexAttribI2uivEXT\0"
   "glVertexAttribI2uiv\0"
   "\0"
   /* _mesa_function_pool[57574]: VertexAttribI3uivEXT (will be remapped) */
   "ip\0"
   "glVertexAttribI3uivEXT\0"
   "glVertexAttribI3uiv\0"
   "\0"
   /* _mesa_function_pool[57621]: VertexAttribI4uivEXT (will be remapped) */
   "ip\0"
   "glVertexAttribI4uivEXT\0"
   "glVertexAttribI4uiv\0"
   "\0"
   /* _mesa_function_pool[57668]: VertexAttribI4bv (will be remapped) */
   "ip\0"
   "glVertexAttribI4bvEXT\0"
   "glVertexAttribI4bv\0"
   "\0"
   /* _mesa_function_pool[57713]: VertexAttribI4sv (will be remapped) */
   "ip\0"
   "glVertexAttribI4svEXT\0"
   "glVertexAttribI4sv\0"
   "\0"
   /* _mesa_function_pool[57758]: VertexAttribI4ubv (will be remapped) */
   "ip\0"
   "glVertexAttribI4ubvEXT\0"
   "glVertexAttribI4ubv\0"
   "\0"
   /* _mesa_function_pool[57805]: VertexAttribI4usv (will be remapped) */
   "ip\0"
   "glVertexAttribI4usvEXT\0"
   "glVertexAttribI4usv\0"
   "\0"
   /* _mesa_function_pool[57852]: VertexAttribIPointer (will be remapped) */
   "iiiip\0"
   "glVertexAttribIPointerEXT\0"
   "glVertexAttribIPointer\0"
   "\0"
   /* _mesa_function_pool[57908]: GetVertexAttribIiv (will be remapped) */
   "iip\0"
   "glGetVertexAttribIivEXT\0"
   "glGetVertexAttribIiv\0"
   "\0"
   /* _mesa_function_pool[57958]: GetVertexAttribIuiv (will be remapped) */
   "iip\0"
   "glGetVertexAttribIuivEXT\0"
   "glGetVertexAttribIuiv\0"
   "\0"
   /* _mesa_function_pool[58010]: Uniform1ui (will be remapped) */
   "ii\0"
   "glUniform1uiEXT\0"
   "glUniform1ui\0"
   "\0"
   /* _mesa_function_pool[58043]: Uniform2ui (will be remapped) */
   "iii\0"
   "glUniform2uiEXT\0"
   "glUniform2ui\0"
   "\0"
   /* _mesa_function_pool[58077]: Uniform3ui (will be remapped) */
   "iiii\0"
   "glUniform3uiEXT\0"
   "glUniform3ui\0"
   "\0"
   /* _mesa_function_pool[58112]: Uniform4ui (will be remapped) */
   "iiiii\0"
   "glUniform4uiEXT\0"
   "glUniform4ui\0"
   "\0"
   /* _mesa_function_pool[58148]: Uniform1uiv (will be remapped) */
   "iip\0"
   "glUniform1uivEXT\0"
   "glUniform1uiv\0"
   "\0"
   /* _mesa_function_pool[58184]: Uniform2uiv (will be remapped) */
   "iip\0"
   "glUniform2uivEXT\0"
   "glUniform2uiv\0"
   "\0"
   /* _mesa_function_pool[58220]: Uniform3uiv (will be remapped) */
   "iip\0"
   "glUniform3uivEXT\0"
   "glUniform3uiv\0"
   "\0"
   /* _mesa_function_pool[58256]: Uniform4uiv (will be remapped) */
   "iip\0"
   "glUniform4uivEXT\0"
   "glUniform4uiv\0"
   "\0"
   /* _mesa_function_pool[58292]: GetUniformuiv (will be remapped) */
   "iip\0"
   "glGetUniformuivEXT\0"
   "glGetUniformuiv\0"
   "\0"
   /* _mesa_function_pool[58332]: BindFragDataLocation (will be remapped) */
   "iip\0"
   "glBindFragDataLocationEXT\0"
   "glBindFragDataLocation\0"
   "\0"
   /* _mesa_function_pool[58386]: GetFragDataLocation (will be remapped) */
   "ip\0"
   "glGetFragDataLocationEXT\0"
   "glGetFragDataLocation\0"
   "\0"
   /* _mesa_function_pool[58437]: ClearBufferiv (will be remapped) */
   "iip\0"
   "glClearBufferiv\0"
   "\0"
   /* _mesa_function_pool[58458]: ClearBufferuiv (will be remapped) */
   "iip\0"
   "glClearBufferuiv\0"
   "\0"
   /* _mesa_function_pool[58480]: ClearBufferfv (will be remapped) */
   "iip\0"
   "glClearBufferfv\0"
   "\0"
   /* _mesa_function_pool[58501]: ClearBufferfi (will be remapped) */
   "iifi\0"
   "glClearBufferfi\0"
   "\0"
   /* _mesa_function_pool[58523]: GetStringi (will be remapped) */
   "ii\0"
   "glGetStringi\0"
   "\0"
   /* _mesa_function_pool[58540]: BeginTransformFeedback (will be remapped) */
   "i\0"
   "glBeginTransformFeedback\0"
   "glBeginTransformFeedbackEXT\0"
   "\0"
   /* _mesa_function_pool[58596]: EndTransformFeedback (will be remapped) */
   "\0"
   "glEndTransformFeedback\0"
   "glEndTransformFeedbackEXT\0"
   "\0"
   /* _mesa_function_pool[58647]: BindBufferRange (will be remapped) */
   "iiiii\0"
   "glBindBufferRange\0"
   "glBindBufferRangeEXT\0"
   "\0"
   /* _mesa_function_pool[58693]: BindBufferBase (will be remapped) */
   "iii\0"
   "glBindBufferBase\0"
   "glBindBufferBaseEXT\0"
   "\0"
   /* _mesa_function_pool[58735]: TransformFeedbackVaryings (will be remapped) */
   "iipi\0"
   "glTransformFeedbackVaryings\0"
   "glTransformFeedbackVaryingsEXT\0"
   "\0"
   /* _mesa_function_pool[58800]: GetTransformFeedbackVarying (will be remapped) */
   "iiipppp\0"
   "glGetTransformFeedbackVarying\0"
   "glGetTransformFeedbackVaryingEXT\0"
   "\0"
   /* _mesa_function_pool[58872]: BeginConditionalRender (will be remapped) */
   "ii\0"
   "glBeginConditionalRender\0"
   "glBeginConditionalRenderNV\0"
   "\0"
   /* _mesa_function_pool[58928]: EndConditionalRender (will be remapped) */
   "\0"
   "glEndConditionalRender\0"
   "glEndConditionalRenderNV\0"
   "\0"
   /* _mesa_function_pool[58978]: PrimitiveRestartIndex (will be remapped) */
   "i\0"
   "glPrimitiveRestartIndex\0"
   "glPrimitiveRestartIndexNV\0"
   "\0"
   /* _mesa_function_pool[59031]: GetInteger64i_v (will be remapped) */
   "iip\0"
   "glGetInteger64i_v\0"
   "\0"
   /* _mesa_function_pool[59054]: GetBufferParameteri64v (will be remapped) */
   "iip\0"
   "glGetBufferParameteri64v\0"
   "\0"
   /* _mesa_function_pool[59084]: FramebufferTexture (will be remapped) */
   "iiii\0"
   "glFramebufferTexture\0"
   "glFramebufferTextureEXT\0"
   "glFramebufferTextureOES\0"
   "\0"
   /* _mesa_function_pool[59159]: PrimitiveRestartNV (will be remapped) */
   "\0"
   "glPrimitiveRestartNV\0"
   "\0"
   /* _mesa_function_pool[59182]: BindBufferOffsetEXT (will be remapped) */
   "iiii\0"
   "glBindBufferOffsetEXT\0"
   "\0"
   /* _mesa_function_pool[59210]: BindTransformFeedback (will be remapped) */
   "ii\0"
   "glBindTransformFeedback\0"
   "\0"
   /* _mesa_function_pool[59238]: DeleteTransformFeedbacks (will be remapped) */
   "ip\0"
   "glDeleteTransformFeedbacks\0"
   "\0"
   /* _mesa_function_pool[59269]: GenTransformFeedbacks (will be remapped) */
   "ip\0"
   "glGenTransformFeedbacks\0"
   "\0"
   /* _mesa_function_pool[59297]: IsTransformFeedback (will be remapped) */
   "i\0"
   "glIsTransformFeedback\0"
   "\0"
   /* _mesa_function_pool[59322]: PauseTransformFeedback (will be remapped) */
   "\0"
   "glPauseTransformFeedback\0"
   "\0"
   /* _mesa_function_pool[59349]: ResumeTransformFeedback (will be remapped) */
   "\0"
   "glResumeTransformFeedback\0"
   "\0"
   /* _mesa_function_pool[59377]: DrawTransformFeedback (will be remapped) */
   "ii\0"
   "glDrawTransformFeedback\0"
   "\0"
   /* _mesa_function_pool[59405]: VDPAUInitNV (will be remapped) */
   "pp\0"
   "glVDPAUInitNV\0"
   "\0"
   /* _mesa_function_pool[59423]: VDPAUFiniNV (will be remapped) */
   "\0"
   "glVDPAUFiniNV\0"
   "\0"
   /* _mesa_function_pool[59439]: VDPAURegisterVideoSurfaceNV (will be remapped) */
   "piip\0"
   "glVDPAURegisterVideoSurfaceNV\0"
   "\0"
   /* _mesa_function_pool[59475]: VDPAURegisterOutputSurfaceNV (will be remapped) */
   "piip\0"
   "glVDPAURegisterOutputSurfaceNV\0"
   "\0"
   /* _mesa_function_pool[59512]: VDPAUIsSurfaceNV (will be remapped) */
   "i\0"
   "glVDPAUIsSurfaceNV\0"
   "\0"
   /* _mesa_function_pool[59534]: VDPAUUnregisterSurfaceNV (will be remapped) */
   "i\0"
   "glVDPAUUnregisterSurfaceNV\0"
   "\0"
   /* _mesa_function_pool[59564]: VDPAUGetSurfaceivNV (will be remapped) */
   "iiipp\0"
   "glVDPAUGetSurfaceivNV\0"
   "\0"
   /* _mesa_function_pool[59593]: VDPAUSurfaceAccessNV (will be remapped) */
   "ii\0"
   "glVDPAUSurfaceAccessNV\0"
   "\0"
   /* _mesa_function_pool[59620]: VDPAUMapSurfacesNV (will be remapped) */
   "ip\0"
   "glVDPAUMapSurfacesNV\0"
   "\0"
   /* _mesa_function_pool[59645]: VDPAUUnmapSurfacesNV (will be remapped) */
   "ip\0"
   "glVDPAUUnmapSurfacesNV\0"
   "\0"
   /* _mesa_function_pool[59672]: GetUnsignedBytevEXT (will be remapped) */
   "ip\0"
   "glGetUnsignedBytevEXT\0"
   "\0"
   /* _mesa_function_pool[59698]: GetUnsignedBytei_vEXT (will be remapped) */
   "iip\0"
   "glGetUnsignedBytei_vEXT\0"
   "\0"
   /* _mesa_function_pool[59727]: DeleteMemoryObjectsEXT (will be remapped) */
   "ip\0"
   "glDeleteMemoryObjectsEXT\0"
   "\0"
   /* _mesa_function_pool[59756]: IsMemoryObjectEXT (will be remapped) */
   "i\0"
   "glIsMemoryObjectEXT\0"
   "\0"
   /* _mesa_function_pool[59779]: CreateMemoryObjectsEXT (will be remapped) */
   "ip\0"
   "glCreateMemoryObjectsEXT\0"
   "\0"
   /* _mesa_function_pool[59808]: MemoryObjectParameterivEXT (will be remapped) */
   "iip\0"
   "glMemoryObjectParameterivEXT\0"
   "\0"
   /* _mesa_function_pool[59842]: GetMemoryObjectParameterivEXT (will be remapped) */
   "iip\0"
   "glGetMemoryObjectParameterivEXT\0"
   "\0"
   /* _mesa_function_pool[59879]: TexStorageMem2DEXT (will be remapped) */
   "iiiiiii\0"
   "glTexStorageMem2DEXT\0"
   "\0"
   /* _mesa_function_pool[59909]: TexStorageMem2DMultisampleEXT (will be remapped) */
   "iiiiiiii\0"
   "glTexStorageMem2DMultisampleEXT\0"
   "\0"
   /* _mesa_function_pool[59951]: TexStorageMem3DEXT (will be remapped) */
   "iiiiiiii\0"
   "glTexStorageMem3DEXT\0"
   "\0"
   /* _mesa_function_pool[59982]: TexStorageMem3DMultisampleEXT (will be remapped) */
   "iiiiiiiii\0"
   "glTexStorageMem3DMultisampleEXT\0"
   "\0"
   /* _mesa_function_pool[60025]: BufferStorageMemEXT (will be remapped) */
   "iiii\0"
   "glBufferStorageMemEXT\0"
   "\0"
   /* _mesa_function_pool[60053]: TextureStorageMem2DEXT (will be remapped) */
   "iiiiiii\0"
   "glTextureStorageMem2DEXT\0"
   "\0"
   /* _mesa_function_pool[60087]: TextureStorageMem2DMultisampleEXT (will be remapped) */
   "iiiiiiii\0"
   "glTextureStorageMem2DMultisampleEXT\0"
   "\0"
   /* _mesa_function_pool[60133]: TextureStorageMem3DEXT (will be remapped) */
   "iiiiiiii\0"
   "glTextureStorageMem3DEXT\0"
   "\0"
   /* _mesa_function_pool[60168]: TextureStorageMem3DMultisampleEXT (will be remapped) */
   "iiiiiiiii\0"
   "glTextureStorageMem3DMultisampleEXT\0"
   "\0"
   /* _mesa_function_pool[60215]: NamedBufferStorageMemEXT (will be remapped) */
   "iiii\0"
   "glNamedBufferStorageMemEXT\0"
   "\0"
   /* _mesa_function_pool[60248]: TexStorageMem1DEXT (will be remapped) */
   "iiiiii\0"
   "glTexStorageMem1DEXT\0"
   "\0"
   /* _mesa_function_pool[60277]: TextureStorageMem1DEXT (will be remapped) */
   "iiiiii\0"
   "glTextureStorageMem1DEXT\0"
   "\0"
   /* _mesa_function_pool[60310]: GenSemaphoresEXT (will be remapped) */
   "ip\0"
   "glGenSemaphoresEXT\0"
   "\0"
   /* _mesa_function_pool[60333]: DeleteSemaphoresEXT (will be remapped) */
   "ip\0"
   "glDeleteSemaphoresEXT\0"
   "\0"
   /* _mesa_function_pool[60359]: IsSemaphoreEXT (will be remapped) */
   "i\0"
   "glIsSemaphoreEXT\0"
   "\0"
   /* _mesa_function_pool[60379]: SemaphoreParameterui64vEXT (will be remapped) */
   "iip\0"
   "glSemaphoreParameterui64vEXT\0"
   "\0"
   /* _mesa_function_pool[60413]: GetSemaphoreParameterui64vEXT (will be remapped) */
   "iip\0"
   "glGetSemaphoreParameterui64vEXT\0"
   "\0"
   /* _mesa_function_pool[60450]: WaitSemaphoreEXT (will be remapped) */
   "iipipp\0"
   "glWaitSemaphoreEXT\0"
   "\0"
   /* _mesa_function_pool[60477]: SignalSemaphoreEXT (will be remapped) */
   "iipipp\0"
   "glSignalSemaphoreEXT\0"
   "\0"
   /* _mesa_function_pool[60506]: ImportMemoryFdEXT (will be remapped) */
   "iiii\0"
   "glImportMemoryFdEXT\0"
   "\0"
   /* _mesa_function_pool[60532]: ImportSemaphoreFdEXT (will be remapped) */
   "iii\0"
   "glImportSemaphoreFdEXT\0"
   "\0"
   /* _mesa_function_pool[60560]: ViewportSwizzleNV (will be remapped) */
   "iiiii\0"
   "glViewportSwizzleNV\0"
   "\0"
   /* _mesa_function_pool[60587]: Vertex2hNV (will be remapped) */
   "dd\0"
   "glVertex2hNV\0"
   "\0"
   /* _mesa_function_pool[60604]: Vertex2hvNV (will be remapped) */
   "p\0"
   "glVertex2hvNV\0"
   "\0"
   /* _mesa_function_pool[60621]: Vertex3hNV (will be remapped) */
   "ddd\0"
   "glVertex3hNV\0"
   "\0"
   /* _mesa_function_pool[60639]: Vertex3hvNV (will be remapped) */
   "p\0"
   "glVertex3hvNV\0"
   "\0"
   /* _mesa_function_pool[60656]: Vertex4hNV (will be remapped) */
   "dddd\0"
   "glVertex4hNV\0"
   "\0"
   /* _mesa_function_pool[60675]: Vertex4hvNV (will be remapped) */
   "p\0"
   "glVertex4hvNV\0"
   "\0"
   /* _mesa_function_pool[60692]: Normal3hNV (will be remapped) */
   "ddd\0"
   "glNormal3hNV\0"
   "\0"
   /* _mesa_function_pool[60710]: Normal3hvNV (will be remapped) */
   "p\0"
   "glNormal3hvNV\0"
   "\0"
   /* _mesa_function_pool[60727]: Color3hNV (will be remapped) */
   "ddd\0"
   "glColor3hNV\0"
   "\0"
   /* _mesa_function_pool[60744]: Color3hvNV (will be remapped) */
   "p\0"
   "glColor3hvNV\0"
   "\0"
   /* _mesa_function_pool[60760]: Color4hNV (will be remapped) */
   "dddd\0"
   "glColor4hNV\0"
   "\0"
   /* _mesa_function_pool[60778]: Color4hvNV (will be remapped) */
   "p\0"
   "glColor4hvNV\0"
   "\0"
   /* _mesa_function_pool[60794]: TexCoord1hNV (will be remapped) */
   "d\0"
   "glTexCoord1hNV\0"
   "\0"
   /* _mesa_function_pool[60812]: TexCoord1hvNV (will be remapped) */
   "p\0"
   "glTexCoord1hvNV\0"
   "\0"
   /* _mesa_function_pool[60831]: TexCoord2hNV (will be remapped) */
   "dd\0"
   "glTexCoord2hNV\0"
   "\0"
   /* _mesa_function_pool[60850]: TexCoord2hvNV (will be remapped) */
   "p\0"
   "glTexCoord2hvNV\0"
   "\0"
   /* _mesa_function_pool[60869]: TexCoord3hNV (will be remapped) */
   "ddd\0"
   "glTexCoord3hNV\0"
   "\0"
   /* _mesa_function_pool[60889]: TexCoord3hvNV (will be remapped) */
   "p\0"
   "glTexCoord3hvNV\0"
   "\0"
   /* _mesa_function_pool[60908]: TexCoord4hNV (will be remapped) */
   "dddd\0"
   "glTexCoord4hNV\0"
   "\0"
   /* _mesa_function_pool[60929]: TexCoord4hvNV (will be remapped) */
   "p\0"
   "glTexCoord4hvNV\0"
   "\0"
   /* _mesa_function_pool[60948]: MultiTexCoord1hNV (will be remapped) */
   "id\0"
   "glMultiTexCoord1hNV\0"
   "\0"
   /* _mesa_function_pool[60972]: MultiTexCoord1hvNV (will be remapped) */
   "ip\0"
   "glMultiTexCoord1hvNV\0"
   "\0"
   /* _mesa_function_pool[60997]: MultiTexCoord2hNV (will be remapped) */
   "idd\0"
   "glMultiTexCoord2hNV\0"
   "\0"
   /* _mesa_function_pool[61022]: MultiTexCoord2hvNV (will be remapped) */
   "ip\0"
   "glMultiTexCoord2hvNV\0"
   "\0"
   /* _mesa_function_pool[61047]: MultiTexCoord3hNV (will be remapped) */
   "iddd\0"
   "glMultiTexCoord3hNV\0"
   "\0"
   /* _mesa_function_pool[61073]: MultiTexCoord3hvNV (will be remapped) */
   "ip\0"
   "glMultiTexCoord3hvNV\0"
   "\0"
   /* _mesa_function_pool[61098]: MultiTexCoord4hNV (will be remapped) */
   "idddd\0"
   "glMultiTexCoord4hNV\0"
   "\0"
   /* _mesa_function_pool[61125]: MultiTexCoord4hvNV (will be remapped) */
   "ip\0"
   "glMultiTexCoord4hvNV\0"
   "\0"
   /* _mesa_function_pool[61150]: VertexAttrib1hNV (will be remapped) */
   "id\0"
   "glVertexAttrib1hNV\0"
   "\0"
   /* _mesa_function_pool[61173]: VertexAttrib1hvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib1hvNV\0"
   "\0"
   /* _mesa_function_pool[61197]: VertexAttrib2hNV (will be remapped) */
   "idd\0"
   "glVertexAttrib2hNV\0"
   "\0"
   /* _mesa_function_pool[61221]: VertexAttrib2hvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib2hvNV\0"
   "\0"
   /* _mesa_function_pool[61245]: VertexAttrib3hNV (will be remapped) */
   "iddd\0"
   "glVertexAttrib3hNV\0"
   "\0"
   /* _mesa_function_pool[61270]: VertexAttrib3hvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib3hvNV\0"
   "\0"
   /* _mesa_function_pool[61294]: VertexAttrib4hNV (will be remapped) */
   "idddd\0"
   "glVertexAttrib4hNV\0"
   "\0"
   /* _mesa_function_pool[61320]: VertexAttrib4hvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib4hvNV\0"
   "\0"
   /* _mesa_function_pool[61344]: VertexAttribs1hvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs1hvNV\0"
   "\0"
   /* _mesa_function_pool[61370]: VertexAttribs2hvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs2hvNV\0"
   "\0"
   /* _mesa_function_pool[61396]: VertexAttribs3hvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs3hvNV\0"
   "\0"
   /* _mesa_function_pool[61422]: VertexAttribs4hvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs4hvNV\0"
   "\0"
   /* _mesa_function_pool[61448]: FogCoordhNV (will be remapped) */
   "d\0"
   "glFogCoordhNV\0"
   "\0"
   /* _mesa_function_pool[61465]: FogCoordhvNV (will be remapped) */
   "p\0"
   "glFogCoordhvNV\0"
   "\0"
   /* _mesa_function_pool[61483]: SecondaryColor3hNV (will be remapped) */
   "ddd\0"
   "glSecondaryColor3hNV\0"
   "\0"
   /* _mesa_function_pool[61509]: SecondaryColor3hvNV (will be remapped) */
   "p\0"
   "glSecondaryColor3hvNV\0"
   "\0"
   /* _mesa_function_pool[61534]: MemoryBarrierByRegion (will be remapped) */
   "i\0"
   "glMemoryBarrierByRegion\0"
   "\0"
   /* _mesa_function_pool[61561]: AlphaFuncx (will be remapped) */
   "ii\0"
   "glAlphaFuncxOES\0"
   "glAlphaFuncx\0"
   "\0"
   /* _mesa_function_pool[61594]: ClearColorx (will be remapped) */
   "iiii\0"
   "glClearColorxOES\0"
   "glClearColorx\0"
   "\0"
   /* _mesa_function_pool[61631]: ClearDepthx (will be remapped) */
   "i\0"
   "glClearDepthxOES\0"
   "glClearDepthx\0"
   "\0"
   /* _mesa_function_pool[61665]: Color4x (will be remapped) */
   "iiii\0"
   "glColor4xOES\0"
   "glColor4x\0"
   "\0"
   /* _mesa_function_pool[61694]: DepthRangex (will be remapped) */
   "ii\0"
   "glDepthRangexOES\0"
   "glDepthRangex\0"
   "\0"
   /* _mesa_function_pool[61729]: Fogx (will be remapped) */
   "ii\0"
   "glFogxOES\0"
   "glFogx\0"
   "\0"
   /* _mesa_function_pool[61750]: Fogxv (will be remapped) */
   "ip\0"
   "glFogxvOES\0"
   "glFogxv\0"
   "\0"
   /* _mesa_function_pool[61773]: Frustumx (will be remapped) */
   "iiiiii\0"
   "glFrustumxOES\0"
   "glFrustumx\0"
   "\0"
   /* _mesa_function_pool[61806]: LightModelx (will be remapped) */
   "ii\0"
   "glLightModelxOES\0"
   "glLightModelx\0"
   "\0"
   /* _mesa_function_pool[61841]: LightModelxv (will be remapped) */
   "ip\0"
   "glLightModelxvOES\0"
   "glLightModelxv\0"
   "\0"
   /* _mesa_function_pool[61878]: Lightx (will be remapped) */
   "iii\0"
   "glLightxOES\0"
   "glLightx\0"
   "\0"
   /* _mesa_function_pool[61904]: Lightxv (will be remapped) */
   "iip\0"
   "glLightxvOES\0"
   "glLightxv\0"
   "\0"
   /* _mesa_function_pool[61932]: LineWidthx (will be remapped) */
   "i\0"
   "glLineWidthxOES\0"
   "glLineWidthx\0"
   "\0"
   /* _mesa_function_pool[61964]: LoadMatrixx (will be remapped) */
   "p\0"
   "glLoadMatrixxOES\0"
   "glLoadMatrixx\0"
   "\0"
   /* _mesa_function_pool[61998]: Materialx (will be remapped) */
   "iii\0"
   "glMaterialxOES\0"
   "glMaterialx\0"
   "\0"
   /* _mesa_function_pool[62030]: Materialxv (will be remapped) */
   "iip\0"
   "glMaterialxvOES\0"
   "glMaterialxv\0"
   "\0"
   /* _mesa_function_pool[62064]: MultMatrixx (will be remapped) */
   "p\0"
   "glMultMatrixxOES\0"
   "glMultMatrixx\0"
   "\0"
   /* _mesa_function_pool[62098]: MultiTexCoord4x (will be remapped) */
   "iiiii\0"
   "glMultiTexCoord4xOES\0"
   "glMultiTexCoord4x\0"
   "\0"
   /* _mesa_function_pool[62144]: Normal3x (will be remapped) */
   "iii\0"
   "glNormal3xOES\0"
   "glNormal3x\0"
   "\0"
   /* _mesa_function_pool[62174]: Orthox (will be remapped) */
   "iiiiii\0"
   "glOrthoxOES\0"
   "glOrthox\0"
   "\0"
   /* _mesa_function_pool[62203]: PointSizex (will be remapped) */
   "i\0"
   "glPointSizexOES\0"
   "glPointSizex\0"
   "\0"
   /* _mesa_function_pool[62235]: PolygonOffsetx (will be remapped) */
   "ii\0"
   "glPolygonOffsetxOES\0"
   "glPolygonOffsetx\0"
   "\0"
   /* _mesa_function_pool[62276]: Rotatex (will be remapped) */
   "iiii\0"
   "glRotatexOES\0"
   "glRotatex\0"
   "\0"
   /* _mesa_function_pool[62305]: SampleCoveragex (will be remapped) */
   "ii\0"
   "glSampleCoveragexOES\0"
   "glSampleCoveragex\0"
   "\0"
   /* _mesa_function_pool[62348]: Scalex (will be remapped) */
   "iii\0"
   "glScalexOES\0"
   "glScalex\0"
   "\0"
   /* _mesa_function_pool[62374]: TexEnvx (will be remapped) */
   "iii\0"
   "glTexEnvxOES\0"
   "glTexEnvx\0"
   "\0"
   /* _mesa_function_pool[62402]: TexEnvxv (will be remapped) */
   "iip\0"
   "glTexEnvxvOES\0"
   "glTexEnvxv\0"
   "\0"
   /* _mesa_function_pool[62432]: TexParameterx (will be remapped) */
   "iii\0"
   "glTexParameterxOES\0"
   "glTexParameterx\0"
   "\0"
   /* _mesa_function_pool[62472]: Translatex (will be remapped) */
   "iii\0"
   "glTranslatexOES\0"
   "glTranslatex\0"
   "\0"
   /* _mesa_function_pool[62506]: ClipPlanex (will be remapped) */
   "ip\0"
   "glClipPlanexOES\0"
   "glClipPlanex\0"
   "\0"
   /* _mesa_function_pool[62539]: GetClipPlanex (will be remapped) */
   "ip\0"
   "glGetClipPlanexOES\0"
   "glGetClipPlanex\0"
   "\0"
   /* _mesa_function_pool[62578]: GetFixedv (will be remapped) */
   "ip\0"
   "glGetFixedvOES\0"
   "glGetFixedv\0"
   "\0"
   /* _mesa_function_pool[62609]: GetLightxv (will be remapped) */
   "iip\0"
   "glGetLightxvOES\0"
   "glGetLightxv\0"
   "\0"
   /* _mesa_function_pool[62643]: GetMaterialxv (will be remapped) */
   "iip\0"
   "glGetMaterialxvOES\0"
   "glGetMaterialxv\0"
   "\0"
   /* _mesa_function_pool[62683]: GetTexEnvxv (will be remapped) */
   "iip\0"
   "glGetTexEnvxvOES\0"
   "glGetTexEnvxv\0"
   "\0"
   /* _mesa_function_pool[62719]: GetTexParameterxv (will be remapped) */
   "iip\0"
   "glGetTexParameterxvOES\0"
   "glGetTexParameterxv\0"
   "\0"
   /* _mesa_function_pool[62767]: PointParameterx (will be remapped) */
   "ii\0"
   "glPointParameterxOES\0"
   "glPointParameterx\0"
   "\0"
   /* _mesa_function_pool[62810]: PointParameterxv (will be remapped) */
   "ip\0"
   "glPointParameterxvOES\0"
   "glPointParameterxv\0"
   "\0"
   /* _mesa_function_pool[62855]: TexParameterxv (will be remapped) */
   "iip\0"
   "glTexParameterxvOES\0"
   "glTexParameterxv\0"
   "\0"
   /* _mesa_function_pool[62897]: GetTexGenxvOES (will be remapped) */
   "iip\0"
   "glGetTexGenxvOES\0"
   "\0"
   /* _mesa_function_pool[62919]: TexGenxOES (will be remapped) */
   "iii\0"
   "glTexGenxOES\0"
   "\0"
   /* _mesa_function_pool[62937]: TexGenxvOES (will be remapped) */
   "iip\0"
   "glTexGenxvOES\0"
   "\0"
   /* _mesa_function_pool[62956]: ClipPlanef (will be remapped) */
   "ip\0"
   "glClipPlanefOES\0"
   "glClipPlanef\0"
   "\0"
   /* _mesa_function_pool[62989]: GetClipPlanef (will be remapped) */
   "ip\0"
   "glGetClipPlanefOES\0"
   "glGetClipPlanef\0"
   "\0"
   /* _mesa_function_pool[63028]: Frustumf (will be remapped) */
   "ffffff\0"
   "glFrustumfOES\0"
   "glFrustumf\0"
   "\0"
   /* _mesa_function_pool[63061]: Orthof (will be remapped) */
   "ffffff\0"
   "glOrthofOES\0"
   "glOrthof\0"
   "\0"
   /* _mesa_function_pool[63090]: DrawTexiOES (will be remapped) */
   "iiiii\0"
   "glDrawTexiOES\0"
   "\0"
   /* _mesa_function_pool[63111]: DrawTexivOES (will be remapped) */
   "p\0"
   "glDrawTexivOES\0"
   "\0"
   /* _mesa_function_pool[63129]: DrawTexfOES (will be remapped) */
   "fffff\0"
   "glDrawTexfOES\0"
   "\0"
   /* _mesa_function_pool[63150]: DrawTexfvOES (will be remapped) */
   "p\0"
   "glDrawTexfvOES\0"
   "\0"
   /* _mesa_function_pool[63168]: DrawTexsOES (will be remapped) */
   "iiiii\0"
   "glDrawTexsOES\0"
   "\0"
   /* _mesa_function_pool[63189]: DrawTexsvOES (will be remapped) */
   "p\0"
   "glDrawTexsvOES\0"
   "\0"
   /* _mesa_function_pool[63207]: DrawTexxOES (will be remapped) */
   "iiiii\0"
   "glDrawTexxOES\0"
   "\0"
   /* _mesa_function_pool[63228]: DrawTexxvOES (will be remapped) */
   "p\0"
   "glDrawTexxvOES\0"
   "\0"
   /* _mesa_function_pool[63246]: LoadPaletteFromModelViewMatrixOES (dynamic) */
   "\0"
   "glLoadPaletteFromModelViewMatrixOES\0"
   "\0"
   /* _mesa_function_pool[63284]: PointSizePointerOES (will be remapped) */
   "iip\0"
   "glPointSizePointerOES\0"
   "\0"
   /* _mesa_function_pool[63311]: QueryMatrixxOES (will be remapped) */
   "pp\0"
   "glQueryMatrixxOES\0"
   "\0"
   /* _mesa_function_pool[63333]: DiscardFramebufferEXT (will be remapped) */
   "iip\0"
   "glDiscardFramebufferEXT\0"
   "\0"
   /* _mesa_function_pool[63362]: FramebufferTexture2DMultisampleEXT (will be remapped) */
   "iiiiii\0"
   "glFramebufferTexture2DMultisampleEXT\0"
   "\0"
   /* _mesa_function_pool[63407]: DepthRangeArrayfvOES (will be remapped) */
   "iip\0"
   "glDepthRangeArrayfvOES\0"
   "\0"
   /* _mesa_function_pool[63435]: DepthRangeIndexedfOES (will be remapped) */
   "iff\0"
   "glDepthRangeIndexedfOES\0"
   "\0"
   /* _mesa_function_pool[63464]: FramebufferParameteriMESA (will be remapped) */
   "iii\0"
   "glFramebufferParameteriMESA\0"
   "\0"
   /* _mesa_function_pool[63497]: GetFramebufferParameterivMESA (will be remapped) */
   "iip\0"
   "glGetFramebufferParameterivMESA\0"
   "\0"
   ;

/* these functions need to be remapped */
static const struct gl_function_pool_remap MESA_remap_table_functions[] = {
   { 10126, CompressedTexImage1D_remap_index },
   { 10067, CompressedTexImage2D_remap_index },
   {  9981, CompressedTexImage3D_remap_index },
   { 10347, CompressedTexSubImage1D_remap_index },
   { 10281, CompressedTexSubImage2D_remap_index },
   { 10184, CompressedTexSubImage3D_remap_index },
   { 10411, GetCompressedTexImage_remap_index },
   {  9784, LoadTransposeMatrixd_remap_index },
   {  9732, LoadTransposeMatrixf_remap_index },
   {  9888, MultTransposeMatrixd_remap_index },
   {  9836, MultTransposeMatrixf_remap_index },
   {  9940, SampleCoverage_remap_index },
   { 10467, BlendFuncSeparate_remap_index },
   { 10687, FogCoordPointer_remap_index },
   { 10625, FogCoordd_remap_index },
   { 10655, FogCoorddv_remap_index },
   { 10731, MultiDrawArrays_remap_index },
   { 10826, PointParameterf_remap_index },
   { 10912, PointParameterfv_remap_index },
   { 11002, PointParameteri_remap_index },
   { 11044, PointParameteriv_remap_index },
   { 11088, SecondaryColor3b_remap_index },
   { 11134, SecondaryColor3bv_remap_index },
   { 11180, SecondaryColor3d_remap_index },
   { 11226, SecondaryColor3dv_remap_index },
   { 11364, SecondaryColor3i_remap_index },
   { 11410, SecondaryColor3iv_remap_index },
   { 11456, SecondaryColor3s_remap_index },
   { 11502, SecondaryColor3sv_remap_index },
   { 11548, SecondaryColor3ub_remap_index },
   { 11596, SecondaryColor3ubv_remap_index },
   { 11644, SecondaryColor3ui_remap_index },
   { 11692, SecondaryColor3uiv_remap_index },
   { 11740, SecondaryColor3us_remap_index },
   { 11788, SecondaryColor3usv_remap_index },
   { 11836, SecondaryColorPointer_remap_index },
   { 11893, WindowPos2d_remap_index },
   { 11946, WindowPos2dv_remap_index },
   { 12001, WindowPos2f_remap_index },
   { 12054, WindowPos2fv_remap_index },
   { 12109, WindowPos2i_remap_index },
   { 12162, WindowPos2iv_remap_index },
   { 12217, WindowPos2s_remap_index },
   { 12270, WindowPos2sv_remap_index },
   { 12325, WindowPos3d_remap_index },
   { 12379, WindowPos3dv_remap_index },
   { 12434, WindowPos3f_remap_index },
   { 12488, WindowPos3fv_remap_index },
   { 12543, WindowPos3i_remap_index },
   { 12597, WindowPos3iv_remap_index },
   { 12652, WindowPos3s_remap_index },
   { 12706, WindowPos3sv_remap_index },
   { 13385, BeginQuery_remap_index },
   { 12761, BindBuffer_remap_index },
   { 12794, BufferData_remap_index },
   { 12829, BufferSubData_remap_index },
   { 12870, DeleteBuffers_remap_index },
   { 13288, DeleteQueries_remap_index },
   { 13434, EndQuery_remap_index },
   { 12909, GenBuffers_remap_index },
   { 13239, GenQueries_remap_index },
   { 12942, GetBufferParameteriv_remap_index },
   { 12996, GetBufferPointerv_remap_index },
   { 13067, GetBufferSubData_remap_index },
   { 13526, GetQueryObjectiv_remap_index },
   { 13594, GetQueryObjectuiv_remap_index },
   { 13476, GetQueryiv_remap_index },
   { 13114, IsBuffer_remap_index },
   { 13346, IsQuery_remap_index },
   { 13142, MapBuffer_remap_index },
   { 13188, UnmapBuffer_remap_index },
   { 13962, AttachShader_remap_index },
   { 13981, BindAttribLocation_remap_index },
   { 13665, BlendEquationSeparate_remap_index },
   { 14031, CompileShader_remap_index },
   { 14069, CreateProgram_remap_index },
   { 14087, CreateShader_remap_index },
   { 14105, DeleteProgram_remap_index },
   { 14124, DeleteShader_remap_index },
   { 14142, DetachShader_remap_index },
   { 14161, DisableVertexAttribArray_remap_index },
   { 13774, DrawBuffers_remap_index },
   { 14221, EnableVertexAttribArray_remap_index },
   { 14279, GetActiveAttrib_remap_index },
   { 14327, GetActiveUniform_remap_index },
   { 14377, GetAttachedShaders_remap_index },
   { 14404, GetAttribLocation_remap_index },
   { 14471, GetProgramInfoLog_remap_index },
   { 14451, GetProgramiv_remap_index },
   { 14516, GetShaderInfoLog_remap_index },
   { 14541, GetShaderSource_remap_index },
   { 14497, GetShaderiv_remap_index },
   { 14586, GetUniformLocation_remap_index },
   { 14635, GetUniformfv_remap_index },
   { 14673, GetUniformiv_remap_index },
   { 14855, GetVertexAttribPointerv_remap_index },
   { 14711, GetVertexAttribdv_remap_index },
   { 14759, GetVertexAttribfv_remap_index },
   { 14807, GetVertexAttribiv_remap_index },
   { 14943, IsProgram_remap_index },
   { 14958, IsShader_remap_index },
   { 14972, LinkProgram_remap_index },
   { 15006, ShaderSource_remap_index },
   { 13859, StencilFuncSeparate_remap_index },
   { 13936, StencilMaskSeparate_remap_index },
   { 13887, StencilOpSeparate_remap_index },
   { 15083, Uniform1f_remap_index },
   { 15343, Uniform1fv_remap_index },
   { 15213, Uniform1i_remap_index },
   { 15479, Uniform1iv_remap_index },
   { 15114, Uniform2f_remap_index },
   { 15377, Uniform2fv_remap_index },
   { 15244, Uniform2i_remap_index },
   { 15513, Uniform2iv_remap_index },
   { 15146, Uniform3f_remap_index },
   { 15411, Uniform3fv_remap_index },
   { 15276, Uniform3i_remap_index },
   { 15547, Uniform3iv_remap_index },
   { 15179, Uniform4f_remap_index },
   { 15445, Uniform4fv_remap_index },
   { 15309, Uniform4i_remap_index },
   { 15581, Uniform4iv_remap_index },
   { 15615, UniformMatrix2fv_remap_index },
   { 15662, UniformMatrix3fv_remap_index },
   { 15709, UniformMatrix4fv_remap_index },
   { 15045, UseProgram_remap_index },
   { 15756, ValidateProgram_remap_index },
   { 15798, VertexAttrib1d_remap_index },
   { 15839, VertexAttrib1dv_remap_index },
   { 15966, VertexAttrib1s_remap_index },
   { 16007, VertexAttrib1sv_remap_index },
   { 16050, VertexAttrib2d_remap_index },
   { 16092, VertexAttrib2dv_remap_index },
   { 16220, VertexAttrib2s_remap_index },
   { 16262, VertexAttrib2sv_remap_index },
   { 16305, VertexAttrib3d_remap_index },
   { 16348, VertexAttrib3dv_remap_index },
   { 16477, VertexAttrib3s_remap_index },
   { 16520, VertexAttrib3sv_remap_index },
   { 16563, VertexAttrib4Nbv_remap_index },
   { 16608, VertexAttrib4Niv_remap_index },
   { 16653, VertexAttrib4Nsv_remap_index },
   { 16698, VertexAttrib4Nub_remap_index },
   { 16746, VertexAttrib4Nubv_remap_index },
   { 16793, VertexAttrib4Nuiv_remap_index },
   { 16840, VertexAttrib4Nusv_remap_index },
   { 16887, VertexAttrib4bv_remap_index },
   { 16930, VertexAttrib4d_remap_index },
   { 16974, VertexAttrib4dv_remap_index },
   { 17104, VertexAttrib4iv_remap_index },
   { 17147, VertexAttrib4s_remap_index },
   { 17191, VertexAttrib4sv_remap_index },
   { 17234, VertexAttrib4ubv_remap_index },
   { 17279, VertexAttrib4uiv_remap_index },
   { 17324, VertexAttrib4usv_remap_index },
   { 17369, VertexAttribPointer_remap_index },
   { 17424, UniformMatrix2x3fv_remap_index },
   { 17478, UniformMatrix2x4fv_remap_index },
   { 17451, UniformMatrix3x2fv_remap_index },
   { 17532, UniformMatrix3x4fv_remap_index },
   { 17505, UniformMatrix4x2fv_remap_index },
   { 17559, UniformMatrix4x3fv_remap_index },
   { 58872, BeginConditionalRender_remap_index },
   { 58540, BeginTransformFeedback_remap_index },
   { 58693, BindBufferBase_remap_index },
   { 58647, BindBufferRange_remap_index },
   { 58332, BindFragDataLocation_remap_index },
   { 18971, ClampColor_remap_index },
   { 58501, ClearBufferfi_remap_index },
   { 58480, ClearBufferfv_remap_index },
   { 58437, ClearBufferiv_remap_index },
   { 58458, ClearBufferuiv_remap_index },
   { 49122, ColorMaski_remap_index },
   { 49345, Disablei_remap_index },
   { 49286, Enablei_remap_index },
   { 58928, EndConditionalRender_remap_index },
   { 58596, EndTransformFeedback_remap_index },
   { 49196, GetBooleani_v_remap_index },
   { 58386, GetFragDataLocation_remap_index },
   { 49241, GetIntegeri_v_remap_index },
   { 58523, GetStringi_remap_index },
   { 56785, GetTexParameterIiv_remap_index },
   { 56859, GetTexParameterIuiv_remap_index },
   { 58800, GetTransformFeedbackVarying_remap_index },
   { 58292, GetUniformuiv_remap_index },
   { 57908, GetVertexAttribIiv_remap_index },
   { 57958, GetVertexAttribIuiv_remap_index },
   { 49408, IsEnabledi_remap_index },
   { 56652, TexParameterIiv_remap_index },
   { 56717, TexParameterIuiv_remap_index },
   { 58735, TransformFeedbackVaryings_remap_index },
   { 58010, Uniform1ui_remap_index },
   { 58148, Uniform1uiv_remap_index },
   { 58043, Uniform2ui_remap_index },
   { 58184, Uniform2uiv_remap_index },
   { 58077, Uniform3ui_remap_index },
   { 58220, Uniform3uiv_remap_index },
   { 58112, Uniform4ui_remap_index },
   { 58256, Uniform4uiv_remap_index },
   { 57300, VertexAttribI1iv_remap_index },
   { 57480, VertexAttribI1uiv_remap_index },
   { 57668, VertexAttribI4bv_remap_index },
   { 57713, VertexAttribI4sv_remap_index },
   { 57758, VertexAttribI4ubv_remap_index },
   { 57805, VertexAttribI4usv_remap_index },
   { 57852, VertexAttribIPointer_remap_index },
   { 58978, PrimitiveRestartIndex_remap_index },
   { 20791, TexBuffer_remap_index },
   { 59084, FramebufferTexture_remap_index },
   { 59054, GetBufferParameteri64v_remap_index },
   { 59031, GetInteger64i_v_remap_index },
   { 20598, VertexAttribDivisor_remap_index },
   { 22358, MinSampleShading_remap_index },
   { 61534, MemoryBarrierByRegion_remap_index },
   { 18003, BindProgramARB_remap_index },
   { 18040, DeleteProgramsARB_remap_index },
   { 18083, GenProgramsARB_remap_index },
   { 18526, GetProgramEnvParameterdvARB_remap_index },
   { 18561, GetProgramEnvParameterfvARB_remap_index },
   { 18596, GetProgramLocalParameterdvARB_remap_index },
   { 18633, GetProgramLocalParameterfvARB_remap_index },
   { 18693, GetProgramStringARB_remap_index },
   { 18670, GetProgramivARB_remap_index },
   { 18120, IsProgramARB_remap_index },
   { 18152, ProgramEnvParameter4dARB_remap_index },
   { 18210, ProgramEnvParameter4dvARB_remap_index },
   { 18267, ProgramEnvParameter4fARB_remap_index },
   { 18325, ProgramEnvParameter4fvARB_remap_index },
   { 18382, ProgramLocalParameter4dARB_remap_index },
   { 18419, ProgramLocalParameter4dvARB_remap_index },
   { 18454, ProgramLocalParameter4fARB_remap_index },
   { 18491, ProgramLocalParameter4fvARB_remap_index },
   { 17978, ProgramStringARB_remap_index },
   { 15882, VertexAttrib1fARB_remap_index },
   { 15923, VertexAttrib1fvARB_remap_index },
   { 16135, VertexAttrib2fARB_remap_index },
   { 16177, VertexAttrib2fvARB_remap_index },
   { 16391, VertexAttrib3fARB_remap_index },
   { 16434, VertexAttrib3fvARB_remap_index },
   { 17017, VertexAttrib4fARB_remap_index },
   { 17061, VertexAttrib4fvARB_remap_index },
   { 18835, AttachObjectARB_remap_index },
   { 18808, CreateProgramObjectARB_remap_index },
   { 18781, CreateShaderObjectARB_remap_index },
   { 18720, DeleteObjectARB_remap_index },
   { 18759, DetachObjectARB_remap_index },
   { 18941, GetAttachedObjectsARB_remap_index },
   { 18741, GetHandleARB_remap_index },
   { 18919, GetInfoLogARB_remap_index },
   { 18857, GetObjectParameterfvARB_remap_index },
   { 18888, GetObjectParameterivARB_remap_index },
   { 19004, DrawArraysInstancedARB_remap_index },
   { 19082, DrawElementsInstancedARB_remap_index },
   { 19724, BindFramebuffer_remap_index },
   { 19227, BindRenderbuffer_remap_index },
   { 20487, BlitFramebuffer_remap_index },
   { 19904, CheckFramebufferStatus_remap_index },
   { 19767, DeleteFramebuffers_remap_index },
   { 19272, DeleteRenderbuffers_remap_index },
   { 20271, FramebufferRenderbuffer_remap_index },
   { 19988, FramebufferTexture1D_remap_index },
   { 20044, FramebufferTexture2D_remap_index },
   { 20126, FramebufferTexture3D_remap_index },
   { 20209, FramebufferTextureLayer_remap_index },
   { 19840, GenFramebuffers_remap_index },
   { 19348, GenRenderbuffers_remap_index },
   { 20538, GenerateMipmap_remap_index },
   { 20361, GetFramebufferAttachmentParameteriv_remap_index },
   { 19569, GetRenderbufferParameteriv_remap_index },
   { 19667, IsFramebuffer_remap_index },
   { 19167, IsRenderbuffer_remap_index },
   { 19415, RenderbufferStorage_remap_index },
   { 19493, RenderbufferStorageMultisample_remap_index },
   { 20733, FlushMappedBufferRange_remap_index },
   { 20690, MapBufferRange_remap_index },
   { 20853, BindVertexArray_remap_index },
   { 20895, DeleteVertexArrays_remap_index },
   { 20944, GenVertexArrays_remap_index },
   { 20987, IsVertexArray_remap_index },
   { 21169, GetActiveUniformBlockName_remap_index },
   { 21137, GetActiveUniformBlockiv_remap_index },
   { 21080, GetActiveUniformName_remap_index },
   { 21051, GetActiveUniformsiv_remap_index },
   { 21110, GetUniformBlockIndex_remap_index },
   { 21025, GetUniformIndices_remap_index },
   { 21204, UniformBlockBinding_remap_index },
   { 21231, CopyBufferSubData_remap_index },
   { 21682, ClientWaitSync_remap_index },
   { 21666, DeleteSync_remap_index },
   { 21638, FenceSync_remap_index },
   { 21720, GetInteger64v_remap_index },
   { 21759, GetSynciv_remap_index },
   { 21654, IsSync_remap_index },
   { 21704, WaitSync_remap_index },
   { 21258, DrawElementsBaseVertex_remap_index },
   { 21522, DrawElementsInstancedBaseVertex_remap_index },
   { 21346, DrawRangeElementsBaseVertex_remap_index },
   { 21451, MultiDrawElementsBaseVertex_remap_index },
   { 49080, ProvokingVertex_remap_index },
   { 21843, GetMultisamplefv_remap_index },
   { 21867, SampleMaski_remap_index },
   { 21778, TexImage2DMultisample_remap_index },
   { 21810, TexImage3DMultisample_remap_index },
   { 21992, BlendEquationSeparateiARB_remap_index },
   { 21885, BlendEquationiARB_remap_index },
   { 22228, BlendFuncSeparateiARB_remap_index },
   { 22140, BlendFunciARB_remap_index },
   { 22585, BindFragDataLocationIndexed_remap_index },
   { 22654, GetFragDataIndex_remap_index },
   { 22753, BindSampler_remap_index },
   { 22717, DeleteSamplers_remap_index },
   { 22699, GenSamplers_remap_index },
   { 23088, GetSamplerParameterIiv_remap_index },
   { 23174, GetSamplerParameterIuiv_remap_index },
   { 23059, GetSamplerParameterfv_remap_index },
   { 23030, GetSamplerParameteriv_remap_index },
   { 22738, IsSampler_remap_index },
   { 22873, SamplerParameterIiv_remap_index },
   { 22950, SamplerParameterIuiv_remap_index },
   { 22796, SamplerParameterf_remap_index },
   { 22847, SamplerParameterfv_remap_index },
   { 22771, SamplerParameteri_remap_index },
   { 22821, SamplerParameteriv_remap_index },
   { 23263, GetQueryObjecti64v_remap_index },
   { 23313, GetQueryObjectui64v_remap_index },
   { 23365, QueryCounter_remap_index },
   { 23902, ColorP3ui_remap_index },
   { 23934, ColorP3uiv_remap_index },
   { 23918, ColorP4ui_remap_index },
   { 23951, ColorP4uiv_remap_index },
   { 23663, MultiTexCoordP1ui_remap_index },
   { 23763, MultiTexCoordP1uiv_remap_index },
   { 23688, MultiTexCoordP2ui_remap_index },
   { 23789, MultiTexCoordP2uiv_remap_index },
   { 23713, MultiTexCoordP3ui_remap_index },
   { 23815, MultiTexCoordP3uiv_remap_index },
   { 23738, MultiTexCoordP4ui_remap_index },
   { 23841, MultiTexCoordP4uiv_remap_index },
   { 23867, NormalP3ui_remap_index },
   { 23884, NormalP3uiv_remap_index },
   { 23968, SecondaryColorP3ui_remap_index },
   { 23993, SecondaryColorP3uiv_remap_index },
   { 23507, TexCoordP1ui_remap_index },
   { 23583, TexCoordP1uiv_remap_index },
   { 23526, TexCoordP2ui_remap_index },
   { 23603, TexCoordP2uiv_remap_index },
   { 23545, TexCoordP3ui_remap_index },
   { 23623, TexCoordP3uiv_remap_index },
   { 23564, TexCoordP4ui_remap_index },
   { 23643, TexCoordP4uiv_remap_index },
   { 24019, VertexAttribP1ui_remap_index },
   { 24119, VertexAttribP1uiv_remap_index },
   { 24044, VertexAttribP2ui_remap_index },
   { 24145, VertexAttribP2uiv_remap_index },
   { 24069, VertexAttribP3ui_remap_index },
   { 24171, VertexAttribP3uiv_remap_index },
   { 24094, VertexAttribP4ui_remap_index },
   { 24197, VertexAttribP4uiv_remap_index },
   { 23402, VertexP2ui_remap_index },
   { 23453, VertexP2uiv_remap_index },
   { 23419, VertexP3ui_remap_index },
   { 23471, VertexP3uiv_remap_index },
   { 23436, VertexP4ui_remap_index },
   { 23489, VertexP4uiv_remap_index },
   { 24571, DrawArraysIndirect_remap_index },
   { 24596, DrawElementsIndirect_remap_index },
   { 25130, GetUniformdv_remap_index },
   { 24751, Uniform1d_remap_index },
   { 24821, Uniform1dv_remap_index },
   { 24767, Uniform2d_remap_index },
   { 24839, Uniform2dv_remap_index },
   { 24784, Uniform3d_remap_index },
   { 24857, Uniform3dv_remap_index },
   { 24802, Uniform4d_remap_index },
   { 24875, Uniform4dv_remap_index },
   { 24893, UniformMatrix2dv_remap_index },
   { 24968, UniformMatrix2x3dv_remap_index },
   { 24995, UniformMatrix2x4dv_remap_index },
   { 24918, UniformMatrix3dv_remap_index },
   { 25022, UniformMatrix3x2dv_remap_index },
   { 25049, UniformMatrix3x4dv_remap_index },
   { 24943, UniformMatrix4dv_remap_index },
   { 25076, UniformMatrix4x2dv_remap_index },
   { 25103, UniformMatrix4x3dv_remap_index },
   { 24364, GetActiveSubroutineName_remap_index },
   { 24323, GetActiveSubroutineUniformName_remap_index },
   { 24285, GetActiveSubroutineUniformiv_remap_index },
   { 24458, GetProgramStageiv_remap_index },
   { 24259, GetSubroutineIndex_remap_index },
   { 24223, GetSubroutineUniformLocation_remap_index },
   { 24427, GetUniformSubroutineuiv_remap_index },
   { 24398, UniformSubroutinesuiv_remap_index },
   { 24548, PatchParameterfv_remap_index },
   { 24484, PatchParameteri_remap_index },
   { 59210, BindTransformFeedback_remap_index },
   { 59238, DeleteTransformFeedbacks_remap_index },
   { 59377, DrawTransformFeedback_remap_index },
   { 59269, GenTransformFeedbacks_remap_index },
   { 59297, IsTransformFeedback_remap_index },
   { 59322, PauseTransformFeedback_remap_index },
   { 59349, ResumeTransformFeedback_remap_index },
   { 26153, BeginQueryIndexed_remap_index },
   { 26118, DrawTransformFeedbackStream_remap_index },
   { 26178, EndQueryIndexed_remap_index },
   { 26200, GetQueryIndexediv_remap_index },
   { 29104, ClearDepthf_remap_index },
   { 29138, DepthRangef_remap_index },
   { 29023, GetShaderPrecisionFormat_remap_index },
   { 29056, ReleaseShaderCompiler_remap_index },
   { 29082, ShaderBinary_remap_index },
   { 29173, GetProgramBinary_remap_index },
   { 29221, ProgramBinary_remap_index },
   { 29262, ProgramParameteri_remap_index },
   { 28928, GetVertexAttribLdv_remap_index },
   { 28514, VertexAttribL1d_remap_index },
   { 28692, VertexAttribL1dv_remap_index },
   { 28557, VertexAttribL2d_remap_index },
   { 28737, VertexAttribL2dv_remap_index },
   { 28601, VertexAttribL3d_remap_index },
   { 28782, VertexAttribL3dv_remap_index },
   { 28646, VertexAttribL4d_remap_index },
   { 28827, VertexAttribL4dv_remap_index },
   { 28872, VertexAttribLPointer_remap_index },
   { 41008, DepthRangeArrayv_remap_index },
   { 41032, DepthRangeIndexed_remap_index },
   { 41132, GetDoublei_v_remap_index },
   { 41057, GetFloati_v_remap_index },
   { 40881, ScissorArrayv_remap_index },
   { 40921, ScissorIndexed_remap_index },
   { 40965, ScissorIndexedv_remap_index },
   { 40744, ViewportArrayv_remap_index },
   { 40786, ViewportIndexedf_remap_index },
   { 40834, ViewportIndexedfv_remap_index },
   { 29625, GetGraphicsResetStatusARB_remap_index },
   { 30005, GetnColorTableARB_remap_index },
   { 30152, GetnCompressedTexImageARB_remap_index },
   { 30032, GetnConvolutionFilterARB_remap_index },
   { 30101, GetnHistogramARB_remap_index },
   { 29736, GetnMapdvARB_remap_index },
   { 29757, GetnMapfvARB_remap_index },
   { 29778, GetnMapivARB_remap_index },
   { 30128, GetnMinmaxARB_remap_index },
   { 29799, GetnPixelMapfvARB_remap_index },
   { 29824, GetnPixelMapuivARB_remap_index },
   { 29850, GetnPixelMapusvARB_remap_index },
   { 29876, GetnPolygonStippleARB_remap_index },
   { 30066, GetnSeparableFilterARB_remap_index },
   { 29904, GetnTexImageARB_remap_index },
   { 30407, GetnUniformdvARB_remap_index },
   { 30186, GetnUniformfvARB_remap_index },
   { 30265, GetnUniformivARB_remap_index },
   { 30344, GetnUniformuivARB_remap_index },
   { 29930, ReadnPixelsARB_remap_index },
   { 30432, DrawArraysInstancedBaseInstance_remap_index },
   { 30510, DrawElementsInstancedBaseInstance_remap_index },
   { 30593, DrawElementsInstancedBaseVertexBaseInstance_remap_index },
   { 30697, DrawTransformFeedbackInstanced_remap_index },
   { 30735, DrawTransformFeedbackStreamInstanced_remap_index },
   { 30780, GetInternalformativ_remap_index },
   { 30809, GetActiveAtomicCounterBufferiv_remap_index },
   { 30848, BindImageTexture_remap_index },
   { 30876, MemoryBarrier_remap_index },
   { 30914, TexStorage1D_remap_index },
   { 30935, TexStorage2D_remap_index },
   { 30957, TexStorage3D_remap_index },
   { 30980, TextureStorage1DEXT_remap_index },
   { 31009, TextureStorage2DEXT_remap_index },
   { 31039, TextureStorage3DEXT_remap_index },
   { 31322, ClearBufferData_remap_index },
   { 31347, ClearBufferSubData_remap_index },
   { 31448, DispatchCompute_remap_index },
   { 31471, DispatchComputeIndirect_remap_index },
   { 31500, CopyImageSubData_remap_index },
   { 31580, TextureView_remap_index },
   { 31638, BindVertexBuffer_remap_index },
   { 31747, VertexAttribBinding_remap_index },
   { 31663, VertexAttribFormat_remap_index },
   { 31691, VertexAttribIFormat_remap_index },
   { 31719, VertexAttribLFormat_remap_index },
   { 31773, VertexBindingDivisor_remap_index },
   { 32052, FramebufferParameteri_remap_index },
   { 32081, GetFramebufferParameteriv_remap_index },
   { 32192, GetInternalformati64v_remap_index },
   { 24624, MultiDrawArraysIndirect_remap_index },
   { 24685, MultiDrawElementsIndirect_remap_index },
   { 32404, GetProgramInterfaceiv_remap_index },
   { 32434, GetProgramResourceIndex_remap_index },
   { 32531, GetProgramResourceLocation_remap_index },
   { 32565, GetProgramResourceLocationIndex_remap_index },
   { 32465, GetProgramResourceName_remap_index },
   { 32498, GetProgramResourceiv_remap_index },
   { 32641, ShaderStorageBlockBinding_remap_index },
   { 32674, TexBufferRange_remap_index },
   { 32770, TexStorage2DMultisample_remap_index },
   { 32804, TexStorage3DMultisample_remap_index },
   { 32953, BufferStorage_remap_index },
   { 33024, ClearTexImage_remap_index },
   { 33047, ClearTexSubImage_remap_index },
   { 33079, BindBuffersBase_remap_index },
   { 33103, BindBuffersRange_remap_index },
   { 33170, BindImageTextures_remap_index },
   { 33150, BindSamplers_remap_index },
   { 33130, BindTextures_remap_index },
   { 33195, BindVertexBuffers_remap_index },
   { 33351, GetImageHandleARB_remap_index },
   { 33222, GetTextureHandleARB_remap_index },
   { 33247, GetTextureSamplerHandleARB_remap_index },
   { 33693, GetVertexAttribLui64vARB_remap_index },
   { 33606, IsImageHandleResidentARB_remap_index },
   { 33574, IsTextureHandleResidentARB_remap_index },
   { 33411, MakeImageHandleNonResidentARB_remap_index },
   { 33378, MakeImageHandleResidentARB_remap_index },
   { 33314, MakeTextureHandleNonResidentARB_remap_index },
   { 33280, MakeTextureHandleResidentARB_remap_index },
   { 33502, ProgramUniformHandleui64ARB_remap_index },
   { 33537, ProgramUniformHandleui64vARB_remap_index },
   { 33446, UniformHandleui64ARB_remap_index },
   { 33473, UniformHandleui64vARB_remap_index },
   { 33636, VertexAttribL1ui64ARB_remap_index },
   { 33664, VertexAttribL1ui64vARB_remap_index },
   { 33725, DispatchComputeGroupSizeARB_remap_index },
   { 33763, MultiDrawArraysIndirectCountARB_remap_index },
   { 33835, MultiDrawElementsIndirectCountARB_remap_index },
   { 33912, ClipControl_remap_index },
   { 36106, BindTextureUnit_remap_index },
   { 35078, BlitNamedFramebuffer_remap_index },
   { 35115, CheckNamedFramebufferStatus_remap_index },
   { 34277, ClearNamedBufferData_remap_index },
   { 34307, ClearNamedBufferSubData_remap_index },
   { 35045, ClearNamedFramebufferfi_remap_index },
   { 35013, ClearNamedFramebufferfv_remap_index },
   { 34948, ClearNamedFramebufferiv_remap_index },
   { 34980, ClearNamedFramebufferuiv_remap_index },
   { 35698, CompressedTextureSubImage1D_remap_index },
   { 35737, CompressedTextureSubImage2D_remap_index },
   { 35778, CompressedTextureSubImage3D_remap_index },
   { 34245, CopyNamedBufferSubData_remap_index },
   { 35821, CopyTextureSubImage1D_remap_index },
   { 35853, CopyTextureSubImage2D_remap_index },
   { 35887, CopyTextureSubImage3D_remap_index },
   { 34147, CreateBuffers_remap_index },
   { 34598, CreateFramebuffers_remap_index },
   { 36840, CreateProgramPipelines_remap_index },
   { 36869, CreateQueries_remap_index },
   { 35236, CreateRenderbuffers_remap_index },
   { 36819, CreateSamplers_remap_index },
   { 35379, CreateTextures_remap_index },
   { 33947, CreateTransformFeedbacks_remap_index },
   { 36377, CreateVertexArrays_remap_index },
   { 36402, DisableVertexArrayAttrib_remap_index },
   { 36433, EnableVertexArrayAttrib_remap_index },
   { 34435, FlushMappedNamedBufferRange_remap_index },
   { 36079, GenerateTextureMipmap_remap_index },
   { 36154, GetCompressedTextureImage_remap_index },
   { 34503, GetNamedBufferParameteri64v_remap_index },
   { 34470, GetNamedBufferParameteriv_remap_index },
   { 34538, GetNamedBufferPointerv_remap_index },
   { 34568, GetNamedBufferSubData_remap_index },
   { 35187, GetNamedFramebufferAttachmentParameteriv_remap_index },
   { 35149, GetNamedFramebufferParameteriv_remap_index },
   { 35340, GetNamedRenderbufferParameteriv_remap_index },
   { 36953, GetQueryBufferObjecti64v_remap_index },
   { 36890, GetQueryBufferObjectiv_remap_index },
   { 36986, GetQueryBufferObjectui64v_remap_index },
   { 36921, GetQueryBufferObjectuiv_remap_index },
   { 36128, GetTextureImage_remap_index },
   { 36188, GetTextureLevelParameterfv_remap_index },
   { 36223, GetTextureLevelParameteriv_remap_index },
   { 36287, GetTextureParameterIiv_remap_index },
   { 36317, GetTextureParameterIuiv_remap_index },
   { 36258, GetTextureParameterfv_remap_index },
   { 36348, GetTextureParameteriv_remap_index },
   { 34113, GetTransformFeedbacki64_v_remap_index },
   { 34081, GetTransformFeedbacki_v_remap_index },
   { 34051, GetTransformFeedbackiv_remap_index },
   { 36785, GetVertexArrayIndexed64iv_remap_index },
   { 36753, GetVertexArrayIndexediv_remap_index },
   { 36729, GetVertexArrayiv_remap_index },
   { 34865, InvalidateNamedFramebufferData_remap_index },
   { 34903, InvalidateNamedFramebufferSubData_remap_index },
   { 34342, MapNamedBuffer_remap_index },
   { 34363, MapNamedBufferRange_remap_index },
   { 34194, NamedBufferData_remap_index },
   { 34167, NamedBufferStorage_remap_index },
   { 34218, NamedBufferSubData_remap_index },
   { 34764, NamedFramebufferDrawBuffer_remap_index },
   { 34797, NamedFramebufferDrawBuffers_remap_index },
   { 34660, NamedFramebufferParameteri_remap_index },
   { 34832, NamedFramebufferReadBuffer_remap_index },
   { 34623, NamedFramebufferRenderbuffer_remap_index },
   { 34694, NamedFramebufferTexture_remap_index },
   { 34726, NamedFramebufferTextureLayer_remap_index },
   { 35262, NamedRenderbufferStorage_remap_index },
   { 35295, NamedRenderbufferStorageMultisample_remap_index },
   { 35401, TextureBuffer_remap_index },
   { 35422, TextureBufferRange_remap_index },
   { 35998, TextureParameterIiv_remap_index },
   { 36025, TextureParameterIuiv_remap_index },
   { 35922, TextureParameterf_remap_index },
   { 35947, TextureParameterfv_remap_index },
   { 35973, TextureParameteri_remap_index },
   { 36053, TextureParameteriv_remap_index },
   { 35450, TextureStorage1D_remap_index },
   { 35475, TextureStorage2D_remap_index },
   { 35528, TextureStorage2DMultisample_remap_index },
   { 35501, TextureStorage3D_remap_index },
   { 35566, TextureStorage3DMultisample_remap_index },
   { 35605, TextureSubImage1D_remap_index },
   { 35634, TextureSubImage2D_remap_index },
   { 35665, TextureSubImage3D_remap_index },
   { 33978, TransformFeedbackBufferBase_remap_index },
   { 34013, TransformFeedbackBufferRange_remap_index },
   { 34391, UnmapNamedBufferEXT_remap_index },
   { 36664, VertexArrayAttribBinding_remap_index },
   { 36562, VertexArrayAttribFormat_remap_index },
   { 36596, VertexArrayAttribIFormat_remap_index },
   { 36630, VertexArrayAttribLFormat_remap_index },
   { 36696, VertexArrayBindingDivisor_remap_index },
   { 36463, VertexArrayElementBuffer_remap_index },
   { 36494, VertexArrayVertexBuffer_remap_index },
   { 36527, VertexArrayVertexBuffers_remap_index },
   { 37055, GetCompressedTextureSubImage_remap_index },
   { 37020, GetTextureSubImage_remap_index },
   { 37136, BufferPageCommitmentARB_remap_index },
   { 37205, NamedBufferPageCommitmentARB_remap_index },
   { 38032, GetUniformi64vARB_remap_index },
   { 38076, GetUniformui64vARB_remap_index },
   { 38122, GetnUniformi64vARB_remap_index },
   { 38149, GetnUniformui64vARB_remap_index },
   { 38177, ProgramUniform1i64ARB_remap_index },
   { 38391, ProgramUniform1i64vARB_remap_index },
   { 38611, ProgramUniform1ui64ARB_remap_index },
   { 38833, ProgramUniform1ui64vARB_remap_index },
   { 38229, ProgramUniform2i64ARB_remap_index },
   { 38446, ProgramUniform2i64vARB_remap_index },
   { 38665, ProgramUniform2ui64ARB_remap_index },
   { 38890, ProgramUniform2ui64vARB_remap_index },
   { 38282, ProgramUniform3i64ARB_remap_index },
   { 38501, ProgramUniform3i64vARB_remap_index },
   { 38720, ProgramUniform3ui64ARB_remap_index },
   { 38947, ProgramUniform3ui64vARB_remap_index },
   { 38336, ProgramUniform4i64ARB_remap_index },
   { 38556, ProgramUniform4i64vARB_remap_index },
   { 38776, ProgramUniform4ui64ARB_remap_index },
   { 39004, ProgramUniform4ui64vARB_remap_index },
   { 37388, Uniform1i64ARB_remap_index },
   { 37542, Uniform1i64vARB_remap_index },
   { 37702, Uniform1ui64ARB_remap_index },
   { 37864, Uniform1ui64vARB_remap_index },
   { 37425, Uniform2i64ARB_remap_index },
   { 37582, Uniform2i64vARB_remap_index },
   { 37741, Uniform2ui64ARB_remap_index },
   { 37906, Uniform2ui64vARB_remap_index },
   { 37463, Uniform3i64ARB_remap_index },
   { 37622, Uniform3i64vARB_remap_index },
   { 37781, Uniform3ui64ARB_remap_index },
   { 37948, Uniform3ui64vARB_remap_index },
   { 37502, Uniform4i64ARB_remap_index },
   { 37662, Uniform4i64vARB_remap_index },
   { 37822, Uniform4ui64ARB_remap_index },
   { 37990, Uniform4ui64vARB_remap_index },
   { 44931, EvaluateDepthValuesARB_remap_index },
   { 44775, FramebufferSampleLocationsfvARB_remap_index },
   { 44848, NamedFramebufferSampleLocationsfvARB_remap_index },
   { 39124, SpecializeShaderARB_remap_index },
   { 32313, InvalidateBufferData_remap_index },
   { 32282, InvalidateBufferSubData_remap_index },
   { 32375, InvalidateFramebuffer_remap_index },
   { 32339, InvalidateSubFramebuffer_remap_index },
   { 32257, InvalidateTexImage_remap_index },
   { 32223, InvalidateTexSubImage_remap_index },
   { 63129, DrawTexfOES_remap_index },
   { 63150, DrawTexfvOES_remap_index },
   { 63090, DrawTexiOES_remap_index },
   { 63111, DrawTexivOES_remap_index },
   { 63168, DrawTexsOES_remap_index },
   { 63189, DrawTexsvOES_remap_index },
   { 63207, DrawTexxOES_remap_index },
   { 63228, DrawTexxvOES_remap_index },
   { 63284, PointSizePointerOES_remap_index },
   { 63311, QueryMatrixxOES_remap_index },
   { 39599, SampleMaskSGIS_remap_index },
   { 39636, SamplePatternSGIS_remap_index },
   { 39678, ColorPointerEXT_remap_index },
   { 39703, EdgeFlagPointerEXT_remap_index },
   { 39729, IndexPointerEXT_remap_index },
   { 39753, NormalPointerEXT_remap_index },
   { 39778, TexCoordPointerEXT_remap_index },
   { 39806, VertexPointerEXT_remap_index },
   { 63333, DiscardFramebufferEXT_remap_index },
   { 26272, ActiveShaderProgram_remap_index },
   { 26377, BindProgramPipeline_remap_index },
   { 26323, CreateShaderProgramv_remap_index },
   { 26427, DeleteProgramPipelines_remap_index },
   { 26484, GenProgramPipelines_remap_index },
   { 28449, GetProgramPipelineInfoLog_remap_index },
   { 26581, GetProgramPipelineiv_remap_index },
   { 26535, IsProgramPipeline_remap_index },
   { 40654, LockArraysEXT_remap_index },
   { 25150, ProgramUniform1d_remap_index },
   { 25340, ProgramUniform1dv_remap_index },
   { 27023, ProgramUniform1f_remap_index },
   { 27613, ProgramUniform1fv_remap_index },
   { 26635, ProgramUniform1i_remap_index },
   { 27213, ProgramUniform1iv_remap_index },
   { 26825, ProgramUniform1ui_remap_index },
   { 27409, ProgramUniform1uiv_remap_index },
   { 25196, ProgramUniform2d_remap_index },
   { 25389, ProgramUniform2dv_remap_index },
   { 27069, ProgramUniform2f_remap_index },
   { 27662, ProgramUniform2fv_remap_index },
   { 26681, ProgramUniform2i_remap_index },
   { 27262, ProgramUniform2iv_remap_index },
   { 26873, ProgramUniform2ui_remap_index },
   { 27460, ProgramUniform2uiv_remap_index },
   { 25243, ProgramUniform3d_remap_index },
   { 25438, ProgramUniform3dv_remap_index },
   { 27116, ProgramUniform3f_remap_index },
   { 27711, ProgramUniform3fv_remap_index },
   { 26728, ProgramUniform3i_remap_index },
   { 27311, ProgramUniform3iv_remap_index },
   { 26922, ProgramUniform3ui_remap_index },
   { 27511, ProgramUniform3uiv_remap_index },
   { 25291, ProgramUniform4d_remap_index },
   { 25487, ProgramUniform4dv_remap_index },
   { 27164, ProgramUniform4f_remap_index },
   { 27760, ProgramUniform4fv_remap_index },
   { 26776, ProgramUniform4i_remap_index },
   { 27360, ProgramUniform4iv_remap_index },
   { 26972, ProgramUniform4ui_remap_index },
   { 27562, ProgramUniform4uiv_remap_index },
   { 25536, ProgramUniformMatrix2dv_remap_index },
   { 27809, ProgramUniformMatrix2fv_remap_index },
   { 25722, ProgramUniformMatrix2x3dv_remap_index },
   { 27995, ProgramUniformMatrix2x3fv_remap_index },
   { 25788, ProgramUniformMatrix2x4dv_remap_index },
   { 28127, ProgramUniformMatrix2x4fv_remap_index },
   { 25598, ProgramUniformMatrix3dv_remap_index },
   { 27871, ProgramUniformMatrix3fv_remap_index },
   { 25854, ProgramUniformMatrix3x2dv_remap_index },
   { 28061, ProgramUniformMatrix3x2fv_remap_index },
   { 25920, ProgramUniformMatrix3x4dv_remap_index },
   { 28259, ProgramUniformMatrix3x4fv_remap_index },
   { 25660, ProgramUniformMatrix4dv_remap_index },
   { 27933, ProgramUniformMatrix4fv_remap_index },
   { 25986, ProgramUniformMatrix4x2dv_remap_index },
   { 28193, ProgramUniformMatrix4x2fv_remap_index },
   { 26052, ProgramUniformMatrix4x3dv_remap_index },
   { 28325, ProgramUniformMatrix4x3fv_remap_index },
   { 40674, UnlockArraysEXT_remap_index },
   { 26226, UseProgramStages_remap_index },
   { 28391, ValidateProgramPipeline_remap_index },
   { 63362, FramebufferTexture2DMultisampleEXT_remap_index },
   { 29467, DebugMessageCallback_remap_index },
   { 29310, DebugMessageControl_remap_index },
   { 29390, DebugMessageInsert_remap_index },
   { 29546, GetDebugMessageLog_remap_index },
   { 31187, GetObjectLabel_remap_index },
   { 31273, GetObjectPtrLabel_remap_index },
   { 31150, ObjectLabel_remap_index },
   { 31231, ObjectPtrLabel_remap_index },
   { 31113, PopDebugGroup_remap_index },
   { 31070, PushDebugGroup_remap_index },
   { 11272, SecondaryColor3fEXT_remap_index },
   { 11318, SecondaryColor3fvEXT_remap_index },
   { 10776, MultiDrawElementsEXT_remap_index },
   { 10563, FogCoordfEXT_remap_index },
   { 10593, FogCoordfvEXT_remap_index },
   { 45546, ResizeBuffersMESA_remap_index },
   { 45568, WindowPos4dMESA_remap_index },
   { 45592, WindowPos4dvMESA_remap_index },
   { 45614, WindowPos4fMESA_remap_index },
   { 45638, WindowPos4fvMESA_remap_index },
   { 45660, WindowPos4iMESA_remap_index },
   { 45684, WindowPos4ivMESA_remap_index },
   { 45706, WindowPos4sMESA_remap_index },
   { 45730, WindowPos4svMESA_remap_index },
   { 45752, MultiModeDrawArraysIBM_remap_index },
   { 45784, MultiModeDrawElementsIBM_remap_index },
   { 46566, AreProgramsResidentNV_remap_index },
   { 46595, ExecuteProgramNV_remap_index },
   { 46619, GetProgramParameterdvNV_remap_index },
   { 46651, GetProgramParameterfvNV_remap_index },
   { 46705, GetProgramStringNV_remap_index },
   { 46683, GetProgramivNV_remap_index },
   { 46731, GetTrackMatrixivNV_remap_index },
   { 46758, GetVertexAttribdvNV_remap_index },
   { 46785, GetVertexAttribfvNV_remap_index },
   { 46812, GetVertexAttribivNV_remap_index },
   { 46839, LoadProgramNV_remap_index },
   { 46861, ProgramParameters4dvNV_remap_index },
   { 46892, ProgramParameters4fvNV_remap_index },
   { 46923, RequestResidentProgramsNV_remap_index },
   { 46955, TrackMatrixNV_remap_index },
   { 47396, VertexAttrib1dNV_remap_index },
   { 47419, VertexAttrib1dvNV_remap_index },
   { 47202, VertexAttrib1fNV_remap_index },
   { 47225, VertexAttrib1fvNV_remap_index },
   { 47008, VertexAttrib1sNV_remap_index },
   { 47031, VertexAttrib1svNV_remap_index },
   { 47443, VertexAttrib2dNV_remap_index },
   { 47467, VertexAttrib2dvNV_remap_index },
   { 47249, VertexAttrib2fNV_remap_index },
   { 47273, VertexAttrib2fvNV_remap_index },
   { 47055, VertexAttrib2sNV_remap_index },
   { 47079, VertexAttrib2svNV_remap_index },
   { 47491, VertexAttrib3dNV_remap_index },
   { 47516, VertexAttrib3dvNV_remap_index },
   { 47297, VertexAttrib3fNV_remap_index },
   { 47322, VertexAttrib3fvNV_remap_index },
   { 47103, VertexAttrib3sNV_remap_index },
   { 47128, VertexAttrib3svNV_remap_index },
   { 47540, VertexAttrib4dNV_remap_index },
   { 47566, VertexAttrib4dvNV_remap_index },
   { 47346, VertexAttrib4fNV_remap_index },
   { 47372, VertexAttrib4fvNV_remap_index },
   { 47152, VertexAttrib4sNV_remap_index },
   { 47178, VertexAttrib4svNV_remap_index },
   { 47590, VertexAttrib4ubNV_remap_index },
   { 47617, VertexAttrib4ubvNV_remap_index },
   { 46977, VertexAttribPointerNV_remap_index },
   { 47850, VertexAttribs1dvNV_remap_index },
   { 47746, VertexAttribs1fvNV_remap_index },
   { 47642, VertexAttribs1svNV_remap_index },
   { 47876, VertexAttribs2dvNV_remap_index },
   { 47772, VertexAttribs2fvNV_remap_index },
   { 47668, VertexAttribs2svNV_remap_index },
   { 47902, VertexAttribs3dvNV_remap_index },
   { 47798, VertexAttribs3fvNV_remap_index },
   { 47694, VertexAttribs3svNV_remap_index },
   { 47928, VertexAttribs4dvNV_remap_index },
   { 47824, VertexAttribs4fvNV_remap_index },
   { 47720, VertexAttribs4svNV_remap_index },
   { 47954, VertexAttribs4ubvNV_remap_index },
   { 48037, GetTexBumpParameterfvATI_remap_index },
   { 48068, GetTexBumpParameterivATI_remap_index },
   { 47981, TexBumpParameterfvATI_remap_index },
   { 48009, TexBumpParameterivATI_remap_index },
   { 48379, AlphaFragmentOp1ATI_remap_index },
   { 48409, AlphaFragmentOp2ATI_remap_index },
   { 48442, AlphaFragmentOp3ATI_remap_index },
   { 48182, BeginFragmentShaderATI_remap_index },
   { 48126, BindFragmentShaderATI_remap_index },
   { 48277, ColorFragmentOp1ATI_remap_index },
   { 48308, ColorFragmentOp2ATI_remap_index },
   { 48342, ColorFragmentOp3ATI_remap_index },
   { 48153, DeleteFragmentShaderATI_remap_index },
   { 48209, EndFragmentShaderATI_remap_index },
   { 48099, GenFragmentShadersATI_remap_index },
   { 48234, PassTexCoordATI_remap_index },
   { 48257, SampleMapATI_remap_index },
   { 48478, SetFragmentShaderConstantATI_remap_index },
   { 63407, DepthRangeArrayfvOES_remap_index },
   { 63435, DepthRangeIndexedfOES_remap_index },
   { 48539, ActiveStencilFaceEXT_remap_index },
   { 48945, GetProgramNamedParameterdvNV_remap_index },
   { 48908, GetProgramNamedParameterfvNV_remap_index },
   { 48801, ProgramNamedParameter4dNV_remap_index },
   { 48873, ProgramNamedParameter4dvNV_remap_index },
   { 48764, ProgramNamedParameter4fNV_remap_index },
   { 48838, ProgramNamedParameter4fvNV_remap_index },
   { 59159, PrimitiveRestartNV_remap_index },
   { 62897, GetTexGenxvOES_remap_index },
   { 62919, TexGenxOES_remap_index },
   { 62937, TexGenxvOES_remap_index },
   { 48982, DepthBoundsEXT_remap_index },
   { 49029, BindFramebufferEXT_remap_index },
   { 49003, BindRenderbufferEXT_remap_index },
   { 49054, StringMarkerGREMEDY_remap_index },
   { 49479, BufferParameteriAPPLE_remap_index },
   { 49508, FlushMappedBufferRangeAPPLE_remap_index },
   { 56936, VertexAttribI1iEXT_remap_index },
   { 57114, VertexAttribI1uiEXT_remap_index },
   { 56979, VertexAttribI2iEXT_remap_index },
   { 57345, VertexAttribI2ivEXT_remap_index },
   { 57159, VertexAttribI2uiEXT_remap_index },
   { 57527, VertexAttribI2uivEXT_remap_index },
   { 57023, VertexAttribI3iEXT_remap_index },
   { 57390, VertexAttribI3ivEXT_remap_index },
   { 57205, VertexAttribI3uiEXT_remap_index },
   { 57574, VertexAttribI3uivEXT_remap_index },
   { 57068, VertexAttribI4iEXT_remap_index },
   { 57435, VertexAttribI4ivEXT_remap_index },
   { 57252, VertexAttribI4uiEXT_remap_index },
   { 57621, VertexAttribI4uivEXT_remap_index },
   { 56603, ClearColorIiEXT_remap_index },
   { 56627, ClearColorIuiEXT_remap_index },
   { 59182, BindBufferOffsetEXT_remap_index },
   { 49814, BeginPerfMonitorAMD_remap_index },
   { 49748, DeletePerfMonitorsAMD_remap_index },
   { 49839, EndPerfMonitorAMD_remap_index },
   { 49723, GenPerfMonitorsAMD_remap_index },
   { 49862, GetPerfMonitorCounterDataAMD_remap_index },
   { 49686, GetPerfMonitorCounterInfoAMD_remap_index },
   { 49646, GetPerfMonitorCounterStringAMD_remap_index },
   { 49574, GetPerfMonitorCountersAMD_remap_index },
   { 49609, GetPerfMonitorGroupStringAMD_remap_index },
   { 49543, GetPerfMonitorGroupsAMD_remap_index },
   { 49776, SelectPerfMonitorCountersAMD_remap_index },
   { 48623, GetObjectParameterivAPPLE_remap_index },
   { 48565, ObjectPurgeableAPPLE_remap_index },
   { 48593, ObjectUnpurgeableAPPLE_remap_index },
   { 49987, ActiveProgramEXT_remap_index },
   { 50009, CreateShaderProgramEXT_remap_index },
   { 49961, UseShaderProgramEXT_remap_index },
   { 37098, TextureBarrierNV_remap_index },
   { 59423, VDPAUFiniNV_remap_index },
   { 59564, VDPAUGetSurfaceivNV_remap_index },
   { 59405, VDPAUInitNV_remap_index },
   { 59512, VDPAUIsSurfaceNV_remap_index },
   { 59620, VDPAUMapSurfacesNV_remap_index },
   { 59475, VDPAURegisterOutputSurfaceNV_remap_index },
   { 59439, VDPAURegisterVideoSurfaceNV_remap_index },
   { 59593, VDPAUSurfaceAccessNV_remap_index },
   { 59645, VDPAUUnmapSurfacesNV_remap_index },
   { 59534, VDPAUUnregisterSurfaceNV_remap_index },
   { 55800, BeginPerfQueryINTEL_remap_index },
   { 55747, CreatePerfQueryINTEL_remap_index },
   { 55774, DeletePerfQueryINTEL_remap_index },
   { 55825, EndPerfQueryINTEL_remap_index },
   { 55583, GetFirstPerfQueryIdINTEL_remap_index },
   { 55613, GetNextPerfQueryIdINTEL_remap_index },
   { 55708, GetPerfCounterInfoINTEL_remap_index },
   { 55848, GetPerfQueryDataINTEL_remap_index },
   { 55643, GetPerfQueryIdByNameINTEL_remap_index },
   { 55675, GetPerfQueryInfoINTEL_remap_index },
   { 55915, PolygonOffsetClampEXT_remap_index },
   { 55479, SubpixelPrecisionBiasNV_remap_index },
   { 55509, ConservativeRasterParameterfNV_remap_index },
   { 55546, ConservativeRasterParameteriNV_remap_index },
   { 55965, WindowRectanglesEXT_remap_index },
   { 60025, BufferStorageMemEXT_remap_index },
   { 59779, CreateMemoryObjectsEXT_remap_index },
   { 59727, DeleteMemoryObjectsEXT_remap_index },
   { 60333, DeleteSemaphoresEXT_remap_index },
   { 60310, GenSemaphoresEXT_remap_index },
   { 59842, GetMemoryObjectParameterivEXT_remap_index },
   { 60413, GetSemaphoreParameterui64vEXT_remap_index },
   { 59698, GetUnsignedBytei_vEXT_remap_index },
   { 59672, GetUnsignedBytevEXT_remap_index },
   { 59756, IsMemoryObjectEXT_remap_index },
   { 60359, IsSemaphoreEXT_remap_index },
   { 59808, MemoryObjectParameterivEXT_remap_index },
   { 60215, NamedBufferStorageMemEXT_remap_index },
   { 60379, SemaphoreParameterui64vEXT_remap_index },
   { 60477, SignalSemaphoreEXT_remap_index },
   { 60248, TexStorageMem1DEXT_remap_index },
   { 59879, TexStorageMem2DEXT_remap_index },
   { 59909, TexStorageMem2DMultisampleEXT_remap_index },
   { 59951, TexStorageMem3DEXT_remap_index },
   { 59982, TexStorageMem3DMultisampleEXT_remap_index },
   { 60277, TextureStorageMem1DEXT_remap_index },
   { 60053, TextureStorageMem2DEXT_remap_index },
   { 60087, TextureStorageMem2DMultisampleEXT_remap_index },
   { 60133, TextureStorageMem3DEXT_remap_index },
   { 60168, TextureStorageMem3DMultisampleEXT_remap_index },
   { 60450, WaitSemaphoreEXT_remap_index },
   { 60506, ImportMemoryFdEXT_remap_index },
   { 60532, ImportSemaphoreFdEXT_remap_index },
   { 55992, FramebufferFetchBarrierEXT_remap_index },
   { 56075, NamedRenderbufferStorageMultisampleAdvancedAMD_remap_index },
   { 56023, RenderbufferStorageMultisampleAdvancedAMD_remap_index },
   { 56132, StencilFuncSeparateATI_remap_index },
   { 56163, ProgramEnvParameters4fvEXT_remap_index },
   { 56198, ProgramLocalParameters4fvEXT_remap_index },
   { 56486, EGLImageTargetRenderbufferStorageOES_remap_index },
   { 56453, EGLImageTargetTexture2DOES_remap_index },
   { 61561, AlphaFuncx_remap_index },
   { 61594, ClearColorx_remap_index },
   { 61631, ClearDepthx_remap_index },
   { 61665, Color4x_remap_index },
   { 61694, DepthRangex_remap_index },
   { 61729, Fogx_remap_index },
   { 61750, Fogxv_remap_index },
   { 63028, Frustumf_remap_index },
   { 61773, Frustumx_remap_index },
   { 61806, LightModelx_remap_index },
   { 61841, LightModelxv_remap_index },
   { 61878, Lightx_remap_index },
   { 61904, Lightxv_remap_index },
   { 61932, LineWidthx_remap_index },
   { 61964, LoadMatrixx_remap_index },
   { 61998, Materialx_remap_index },
   { 62030, Materialxv_remap_index },
   { 62064, MultMatrixx_remap_index },
   { 62098, MultiTexCoord4x_remap_index },
   { 62144, Normal3x_remap_index },
   { 63061, Orthof_remap_index },
   { 62174, Orthox_remap_index },
   { 62203, PointSizex_remap_index },
   { 62235, PolygonOffsetx_remap_index },
   { 62276, Rotatex_remap_index },
   { 62305, SampleCoveragex_remap_index },
   { 62348, Scalex_remap_index },
   { 62374, TexEnvx_remap_index },
   { 62402, TexEnvxv_remap_index },
   { 62432, TexParameterx_remap_index },
   { 62472, Translatex_remap_index },
   { 62956, ClipPlanef_remap_index },
   { 62506, ClipPlanex_remap_index },
   { 62989, GetClipPlanef_remap_index },
   { 62539, GetClipPlanex_remap_index },
   { 62578, GetFixedv_remap_index },
   { 62609, GetLightxv_remap_index },
   { 62643, GetMaterialxv_remap_index },
   { 62683, GetTexEnvxv_remap_index },
   { 62719, GetTexParameterxv_remap_index },
   { 62767, PointParameterx_remap_index },
   { 62810, PointParameterxv_remap_index },
   { 62855, TexParameterxv_remap_index },
   { 37353, BlendBarrier_remap_index },
   { 37242, PrimitiveBoundingBox_remap_index },
   { 39061, MaxShaderCompilerThreadsKHR_remap_index },
   { 50076, MatrixLoadfEXT_remap_index },
   { 50097, MatrixLoaddEXT_remap_index },
   { 50118, MatrixMultfEXT_remap_index },
   { 50139, MatrixMultdEXT_remap_index },
   { 50160, MatrixLoadIdentityEXT_remap_index },
   { 50187, MatrixRotatefEXT_remap_index },
   { 50213, MatrixRotatedEXT_remap_index },
   { 50239, MatrixScalefEXT_remap_index },
   { 50263, MatrixScaledEXT_remap_index },
   { 50287, MatrixTranslatefEXT_remap_index },
   { 50315, MatrixTranslatedEXT_remap_index },
   { 50343, MatrixOrthoEXT_remap_index },
   { 50369, MatrixFrustumEXT_remap_index },
   { 50397, MatrixPushEXT_remap_index },
   { 50416, MatrixPopEXT_remap_index },
   { 52473, MatrixLoadTransposefEXT_remap_index },
   { 52503, MatrixLoadTransposedEXT_remap_index },
   { 52533, MatrixMultTransposefEXT_remap_index },
   { 52563, MatrixMultTransposedEXT_remap_index },
   { 51168, BindMultiTextureEXT_remap_index },
   { 53190, NamedBufferDataEXT_remap_index },
   { 53217, NamedBufferSubDataEXT_remap_index },
   { 32994, NamedBufferStorageEXT_remap_index },
   { 53411, MapNamedBufferRangeEXT_remap_index },
   { 50756, TextureImage1DEXT_remap_index },
   { 50787, TextureImage2DEXT_remap_index },
   { 50819, TextureImage3DEXT_remap_index },
   { 50852, TextureSubImage1DEXT_remap_index },
   { 50885, TextureSubImage2DEXT_remap_index },
   { 50920, TextureSubImage3DEXT_remap_index },
   { 50957, CopyTextureImage1DEXT_remap_index },
   { 50991, CopyTextureImage2DEXT_remap_index },
   { 51026, CopyTextureSubImage1DEXT_remap_index },
   { 51062, CopyTextureSubImage2DEXT_remap_index },
   { 51100, CopyTextureSubImage3DEXT_remap_index },
   { 53247, MapNamedBufferEXT_remap_index },
   { 50494, GetTextureParameterivEXT_remap_index },
   { 50527, GetTextureParameterfvEXT_remap_index },
   { 50638, TextureParameteriEXT_remap_index },
   { 50667, TextureParameterivEXT_remap_index },
   { 50697, TextureParameterfEXT_remap_index },
   { 50726, TextureParameterfvEXT_remap_index },
   { 51139, GetTextureImageEXT_remap_index },
   { 50560, GetTextureLevelParameterivEXT_remap_index },
   { 50599, GetTextureLevelParameterfvEXT_remap_index },
   { 53271, GetNamedBufferSubDataEXT_remap_index },
   { 53304, GetNamedBufferPointervEXT_remap_index },
   { 53337, GetNamedBufferParameterivEXT_remap_index },
   { 53373, FlushMappedNamedBufferRangeEXT_remap_index },
   { 53442, FramebufferDrawBufferEXT_remap_index },
   { 53473, FramebufferDrawBuffersEXT_remap_index },
   { 53506, FramebufferReadBufferEXT_remap_index },
   { 53537, GetFramebufferParameterivEXT_remap_index },
   { 53573, CheckNamedFramebufferStatusEXT_remap_index },
   { 53610, NamedFramebufferTexture1DEXT_remap_index },
   { 53648, NamedFramebufferTexture2DEXT_remap_index },
   { 53686, NamedFramebufferTexture3DEXT_remap_index },
   { 53725, NamedFramebufferRenderbufferEXT_remap_index },
   { 53765, GetNamedFramebufferAttachmentParameterivEXT_remap_index },
   { 51195, EnableClientStateiEXT_remap_index },
   { 51253, DisableClientStateiEXT_remap_index },
   { 51313, GetPointerIndexedvEXT_remap_index },
   { 51361, MultiTexEnviEXT_remap_index },
   { 51385, MultiTexEnvivEXT_remap_index },
   { 51410, MultiTexEnvfEXT_remap_index },
   { 51434, MultiTexEnvfvEXT_remap_index },
   { 51459, GetMultiTexEnvivEXT_remap_index },
   { 51487, GetMultiTexEnvfvEXT_remap_index },
   { 51515, MultiTexParameteriEXT_remap_index },
   { 51545, MultiTexParameterivEXT_remap_index },
   { 51576, MultiTexParameterfEXT_remap_index },
   { 51606, MultiTexParameterfvEXT_remap_index },
   { 51705, GetMultiTexImageEXT_remap_index },
   { 51815, MultiTexImage1DEXT_remap_index },
   { 51847, MultiTexImage2DEXT_remap_index },
   { 51880, MultiTexImage3DEXT_remap_index },
   { 51914, MultiTexSubImage1DEXT_remap_index },
   { 51948, MultiTexSubImage2DEXT_remap_index },
   { 51984, MultiTexSubImage3DEXT_remap_index },
   { 51637, GetMultiTexParameterivEXT_remap_index },
   { 51671, GetMultiTexParameterfvEXT_remap_index },
   { 52022, CopyMultiTexImage1DEXT_remap_index },
   { 52057, CopyMultiTexImage2DEXT_remap_index },
   { 52093, CopyMultiTexSubImage1DEXT_remap_index },
   { 52130, CopyMultiTexSubImage2DEXT_remap_index },
   { 52169, CopyMultiTexSubImage3DEXT_remap_index },
   { 52209, MultiTexGendEXT_remap_index },
   { 52233, MultiTexGendvEXT_remap_index },
   { 52258, MultiTexGenfEXT_remap_index },
   { 52282, MultiTexGenfvEXT_remap_index },
   { 52307, MultiTexGeniEXT_remap_index },
   { 52331, MultiTexGenivEXT_remap_index },
   { 52356, GetMultiTexGendvEXT_remap_index },
   { 52384, GetMultiTexGenfvEXT_remap_index },
   { 52412, GetMultiTexGenivEXT_remap_index },
   { 52440, MultiTexCoordPointerEXT_remap_index },
   { 55448, BindImageTextureEXT_remap_index },
   { 52593, CompressedTextureImage1DEXT_remap_index },
   { 52633, CompressedTextureImage2DEXT_remap_index },
   { 52674, CompressedTextureImage3DEXT_remap_index },
   { 52716, CompressedTextureSubImage1DEXT_remap_index },
   { 52759, CompressedTextureSubImage2DEXT_remap_index },
   { 52804, CompressedTextureSubImage3DEXT_remap_index },
   { 52851, GetCompressedTextureImageEXT_remap_index },
   { 52888, CompressedMultiTexImage1DEXT_remap_index },
   { 52929, CompressedMultiTexImage2DEXT_remap_index },
   { 52971, CompressedMultiTexImage3DEXT_remap_index },
   { 53014, CompressedMultiTexSubImage1DEXT_remap_index },
   { 53058, CompressedMultiTexSubImage2DEXT_remap_index },
   { 53104, CompressedMultiTexSubImage3DEXT_remap_index },
   { 53152, GetCompressedMultiTexImageEXT_remap_index },
   { 51735, GetMultiTexLevelParameterivEXT_remap_index },
   { 51775, GetMultiTexLevelParameterfvEXT_remap_index },
   { 63464, FramebufferParameteriMESA_remap_index },
   { 63497, GetFramebufferParameterivMESA_remap_index },
   { 53817, NamedRenderbufferStorageEXT_remap_index },
   { 53853, GetNamedRenderbufferParameterivEXT_remap_index },
   { 50434, ClientAttribDefaultEXT_remap_index },
   { 50462, PushClientAttribDefaultEXT_remap_index },
   { 54739, NamedProgramStringEXT_remap_index },
   { 54770, GetNamedProgramStringEXT_remap_index },
   { 54803, NamedProgramLocalParameter4fEXT_remap_index },
   { 54846, NamedProgramLocalParameter4fvEXT_remap_index },
   { 54887, GetNamedProgramLocalParameterfvEXT_remap_index },
   { 54930, NamedProgramLocalParameter4dEXT_remap_index },
   { 54973, NamedProgramLocalParameter4dvEXT_remap_index },
   { 55014, GetNamedProgramLocalParameterdvEXT_remap_index },
   { 55057, GetNamedProgramivEXT_remap_index },
   { 55086, TextureBufferEXT_remap_index },
   { 55111, MultiTexBufferEXT_remap_index },
   { 55137, TextureParameterIivEXT_remap_index },
   { 55168, TextureParameterIuivEXT_remap_index },
   { 55200, GetTextureParameterIivEXT_remap_index },
   { 55234, GetTextureParameterIuivEXT_remap_index },
   { 55269, MultiTexParameterIivEXT_remap_index },
   { 55301, MultiTexParameterIuivEXT_remap_index },
   { 55334, GetMultiTexParameterIivEXT_remap_index },
   { 55369, GetMultiTexParameterIuivEXT_remap_index },
   { 55405, NamedProgramLocalParameters4fvEXT_remap_index },
   { 53895, GenerateTextureMipmapEXT_remap_index },
   { 53926, GenerateMultiTexMipmapEXT_remap_index },
   { 53958, NamedRenderbufferStorageMultisampleEXT_remap_index },
   { 54006, NamedCopyBufferSubDataEXT_remap_index },
   { 54041, VertexArrayVertexOffsetEXT_remap_index },
   { 54078, VertexArrayColorOffsetEXT_remap_index },
   { 54114, VertexArrayEdgeFlagOffsetEXT_remap_index },
   { 54151, VertexArrayIndexOffsetEXT_remap_index },
   { 54186, VertexArrayNormalOffsetEXT_remap_index },
   { 54222, VertexArrayTexCoordOffsetEXT_remap_index },
   { 54261, VertexArrayMultiTexCoordOffsetEXT_remap_index },
   { 54306, VertexArrayFogCoordOffsetEXT_remap_index },
   { 54344, VertexArraySecondaryColorOffsetEXT_remap_index },
   { 54389, VertexArrayVertexAttribOffsetEXT_remap_index },
   { 54434, VertexArrayVertexAttribIOffsetEXT_remap_index },
   { 54479, EnableVertexArrayEXT_remap_index },
   { 54506, DisableVertexArrayEXT_remap_index },
   { 54534, EnableVertexArrayAttribEXT_remap_index },
   { 54567, DisableVertexArrayAttribEXT_remap_index },
   { 54601, GetVertexArrayIntegervEXT_remap_index },
   { 54634, GetVertexArrayPointervEXT_remap_index },
   { 54667, GetVertexArrayIntegeri_vEXT_remap_index },
   { 54703, GetVertexArrayPointeri_vEXT_remap_index },
   { 31377, ClearNamedBufferDataEXT_remap_index },
   { 31410, ClearNamedBufferSubDataEXT_remap_index },
   { 32114, NamedFramebufferParameteriEXT_remap_index },
   { 32151, GetNamedFramebufferParameterivEXT_remap_index },
   { 28978, VertexArrayVertexAttribLOffsetEXT_remap_index },
   { 20649, VertexArrayVertexAttribDivisorEXT_remap_index },
   { 32738, TextureBufferRangeEXT_remap_index },
   { 32868, TextureStorage2DMultisampleEXT_remap_index },
   { 32910, TextureStorage3DMultisampleEXT_remap_index },
   { 31800, VertexArrayBindVertexBufferEXT_remap_index },
   { 31840, VertexArrayVertexAttribFormatEXT_remap_index },
   { 31883, VertexArrayVertexAttribIFormatEXT_remap_index },
   { 31926, VertexArrayVertexAttribLFormatEXT_remap_index },
   { 31969, VertexArrayVertexAttribBindingEXT_remap_index },
   { 32010, VertexArrayVertexBindingDivisorEXT_remap_index },
   { 37168, NamedBufferPageCommitmentEXT_remap_index },
   { 22424, NamedStringARB_remap_index },
   { 22448, DeleteNamedStringARB_remap_index },
   { 22475, CompileShaderIncludeARB_remap_index },
   { 22507, IsNamedStringARB_remap_index },
   { 22530, GetNamedStringARB_remap_index },
   { 22557, GetNamedStringivARB_remap_index },
   { 56529, EGLImageTargetTexStorageEXT_remap_index },
   { 56564, EGLImageTargetTextureStorageEXT_remap_index },
   { 50038, CopyImageSubDataNV_remap_index },
   { 60560, ViewportSwizzleNV_remap_index },
   { 55879, AlphaToCoverageDitherControlNV_remap_index },
   { 56390, InternalBufferSubDataCopyMESA_remap_index },
   { 60587, Vertex2hNV_remap_index },
   { 60604, Vertex2hvNV_remap_index },
   { 60621, Vertex3hNV_remap_index },
   { 60639, Vertex3hvNV_remap_index },
   { 60656, Vertex4hNV_remap_index },
   { 60675, Vertex4hvNV_remap_index },
   { 60692, Normal3hNV_remap_index },
   { 60710, Normal3hvNV_remap_index },
   { 60727, Color3hNV_remap_index },
   { 60744, Color3hvNV_remap_index },
   { 60760, Color4hNV_remap_index },
   { 60778, Color4hvNV_remap_index },
   { 60794, TexCoord1hNV_remap_index },
   { 60812, TexCoord1hvNV_remap_index },
   { 60831, TexCoord2hNV_remap_index },
   { 60850, TexCoord2hvNV_remap_index },
   { 60869, TexCoord3hNV_remap_index },
   { 60889, TexCoord3hvNV_remap_index },
   { 60908, TexCoord4hNV_remap_index },
   { 60929, TexCoord4hvNV_remap_index },
   { 60948, MultiTexCoord1hNV_remap_index },
   { 60972, MultiTexCoord1hvNV_remap_index },
   { 60997, MultiTexCoord2hNV_remap_index },
   { 61022, MultiTexCoord2hvNV_remap_index },
   { 61047, MultiTexCoord3hNV_remap_index },
   { 61073, MultiTexCoord3hvNV_remap_index },
   { 61098, MultiTexCoord4hNV_remap_index },
   { 61125, MultiTexCoord4hvNV_remap_index },
   { 61448, FogCoordhNV_remap_index },
   { 61465, FogCoordhvNV_remap_index },
   { 61483, SecondaryColor3hNV_remap_index },
   { 61509, SecondaryColor3hvNV_remap_index },
   { 56431, InternalSetError_remap_index },
   { 61150, VertexAttrib1hNV_remap_index },
   { 61173, VertexAttrib1hvNV_remap_index },
   { 61197, VertexAttrib2hNV_remap_index },
   { 61221, VertexAttrib2hvNV_remap_index },
   { 61245, VertexAttrib3hNV_remap_index },
   { 61270, VertexAttrib3hvNV_remap_index },
   { 61294, VertexAttrib4hNV_remap_index },
   { 61320, VertexAttrib4hvNV_remap_index },
   { 61344, VertexAttribs1hvNV_remap_index },
   { 61370, VertexAttribs2hvNV_remap_index },
   { 61396, VertexAttribs3hvNV_remap_index },
   { 61422, VertexAttribs4hvNV_remap_index },
   {    -1, -1 }
};

