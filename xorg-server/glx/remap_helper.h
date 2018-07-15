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
   /* _mesa_function_pool[20649]: MapBufferRange (will be remapped) */
   "iiii\0"
   "glMapBufferRange\0"
   "glMapBufferRangeEXT\0"
   "\0"
   /* _mesa_function_pool[20692]: FlushMappedBufferRange (will be remapped) */
   "iii\0"
   "glFlushMappedBufferRange\0"
   "glFlushMappedBufferRangeEXT\0"
   "\0"
   /* _mesa_function_pool[20750]: TexBuffer (will be remapped) */
   "iii\0"
   "glTexBufferARB\0"
   "glTexBuffer\0"
   "glTexBufferEXT\0"
   "glTexBufferOES\0"
   "\0"
   /* _mesa_function_pool[20812]: BindVertexArray (will be remapped) */
   "i\0"
   "glBindVertexArray\0"
   "glBindVertexArrayOES\0"
   "\0"
   /* _mesa_function_pool[20854]: DeleteVertexArrays (will be remapped) */
   "ip\0"
   "glDeleteVertexArrays\0"
   "glDeleteVertexArraysOES\0"
   "\0"
   /* _mesa_function_pool[20903]: GenVertexArrays (will be remapped) */
   "ip\0"
   "glGenVertexArrays\0"
   "glGenVertexArraysOES\0"
   "\0"
   /* _mesa_function_pool[20946]: IsVertexArray (will be remapped) */
   "i\0"
   "glIsVertexArray\0"
   "glIsVertexArrayOES\0"
   "\0"
   /* _mesa_function_pool[20984]: GetUniformIndices (will be remapped) */
   "iipp\0"
   "glGetUniformIndices\0"
   "\0"
   /* _mesa_function_pool[21010]: GetActiveUniformsiv (will be remapped) */
   "iipip\0"
   "glGetActiveUniformsiv\0"
   "\0"
   /* _mesa_function_pool[21039]: GetActiveUniformName (will be remapped) */
   "iiipp\0"
   "glGetActiveUniformName\0"
   "\0"
   /* _mesa_function_pool[21069]: GetUniformBlockIndex (will be remapped) */
   "ip\0"
   "glGetUniformBlockIndex\0"
   "\0"
   /* _mesa_function_pool[21096]: GetActiveUniformBlockiv (will be remapped) */
   "iiip\0"
   "glGetActiveUniformBlockiv\0"
   "\0"
   /* _mesa_function_pool[21128]: GetActiveUniformBlockName (will be remapped) */
   "iiipp\0"
   "glGetActiveUniformBlockName\0"
   "\0"
   /* _mesa_function_pool[21163]: UniformBlockBinding (will be remapped) */
   "iii\0"
   "glUniformBlockBinding\0"
   "\0"
   /* _mesa_function_pool[21190]: CopyBufferSubData (will be remapped) */
   "iiiii\0"
   "glCopyBufferSubData\0"
   "\0"
   /* _mesa_function_pool[21217]: DrawElementsBaseVertex (will be remapped) */
   "iiipi\0"
   "glDrawElementsBaseVertex\0"
   "glDrawElementsBaseVertexEXT\0"
   "glDrawElementsBaseVertexOES\0"
   "\0"
   /* _mesa_function_pool[21305]: DrawRangeElementsBaseVertex (will be remapped) */
   "iiiiipi\0"
   "glDrawRangeElementsBaseVertex\0"
   "glDrawRangeElementsBaseVertexEXT\0"
   "glDrawRangeElementsBaseVertexOES\0"
   "\0"
   /* _mesa_function_pool[21410]: MultiDrawElementsBaseVertex (will be remapped) */
   "ipipip\0"
   "glMultiDrawElementsBaseVertex\0"
   "glMultiDrawElementsBaseVertexEXT\0"
   "\0"
   /* _mesa_function_pool[21481]: DrawElementsInstancedBaseVertex (will be remapped) */
   "iiipii\0"
   "glDrawElementsInstancedBaseVertex\0"
   "glDrawElementsInstancedBaseVertexEXT\0"
   "glDrawElementsInstancedBaseVertexOES\0"
   "\0"
   /* _mesa_function_pool[21597]: FenceSync (will be remapped) */
   "ii\0"
   "glFenceSync\0"
   "\0"
   /* _mesa_function_pool[21613]: IsSync (will be remapped) */
   "i\0"
   "glIsSync\0"
   "\0"
   /* _mesa_function_pool[21625]: DeleteSync (will be remapped) */
   "i\0"
   "glDeleteSync\0"
   "\0"
   /* _mesa_function_pool[21641]: ClientWaitSync (will be remapped) */
   "iii\0"
   "glClientWaitSync\0"
   "\0"
   /* _mesa_function_pool[21663]: WaitSync (will be remapped) */
   "iii\0"
   "glWaitSync\0"
   "\0"
   /* _mesa_function_pool[21679]: GetInteger64v (will be remapped) */
   "ip\0"
   "glGetInteger64v\0"
   "\0"
   /* _mesa_function_pool[21699]: GetSynciv (will be remapped) */
   "iiipp\0"
   "glGetSynciv\0"
   "\0"
   /* _mesa_function_pool[21718]: TexImage2DMultisample (will be remapped) */
   "iiiiii\0"
   "glTexImage2DMultisample\0"
   "\0"
   /* _mesa_function_pool[21750]: TexImage3DMultisample (will be remapped) */
   "iiiiiii\0"
   "glTexImage3DMultisample\0"
   "\0"
   /* _mesa_function_pool[21783]: GetMultisamplefv (will be remapped) */
   "iip\0"
   "glGetMultisamplefv\0"
   "\0"
   /* _mesa_function_pool[21807]: SampleMaski (will be remapped) */
   "ii\0"
   "glSampleMaski\0"
   "\0"
   /* _mesa_function_pool[21825]: BlendEquationiARB (will be remapped) */
   "ii\0"
   "glBlendEquationiARB\0"
   "glBlendEquationIndexedAMD\0"
   "glBlendEquationi\0"
   "glBlendEquationiEXT\0"
   "glBlendEquationiOES\0"
   "\0"
   /* _mesa_function_pool[21932]: BlendEquationSeparateiARB (will be remapped) */
   "iii\0"
   "glBlendEquationSeparateiARB\0"
   "glBlendEquationSeparateIndexedAMD\0"
   "glBlendEquationSeparatei\0"
   "glBlendEquationSeparateiEXT\0"
   "glBlendEquationSeparateiOES\0"
   "\0"
   /* _mesa_function_pool[22080]: BlendFunciARB (will be remapped) */
   "iii\0"
   "glBlendFunciARB\0"
   "glBlendFuncIndexedAMD\0"
   "glBlendFunci\0"
   "glBlendFunciEXT\0"
   "glBlendFunciOES\0"
   "\0"
   /* _mesa_function_pool[22168]: BlendFuncSeparateiARB (will be remapped) */
   "iiiii\0"
   "glBlendFuncSeparateiARB\0"
   "glBlendFuncSeparateIndexedAMD\0"
   "glBlendFuncSeparatei\0"
   "glBlendFuncSeparateiEXT\0"
   "glBlendFuncSeparateiOES\0"
   "\0"
   /* _mesa_function_pool[22298]: MinSampleShading (will be remapped) */
   "f\0"
   "glMinSampleShadingARB\0"
   "glMinSampleShading\0"
   "glMinSampleShadingOES\0"
   "\0"
   /* _mesa_function_pool[22364]: BindFragDataLocationIndexed (will be remapped) */
   "iiip\0"
   "glBindFragDataLocationIndexed\0"
   "glBindFragDataLocationIndexedEXT\0"
   "\0"
   /* _mesa_function_pool[22433]: GetFragDataIndex (will be remapped) */
   "ip\0"
   "glGetFragDataIndex\0"
   "glGetFragDataIndexEXT\0"
   "\0"
   /* _mesa_function_pool[22478]: GenSamplers (will be remapped) */
   "ip\0"
   "glGenSamplers\0"
   "\0"
   /* _mesa_function_pool[22496]: DeleteSamplers (will be remapped) */
   "ip\0"
   "glDeleteSamplers\0"
   "\0"
   /* _mesa_function_pool[22517]: IsSampler (will be remapped) */
   "i\0"
   "glIsSampler\0"
   "\0"
   /* _mesa_function_pool[22532]: BindSampler (will be remapped) */
   "ii\0"
   "glBindSampler\0"
   "\0"
   /* _mesa_function_pool[22550]: SamplerParameteri (will be remapped) */
   "iii\0"
   "glSamplerParameteri\0"
   "\0"
   /* _mesa_function_pool[22575]: SamplerParameterf (will be remapped) */
   "iif\0"
   "glSamplerParameterf\0"
   "\0"
   /* _mesa_function_pool[22600]: SamplerParameteriv (will be remapped) */
   "iip\0"
   "glSamplerParameteriv\0"
   "\0"
   /* _mesa_function_pool[22626]: SamplerParameterfv (will be remapped) */
   "iip\0"
   "glSamplerParameterfv\0"
   "\0"
   /* _mesa_function_pool[22652]: SamplerParameterIiv (will be remapped) */
   "iip\0"
   "glSamplerParameterIiv\0"
   "glSamplerParameterIivEXT\0"
   "glSamplerParameterIivOES\0"
   "\0"
   /* _mesa_function_pool[22729]: SamplerParameterIuiv (will be remapped) */
   "iip\0"
   "glSamplerParameterIuiv\0"
   "glSamplerParameterIuivEXT\0"
   "glSamplerParameterIuivOES\0"
   "\0"
   /* _mesa_function_pool[22809]: GetSamplerParameteriv (will be remapped) */
   "iip\0"
   "glGetSamplerParameteriv\0"
   "\0"
   /* _mesa_function_pool[22838]: GetSamplerParameterfv (will be remapped) */
   "iip\0"
   "glGetSamplerParameterfv\0"
   "\0"
   /* _mesa_function_pool[22867]: GetSamplerParameterIiv (will be remapped) */
   "iip\0"
   "glGetSamplerParameterIiv\0"
   "glGetSamplerParameterIivEXT\0"
   "glGetSamplerParameterIivOES\0"
   "\0"
   /* _mesa_function_pool[22953]: GetSamplerParameterIuiv (will be remapped) */
   "iip\0"
   "glGetSamplerParameterIuiv\0"
   "glGetSamplerParameterIuivEXT\0"
   "glGetSamplerParameterIuivOES\0"
   "\0"
   /* _mesa_function_pool[23042]: GetQueryObjecti64v (will be remapped) */
   "iip\0"
   "glGetQueryObjecti64v\0"
   "glGetQueryObjecti64vEXT\0"
   "\0"
   /* _mesa_function_pool[23092]: GetQueryObjectui64v (will be remapped) */
   "iip\0"
   "glGetQueryObjectui64v\0"
   "glGetQueryObjectui64vEXT\0"
   "\0"
   /* _mesa_function_pool[23144]: QueryCounter (will be remapped) */
   "ii\0"
   "glQueryCounter\0"
   "glQueryCounterEXT\0"
   "\0"
   /* _mesa_function_pool[23181]: VertexP2ui (will be remapped) */
   "ii\0"
   "glVertexP2ui\0"
   "\0"
   /* _mesa_function_pool[23198]: VertexP3ui (will be remapped) */
   "ii\0"
   "glVertexP3ui\0"
   "\0"
   /* _mesa_function_pool[23215]: VertexP4ui (will be remapped) */
   "ii\0"
   "glVertexP4ui\0"
   "\0"
   /* _mesa_function_pool[23232]: VertexP2uiv (will be remapped) */
   "ip\0"
   "glVertexP2uiv\0"
   "\0"
   /* _mesa_function_pool[23250]: VertexP3uiv (will be remapped) */
   "ip\0"
   "glVertexP3uiv\0"
   "\0"
   /* _mesa_function_pool[23268]: VertexP4uiv (will be remapped) */
   "ip\0"
   "glVertexP4uiv\0"
   "\0"
   /* _mesa_function_pool[23286]: TexCoordP1ui (will be remapped) */
   "ii\0"
   "glTexCoordP1ui\0"
   "\0"
   /* _mesa_function_pool[23305]: TexCoordP2ui (will be remapped) */
   "ii\0"
   "glTexCoordP2ui\0"
   "\0"
   /* _mesa_function_pool[23324]: TexCoordP3ui (will be remapped) */
   "ii\0"
   "glTexCoordP3ui\0"
   "\0"
   /* _mesa_function_pool[23343]: TexCoordP4ui (will be remapped) */
   "ii\0"
   "glTexCoordP4ui\0"
   "\0"
   /* _mesa_function_pool[23362]: TexCoordP1uiv (will be remapped) */
   "ip\0"
   "glTexCoordP1uiv\0"
   "\0"
   /* _mesa_function_pool[23382]: TexCoordP2uiv (will be remapped) */
   "ip\0"
   "glTexCoordP2uiv\0"
   "\0"
   /* _mesa_function_pool[23402]: TexCoordP3uiv (will be remapped) */
   "ip\0"
   "glTexCoordP3uiv\0"
   "\0"
   /* _mesa_function_pool[23422]: TexCoordP4uiv (will be remapped) */
   "ip\0"
   "glTexCoordP4uiv\0"
   "\0"
   /* _mesa_function_pool[23442]: MultiTexCoordP1ui (will be remapped) */
   "iii\0"
   "glMultiTexCoordP1ui\0"
   "\0"
   /* _mesa_function_pool[23467]: MultiTexCoordP2ui (will be remapped) */
   "iii\0"
   "glMultiTexCoordP2ui\0"
   "\0"
   /* _mesa_function_pool[23492]: MultiTexCoordP3ui (will be remapped) */
   "iii\0"
   "glMultiTexCoordP3ui\0"
   "\0"
   /* _mesa_function_pool[23517]: MultiTexCoordP4ui (will be remapped) */
   "iii\0"
   "glMultiTexCoordP4ui\0"
   "\0"
   /* _mesa_function_pool[23542]: MultiTexCoordP1uiv (will be remapped) */
   "iip\0"
   "glMultiTexCoordP1uiv\0"
   "\0"
   /* _mesa_function_pool[23568]: MultiTexCoordP2uiv (will be remapped) */
   "iip\0"
   "glMultiTexCoordP2uiv\0"
   "\0"
   /* _mesa_function_pool[23594]: MultiTexCoordP3uiv (will be remapped) */
   "iip\0"
   "glMultiTexCoordP3uiv\0"
   "\0"
   /* _mesa_function_pool[23620]: MultiTexCoordP4uiv (will be remapped) */
   "iip\0"
   "glMultiTexCoordP4uiv\0"
   "\0"
   /* _mesa_function_pool[23646]: NormalP3ui (will be remapped) */
   "ii\0"
   "glNormalP3ui\0"
   "\0"
   /* _mesa_function_pool[23663]: NormalP3uiv (will be remapped) */
   "ip\0"
   "glNormalP3uiv\0"
   "\0"
   /* _mesa_function_pool[23681]: ColorP3ui (will be remapped) */
   "ii\0"
   "glColorP3ui\0"
   "\0"
   /* _mesa_function_pool[23697]: ColorP4ui (will be remapped) */
   "ii\0"
   "glColorP4ui\0"
   "\0"
   /* _mesa_function_pool[23713]: ColorP3uiv (will be remapped) */
   "ip\0"
   "glColorP3uiv\0"
   "\0"
   /* _mesa_function_pool[23730]: ColorP4uiv (will be remapped) */
   "ip\0"
   "glColorP4uiv\0"
   "\0"
   /* _mesa_function_pool[23747]: SecondaryColorP3ui (will be remapped) */
   "ii\0"
   "glSecondaryColorP3ui\0"
   "\0"
   /* _mesa_function_pool[23772]: SecondaryColorP3uiv (will be remapped) */
   "ip\0"
   "glSecondaryColorP3uiv\0"
   "\0"
   /* _mesa_function_pool[23798]: VertexAttribP1ui (will be remapped) */
   "iiii\0"
   "glVertexAttribP1ui\0"
   "\0"
   /* _mesa_function_pool[23823]: VertexAttribP2ui (will be remapped) */
   "iiii\0"
   "glVertexAttribP2ui\0"
   "\0"
   /* _mesa_function_pool[23848]: VertexAttribP3ui (will be remapped) */
   "iiii\0"
   "glVertexAttribP3ui\0"
   "\0"
   /* _mesa_function_pool[23873]: VertexAttribP4ui (will be remapped) */
   "iiii\0"
   "glVertexAttribP4ui\0"
   "\0"
   /* _mesa_function_pool[23898]: VertexAttribP1uiv (will be remapped) */
   "iiip\0"
   "glVertexAttribP1uiv\0"
   "\0"
   /* _mesa_function_pool[23924]: VertexAttribP2uiv (will be remapped) */
   "iiip\0"
   "glVertexAttribP2uiv\0"
   "\0"
   /* _mesa_function_pool[23950]: VertexAttribP3uiv (will be remapped) */
   "iiip\0"
   "glVertexAttribP3uiv\0"
   "\0"
   /* _mesa_function_pool[23976]: VertexAttribP4uiv (will be remapped) */
   "iiip\0"
   "glVertexAttribP4uiv\0"
   "\0"
   /* _mesa_function_pool[24002]: GetSubroutineUniformLocation (will be remapped) */
   "iip\0"
   "glGetSubroutineUniformLocation\0"
   "\0"
   /* _mesa_function_pool[24038]: GetSubroutineIndex (will be remapped) */
   "iip\0"
   "glGetSubroutineIndex\0"
   "\0"
   /* _mesa_function_pool[24064]: GetActiveSubroutineUniformiv (will be remapped) */
   "iiiip\0"
   "glGetActiveSubroutineUniformiv\0"
   "\0"
   /* _mesa_function_pool[24102]: GetActiveSubroutineUniformName (will be remapped) */
   "iiiipp\0"
   "glGetActiveSubroutineUniformName\0"
   "\0"
   /* _mesa_function_pool[24143]: GetActiveSubroutineName (will be remapped) */
   "iiiipp\0"
   "glGetActiveSubroutineName\0"
   "\0"
   /* _mesa_function_pool[24177]: UniformSubroutinesuiv (will be remapped) */
   "iip\0"
   "glUniformSubroutinesuiv\0"
   "\0"
   /* _mesa_function_pool[24206]: GetUniformSubroutineuiv (will be remapped) */
   "iip\0"
   "glGetUniformSubroutineuiv\0"
   "\0"
   /* _mesa_function_pool[24237]: GetProgramStageiv (will be remapped) */
   "iiip\0"
   "glGetProgramStageiv\0"
   "\0"
   /* _mesa_function_pool[24263]: PatchParameteri (will be remapped) */
   "ii\0"
   "glPatchParameteri\0"
   "glPatchParameteriEXT\0"
   "glPatchParameteriOES\0"
   "\0"
   /* _mesa_function_pool[24327]: PatchParameterfv (will be remapped) */
   "ip\0"
   "glPatchParameterfv\0"
   "\0"
   /* _mesa_function_pool[24350]: DrawArraysIndirect (will be remapped) */
   "ip\0"
   "glDrawArraysIndirect\0"
   "\0"
   /* _mesa_function_pool[24375]: DrawElementsIndirect (will be remapped) */
   "iip\0"
   "glDrawElementsIndirect\0"
   "\0"
   /* _mesa_function_pool[24403]: MultiDrawArraysIndirect (will be remapped) */
   "ipii\0"
   "glMultiDrawArraysIndirect\0"
   "\0"
   /* _mesa_function_pool[24435]: MultiDrawElementsIndirect (will be remapped) */
   "iipii\0"
   "glMultiDrawElementsIndirect\0"
   "\0"
   /* _mesa_function_pool[24470]: Uniform1d (will be remapped) */
   "id\0"
   "glUniform1d\0"
   "\0"
   /* _mesa_function_pool[24486]: Uniform2d (will be remapped) */
   "idd\0"
   "glUniform2d\0"
   "\0"
   /* _mesa_function_pool[24503]: Uniform3d (will be remapped) */
   "iddd\0"
   "glUniform3d\0"
   "\0"
   /* _mesa_function_pool[24521]: Uniform4d (will be remapped) */
   "idddd\0"
   "glUniform4d\0"
   "\0"
   /* _mesa_function_pool[24540]: Uniform1dv (will be remapped) */
   "iip\0"
   "glUniform1dv\0"
   "\0"
   /* _mesa_function_pool[24558]: Uniform2dv (will be remapped) */
   "iip\0"
   "glUniform2dv\0"
   "\0"
   /* _mesa_function_pool[24576]: Uniform3dv (will be remapped) */
   "iip\0"
   "glUniform3dv\0"
   "\0"
   /* _mesa_function_pool[24594]: Uniform4dv (will be remapped) */
   "iip\0"
   "glUniform4dv\0"
   "\0"
   /* _mesa_function_pool[24612]: UniformMatrix2dv (will be remapped) */
   "iiip\0"
   "glUniformMatrix2dv\0"
   "\0"
   /* _mesa_function_pool[24637]: UniformMatrix3dv (will be remapped) */
   "iiip\0"
   "glUniformMatrix3dv\0"
   "\0"
   /* _mesa_function_pool[24662]: UniformMatrix4dv (will be remapped) */
   "iiip\0"
   "glUniformMatrix4dv\0"
   "\0"
   /* _mesa_function_pool[24687]: UniformMatrix2x3dv (will be remapped) */
   "iiip\0"
   "glUniformMatrix2x3dv\0"
   "\0"
   /* _mesa_function_pool[24714]: UniformMatrix2x4dv (will be remapped) */
   "iiip\0"
   "glUniformMatrix2x4dv\0"
   "\0"
   /* _mesa_function_pool[24741]: UniformMatrix3x2dv (will be remapped) */
   "iiip\0"
   "glUniformMatrix3x2dv\0"
   "\0"
   /* _mesa_function_pool[24768]: UniformMatrix3x4dv (will be remapped) */
   "iiip\0"
   "glUniformMatrix3x4dv\0"
   "\0"
   /* _mesa_function_pool[24795]: UniformMatrix4x2dv (will be remapped) */
   "iiip\0"
   "glUniformMatrix4x2dv\0"
   "\0"
   /* _mesa_function_pool[24822]: UniformMatrix4x3dv (will be remapped) */
   "iiip\0"
   "glUniformMatrix4x3dv\0"
   "\0"
   /* _mesa_function_pool[24849]: GetUniformdv (will be remapped) */
   "iip\0"
   "glGetUniformdv\0"
   "\0"
   /* _mesa_function_pool[24869]: DrawTransformFeedbackStream (will be remapped) */
   "iii\0"
   "glDrawTransformFeedbackStream\0"
   "\0"
   /* _mesa_function_pool[24904]: BeginQueryIndexed (will be remapped) */
   "iii\0"
   "glBeginQueryIndexed\0"
   "\0"
   /* _mesa_function_pool[24929]: EndQueryIndexed (will be remapped) */
   "ii\0"
   "glEndQueryIndexed\0"
   "\0"
   /* _mesa_function_pool[24951]: GetQueryIndexediv (will be remapped) */
   "iiip\0"
   "glGetQueryIndexediv\0"
   "\0"
   /* _mesa_function_pool[24977]: UseProgramStages (will be remapped) */
   "iii\0"
   "glUseProgramStages\0"
   "glUseProgramStagesEXT\0"
   "\0"
   /* _mesa_function_pool[25023]: ActiveShaderProgram (will be remapped) */
   "ii\0"
   "glActiveShaderProgram\0"
   "glActiveShaderProgramEXT\0"
   "\0"
   /* _mesa_function_pool[25074]: CreateShaderProgramv (will be remapped) */
   "iip\0"
   "glCreateShaderProgramv\0"
   "glCreateShaderProgramvEXT\0"
   "\0"
   /* _mesa_function_pool[25128]: BindProgramPipeline (will be remapped) */
   "i\0"
   "glBindProgramPipeline\0"
   "glBindProgramPipelineEXT\0"
   "\0"
   /* _mesa_function_pool[25178]: DeleteProgramPipelines (will be remapped) */
   "ip\0"
   "glDeleteProgramPipelines\0"
   "glDeleteProgramPipelinesEXT\0"
   "\0"
   /* _mesa_function_pool[25235]: GenProgramPipelines (will be remapped) */
   "ip\0"
   "glGenProgramPipelines\0"
   "glGenProgramPipelinesEXT\0"
   "\0"
   /* _mesa_function_pool[25286]: IsProgramPipeline (will be remapped) */
   "i\0"
   "glIsProgramPipeline\0"
   "glIsProgramPipelineEXT\0"
   "\0"
   /* _mesa_function_pool[25332]: GetProgramPipelineiv (will be remapped) */
   "iip\0"
   "glGetProgramPipelineiv\0"
   "glGetProgramPipelineivEXT\0"
   "\0"
   /* _mesa_function_pool[25386]: ProgramUniform1i (will be remapped) */
   "iii\0"
   "glProgramUniform1i\0"
   "glProgramUniform1iEXT\0"
   "\0"
   /* _mesa_function_pool[25432]: ProgramUniform2i (will be remapped) */
   "iiii\0"
   "glProgramUniform2i\0"
   "glProgramUniform2iEXT\0"
   "\0"
   /* _mesa_function_pool[25479]: ProgramUniform3i (will be remapped) */
   "iiiii\0"
   "glProgramUniform3i\0"
   "glProgramUniform3iEXT\0"
   "\0"
   /* _mesa_function_pool[25527]: ProgramUniform4i (will be remapped) */
   "iiiiii\0"
   "glProgramUniform4i\0"
   "glProgramUniform4iEXT\0"
   "\0"
   /* _mesa_function_pool[25576]: ProgramUniform1ui (will be remapped) */
   "iii\0"
   "glProgramUniform1ui\0"
   "glProgramUniform1uiEXT\0"
   "\0"
   /* _mesa_function_pool[25624]: ProgramUniform2ui (will be remapped) */
   "iiii\0"
   "glProgramUniform2ui\0"
   "glProgramUniform2uiEXT\0"
   "\0"
   /* _mesa_function_pool[25673]: ProgramUniform3ui (will be remapped) */
   "iiiii\0"
   "glProgramUniform3ui\0"
   "glProgramUniform3uiEXT\0"
   "\0"
   /* _mesa_function_pool[25723]: ProgramUniform4ui (will be remapped) */
   "iiiiii\0"
   "glProgramUniform4ui\0"
   "glProgramUniform4uiEXT\0"
   "\0"
   /* _mesa_function_pool[25774]: ProgramUniform1f (will be remapped) */
   "iif\0"
   "glProgramUniform1f\0"
   "glProgramUniform1fEXT\0"
   "\0"
   /* _mesa_function_pool[25820]: ProgramUniform2f (will be remapped) */
   "iiff\0"
   "glProgramUniform2f\0"
   "glProgramUniform2fEXT\0"
   "\0"
   /* _mesa_function_pool[25867]: ProgramUniform3f (will be remapped) */
   "iifff\0"
   "glProgramUniform3f\0"
   "glProgramUniform3fEXT\0"
   "\0"
   /* _mesa_function_pool[25915]: ProgramUniform4f (will be remapped) */
   "iiffff\0"
   "glProgramUniform4f\0"
   "glProgramUniform4fEXT\0"
   "\0"
   /* _mesa_function_pool[25964]: ProgramUniform1iv (will be remapped) */
   "iiip\0"
   "glProgramUniform1iv\0"
   "glProgramUniform1ivEXT\0"
   "\0"
   /* _mesa_function_pool[26013]: ProgramUniform2iv (will be remapped) */
   "iiip\0"
   "glProgramUniform2iv\0"
   "glProgramUniform2ivEXT\0"
   "\0"
   /* _mesa_function_pool[26062]: ProgramUniform3iv (will be remapped) */
   "iiip\0"
   "glProgramUniform3iv\0"
   "glProgramUniform3ivEXT\0"
   "\0"
   /* _mesa_function_pool[26111]: ProgramUniform4iv (will be remapped) */
   "iiip\0"
   "glProgramUniform4iv\0"
   "glProgramUniform4ivEXT\0"
   "\0"
   /* _mesa_function_pool[26160]: ProgramUniform1uiv (will be remapped) */
   "iiip\0"
   "glProgramUniform1uiv\0"
   "glProgramUniform1uivEXT\0"
   "\0"
   /* _mesa_function_pool[26211]: ProgramUniform2uiv (will be remapped) */
   "iiip\0"
   "glProgramUniform2uiv\0"
   "glProgramUniform2uivEXT\0"
   "\0"
   /* _mesa_function_pool[26262]: ProgramUniform3uiv (will be remapped) */
   "iiip\0"
   "glProgramUniform3uiv\0"
   "glProgramUniform3uivEXT\0"
   "\0"
   /* _mesa_function_pool[26313]: ProgramUniform4uiv (will be remapped) */
   "iiip\0"
   "glProgramUniform4uiv\0"
   "glProgramUniform4uivEXT\0"
   "\0"
   /* _mesa_function_pool[26364]: ProgramUniform1fv (will be remapped) */
   "iiip\0"
   "glProgramUniform1fv\0"
   "glProgramUniform1fvEXT\0"
   "\0"
   /* _mesa_function_pool[26413]: ProgramUniform2fv (will be remapped) */
   "iiip\0"
   "glProgramUniform2fv\0"
   "glProgramUniform2fvEXT\0"
   "\0"
   /* _mesa_function_pool[26462]: ProgramUniform3fv (will be remapped) */
   "iiip\0"
   "glProgramUniform3fv\0"
   "glProgramUniform3fvEXT\0"
   "\0"
   /* _mesa_function_pool[26511]: ProgramUniform4fv (will be remapped) */
   "iiip\0"
   "glProgramUniform4fv\0"
   "glProgramUniform4fvEXT\0"
   "\0"
   /* _mesa_function_pool[26560]: ProgramUniformMatrix2fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix2fv\0"
   "glProgramUniformMatrix2fvEXT\0"
   "\0"
   /* _mesa_function_pool[26622]: ProgramUniformMatrix3fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix3fv\0"
   "glProgramUniformMatrix3fvEXT\0"
   "\0"
   /* _mesa_function_pool[26684]: ProgramUniformMatrix4fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix4fv\0"
   "glProgramUniformMatrix4fvEXT\0"
   "\0"
   /* _mesa_function_pool[26746]: ProgramUniformMatrix2x3fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix2x3fv\0"
   "glProgramUniformMatrix2x3fvEXT\0"
   "\0"
   /* _mesa_function_pool[26812]: ProgramUniformMatrix3x2fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix3x2fv\0"
   "glProgramUniformMatrix3x2fvEXT\0"
   "\0"
   /* _mesa_function_pool[26878]: ProgramUniformMatrix2x4fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix2x4fv\0"
   "glProgramUniformMatrix2x4fvEXT\0"
   "\0"
   /* _mesa_function_pool[26944]: ProgramUniformMatrix4x2fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix4x2fv\0"
   "glProgramUniformMatrix4x2fvEXT\0"
   "\0"
   /* _mesa_function_pool[27010]: ProgramUniformMatrix3x4fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix3x4fv\0"
   "glProgramUniformMatrix3x4fvEXT\0"
   "\0"
   /* _mesa_function_pool[27076]: ProgramUniformMatrix4x3fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix4x3fv\0"
   "glProgramUniformMatrix4x3fvEXT\0"
   "\0"
   /* _mesa_function_pool[27142]: ValidateProgramPipeline (will be remapped) */
   "i\0"
   "glValidateProgramPipeline\0"
   "glValidateProgramPipelineEXT\0"
   "\0"
   /* _mesa_function_pool[27200]: GetProgramPipelineInfoLog (will be remapped) */
   "iipp\0"
   "glGetProgramPipelineInfoLog\0"
   "glGetProgramPipelineInfoLogEXT\0"
   "\0"
   /* _mesa_function_pool[27265]: ProgramUniform1d (will be remapped) */
   "iid\0"
   "glProgramUniform1d\0"
   "\0"
   /* _mesa_function_pool[27289]: ProgramUniform2d (will be remapped) */
   "iidd\0"
   "glProgramUniform2d\0"
   "\0"
   /* _mesa_function_pool[27314]: ProgramUniform3d (will be remapped) */
   "iiddd\0"
   "glProgramUniform3d\0"
   "\0"
   /* _mesa_function_pool[27340]: ProgramUniform4d (will be remapped) */
   "iidddd\0"
   "glProgramUniform4d\0"
   "\0"
   /* _mesa_function_pool[27367]: ProgramUniformMatrix2x3dv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix2x3dv\0"
   "\0"
   /* _mesa_function_pool[27402]: ProgramUniformMatrix3x2dv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix3x2dv\0"
   "\0"
   /* _mesa_function_pool[27437]: ProgramUniformMatrix2x4dv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix2x4dv\0"
   "\0"
   /* _mesa_function_pool[27472]: ProgramUniformMatrix4x2dv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix4x2dv\0"
   "\0"
   /* _mesa_function_pool[27507]: ProgramUniformMatrix3x4dv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix3x4dv\0"
   "\0"
   /* _mesa_function_pool[27542]: ProgramUniformMatrix4x3dv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix4x3dv\0"
   "\0"
   /* _mesa_function_pool[27577]: ProgramUniformMatrix2dv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix2dv\0"
   "\0"
   /* _mesa_function_pool[27610]: ProgramUniformMatrix3dv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix3dv\0"
   "\0"
   /* _mesa_function_pool[27643]: ProgramUniformMatrix4dv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix4dv\0"
   "\0"
   /* _mesa_function_pool[27676]: ProgramUniform1dv (will be remapped) */
   "iiip\0"
   "glProgramUniform1dv\0"
   "\0"
   /* _mesa_function_pool[27702]: ProgramUniform2dv (will be remapped) */
   "iiip\0"
   "glProgramUniform2dv\0"
   "\0"
   /* _mesa_function_pool[27728]: ProgramUniform3dv (will be remapped) */
   "iiip\0"
   "glProgramUniform3dv\0"
   "\0"
   /* _mesa_function_pool[27754]: ProgramUniform4dv (will be remapped) */
   "iiip\0"
   "glProgramUniform4dv\0"
   "\0"
   /* _mesa_function_pool[27780]: VertexAttribL1d (will be remapped) */
   "id\0"
   "glVertexAttribL1d\0"
   "\0"
   /* _mesa_function_pool[27802]: VertexAttribL2d (will be remapped) */
   "idd\0"
   "glVertexAttribL2d\0"
   "\0"
   /* _mesa_function_pool[27825]: VertexAttribL3d (will be remapped) */
   "iddd\0"
   "glVertexAttribL3d\0"
   "\0"
   /* _mesa_function_pool[27849]: VertexAttribL4d (will be remapped) */
   "idddd\0"
   "glVertexAttribL4d\0"
   "\0"
   /* _mesa_function_pool[27874]: VertexAttribL1dv (will be remapped) */
   "ip\0"
   "glVertexAttribL1dv\0"
   "\0"
   /* _mesa_function_pool[27897]: VertexAttribL2dv (will be remapped) */
   "ip\0"
   "glVertexAttribL2dv\0"
   "\0"
   /* _mesa_function_pool[27920]: VertexAttribL3dv (will be remapped) */
   "ip\0"
   "glVertexAttribL3dv\0"
   "\0"
   /* _mesa_function_pool[27943]: VertexAttribL4dv (will be remapped) */
   "ip\0"
   "glVertexAttribL4dv\0"
   "\0"
   /* _mesa_function_pool[27966]: VertexAttribLPointer (will be remapped) */
   "iiiip\0"
   "glVertexAttribLPointer\0"
   "\0"
   /* _mesa_function_pool[27996]: GetVertexAttribLdv (will be remapped) */
   "iip\0"
   "glGetVertexAttribLdv\0"
   "\0"
   /* _mesa_function_pool[28022]: GetShaderPrecisionFormat (will be remapped) */
   "iipp\0"
   "glGetShaderPrecisionFormat\0"
   "\0"
   /* _mesa_function_pool[28055]: ReleaseShaderCompiler (will be remapped) */
   "\0"
   "glReleaseShaderCompiler\0"
   "\0"
   /* _mesa_function_pool[28081]: ShaderBinary (will be remapped) */
   "ipipi\0"
   "glShaderBinary\0"
   "\0"
   /* _mesa_function_pool[28103]: ClearDepthf (will be remapped) */
   "f\0"
   "glClearDepthf\0"
   "glClearDepthfOES\0"
   "\0"
   /* _mesa_function_pool[28137]: DepthRangef (will be remapped) */
   "ff\0"
   "glDepthRangef\0"
   "glDepthRangefOES\0"
   "\0"
   /* _mesa_function_pool[28172]: GetProgramBinary (will be remapped) */
   "iippp\0"
   "glGetProgramBinary\0"
   "glGetProgramBinaryOES\0"
   "\0"
   /* _mesa_function_pool[28220]: ProgramBinary (will be remapped) */
   "iipi\0"
   "glProgramBinary\0"
   "glProgramBinaryOES\0"
   "\0"
   /* _mesa_function_pool[28261]: ProgramParameteri (will be remapped) */
   "iii\0"
   "glProgramParameteri\0"
   "glProgramParameteriEXT\0"
   "\0"
   /* _mesa_function_pool[28309]: DebugMessageControl (will be remapped) */
   "iiiipi\0"
   "glDebugMessageControlARB\0"
   "glDebugMessageControl\0"
   "glDebugMessageControlKHR\0"
   "\0"
   /* _mesa_function_pool[28389]: DebugMessageInsert (will be remapped) */
   "iiiiip\0"
   "glDebugMessageInsertARB\0"
   "glDebugMessageInsert\0"
   "glDebugMessageInsertKHR\0"
   "\0"
   /* _mesa_function_pool[28466]: DebugMessageCallback (will be remapped) */
   "pp\0"
   "glDebugMessageCallbackARB\0"
   "glDebugMessageCallback\0"
   "glDebugMessageCallbackKHR\0"
   "\0"
   /* _mesa_function_pool[28545]: GetDebugMessageLog (will be remapped) */
   "iipppppp\0"
   "glGetDebugMessageLogARB\0"
   "glGetDebugMessageLog\0"
   "glGetDebugMessageLogKHR\0"
   "\0"
   /* _mesa_function_pool[28624]: GetGraphicsResetStatusARB (will be remapped) */
   "\0"
   "glGetGraphicsResetStatusARB\0"
   "glGetGraphicsResetStatus\0"
   "glGetGraphicsResetStatusKHR\0"
   "\0"
   /* _mesa_function_pool[28707]: GetnMapdvARB (will be remapped) */
   "iiip\0"
   "glGetnMapdvARB\0"
   "\0"
   /* _mesa_function_pool[28728]: GetnMapfvARB (will be remapped) */
   "iiip\0"
   "glGetnMapfvARB\0"
   "\0"
   /* _mesa_function_pool[28749]: GetnMapivARB (will be remapped) */
   "iiip\0"
   "glGetnMapivARB\0"
   "\0"
   /* _mesa_function_pool[28770]: GetnPixelMapfvARB (will be remapped) */
   "iip\0"
   "glGetnPixelMapfvARB\0"
   "\0"
   /* _mesa_function_pool[28795]: GetnPixelMapuivARB (will be remapped) */
   "iip\0"
   "glGetnPixelMapuivARB\0"
   "\0"
   /* _mesa_function_pool[28821]: GetnPixelMapusvARB (will be remapped) */
   "iip\0"
   "glGetnPixelMapusvARB\0"
   "\0"
   /* _mesa_function_pool[28847]: GetnPolygonStippleARB (will be remapped) */
   "ip\0"
   "glGetnPolygonStippleARB\0"
   "\0"
   /* _mesa_function_pool[28875]: GetnTexImageARB (will be remapped) */
   "iiiiip\0"
   "glGetnTexImageARB\0"
   "\0"
   /* _mesa_function_pool[28901]: ReadnPixelsARB (will be remapped) */
   "iiiiiiip\0"
   "glReadnPixelsARB\0"
   "glReadnPixels\0"
   "glReadnPixelsKHR\0"
   "\0"
   /* _mesa_function_pool[28959]: GetnColorTableARB (will be remapped) */
   "iiiip\0"
   "glGetnColorTableARB\0"
   "\0"
   /* _mesa_function_pool[28986]: GetnConvolutionFilterARB (will be remapped) */
   "iiiip\0"
   "glGetnConvolutionFilterARB\0"
   "\0"
   /* _mesa_function_pool[29020]: GetnSeparableFilterARB (will be remapped) */
   "iiiipipp\0"
   "glGetnSeparableFilterARB\0"
   "\0"
   /* _mesa_function_pool[29055]: GetnHistogramARB (will be remapped) */
   "iiiiip\0"
   "glGetnHistogramARB\0"
   "\0"
   /* _mesa_function_pool[29082]: GetnMinmaxARB (will be remapped) */
   "iiiiip\0"
   "glGetnMinmaxARB\0"
   "\0"
   /* _mesa_function_pool[29106]: GetnCompressedTexImageARB (will be remapped) */
   "iiip\0"
   "glGetnCompressedTexImageARB\0"
   "\0"
   /* _mesa_function_pool[29140]: GetnUniformfvARB (will be remapped) */
   "iiip\0"
   "glGetnUniformfvARB\0"
   "glGetnUniformfv\0"
   "glGetnUniformfvKHR\0"
   "\0"
   /* _mesa_function_pool[29200]: GetnUniformivARB (will be remapped) */
   "iiip\0"
   "glGetnUniformivARB\0"
   "glGetnUniformiv\0"
   "glGetnUniformivKHR\0"
   "\0"
   /* _mesa_function_pool[29260]: GetnUniformuivARB (will be remapped) */
   "iiip\0"
   "glGetnUniformuivARB\0"
   "glGetnUniformuiv\0"
   "glGetnUniformuivKHR\0"
   "\0"
   /* _mesa_function_pool[29323]: GetnUniformdvARB (will be remapped) */
   "iiip\0"
   "glGetnUniformdvARB\0"
   "\0"
   /* _mesa_function_pool[29348]: DrawArraysInstancedBaseInstance (will be remapped) */
   "iiiii\0"
   "glDrawArraysInstancedBaseInstance\0"
   "glDrawArraysInstancedBaseInstanceEXT\0"
   "\0"
   /* _mesa_function_pool[29426]: DrawElementsInstancedBaseInstance (will be remapped) */
   "iiipii\0"
   "glDrawElementsInstancedBaseInstance\0"
   "glDrawElementsInstancedBaseInstanceEXT\0"
   "\0"
   /* _mesa_function_pool[29509]: DrawElementsInstancedBaseVertexBaseInstance (will be remapped) */
   "iiipiii\0"
   "glDrawElementsInstancedBaseVertexBaseInstance\0"
   "glDrawElementsInstancedBaseVertexBaseInstanceEXT\0"
   "\0"
   /* _mesa_function_pool[29613]: DrawTransformFeedbackInstanced (will be remapped) */
   "iii\0"
   "glDrawTransformFeedbackInstanced\0"
   "\0"
   /* _mesa_function_pool[29651]: DrawTransformFeedbackStreamInstanced (will be remapped) */
   "iiii\0"
   "glDrawTransformFeedbackStreamInstanced\0"
   "\0"
   /* _mesa_function_pool[29696]: GetInternalformativ (will be remapped) */
   "iiiip\0"
   "glGetInternalformativ\0"
   "\0"
   /* _mesa_function_pool[29725]: GetActiveAtomicCounterBufferiv (will be remapped) */
   "iiip\0"
   "glGetActiveAtomicCounterBufferiv\0"
   "\0"
   /* _mesa_function_pool[29764]: BindImageTexture (will be remapped) */
   "iiiiiii\0"
   "glBindImageTexture\0"
   "\0"
   /* _mesa_function_pool[29792]: MemoryBarrier (will be remapped) */
   "i\0"
   "glMemoryBarrier\0"
   "\0"
   /* _mesa_function_pool[29811]: TexStorage1D (will be remapped) */
   "iiii\0"
   "glTexStorage1D\0"
   "\0"
   /* _mesa_function_pool[29832]: TexStorage2D (will be remapped) */
   "iiiii\0"
   "glTexStorage2D\0"
   "\0"
   /* _mesa_function_pool[29854]: TexStorage3D (will be remapped) */
   "iiiiii\0"
   "glTexStorage3D\0"
   "\0"
   /* _mesa_function_pool[29877]: TextureStorage1DEXT (will be remapped) */
   "iiiii\0"
   "glTextureStorage1DEXT\0"
   "\0"
   /* _mesa_function_pool[29906]: TextureStorage2DEXT (will be remapped) */
   "iiiiii\0"
   "glTextureStorage2DEXT\0"
   "\0"
   /* _mesa_function_pool[29936]: TextureStorage3DEXT (will be remapped) */
   "iiiiiii\0"
   "glTextureStorage3DEXT\0"
   "\0"
   /* _mesa_function_pool[29967]: PushDebugGroup (will be remapped) */
   "iiip\0"
   "glPushDebugGroup\0"
   "glPushDebugGroupKHR\0"
   "\0"
   /* _mesa_function_pool[30010]: PopDebugGroup (will be remapped) */
   "\0"
   "glPopDebugGroup\0"
   "glPopDebugGroupKHR\0"
   "\0"
   /* _mesa_function_pool[30047]: ObjectLabel (will be remapped) */
   "iiip\0"
   "glObjectLabel\0"
   "glObjectLabelKHR\0"
   "\0"
   /* _mesa_function_pool[30084]: GetObjectLabel (will be remapped) */
   "iiipp\0"
   "glGetObjectLabel\0"
   "glGetObjectLabelKHR\0"
   "\0"
   /* _mesa_function_pool[30128]: ObjectPtrLabel (will be remapped) */
   "pip\0"
   "glObjectPtrLabel\0"
   "glObjectPtrLabelKHR\0"
   "\0"
   /* _mesa_function_pool[30170]: GetObjectPtrLabel (will be remapped) */
   "pipp\0"
   "glGetObjectPtrLabel\0"
   "glGetObjectPtrLabelKHR\0"
   "\0"
   /* _mesa_function_pool[30219]: ClearBufferData (will be remapped) */
   "iiiip\0"
   "glClearBufferData\0"
   "\0"
   /* _mesa_function_pool[30244]: ClearBufferSubData (will be remapped) */
   "iiiiiip\0"
   "glClearBufferSubData\0"
   "\0"
   /* _mesa_function_pool[30274]: DispatchCompute (will be remapped) */
   "iii\0"
   "glDispatchCompute\0"
   "\0"
   /* _mesa_function_pool[30297]: DispatchComputeIndirect (will be remapped) */
   "i\0"
   "glDispatchComputeIndirect\0"
   "\0"
   /* _mesa_function_pool[30326]: CopyImageSubData (will be remapped) */
   "iiiiiiiiiiiiiii\0"
   "glCopyImageSubData\0"
   "glCopyImageSubDataEXT\0"
   "glCopyImageSubDataOES\0"
   "\0"
   /* _mesa_function_pool[30406]: TextureView (will be remapped) */
   "iiiiiiii\0"
   "glTextureView\0"
   "glTextureViewOES\0"
   "\0"
   /* _mesa_function_pool[30447]: BindVertexBuffer (will be remapped) */
   "iiii\0"
   "glBindVertexBuffer\0"
   "\0"
   /* _mesa_function_pool[30472]: VertexAttribFormat (will be remapped) */
   "iiiii\0"
   "glVertexAttribFormat\0"
   "\0"
   /* _mesa_function_pool[30500]: VertexAttribIFormat (will be remapped) */
   "iiii\0"
   "glVertexAttribIFormat\0"
   "\0"
   /* _mesa_function_pool[30528]: VertexAttribLFormat (will be remapped) */
   "iiii\0"
   "glVertexAttribLFormat\0"
   "\0"
   /* _mesa_function_pool[30556]: VertexAttribBinding (will be remapped) */
   "ii\0"
   "glVertexAttribBinding\0"
   "\0"
   /* _mesa_function_pool[30582]: VertexBindingDivisor (will be remapped) */
   "ii\0"
   "glVertexBindingDivisor\0"
   "\0"
   /* _mesa_function_pool[30609]: FramebufferParameteri (will be remapped) */
   "iii\0"
   "glFramebufferParameteri\0"
   "\0"
   /* _mesa_function_pool[30638]: GetFramebufferParameteriv (will be remapped) */
   "iip\0"
   "glGetFramebufferParameteriv\0"
   "\0"
   /* _mesa_function_pool[30671]: GetInternalformati64v (will be remapped) */
   "iiiip\0"
   "glGetInternalformati64v\0"
   "\0"
   /* _mesa_function_pool[30702]: InvalidateTexSubImage (will be remapped) */
   "iiiiiiii\0"
   "glInvalidateTexSubImage\0"
   "\0"
   /* _mesa_function_pool[30736]: InvalidateTexImage (will be remapped) */
   "ii\0"
   "glInvalidateTexImage\0"
   "\0"
   /* _mesa_function_pool[30761]: InvalidateBufferSubData (will be remapped) */
   "iii\0"
   "glInvalidateBufferSubData\0"
   "\0"
   /* _mesa_function_pool[30792]: InvalidateBufferData (will be remapped) */
   "i\0"
   "glInvalidateBufferData\0"
   "\0"
   /* _mesa_function_pool[30818]: InvalidateSubFramebuffer (will be remapped) */
   "iipiiii\0"
   "glInvalidateSubFramebuffer\0"
   "\0"
   /* _mesa_function_pool[30854]: InvalidateFramebuffer (will be remapped) */
   "iip\0"
   "glInvalidateFramebuffer\0"
   "\0"
   /* _mesa_function_pool[30883]: GetProgramInterfaceiv (will be remapped) */
   "iiip\0"
   "glGetProgramInterfaceiv\0"
   "\0"
   /* _mesa_function_pool[30913]: GetProgramResourceIndex (will be remapped) */
   "iip\0"
   "glGetProgramResourceIndex\0"
   "\0"
   /* _mesa_function_pool[30944]: GetProgramResourceName (will be remapped) */
   "iiiipp\0"
   "glGetProgramResourceName\0"
   "\0"
   /* _mesa_function_pool[30977]: GetProgramResourceiv (will be remapped) */
   "iiiipipp\0"
   "glGetProgramResourceiv\0"
   "\0"
   /* _mesa_function_pool[31010]: GetProgramResourceLocation (will be remapped) */
   "iip\0"
   "glGetProgramResourceLocation\0"
   "\0"
   /* _mesa_function_pool[31044]: GetProgramResourceLocationIndex (will be remapped) */
   "iip\0"
   "glGetProgramResourceLocationIndex\0"
   "glGetProgramResourceLocationIndexEXT\0"
   "\0"
   /* _mesa_function_pool[31120]: ShaderStorageBlockBinding (will be remapped) */
   "iii\0"
   "glShaderStorageBlockBinding\0"
   "\0"
   /* _mesa_function_pool[31153]: TexBufferRange (will be remapped) */
   "iiiii\0"
   "glTexBufferRange\0"
   "glTexBufferRangeEXT\0"
   "glTexBufferRangeOES\0"
   "\0"
   /* _mesa_function_pool[31217]: TexStorage2DMultisample (will be remapped) */
   "iiiiii\0"
   "glTexStorage2DMultisample\0"
   "\0"
   /* _mesa_function_pool[31251]: TexStorage3DMultisample (will be remapped) */
   "iiiiiii\0"
   "glTexStorage3DMultisample\0"
   "glTexStorage3DMultisampleOES\0"
   "\0"
   /* _mesa_function_pool[31315]: BufferStorage (will be remapped) */
   "iipi\0"
   "glBufferStorage\0"
   "glBufferStorageEXT\0"
   "\0"
   /* _mesa_function_pool[31356]: ClearTexImage (will be remapped) */
   "iiiip\0"
   "glClearTexImage\0"
   "\0"
   /* _mesa_function_pool[31379]: ClearTexSubImage (will be remapped) */
   "iiiiiiiiiip\0"
   "glClearTexSubImage\0"
   "\0"
   /* _mesa_function_pool[31411]: BindBuffersBase (will be remapped) */
   "iiip\0"
   "glBindBuffersBase\0"
   "\0"
   /* _mesa_function_pool[31435]: BindBuffersRange (will be remapped) */
   "iiippp\0"
   "glBindBuffersRange\0"
   "\0"
   /* _mesa_function_pool[31462]: BindTextures (will be remapped) */
   "iip\0"
   "glBindTextures\0"
   "\0"
   /* _mesa_function_pool[31482]: BindSamplers (will be remapped) */
   "iip\0"
   "glBindSamplers\0"
   "\0"
   /* _mesa_function_pool[31502]: BindImageTextures (will be remapped) */
   "iip\0"
   "glBindImageTextures\0"
   "\0"
   /* _mesa_function_pool[31527]: BindVertexBuffers (will be remapped) */
   "iippp\0"
   "glBindVertexBuffers\0"
   "\0"
   /* _mesa_function_pool[31554]: GetTextureHandleARB (will be remapped) */
   "i\0"
   "glGetTextureHandleARB\0"
   "\0"
   /* _mesa_function_pool[31579]: GetTextureSamplerHandleARB (will be remapped) */
   "ii\0"
   "glGetTextureSamplerHandleARB\0"
   "\0"
   /* _mesa_function_pool[31612]: MakeTextureHandleResidentARB (will be remapped) */
   "i\0"
   "glMakeTextureHandleResidentARB\0"
   "\0"
   /* _mesa_function_pool[31646]: MakeTextureHandleNonResidentARB (will be remapped) */
   "i\0"
   "glMakeTextureHandleNonResidentARB\0"
   "\0"
   /* _mesa_function_pool[31683]: GetImageHandleARB (will be remapped) */
   "iiiii\0"
   "glGetImageHandleARB\0"
   "\0"
   /* _mesa_function_pool[31710]: MakeImageHandleResidentARB (will be remapped) */
   "ii\0"
   "glMakeImageHandleResidentARB\0"
   "\0"
   /* _mesa_function_pool[31743]: MakeImageHandleNonResidentARB (will be remapped) */
   "i\0"
   "glMakeImageHandleNonResidentARB\0"
   "\0"
   /* _mesa_function_pool[31778]: UniformHandleui64ARB (will be remapped) */
   "ii\0"
   "glUniformHandleui64ARB\0"
   "\0"
   /* _mesa_function_pool[31805]: UniformHandleui64vARB (will be remapped) */
   "iip\0"
   "glUniformHandleui64vARB\0"
   "\0"
   /* _mesa_function_pool[31834]: ProgramUniformHandleui64ARB (will be remapped) */
   "iii\0"
   "glProgramUniformHandleui64ARB\0"
   "\0"
   /* _mesa_function_pool[31869]: ProgramUniformHandleui64vARB (will be remapped) */
   "iiip\0"
   "glProgramUniformHandleui64vARB\0"
   "\0"
   /* _mesa_function_pool[31906]: IsTextureHandleResidentARB (will be remapped) */
   "i\0"
   "glIsTextureHandleResidentARB\0"
   "\0"
   /* _mesa_function_pool[31938]: IsImageHandleResidentARB (will be remapped) */
   "i\0"
   "glIsImageHandleResidentARB\0"
   "\0"
   /* _mesa_function_pool[31968]: VertexAttribL1ui64ARB (will be remapped) */
   "ii\0"
   "glVertexAttribL1ui64ARB\0"
   "\0"
   /* _mesa_function_pool[31996]: VertexAttribL1ui64vARB (will be remapped) */
   "ip\0"
   "glVertexAttribL1ui64vARB\0"
   "\0"
   /* _mesa_function_pool[32025]: GetVertexAttribLui64vARB (will be remapped) */
   "iip\0"
   "glGetVertexAttribLui64vARB\0"
   "\0"
   /* _mesa_function_pool[32057]: DispatchComputeGroupSizeARB (will be remapped) */
   "iiiiii\0"
   "glDispatchComputeGroupSizeARB\0"
   "\0"
   /* _mesa_function_pool[32095]: MultiDrawArraysIndirectCountARB (will be remapped) */
   "iiiii\0"
   "glMultiDrawArraysIndirectCountARB\0"
   "glMultiDrawArraysIndirectCount\0"
   "\0"
   /* _mesa_function_pool[32167]: MultiDrawElementsIndirectCountARB (will be remapped) */
   "iiiiii\0"
   "glMultiDrawElementsIndirectCountARB\0"
   "glMultiDrawElementsIndirectCount\0"
   "\0"
   /* _mesa_function_pool[32244]: ClipControl (will be remapped) */
   "ii\0"
   "glClipControl\0"
   "\0"
   /* _mesa_function_pool[32262]: CreateTransformFeedbacks (will be remapped) */
   "ip\0"
   "glCreateTransformFeedbacks\0"
   "\0"
   /* _mesa_function_pool[32293]: TransformFeedbackBufferBase (will be remapped) */
   "iii\0"
   "glTransformFeedbackBufferBase\0"
   "\0"
   /* _mesa_function_pool[32328]: TransformFeedbackBufferRange (will be remapped) */
   "iiiii\0"
   "glTransformFeedbackBufferRange\0"
   "\0"
   /* _mesa_function_pool[32366]: GetTransformFeedbackiv (will be remapped) */
   "iip\0"
   "glGetTransformFeedbackiv\0"
   "\0"
   /* _mesa_function_pool[32396]: GetTransformFeedbacki_v (will be remapped) */
   "iiip\0"
   "glGetTransformFeedbacki_v\0"
   "\0"
   /* _mesa_function_pool[32428]: GetTransformFeedbacki64_v (will be remapped) */
   "iiip\0"
   "glGetTransformFeedbacki64_v\0"
   "\0"
   /* _mesa_function_pool[32462]: CreateBuffers (will be remapped) */
   "ip\0"
   "glCreateBuffers\0"
   "\0"
   /* _mesa_function_pool[32482]: NamedBufferStorage (will be remapped) */
   "iipi\0"
   "glNamedBufferStorage\0"
   "\0"
   /* _mesa_function_pool[32509]: NamedBufferData (will be remapped) */
   "iipi\0"
   "glNamedBufferData\0"
   "\0"
   /* _mesa_function_pool[32533]: NamedBufferSubData (will be remapped) */
   "iiip\0"
   "glNamedBufferSubData\0"
   "\0"
   /* _mesa_function_pool[32560]: CopyNamedBufferSubData (will be remapped) */
   "iiiii\0"
   "glCopyNamedBufferSubData\0"
   "\0"
   /* _mesa_function_pool[32592]: ClearNamedBufferData (will be remapped) */
   "iiiip\0"
   "glClearNamedBufferData\0"
   "\0"
   /* _mesa_function_pool[32622]: ClearNamedBufferSubData (will be remapped) */
   "iiiiiip\0"
   "glClearNamedBufferSubData\0"
   "\0"
   /* _mesa_function_pool[32657]: MapNamedBuffer (will be remapped) */
   "ii\0"
   "glMapNamedBuffer\0"
   "\0"
   /* _mesa_function_pool[32678]: MapNamedBufferRange (will be remapped) */
   "iiii\0"
   "glMapNamedBufferRange\0"
   "\0"
   /* _mesa_function_pool[32706]: UnmapNamedBuffer (will be remapped) */
   "i\0"
   "glUnmapNamedBuffer\0"
   "\0"
   /* _mesa_function_pool[32728]: FlushMappedNamedBufferRange (will be remapped) */
   "iii\0"
   "glFlushMappedNamedBufferRange\0"
   "\0"
   /* _mesa_function_pool[32763]: GetNamedBufferParameteriv (will be remapped) */
   "iip\0"
   "glGetNamedBufferParameteriv\0"
   "\0"
   /* _mesa_function_pool[32796]: GetNamedBufferParameteri64v (will be remapped) */
   "iip\0"
   "glGetNamedBufferParameteri64v\0"
   "\0"
   /* _mesa_function_pool[32831]: GetNamedBufferPointerv (will be remapped) */
   "iip\0"
   "glGetNamedBufferPointerv\0"
   "\0"
   /* _mesa_function_pool[32861]: GetNamedBufferSubData (will be remapped) */
   "iiip\0"
   "glGetNamedBufferSubData\0"
   "\0"
   /* _mesa_function_pool[32891]: CreateFramebuffers (will be remapped) */
   "ip\0"
   "glCreateFramebuffers\0"
   "\0"
   /* _mesa_function_pool[32916]: NamedFramebufferRenderbuffer (will be remapped) */
   "iiii\0"
   "glNamedFramebufferRenderbuffer\0"
   "\0"
   /* _mesa_function_pool[32953]: NamedFramebufferParameteri (will be remapped) */
   "iii\0"
   "glNamedFramebufferParameteri\0"
   "\0"
   /* _mesa_function_pool[32987]: NamedFramebufferTexture (will be remapped) */
   "iiii\0"
   "glNamedFramebufferTexture\0"
   "\0"
   /* _mesa_function_pool[33019]: NamedFramebufferTextureLayer (will be remapped) */
   "iiiii\0"
   "glNamedFramebufferTextureLayer\0"
   "\0"
   /* _mesa_function_pool[33057]: NamedFramebufferDrawBuffer (will be remapped) */
   "ii\0"
   "glNamedFramebufferDrawBuffer\0"
   "\0"
   /* _mesa_function_pool[33090]: NamedFramebufferDrawBuffers (will be remapped) */
   "iip\0"
   "glNamedFramebufferDrawBuffers\0"
   "\0"
   /* _mesa_function_pool[33125]: NamedFramebufferReadBuffer (will be remapped) */
   "ii\0"
   "glNamedFramebufferReadBuffer\0"
   "\0"
   /* _mesa_function_pool[33158]: InvalidateNamedFramebufferData (will be remapped) */
   "iip\0"
   "glInvalidateNamedFramebufferData\0"
   "\0"
   /* _mesa_function_pool[33196]: InvalidateNamedFramebufferSubData (will be remapped) */
   "iipiiii\0"
   "glInvalidateNamedFramebufferSubData\0"
   "\0"
   /* _mesa_function_pool[33241]: ClearNamedFramebufferiv (will be remapped) */
   "iiip\0"
   "glClearNamedFramebufferiv\0"
   "\0"
   /* _mesa_function_pool[33273]: ClearNamedFramebufferuiv (will be remapped) */
   "iiip\0"
   "glClearNamedFramebufferuiv\0"
   "\0"
   /* _mesa_function_pool[33306]: ClearNamedFramebufferfv (will be remapped) */
   "iiip\0"
   "glClearNamedFramebufferfv\0"
   "\0"
   /* _mesa_function_pool[33338]: ClearNamedFramebufferfi (will be remapped) */
   "iiifi\0"
   "glClearNamedFramebufferfi\0"
   "\0"
   /* _mesa_function_pool[33371]: BlitNamedFramebuffer (will be remapped) */
   "iiiiiiiiiiii\0"
   "glBlitNamedFramebuffer\0"
   "\0"
   /* _mesa_function_pool[33408]: CheckNamedFramebufferStatus (will be remapped) */
   "ii\0"
   "glCheckNamedFramebufferStatus\0"
   "\0"
   /* _mesa_function_pool[33442]: GetNamedFramebufferParameteriv (will be remapped) */
   "iip\0"
   "glGetNamedFramebufferParameteriv\0"
   "\0"
   /* _mesa_function_pool[33480]: GetNamedFramebufferAttachmentParameteriv (will be remapped) */
   "iiip\0"
   "glGetNamedFramebufferAttachmentParameteriv\0"
   "\0"
   /* _mesa_function_pool[33529]: CreateRenderbuffers (will be remapped) */
   "ip\0"
   "glCreateRenderbuffers\0"
   "\0"
   /* _mesa_function_pool[33555]: NamedRenderbufferStorage (will be remapped) */
   "iiii\0"
   "glNamedRenderbufferStorage\0"
   "\0"
   /* _mesa_function_pool[33588]: NamedRenderbufferStorageMultisample (will be remapped) */
   "iiiii\0"
   "glNamedRenderbufferStorageMultisample\0"
   "\0"
   /* _mesa_function_pool[33633]: GetNamedRenderbufferParameteriv (will be remapped) */
   "iip\0"
   "glGetNamedRenderbufferParameteriv\0"
   "\0"
   /* _mesa_function_pool[33672]: CreateTextures (will be remapped) */
   "iip\0"
   "glCreateTextures\0"
   "\0"
   /* _mesa_function_pool[33694]: TextureBuffer (will be remapped) */
   "iii\0"
   "glTextureBuffer\0"
   "\0"
   /* _mesa_function_pool[33715]: TextureBufferRange (will be remapped) */
   "iiiii\0"
   "glTextureBufferRange\0"
   "\0"
   /* _mesa_function_pool[33743]: TextureStorage1D (will be remapped) */
   "iiii\0"
   "glTextureStorage1D\0"
   "\0"
   /* _mesa_function_pool[33768]: TextureStorage2D (will be remapped) */
   "iiiii\0"
   "glTextureStorage2D\0"
   "\0"
   /* _mesa_function_pool[33794]: TextureStorage3D (will be remapped) */
   "iiiiii\0"
   "glTextureStorage3D\0"
   "\0"
   /* _mesa_function_pool[33821]: TextureStorage2DMultisample (will be remapped) */
   "iiiiii\0"
   "glTextureStorage2DMultisample\0"
   "\0"
   /* _mesa_function_pool[33859]: TextureStorage3DMultisample (will be remapped) */
   "iiiiiii\0"
   "glTextureStorage3DMultisample\0"
   "\0"
   /* _mesa_function_pool[33898]: TextureSubImage1D (will be remapped) */
   "iiiiiip\0"
   "glTextureSubImage1D\0"
   "\0"
   /* _mesa_function_pool[33927]: TextureSubImage2D (will be remapped) */
   "iiiiiiiip\0"
   "glTextureSubImage2D\0"
   "\0"
   /* _mesa_function_pool[33958]: TextureSubImage3D (will be remapped) */
   "iiiiiiiiiip\0"
   "glTextureSubImage3D\0"
   "\0"
   /* _mesa_function_pool[33991]: CompressedTextureSubImage1D (will be remapped) */
   "iiiiiip\0"
   "glCompressedTextureSubImage1D\0"
   "\0"
   /* _mesa_function_pool[34030]: CompressedTextureSubImage2D (will be remapped) */
   "iiiiiiiip\0"
   "glCompressedTextureSubImage2D\0"
   "\0"
   /* _mesa_function_pool[34071]: CompressedTextureSubImage3D (will be remapped) */
   "iiiiiiiiiip\0"
   "glCompressedTextureSubImage3D\0"
   "\0"
   /* _mesa_function_pool[34114]: CopyTextureSubImage1D (will be remapped) */
   "iiiiii\0"
   "glCopyTextureSubImage1D\0"
   "\0"
   /* _mesa_function_pool[34146]: CopyTextureSubImage2D (will be remapped) */
   "iiiiiiii\0"
   "glCopyTextureSubImage2D\0"
   "\0"
   /* _mesa_function_pool[34180]: CopyTextureSubImage3D (will be remapped) */
   "iiiiiiiii\0"
   "glCopyTextureSubImage3D\0"
   "\0"
   /* _mesa_function_pool[34215]: TextureParameterf (will be remapped) */
   "iif\0"
   "glTextureParameterf\0"
   "\0"
   /* _mesa_function_pool[34240]: TextureParameterfv (will be remapped) */
   "iip\0"
   "glTextureParameterfv\0"
   "\0"
   /* _mesa_function_pool[34266]: TextureParameteri (will be remapped) */
   "iii\0"
   "glTextureParameteri\0"
   "\0"
   /* _mesa_function_pool[34291]: TextureParameterIiv (will be remapped) */
   "iip\0"
   "glTextureParameterIiv\0"
   "\0"
   /* _mesa_function_pool[34318]: TextureParameterIuiv (will be remapped) */
   "iip\0"
   "glTextureParameterIuiv\0"
   "\0"
   /* _mesa_function_pool[34346]: TextureParameteriv (will be remapped) */
   "iip\0"
   "glTextureParameteriv\0"
   "\0"
   /* _mesa_function_pool[34372]: GenerateTextureMipmap (will be remapped) */
   "i\0"
   "glGenerateTextureMipmap\0"
   "\0"
   /* _mesa_function_pool[34399]: BindTextureUnit (will be remapped) */
   "ii\0"
   "glBindTextureUnit\0"
   "\0"
   /* _mesa_function_pool[34421]: GetTextureImage (will be remapped) */
   "iiiiip\0"
   "glGetTextureImage\0"
   "\0"
   /* _mesa_function_pool[34447]: GetCompressedTextureImage (will be remapped) */
   "iiip\0"
   "glGetCompressedTextureImage\0"
   "\0"
   /* _mesa_function_pool[34481]: GetTextureLevelParameterfv (will be remapped) */
   "iiip\0"
   "glGetTextureLevelParameterfv\0"
   "\0"
   /* _mesa_function_pool[34516]: GetTextureLevelParameteriv (will be remapped) */
   "iiip\0"
   "glGetTextureLevelParameteriv\0"
   "\0"
   /* _mesa_function_pool[34551]: GetTextureParameterfv (will be remapped) */
   "iip\0"
   "glGetTextureParameterfv\0"
   "\0"
   /* _mesa_function_pool[34580]: GetTextureParameterIiv (will be remapped) */
   "iip\0"
   "glGetTextureParameterIiv\0"
   "\0"
   /* _mesa_function_pool[34610]: GetTextureParameterIuiv (will be remapped) */
   "iip\0"
   "glGetTextureParameterIuiv\0"
   "\0"
   /* _mesa_function_pool[34641]: GetTextureParameteriv (will be remapped) */
   "iip\0"
   "glGetTextureParameteriv\0"
   "\0"
   /* _mesa_function_pool[34670]: CreateVertexArrays (will be remapped) */
   "ip\0"
   "glCreateVertexArrays\0"
   "\0"
   /* _mesa_function_pool[34695]: DisableVertexArrayAttrib (will be remapped) */
   "ii\0"
   "glDisableVertexArrayAttrib\0"
   "\0"
   /* _mesa_function_pool[34726]: EnableVertexArrayAttrib (will be remapped) */
   "ii\0"
   "glEnableVertexArrayAttrib\0"
   "\0"
   /* _mesa_function_pool[34756]: VertexArrayElementBuffer (will be remapped) */
   "ii\0"
   "glVertexArrayElementBuffer\0"
   "\0"
   /* _mesa_function_pool[34787]: VertexArrayVertexBuffer (will be remapped) */
   "iiiii\0"
   "glVertexArrayVertexBuffer\0"
   "\0"
   /* _mesa_function_pool[34820]: VertexArrayVertexBuffers (will be remapped) */
   "iiippp\0"
   "glVertexArrayVertexBuffers\0"
   "\0"
   /* _mesa_function_pool[34855]: VertexArrayAttribFormat (will be remapped) */
   "iiiiii\0"
   "glVertexArrayAttribFormat\0"
   "\0"
   /* _mesa_function_pool[34889]: VertexArrayAttribIFormat (will be remapped) */
   "iiiii\0"
   "glVertexArrayAttribIFormat\0"
   "\0"
   /* _mesa_function_pool[34923]: VertexArrayAttribLFormat (will be remapped) */
   "iiiii\0"
   "glVertexArrayAttribLFormat\0"
   "\0"
   /* _mesa_function_pool[34957]: VertexArrayAttribBinding (will be remapped) */
   "iii\0"
   "glVertexArrayAttribBinding\0"
   "\0"
   /* _mesa_function_pool[34989]: VertexArrayBindingDivisor (will be remapped) */
   "iii\0"
   "glVertexArrayBindingDivisor\0"
   "\0"
   /* _mesa_function_pool[35022]: GetVertexArrayiv (will be remapped) */
   "iip\0"
   "glGetVertexArrayiv\0"
   "\0"
   /* _mesa_function_pool[35046]: GetVertexArrayIndexediv (will be remapped) */
   "iiip\0"
   "glGetVertexArrayIndexediv\0"
   "\0"
   /* _mesa_function_pool[35078]: GetVertexArrayIndexed64iv (will be remapped) */
   "iiip\0"
   "glGetVertexArrayIndexed64iv\0"
   "\0"
   /* _mesa_function_pool[35112]: CreateSamplers (will be remapped) */
   "ip\0"
   "glCreateSamplers\0"
   "\0"
   /* _mesa_function_pool[35133]: CreateProgramPipelines (will be remapped) */
   "ip\0"
   "glCreateProgramPipelines\0"
   "\0"
   /* _mesa_function_pool[35162]: CreateQueries (will be remapped) */
   "iip\0"
   "glCreateQueries\0"
   "\0"
   /* _mesa_function_pool[35183]: GetQueryBufferObjectiv (will be remapped) */
   "iiii\0"
   "glGetQueryBufferObjectiv\0"
   "\0"
   /* _mesa_function_pool[35214]: GetQueryBufferObjectuiv (will be remapped) */
   "iiii\0"
   "glGetQueryBufferObjectuiv\0"
   "\0"
   /* _mesa_function_pool[35246]: GetQueryBufferObjecti64v (will be remapped) */
   "iiii\0"
   "glGetQueryBufferObjecti64v\0"
   "\0"
   /* _mesa_function_pool[35279]: GetQueryBufferObjectui64v (will be remapped) */
   "iiii\0"
   "glGetQueryBufferObjectui64v\0"
   "\0"
   /* _mesa_function_pool[35313]: GetTextureSubImage (will be remapped) */
   "iiiiiiiiiiip\0"
   "glGetTextureSubImage\0"
   "\0"
   /* _mesa_function_pool[35348]: GetCompressedTextureSubImage (will be remapped) */
   "iiiiiiiiip\0"
   "glGetCompressedTextureSubImage\0"
   "\0"
   /* _mesa_function_pool[35391]: TextureBarrierNV (will be remapped) */
   "\0"
   "glTextureBarrier\0"
   "glTextureBarrierNV\0"
   "\0"
   /* _mesa_function_pool[35429]: BufferPageCommitmentARB (will be remapped) */
   "iiii\0"
   "glBufferPageCommitmentARB\0"
   "\0"
   /* _mesa_function_pool[35461]: NamedBufferPageCommitmentARB (will be remapped) */
   "iiii\0"
   "glNamedBufferPageCommitmentARB\0"
   "\0"
   /* _mesa_function_pool[35498]: PrimitiveBoundingBox (will be remapped) */
   "ffffffff\0"
   "glPrimitiveBoundingBox\0"
   "glPrimitiveBoundingBoxARB\0"
   "glPrimitiveBoundingBoxEXT\0"
   "glPrimitiveBoundingBoxOES\0"
   "\0"
   /* _mesa_function_pool[35609]: BlendBarrier (will be remapped) */
   "\0"
   "glBlendBarrier\0"
   "glBlendBarrierKHR\0"
   "\0"
   /* _mesa_function_pool[35644]: Uniform1i64ARB (will be remapped) */
   "ii\0"
   "glUniform1i64ARB\0"
   "\0"
   /* _mesa_function_pool[35665]: Uniform2i64ARB (will be remapped) */
   "iii\0"
   "glUniform2i64ARB\0"
   "\0"
   /* _mesa_function_pool[35687]: Uniform3i64ARB (will be remapped) */
   "iiii\0"
   "glUniform3i64ARB\0"
   "\0"
   /* _mesa_function_pool[35710]: Uniform4i64ARB (will be remapped) */
   "iiiii\0"
   "glUniform4i64ARB\0"
   "\0"
   /* _mesa_function_pool[35734]: Uniform1i64vARB (will be remapped) */
   "iip\0"
   "glUniform1i64vARB\0"
   "\0"
   /* _mesa_function_pool[35757]: Uniform2i64vARB (will be remapped) */
   "iip\0"
   "glUniform2i64vARB\0"
   "\0"
   /* _mesa_function_pool[35780]: Uniform3i64vARB (will be remapped) */
   "iip\0"
   "glUniform3i64vARB\0"
   "\0"
   /* _mesa_function_pool[35803]: Uniform4i64vARB (will be remapped) */
   "iip\0"
   "glUniform4i64vARB\0"
   "\0"
   /* _mesa_function_pool[35826]: Uniform1ui64ARB (will be remapped) */
   "ii\0"
   "glUniform1ui64ARB\0"
   "\0"
   /* _mesa_function_pool[35848]: Uniform2ui64ARB (will be remapped) */
   "iii\0"
   "glUniform2ui64ARB\0"
   "\0"
   /* _mesa_function_pool[35871]: Uniform3ui64ARB (will be remapped) */
   "iiii\0"
   "glUniform3ui64ARB\0"
   "\0"
   /* _mesa_function_pool[35895]: Uniform4ui64ARB (will be remapped) */
   "iiiii\0"
   "glUniform4ui64ARB\0"
   "\0"
   /* _mesa_function_pool[35920]: Uniform1ui64vARB (will be remapped) */
   "iip\0"
   "glUniform1ui64vARB\0"
   "\0"
   /* _mesa_function_pool[35944]: Uniform2ui64vARB (will be remapped) */
   "iip\0"
   "glUniform2ui64vARB\0"
   "\0"
   /* _mesa_function_pool[35968]: Uniform3ui64vARB (will be remapped) */
   "iip\0"
   "glUniform3ui64vARB\0"
   "\0"
   /* _mesa_function_pool[35992]: Uniform4ui64vARB (will be remapped) */
   "iip\0"
   "glUniform4ui64vARB\0"
   "\0"
   /* _mesa_function_pool[36016]: GetUniformi64vARB (will be remapped) */
   "iip\0"
   "glGetUniformi64vARB\0"
   "\0"
   /* _mesa_function_pool[36041]: GetUniformui64vARB (will be remapped) */
   "iip\0"
   "glGetUniformui64vARB\0"
   "\0"
   /* _mesa_function_pool[36067]: GetnUniformi64vARB (will be remapped) */
   "iiip\0"
   "glGetnUniformi64vARB\0"
   "\0"
   /* _mesa_function_pool[36094]: GetnUniformui64vARB (will be remapped) */
   "iiip\0"
   "glGetnUniformui64vARB\0"
   "\0"
   /* _mesa_function_pool[36122]: ProgramUniform1i64ARB (will be remapped) */
   "iii\0"
   "glProgramUniform1i64ARB\0"
   "\0"
   /* _mesa_function_pool[36151]: ProgramUniform2i64ARB (will be remapped) */
   "iiii\0"
   "glProgramUniform2i64ARB\0"
   "\0"
   /* _mesa_function_pool[36181]: ProgramUniform3i64ARB (will be remapped) */
   "iiiii\0"
   "glProgramUniform3i64ARB\0"
   "\0"
   /* _mesa_function_pool[36212]: ProgramUniform4i64ARB (will be remapped) */
   "iiiiii\0"
   "glProgramUniform4i64ARB\0"
   "\0"
   /* _mesa_function_pool[36244]: ProgramUniform1i64vARB (will be remapped) */
   "iiip\0"
   "glProgramUniform1i64vARB\0"
   "\0"
   /* _mesa_function_pool[36275]: ProgramUniform2i64vARB (will be remapped) */
   "iiip\0"
   "glProgramUniform2i64vARB\0"
   "\0"
   /* _mesa_function_pool[36306]: ProgramUniform3i64vARB (will be remapped) */
   "iiip\0"
   "glProgramUniform3i64vARB\0"
   "\0"
   /* _mesa_function_pool[36337]: ProgramUniform4i64vARB (will be remapped) */
   "iiip\0"
   "glProgramUniform4i64vARB\0"
   "\0"
   /* _mesa_function_pool[36368]: ProgramUniform1ui64ARB (will be remapped) */
   "iii\0"
   "glProgramUniform1ui64ARB\0"
   "\0"
   /* _mesa_function_pool[36398]: ProgramUniform2ui64ARB (will be remapped) */
   "iiii\0"
   "glProgramUniform2ui64ARB\0"
   "\0"
   /* _mesa_function_pool[36429]: ProgramUniform3ui64ARB (will be remapped) */
   "iiiii\0"
   "glProgramUniform3ui64ARB\0"
   "\0"
   /* _mesa_function_pool[36461]: ProgramUniform4ui64ARB (will be remapped) */
   "iiiiii\0"
   "glProgramUniform4ui64ARB\0"
   "\0"
   /* _mesa_function_pool[36494]: ProgramUniform1ui64vARB (will be remapped) */
   "iiip\0"
   "glProgramUniform1ui64vARB\0"
   "\0"
   /* _mesa_function_pool[36526]: ProgramUniform2ui64vARB (will be remapped) */
   "iiip\0"
   "glProgramUniform2ui64vARB\0"
   "\0"
   /* _mesa_function_pool[36558]: ProgramUniform3ui64vARB (will be remapped) */
   "iiip\0"
   "glProgramUniform3ui64vARB\0"
   "\0"
   /* _mesa_function_pool[36590]: ProgramUniform4ui64vARB (will be remapped) */
   "iiip\0"
   "glProgramUniform4ui64vARB\0"
   "\0"
   /* _mesa_function_pool[36622]: SpecializeShaderARB (will be remapped) */
   "ipipp\0"
   "glSpecializeShaderARB\0"
   "glSpecializeShader\0"
   "\0"
   /* _mesa_function_pool[36670]: GetTexFilterFuncSGIS (dynamic) */
   "iip\0"
   "glGetTexFilterFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[36698]: TexFilterFuncSGIS (dynamic) */
   "iiip\0"
   "glTexFilterFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[36724]: PixelTexGenParameteriSGIS (dynamic) */
   "ii\0"
   "glPixelTexGenParameteriSGIS\0"
   "\0"
   /* _mesa_function_pool[36756]: PixelTexGenParameterivSGIS (dynamic) */
   "ip\0"
   "glPixelTexGenParameterivSGIS\0"
   "\0"
   /* _mesa_function_pool[36789]: PixelTexGenParameterfSGIS (dynamic) */
   "if\0"
   "glPixelTexGenParameterfSGIS\0"
   "\0"
   /* _mesa_function_pool[36821]: PixelTexGenParameterfvSGIS (dynamic) */
   "ip\0"
   "glPixelTexGenParameterfvSGIS\0"
   "\0"
   /* _mesa_function_pool[36854]: GetPixelTexGenParameterivSGIS (dynamic) */
   "ip\0"
   "glGetPixelTexGenParameterivSGIS\0"
   "\0"
   /* _mesa_function_pool[36890]: GetPixelTexGenParameterfvSGIS (dynamic) */
   "ip\0"
   "glGetPixelTexGenParameterfvSGIS\0"
   "\0"
   /* _mesa_function_pool[36926]: TexImage4DSGIS (dynamic) */
   "iiiiiiiiiip\0"
   "glTexImage4DSGIS\0"
   "\0"
   /* _mesa_function_pool[36956]: TexSubImage4DSGIS (dynamic) */
   "iiiiiiiiiiiip\0"
   "glTexSubImage4DSGIS\0"
   "\0"
   /* _mesa_function_pool[36991]: DetailTexFuncSGIS (dynamic) */
   "iip\0"
   "glDetailTexFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[37016]: GetDetailTexFuncSGIS (dynamic) */
   "ip\0"
   "glGetDetailTexFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[37043]: SharpenTexFuncSGIS (dynamic) */
   "iip\0"
   "glSharpenTexFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[37069]: GetSharpenTexFuncSGIS (dynamic) */
   "ip\0"
   "glGetSharpenTexFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[37097]: SampleMaskSGIS (will be remapped) */
   "fi\0"
   "glSampleMaskSGIS\0"
   "glSampleMaskEXT\0"
   "\0"
   /* _mesa_function_pool[37134]: SamplePatternSGIS (will be remapped) */
   "i\0"
   "glSamplePatternSGIS\0"
   "glSamplePatternEXT\0"
   "\0"
   /* _mesa_function_pool[37176]: ColorPointerEXT (will be remapped) */
   "iiiip\0"
   "glColorPointerEXT\0"
   "\0"
   /* _mesa_function_pool[37201]: EdgeFlagPointerEXT (will be remapped) */
   "iip\0"
   "glEdgeFlagPointerEXT\0"
   "\0"
   /* _mesa_function_pool[37227]: IndexPointerEXT (will be remapped) */
   "iiip\0"
   "glIndexPointerEXT\0"
   "\0"
   /* _mesa_function_pool[37251]: NormalPointerEXT (will be remapped) */
   "iiip\0"
   "glNormalPointerEXT\0"
   "\0"
   /* _mesa_function_pool[37276]: TexCoordPointerEXT (will be remapped) */
   "iiiip\0"
   "glTexCoordPointerEXT\0"
   "\0"
   /* _mesa_function_pool[37304]: VertexPointerEXT (will be remapped) */
   "iiiip\0"
   "glVertexPointerEXT\0"
   "\0"
   /* _mesa_function_pool[37330]: SpriteParameterfSGIX (dynamic) */
   "if\0"
   "glSpriteParameterfSGIX\0"
   "\0"
   /* _mesa_function_pool[37357]: SpriteParameterfvSGIX (dynamic) */
   "ip\0"
   "glSpriteParameterfvSGIX\0"
   "\0"
   /* _mesa_function_pool[37385]: SpriteParameteriSGIX (dynamic) */
   "ii\0"
   "glSpriteParameteriSGIX\0"
   "\0"
   /* _mesa_function_pool[37412]: SpriteParameterivSGIX (dynamic) */
   "ip\0"
   "glSpriteParameterivSGIX\0"
   "\0"
   /* _mesa_function_pool[37440]: GetInstrumentsSGIX (dynamic) */
   "\0"
   "glGetInstrumentsSGIX\0"
   "\0"
   /* _mesa_function_pool[37463]: InstrumentsBufferSGIX (dynamic) */
   "ip\0"
   "glInstrumentsBufferSGIX\0"
   "\0"
   /* _mesa_function_pool[37491]: PollInstrumentsSGIX (dynamic) */
   "p\0"
   "glPollInstrumentsSGIX\0"
   "\0"
   /* _mesa_function_pool[37516]: ReadInstrumentsSGIX (dynamic) */
   "i\0"
   "glReadInstrumentsSGIX\0"
   "\0"
   /* _mesa_function_pool[37541]: StartInstrumentsSGIX (dynamic) */
   "\0"
   "glStartInstrumentsSGIX\0"
   "\0"
   /* _mesa_function_pool[37566]: StopInstrumentsSGIX (dynamic) */
   "i\0"
   "glStopInstrumentsSGIX\0"
   "\0"
   /* _mesa_function_pool[37591]: FrameZoomSGIX (dynamic) */
   "i\0"
   "glFrameZoomSGIX\0"
   "\0"
   /* _mesa_function_pool[37610]: TagSampleBufferSGIX (dynamic) */
   "\0"
   "glTagSampleBufferSGIX\0"
   "\0"
   /* _mesa_function_pool[37634]: ReferencePlaneSGIX (dynamic) */
   "p\0"
   "glReferencePlaneSGIX\0"
   "\0"
   /* _mesa_function_pool[37658]: FlushRasterSGIX (dynamic) */
   "\0"
   "glFlushRasterSGIX\0"
   "\0"
   /* _mesa_function_pool[37678]: FogFuncSGIS (dynamic) */
   "ip\0"
   "glFogFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[37696]: GetFogFuncSGIS (dynamic) */
   "p\0"
   "glGetFogFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[37716]: ImageTransformParameteriHP (dynamic) */
   "iii\0"
   "glImageTransformParameteriHP\0"
   "\0"
   /* _mesa_function_pool[37750]: ImageTransformParameterfHP (dynamic) */
   "iif\0"
   "glImageTransformParameterfHP\0"
   "\0"
   /* _mesa_function_pool[37784]: ImageTransformParameterivHP (dynamic) */
   "iip\0"
   "glImageTransformParameterivHP\0"
   "\0"
   /* _mesa_function_pool[37819]: ImageTransformParameterfvHP (dynamic) */
   "iip\0"
   "glImageTransformParameterfvHP\0"
   "\0"
   /* _mesa_function_pool[37854]: GetImageTransformParameterivHP (dynamic) */
   "iip\0"
   "glGetImageTransformParameterivHP\0"
   "\0"
   /* _mesa_function_pool[37892]: GetImageTransformParameterfvHP (dynamic) */
   "iip\0"
   "glGetImageTransformParameterfvHP\0"
   "\0"
   /* _mesa_function_pool[37930]: HintPGI (dynamic) */
   "ii\0"
   "glHintPGI\0"
   "\0"
   /* _mesa_function_pool[37944]: GetListParameterfvSGIX (dynamic) */
   "iip\0"
   "glGetListParameterfvSGIX\0"
   "\0"
   /* _mesa_function_pool[37974]: GetListParameterivSGIX (dynamic) */
   "iip\0"
   "glGetListParameterivSGIX\0"
   "\0"
   /* _mesa_function_pool[38004]: ListParameterfSGIX (dynamic) */
   "iif\0"
   "glListParameterfSGIX\0"
   "\0"
   /* _mesa_function_pool[38030]: ListParameterfvSGIX (dynamic) */
   "iip\0"
   "glListParameterfvSGIX\0"
   "\0"
   /* _mesa_function_pool[38057]: ListParameteriSGIX (dynamic) */
   "iii\0"
   "glListParameteriSGIX\0"
   "\0"
   /* _mesa_function_pool[38083]: ListParameterivSGIX (dynamic) */
   "iip\0"
   "glListParameterivSGIX\0"
   "\0"
   /* _mesa_function_pool[38110]: IndexMaterialEXT (dynamic) */
   "ii\0"
   "glIndexMaterialEXT\0"
   "\0"
   /* _mesa_function_pool[38133]: IndexFuncEXT (dynamic) */
   "if\0"
   "glIndexFuncEXT\0"
   "\0"
   /* _mesa_function_pool[38152]: LockArraysEXT (will be remapped) */
   "ii\0"
   "glLockArraysEXT\0"
   "\0"
   /* _mesa_function_pool[38172]: UnlockArraysEXT (will be remapped) */
   "\0"
   "glUnlockArraysEXT\0"
   "\0"
   /* _mesa_function_pool[38192]: CullParameterdvEXT (dynamic) */
   "ip\0"
   "glCullParameterdvEXT\0"
   "\0"
   /* _mesa_function_pool[38217]: CullParameterfvEXT (dynamic) */
   "ip\0"
   "glCullParameterfvEXT\0"
   "\0"
   /* _mesa_function_pool[38242]: ViewportArrayv (will be remapped) */
   "iip\0"
   "glViewportArrayv\0"
   "glViewportArrayvOES\0"
   "\0"
   /* _mesa_function_pool[38284]: ViewportIndexedf (will be remapped) */
   "iffff\0"
   "glViewportIndexedf\0"
   "glViewportIndexedfOES\0"
   "\0"
   /* _mesa_function_pool[38332]: ViewportIndexedfv (will be remapped) */
   "ip\0"
   "glViewportIndexedfv\0"
   "glViewportIndexedfvOES\0"
   "\0"
   /* _mesa_function_pool[38379]: ScissorArrayv (will be remapped) */
   "iip\0"
   "glScissorArrayv\0"
   "glScissorArrayvOES\0"
   "\0"
   /* _mesa_function_pool[38419]: ScissorIndexed (will be remapped) */
   "iiiii\0"
   "glScissorIndexed\0"
   "glScissorIndexedOES\0"
   "\0"
   /* _mesa_function_pool[38463]: ScissorIndexedv (will be remapped) */
   "ip\0"
   "glScissorIndexedv\0"
   "glScissorIndexedvOES\0"
   "\0"
   /* _mesa_function_pool[38506]: DepthRangeArrayv (will be remapped) */
   "iip\0"
   "glDepthRangeArrayv\0"
   "\0"
   /* _mesa_function_pool[38530]: DepthRangeIndexed (will be remapped) */
   "idd\0"
   "glDepthRangeIndexed\0"
   "\0"
   /* _mesa_function_pool[38555]: GetFloati_v (will be remapped) */
   "iip\0"
   "glGetFloati_v\0"
   "glGetFloati_vOES\0"
   "\0"
   /* _mesa_function_pool[38591]: GetDoublei_v (will be remapped) */
   "iip\0"
   "glGetDoublei_v\0"
   "\0"
   /* _mesa_function_pool[38611]: FragmentColorMaterialSGIX (dynamic) */
   "ii\0"
   "glFragmentColorMaterialSGIX\0"
   "\0"
   /* _mesa_function_pool[38643]: FragmentLightfSGIX (dynamic) */
   "iif\0"
   "glFragmentLightfSGIX\0"
   "\0"
   /* _mesa_function_pool[38669]: FragmentLightfvSGIX (dynamic) */
   "iip\0"
   "glFragmentLightfvSGIX\0"
   "\0"
   /* _mesa_function_pool[38696]: FragmentLightiSGIX (dynamic) */
   "iii\0"
   "glFragmentLightiSGIX\0"
   "\0"
   /* _mesa_function_pool[38722]: FragmentLightivSGIX (dynamic) */
   "iip\0"
   "glFragmentLightivSGIX\0"
   "\0"
   /* _mesa_function_pool[38749]: FragmentLightModelfSGIX (dynamic) */
   "if\0"
   "glFragmentLightModelfSGIX\0"
   "\0"
   /* _mesa_function_pool[38779]: FragmentLightModelfvSGIX (dynamic) */
   "ip\0"
   "glFragmentLightModelfvSGIX\0"
   "\0"
   /* _mesa_function_pool[38810]: FragmentLightModeliSGIX (dynamic) */
   "ii\0"
   "glFragmentLightModeliSGIX\0"
   "\0"
   /* _mesa_function_pool[38840]: FragmentLightModelivSGIX (dynamic) */
   "ip\0"
   "glFragmentLightModelivSGIX\0"
   "\0"
   /* _mesa_function_pool[38871]: FragmentMaterialfSGIX (dynamic) */
   "iif\0"
   "glFragmentMaterialfSGIX\0"
   "\0"
   /* _mesa_function_pool[38900]: FragmentMaterialfvSGIX (dynamic) */
   "iip\0"
   "glFragmentMaterialfvSGIX\0"
   "\0"
   /* _mesa_function_pool[38930]: FragmentMaterialiSGIX (dynamic) */
   "iii\0"
   "glFragmentMaterialiSGIX\0"
   "\0"
   /* _mesa_function_pool[38959]: FragmentMaterialivSGIX (dynamic) */
   "iip\0"
   "glFragmentMaterialivSGIX\0"
   "\0"
   /* _mesa_function_pool[38989]: GetFragmentLightfvSGIX (dynamic) */
   "iip\0"
   "glGetFragmentLightfvSGIX\0"
   "\0"
   /* _mesa_function_pool[39019]: GetFragmentLightivSGIX (dynamic) */
   "iip\0"
   "glGetFragmentLightivSGIX\0"
   "\0"
   /* _mesa_function_pool[39049]: GetFragmentMaterialfvSGIX (dynamic) */
   "iip\0"
   "glGetFragmentMaterialfvSGIX\0"
   "\0"
   /* _mesa_function_pool[39082]: GetFragmentMaterialivSGIX (dynamic) */
   "iip\0"
   "glGetFragmentMaterialivSGIX\0"
   "\0"
   /* _mesa_function_pool[39115]: LightEnviSGIX (dynamic) */
   "ii\0"
   "glLightEnviSGIX\0"
   "\0"
   /* _mesa_function_pool[39135]: ApplyTextureEXT (dynamic) */
   "i\0"
   "glApplyTextureEXT\0"
   "\0"
   /* _mesa_function_pool[39156]: TextureLightEXT (dynamic) */
   "i\0"
   "glTextureLightEXT\0"
   "\0"
   /* _mesa_function_pool[39177]: TextureMaterialEXT (dynamic) */
   "ii\0"
   "glTextureMaterialEXT\0"
   "\0"
   /* _mesa_function_pool[39202]: AsyncMarkerSGIX (dynamic) */
   "i\0"
   "glAsyncMarkerSGIX\0"
   "\0"
   /* _mesa_function_pool[39223]: FinishAsyncSGIX (dynamic) */
   "p\0"
   "glFinishAsyncSGIX\0"
   "\0"
   /* _mesa_function_pool[39244]: PollAsyncSGIX (dynamic) */
   "p\0"
   "glPollAsyncSGIX\0"
   "\0"
   /* _mesa_function_pool[39263]: GenAsyncMarkersSGIX (dynamic) */
   "i\0"
   "glGenAsyncMarkersSGIX\0"
   "\0"
   /* _mesa_function_pool[39288]: DeleteAsyncMarkersSGIX (dynamic) */
   "ii\0"
   "glDeleteAsyncMarkersSGIX\0"
   "\0"
   /* _mesa_function_pool[39317]: IsAsyncMarkerSGIX (dynamic) */
   "i\0"
   "glIsAsyncMarkerSGIX\0"
   "\0"
   /* _mesa_function_pool[39340]: VertexPointervINTEL (dynamic) */
   "iip\0"
   "glVertexPointervINTEL\0"
   "\0"
   /* _mesa_function_pool[39367]: NormalPointervINTEL (dynamic) */
   "ip\0"
   "glNormalPointervINTEL\0"
   "\0"
   /* _mesa_function_pool[39393]: ColorPointervINTEL (dynamic) */
   "iip\0"
   "glColorPointervINTEL\0"
   "\0"
   /* _mesa_function_pool[39419]: TexCoordPointervINTEL (dynamic) */
   "iip\0"
   "glTexCoordPointervINTEL\0"
   "\0"
   /* _mesa_function_pool[39448]: PixelTransformParameteriEXT (dynamic) */
   "iii\0"
   "glPixelTransformParameteriEXT\0"
   "\0"
   /* _mesa_function_pool[39483]: PixelTransformParameterfEXT (dynamic) */
   "iif\0"
   "glPixelTransformParameterfEXT\0"
   "\0"
   /* _mesa_function_pool[39518]: PixelTransformParameterivEXT (dynamic) */
   "iip\0"
   "glPixelTransformParameterivEXT\0"
   "\0"
   /* _mesa_function_pool[39554]: PixelTransformParameterfvEXT (dynamic) */
   "iip\0"
   "glPixelTransformParameterfvEXT\0"
   "\0"
   /* _mesa_function_pool[39590]: TextureNormalEXT (dynamic) */
   "i\0"
   "glTextureNormalEXT\0"
   "\0"
   /* _mesa_function_pool[39612]: Tangent3bEXT (dynamic) */
   "iii\0"
   "glTangent3bEXT\0"
   "\0"
   /* _mesa_function_pool[39632]: Tangent3bvEXT (dynamic) */
   "p\0"
   "glTangent3bvEXT\0"
   "\0"
   /* _mesa_function_pool[39651]: Tangent3dEXT (dynamic) */
   "ddd\0"
   "glTangent3dEXT\0"
   "\0"
   /* _mesa_function_pool[39671]: Tangent3dvEXT (dynamic) */
   "p\0"
   "glTangent3dvEXT\0"
   "\0"
   /* _mesa_function_pool[39690]: Tangent3fEXT (dynamic) */
   "fff\0"
   "glTangent3fEXT\0"
   "\0"
   /* _mesa_function_pool[39710]: Tangent3fvEXT (dynamic) */
   "p\0"
   "glTangent3fvEXT\0"
   "\0"
   /* _mesa_function_pool[39729]: Tangent3iEXT (dynamic) */
   "iii\0"
   "glTangent3iEXT\0"
   "\0"
   /* _mesa_function_pool[39749]: Tangent3ivEXT (dynamic) */
   "p\0"
   "glTangent3ivEXT\0"
   "\0"
   /* _mesa_function_pool[39768]: Tangent3sEXT (dynamic) */
   "iii\0"
   "glTangent3sEXT\0"
   "\0"
   /* _mesa_function_pool[39788]: Tangent3svEXT (dynamic) */
   "p\0"
   "glTangent3svEXT\0"
   "\0"
   /* _mesa_function_pool[39807]: Binormal3bEXT (dynamic) */
   "iii\0"
   "glBinormal3bEXT\0"
   "\0"
   /* _mesa_function_pool[39828]: Binormal3bvEXT (dynamic) */
   "p\0"
   "glBinormal3bvEXT\0"
   "\0"
   /* _mesa_function_pool[39848]: Binormal3dEXT (dynamic) */
   "ddd\0"
   "glBinormal3dEXT\0"
   "\0"
   /* _mesa_function_pool[39869]: Binormal3dvEXT (dynamic) */
   "p\0"
   "glBinormal3dvEXT\0"
   "\0"
   /* _mesa_function_pool[39889]: Binormal3fEXT (dynamic) */
   "fff\0"
   "glBinormal3fEXT\0"
   "\0"
   /* _mesa_function_pool[39910]: Binormal3fvEXT (dynamic) */
   "p\0"
   "glBinormal3fvEXT\0"
   "\0"
   /* _mesa_function_pool[39930]: Binormal3iEXT (dynamic) */
   "iii\0"
   "glBinormal3iEXT\0"
   "\0"
   /* _mesa_function_pool[39951]: Binormal3ivEXT (dynamic) */
   "p\0"
   "glBinormal3ivEXT\0"
   "\0"
   /* _mesa_function_pool[39971]: Binormal3sEXT (dynamic) */
   "iii\0"
   "glBinormal3sEXT\0"
   "\0"
   /* _mesa_function_pool[39992]: Binormal3svEXT (dynamic) */
   "p\0"
   "glBinormal3svEXT\0"
   "\0"
   /* _mesa_function_pool[40012]: TangentPointerEXT (dynamic) */
   "iip\0"
   "glTangentPointerEXT\0"
   "\0"
   /* _mesa_function_pool[40037]: BinormalPointerEXT (dynamic) */
   "iip\0"
   "glBinormalPointerEXT\0"
   "\0"
   /* _mesa_function_pool[40063]: PixelTexGenSGIX (dynamic) */
   "i\0"
   "glPixelTexGenSGIX\0"
   "\0"
   /* _mesa_function_pool[40084]: FinishTextureSUNX (dynamic) */
   "\0"
   "glFinishTextureSUNX\0"
   "\0"
   /* _mesa_function_pool[40106]: GlobalAlphaFactorbSUN (dynamic) */
   "i\0"
   "glGlobalAlphaFactorbSUN\0"
   "\0"
   /* _mesa_function_pool[40133]: GlobalAlphaFactorsSUN (dynamic) */
   "i\0"
   "glGlobalAlphaFactorsSUN\0"
   "\0"
   /* _mesa_function_pool[40160]: GlobalAlphaFactoriSUN (dynamic) */
   "i\0"
   "glGlobalAlphaFactoriSUN\0"
   "\0"
   /* _mesa_function_pool[40187]: GlobalAlphaFactorfSUN (dynamic) */
   "f\0"
   "glGlobalAlphaFactorfSUN\0"
   "\0"
   /* _mesa_function_pool[40214]: GlobalAlphaFactordSUN (dynamic) */
   "d\0"
   "glGlobalAlphaFactordSUN\0"
   "\0"
   /* _mesa_function_pool[40241]: GlobalAlphaFactorubSUN (dynamic) */
   "i\0"
   "glGlobalAlphaFactorubSUN\0"
   "\0"
   /* _mesa_function_pool[40269]: GlobalAlphaFactorusSUN (dynamic) */
   "i\0"
   "glGlobalAlphaFactorusSUN\0"
   "\0"
   /* _mesa_function_pool[40297]: GlobalAlphaFactoruiSUN (dynamic) */
   "i\0"
   "glGlobalAlphaFactoruiSUN\0"
   "\0"
   /* _mesa_function_pool[40325]: ReplacementCodeuiSUN (dynamic) */
   "i\0"
   "glReplacementCodeuiSUN\0"
   "\0"
   /* _mesa_function_pool[40351]: ReplacementCodeusSUN (dynamic) */
   "i\0"
   "glReplacementCodeusSUN\0"
   "\0"
   /* _mesa_function_pool[40377]: ReplacementCodeubSUN (dynamic) */
   "i\0"
   "glReplacementCodeubSUN\0"
   "\0"
   /* _mesa_function_pool[40403]: ReplacementCodeuivSUN (dynamic) */
   "p\0"
   "glReplacementCodeuivSUN\0"
   "\0"
   /* _mesa_function_pool[40430]: ReplacementCodeusvSUN (dynamic) */
   "p\0"
   "glReplacementCodeusvSUN\0"
   "\0"
   /* _mesa_function_pool[40457]: ReplacementCodeubvSUN (dynamic) */
   "p\0"
   "glReplacementCodeubvSUN\0"
   "\0"
   /* _mesa_function_pool[40484]: ReplacementCodePointerSUN (dynamic) */
   "iip\0"
   "glReplacementCodePointerSUN\0"
   "\0"
   /* _mesa_function_pool[40517]: Color4ubVertex2fSUN (dynamic) */
   "iiiiff\0"
   "glColor4ubVertex2fSUN\0"
   "\0"
   /* _mesa_function_pool[40547]: Color4ubVertex2fvSUN (dynamic) */
   "pp\0"
   "glColor4ubVertex2fvSUN\0"
   "\0"
   /* _mesa_function_pool[40574]: Color4ubVertex3fSUN (dynamic) */
   "iiiifff\0"
   "glColor4ubVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[40605]: Color4ubVertex3fvSUN (dynamic) */
   "pp\0"
   "glColor4ubVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[40632]: Color3fVertex3fSUN (dynamic) */
   "ffffff\0"
   "glColor3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[40661]: Color3fVertex3fvSUN (dynamic) */
   "pp\0"
   "glColor3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[40687]: Normal3fVertex3fSUN (dynamic) */
   "ffffff\0"
   "glNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[40717]: Normal3fVertex3fvSUN (dynamic) */
   "pp\0"
   "glNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[40744]: Color4fNormal3fVertex3fSUN (dynamic) */
   "ffffffffff\0"
   "glColor4fNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[40785]: Color4fNormal3fVertex3fvSUN (dynamic) */
   "ppp\0"
   "glColor4fNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[40820]: TexCoord2fVertex3fSUN (dynamic) */
   "fffff\0"
   "glTexCoord2fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[40851]: TexCoord2fVertex3fvSUN (dynamic) */
   "pp\0"
   "glTexCoord2fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[40880]: TexCoord4fVertex4fSUN (dynamic) */
   "ffffffff\0"
   "glTexCoord4fVertex4fSUN\0"
   "\0"
   /* _mesa_function_pool[40914]: TexCoord4fVertex4fvSUN (dynamic) */
   "pp\0"
   "glTexCoord4fVertex4fvSUN\0"
   "\0"
   /* _mesa_function_pool[40943]: TexCoord2fColor4ubVertex3fSUN (dynamic) */
   "ffiiiifff\0"
   "glTexCoord2fColor4ubVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[40986]: TexCoord2fColor4ubVertex3fvSUN (dynamic) */
   "ppp\0"
   "glTexCoord2fColor4ubVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[41024]: TexCoord2fColor3fVertex3fSUN (dynamic) */
   "ffffffff\0"
   "glTexCoord2fColor3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[41065]: TexCoord2fColor3fVertex3fvSUN (dynamic) */
   "ppp\0"
   "glTexCoord2fColor3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[41102]: TexCoord2fNormal3fVertex3fSUN (dynamic) */
   "ffffffff\0"
   "glTexCoord2fNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[41144]: TexCoord2fNormal3fVertex3fvSUN (dynamic) */
   "ppp\0"
   "glTexCoord2fNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[41182]: TexCoord2fColor4fNormal3fVertex3fSUN (dynamic) */
   "ffffffffffff\0"
   "glTexCoord2fColor4fNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[41235]: TexCoord2fColor4fNormal3fVertex3fvSUN (dynamic) */
   "pppp\0"
   "glTexCoord2fColor4fNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[41281]: TexCoord4fColor4fNormal3fVertex4fSUN (dynamic) */
   "fffffffffffffff\0"
   "glTexCoord4fColor4fNormal3fVertex4fSUN\0"
   "\0"
   /* _mesa_function_pool[41337]: TexCoord4fColor4fNormal3fVertex4fvSUN (dynamic) */
   "pppp\0"
   "glTexCoord4fColor4fNormal3fVertex4fvSUN\0"
   "\0"
   /* _mesa_function_pool[41383]: ReplacementCodeuiVertex3fSUN (dynamic) */
   "ifff\0"
   "glReplacementCodeuiVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[41420]: ReplacementCodeuiVertex3fvSUN (dynamic) */
   "pp\0"
   "glReplacementCodeuiVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[41456]: ReplacementCodeuiColor4ubVertex3fSUN (dynamic) */
   "iiiiifff\0"
   "glReplacementCodeuiColor4ubVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[41505]: ReplacementCodeuiColor4ubVertex3fvSUN (dynamic) */
   "ppp\0"
   "glReplacementCodeuiColor4ubVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[41550]: ReplacementCodeuiColor3fVertex3fSUN (dynamic) */
   "iffffff\0"
   "glReplacementCodeuiColor3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[41597]: ReplacementCodeuiColor3fVertex3fvSUN (dynamic) */
   "ppp\0"
   "glReplacementCodeuiColor3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[41641]: ReplacementCodeuiNormal3fVertex3fSUN (dynamic) */
   "iffffff\0"
   "glReplacementCodeuiNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[41689]: ReplacementCodeuiNormal3fVertex3fvSUN (dynamic) */
   "ppp\0"
   "glReplacementCodeuiNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[41734]: ReplacementCodeuiColor4fNormal3fVertex3fSUN (dynamic) */
   "iffffffffff\0"
   "glReplacementCodeuiColor4fNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[41793]: ReplacementCodeuiColor4fNormal3fVertex3fvSUN (dynamic) */
   "pppp\0"
   "glReplacementCodeuiColor4fNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[41846]: ReplacementCodeuiTexCoord2fVertex3fSUN (dynamic) */
   "ifffff\0"
   "glReplacementCodeuiTexCoord2fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[41895]: ReplacementCodeuiTexCoord2fVertex3fvSUN (dynamic) */
   "ppp\0"
   "glReplacementCodeuiTexCoord2fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[41942]: ReplacementCodeuiTexCoord2fNormal3fVertex3fSUN (dynamic) */
   "iffffffff\0"
   "glReplacementCodeuiTexCoord2fNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[42002]: ReplacementCodeuiTexCoord2fNormal3fVertex3fvSUN (dynamic) */
   "pppp\0"
   "glReplacementCodeuiTexCoord2fNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[42058]: ReplacementCodeuiTexCoord2fColor4fNormal3fVertex3fSUN (dynamic) */
   "iffffffffffff\0"
   "glReplacementCodeuiTexCoord2fColor4fNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[42129]: ReplacementCodeuiTexCoord2fColor4fNormal3fVertex3fvSUN (dynamic) */
   "ppppp\0"
   "glReplacementCodeuiTexCoord2fColor4fNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[42193]: FramebufferSampleLocationsfvARB (will be remapped) */
   "iiip\0"
   "glFramebufferSampleLocationsfvARB\0"
   "glFramebufferSampleLocationsfvNV\0"
   "\0"
   /* _mesa_function_pool[42266]: NamedFramebufferSampleLocationsfvARB (will be remapped) */
   "iiip\0"
   "glNamedFramebufferSampleLocationsfvARB\0"
   "glNamedFramebufferSampleLocationsfvNV\0"
   "\0"
   /* _mesa_function_pool[42349]: EvaluateDepthValuesARB (will be remapped) */
   "\0"
   "glEvaluateDepthValuesARB\0"
   "glResolveDepthValuesNV\0"
   "\0"
   /* _mesa_function_pool[42399]: VertexWeightfEXT (dynamic) */
   "f\0"
   "glVertexWeightfEXT\0"
   "\0"
   /* _mesa_function_pool[42421]: VertexWeightfvEXT (dynamic) */
   "p\0"
   "glVertexWeightfvEXT\0"
   "\0"
   /* _mesa_function_pool[42444]: VertexWeightPointerEXT (dynamic) */
   "iiip\0"
   "glVertexWeightPointerEXT\0"
   "\0"
   /* _mesa_function_pool[42475]: FlushVertexArrayRangeNV (dynamic) */
   "\0"
   "glFlushVertexArrayRangeNV\0"
   "\0"
   /* _mesa_function_pool[42503]: VertexArrayRangeNV (dynamic) */
   "ip\0"
   "glVertexArrayRangeNV\0"
   "\0"
   /* _mesa_function_pool[42528]: CombinerParameterfvNV (dynamic) */
   "ip\0"
   "glCombinerParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[42556]: CombinerParameterfNV (dynamic) */
   "if\0"
   "glCombinerParameterfNV\0"
   "\0"
   /* _mesa_function_pool[42583]: CombinerParameterivNV (dynamic) */
   "ip\0"
   "glCombinerParameterivNV\0"
   "\0"
   /* _mesa_function_pool[42611]: CombinerParameteriNV (dynamic) */
   "ii\0"
   "glCombinerParameteriNV\0"
   "\0"
   /* _mesa_function_pool[42638]: CombinerInputNV (dynamic) */
   "iiiiii\0"
   "glCombinerInputNV\0"
   "\0"
   /* _mesa_function_pool[42664]: CombinerOutputNV (dynamic) */
   "iiiiiiiiii\0"
   "glCombinerOutputNV\0"
   "\0"
   /* _mesa_function_pool[42695]: FinalCombinerInputNV (dynamic) */
   "iiii\0"
   "glFinalCombinerInputNV\0"
   "\0"
   /* _mesa_function_pool[42724]: GetCombinerInputParameterfvNV (dynamic) */
   "iiiip\0"
   "glGetCombinerInputParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[42763]: GetCombinerInputParameterivNV (dynamic) */
   "iiiip\0"
   "glGetCombinerInputParameterivNV\0"
   "\0"
   /* _mesa_function_pool[42802]: GetCombinerOutputParameterfvNV (dynamic) */
   "iiip\0"
   "glGetCombinerOutputParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[42841]: GetCombinerOutputParameterivNV (dynamic) */
   "iiip\0"
   "glGetCombinerOutputParameterivNV\0"
   "\0"
   /* _mesa_function_pool[42880]: GetFinalCombinerInputParameterfvNV (dynamic) */
   "iip\0"
   "glGetFinalCombinerInputParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[42922]: GetFinalCombinerInputParameterivNV (dynamic) */
   "iip\0"
   "glGetFinalCombinerInputParameterivNV\0"
   "\0"
   /* _mesa_function_pool[42964]: ResizeBuffersMESA (will be remapped) */
   "\0"
   "glResizeBuffersMESA\0"
   "\0"
   /* _mesa_function_pool[42986]: WindowPos4dMESA (will be remapped) */
   "dddd\0"
   "glWindowPos4dMESA\0"
   "\0"
   /* _mesa_function_pool[43010]: WindowPos4dvMESA (will be remapped) */
   "p\0"
   "glWindowPos4dvMESA\0"
   "\0"
   /* _mesa_function_pool[43032]: WindowPos4fMESA (will be remapped) */
   "ffff\0"
   "glWindowPos4fMESA\0"
   "\0"
   /* _mesa_function_pool[43056]: WindowPos4fvMESA (will be remapped) */
   "p\0"
   "glWindowPos4fvMESA\0"
   "\0"
   /* _mesa_function_pool[43078]: WindowPos4iMESA (will be remapped) */
   "iiii\0"
   "glWindowPos4iMESA\0"
   "\0"
   /* _mesa_function_pool[43102]: WindowPos4ivMESA (will be remapped) */
   "p\0"
   "glWindowPos4ivMESA\0"
   "\0"
   /* _mesa_function_pool[43124]: WindowPos4sMESA (will be remapped) */
   "iiii\0"
   "glWindowPos4sMESA\0"
   "\0"
   /* _mesa_function_pool[43148]: WindowPos4svMESA (will be remapped) */
   "p\0"
   "glWindowPos4svMESA\0"
   "\0"
   /* _mesa_function_pool[43170]: MultiModeDrawArraysIBM (will be remapped) */
   "pppii\0"
   "glMultiModeDrawArraysIBM\0"
   "\0"
   /* _mesa_function_pool[43202]: MultiModeDrawElementsIBM (will be remapped) */
   "ppipii\0"
   "glMultiModeDrawElementsIBM\0"
   "\0"
   /* _mesa_function_pool[43237]: ColorPointerListIBM (dynamic) */
   "iiipi\0"
   "glColorPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[43266]: SecondaryColorPointerListIBM (dynamic) */
   "iiipi\0"
   "glSecondaryColorPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[43304]: EdgeFlagPointerListIBM (dynamic) */
   "ipi\0"
   "glEdgeFlagPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[43334]: FogCoordPointerListIBM (dynamic) */
   "iipi\0"
   "glFogCoordPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[43365]: IndexPointerListIBM (dynamic) */
   "iipi\0"
   "glIndexPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[43393]: NormalPointerListIBM (dynamic) */
   "iipi\0"
   "glNormalPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[43422]: TexCoordPointerListIBM (dynamic) */
   "iiipi\0"
   "glTexCoordPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[43454]: VertexPointerListIBM (dynamic) */
   "iiipi\0"
   "glVertexPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[43484]: TbufferMask3DFX (dynamic) */
   "i\0"
   "glTbufferMask3DFX\0"
   "\0"
   /* _mesa_function_pool[43505]: TextureColorMaskSGIS (dynamic) */
   "iiii\0"
   "glTextureColorMaskSGIS\0"
   "\0"
   /* _mesa_function_pool[43534]: DeleteFencesNV (dynamic) */
   "ip\0"
   "glDeleteFencesNV\0"
   "\0"
   /* _mesa_function_pool[43555]: GenFencesNV (dynamic) */
   "ip\0"
   "glGenFencesNV\0"
   "\0"
   /* _mesa_function_pool[43573]: IsFenceNV (dynamic) */
   "i\0"
   "glIsFenceNV\0"
   "\0"
   /* _mesa_function_pool[43588]: TestFenceNV (dynamic) */
   "i\0"
   "glTestFenceNV\0"
   "\0"
   /* _mesa_function_pool[43605]: GetFenceivNV (dynamic) */
   "iip\0"
   "glGetFenceivNV\0"
   "\0"
   /* _mesa_function_pool[43625]: FinishFenceNV (dynamic) */
   "i\0"
   "glFinishFenceNV\0"
   "\0"
   /* _mesa_function_pool[43644]: SetFenceNV (dynamic) */
   "ii\0"
   "glSetFenceNV\0"
   "\0"
   /* _mesa_function_pool[43661]: MapControlPointsNV (dynamic) */
   "iiiiiiiip\0"
   "glMapControlPointsNV\0"
   "\0"
   /* _mesa_function_pool[43693]: MapParameterivNV (dynamic) */
   "iip\0"
   "glMapParameterivNV\0"
   "\0"
   /* _mesa_function_pool[43717]: MapParameterfvNV (dynamic) */
   "iip\0"
   "glMapParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[43741]: GetMapControlPointsNV (dynamic) */
   "iiiiiip\0"
   "glGetMapControlPointsNV\0"
   "\0"
   /* _mesa_function_pool[43774]: GetMapParameterivNV (dynamic) */
   "iip\0"
   "glGetMapParameterivNV\0"
   "\0"
   /* _mesa_function_pool[43801]: GetMapParameterfvNV (dynamic) */
   "iip\0"
   "glGetMapParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[43828]: GetMapAttribParameterivNV (dynamic) */
   "iiip\0"
   "glGetMapAttribParameterivNV\0"
   "\0"
   /* _mesa_function_pool[43862]: GetMapAttribParameterfvNV (dynamic) */
   "iiip\0"
   "glGetMapAttribParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[43896]: EvalMapsNV (dynamic) */
   "ii\0"
   "glEvalMapsNV\0"
   "\0"
   /* _mesa_function_pool[43913]: CombinerStageParameterfvNV (dynamic) */
   "iip\0"
   "glCombinerStageParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[43947]: GetCombinerStageParameterfvNV (dynamic) */
   "iip\0"
   "glGetCombinerStageParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[43984]: AreProgramsResidentNV (will be remapped) */
   "ipp\0"
   "glAreProgramsResidentNV\0"
   "\0"
   /* _mesa_function_pool[44013]: ExecuteProgramNV (will be remapped) */
   "iip\0"
   "glExecuteProgramNV\0"
   "\0"
   /* _mesa_function_pool[44037]: GetProgramParameterdvNV (will be remapped) */
   "iiip\0"
   "glGetProgramParameterdvNV\0"
   "\0"
   /* _mesa_function_pool[44069]: GetProgramParameterfvNV (will be remapped) */
   "iiip\0"
   "glGetProgramParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[44101]: GetProgramivNV (will be remapped) */
   "iip\0"
   "glGetProgramivNV\0"
   "\0"
   /* _mesa_function_pool[44123]: GetProgramStringNV (will be remapped) */
   "iip\0"
   "glGetProgramStringNV\0"
   "\0"
   /* _mesa_function_pool[44149]: GetTrackMatrixivNV (will be remapped) */
   "iiip\0"
   "glGetTrackMatrixivNV\0"
   "\0"
   /* _mesa_function_pool[44176]: GetVertexAttribdvNV (will be remapped) */
   "iip\0"
   "glGetVertexAttribdvNV\0"
   "\0"
   /* _mesa_function_pool[44203]: GetVertexAttribfvNV (will be remapped) */
   "iip\0"
   "glGetVertexAttribfvNV\0"
   "\0"
   /* _mesa_function_pool[44230]: GetVertexAttribivNV (will be remapped) */
   "iip\0"
   "glGetVertexAttribivNV\0"
   "\0"
   /* _mesa_function_pool[44257]: LoadProgramNV (will be remapped) */
   "iiip\0"
   "glLoadProgramNV\0"
   "\0"
   /* _mesa_function_pool[44279]: ProgramParameters4dvNV (will be remapped) */
   "iiip\0"
   "glProgramParameters4dvNV\0"
   "\0"
   /* _mesa_function_pool[44310]: ProgramParameters4fvNV (will be remapped) */
   "iiip\0"
   "glProgramParameters4fvNV\0"
   "\0"
   /* _mesa_function_pool[44341]: RequestResidentProgramsNV (will be remapped) */
   "ip\0"
   "glRequestResidentProgramsNV\0"
   "\0"
   /* _mesa_function_pool[44373]: TrackMatrixNV (will be remapped) */
   "iiii\0"
   "glTrackMatrixNV\0"
   "\0"
   /* _mesa_function_pool[44395]: VertexAttribPointerNV (will be remapped) */
   "iiiip\0"
   "glVertexAttribPointerNV\0"
   "\0"
   /* _mesa_function_pool[44426]: VertexAttrib1sNV (will be remapped) */
   "ii\0"
   "glVertexAttrib1sNV\0"
   "\0"
   /* _mesa_function_pool[44449]: VertexAttrib1svNV (will be remapped) */
   "ip\0"
   "glVertexAttrib1svNV\0"
   "\0"
   /* _mesa_function_pool[44473]: VertexAttrib2sNV (will be remapped) */
   "iii\0"
   "glVertexAttrib2sNV\0"
   "\0"
   /* _mesa_function_pool[44497]: VertexAttrib2svNV (will be remapped) */
   "ip\0"
   "glVertexAttrib2svNV\0"
   "\0"
   /* _mesa_function_pool[44521]: VertexAttrib3sNV (will be remapped) */
   "iiii\0"
   "glVertexAttrib3sNV\0"
   "\0"
   /* _mesa_function_pool[44546]: VertexAttrib3svNV (will be remapped) */
   "ip\0"
   "glVertexAttrib3svNV\0"
   "\0"
   /* _mesa_function_pool[44570]: VertexAttrib4sNV (will be remapped) */
   "iiiii\0"
   "glVertexAttrib4sNV\0"
   "\0"
   /* _mesa_function_pool[44596]: VertexAttrib4svNV (will be remapped) */
   "ip\0"
   "glVertexAttrib4svNV\0"
   "\0"
   /* _mesa_function_pool[44620]: VertexAttrib1fNV (will be remapped) */
   "if\0"
   "glVertexAttrib1fNV\0"
   "\0"
   /* _mesa_function_pool[44643]: VertexAttrib1fvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib1fvNV\0"
   "\0"
   /* _mesa_function_pool[44667]: VertexAttrib2fNV (will be remapped) */
   "iff\0"
   "glVertexAttrib2fNV\0"
   "\0"
   /* _mesa_function_pool[44691]: VertexAttrib2fvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib2fvNV\0"
   "\0"
   /* _mesa_function_pool[44715]: VertexAttrib3fNV (will be remapped) */
   "ifff\0"
   "glVertexAttrib3fNV\0"
   "\0"
   /* _mesa_function_pool[44740]: VertexAttrib3fvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib3fvNV\0"
   "\0"
   /* _mesa_function_pool[44764]: VertexAttrib4fNV (will be remapped) */
   "iffff\0"
   "glVertexAttrib4fNV\0"
   "\0"
   /* _mesa_function_pool[44790]: VertexAttrib4fvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib4fvNV\0"
   "\0"
   /* _mesa_function_pool[44814]: VertexAttrib1dNV (will be remapped) */
   "id\0"
   "glVertexAttrib1dNV\0"
   "\0"
   /* _mesa_function_pool[44837]: VertexAttrib1dvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib1dvNV\0"
   "\0"
   /* _mesa_function_pool[44861]: VertexAttrib2dNV (will be remapped) */
   "idd\0"
   "glVertexAttrib2dNV\0"
   "\0"
   /* _mesa_function_pool[44885]: VertexAttrib2dvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib2dvNV\0"
   "\0"
   /* _mesa_function_pool[44909]: VertexAttrib3dNV (will be remapped) */
   "iddd\0"
   "glVertexAttrib3dNV\0"
   "\0"
   /* _mesa_function_pool[44934]: VertexAttrib3dvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib3dvNV\0"
   "\0"
   /* _mesa_function_pool[44958]: VertexAttrib4dNV (will be remapped) */
   "idddd\0"
   "glVertexAttrib4dNV\0"
   "\0"
   /* _mesa_function_pool[44984]: VertexAttrib4dvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib4dvNV\0"
   "\0"
   /* _mesa_function_pool[45008]: VertexAttrib4ubNV (will be remapped) */
   "iiiii\0"
   "glVertexAttrib4ubNV\0"
   "\0"
   /* _mesa_function_pool[45035]: VertexAttrib4ubvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib4ubvNV\0"
   "\0"
   /* _mesa_function_pool[45060]: VertexAttribs1svNV (will be remapped) */
   "iip\0"
   "glVertexAttribs1svNV\0"
   "\0"
   /* _mesa_function_pool[45086]: VertexAttribs2svNV (will be remapped) */
   "iip\0"
   "glVertexAttribs2svNV\0"
   "\0"
   /* _mesa_function_pool[45112]: VertexAttribs3svNV (will be remapped) */
   "iip\0"
   "glVertexAttribs3svNV\0"
   "\0"
   /* _mesa_function_pool[45138]: VertexAttribs4svNV (will be remapped) */
   "iip\0"
   "glVertexAttribs4svNV\0"
   "\0"
   /* _mesa_function_pool[45164]: VertexAttribs1fvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs1fvNV\0"
   "\0"
   /* _mesa_function_pool[45190]: VertexAttribs2fvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs2fvNV\0"
   "\0"
   /* _mesa_function_pool[45216]: VertexAttribs3fvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs3fvNV\0"
   "\0"
   /* _mesa_function_pool[45242]: VertexAttribs4fvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs4fvNV\0"
   "\0"
   /* _mesa_function_pool[45268]: VertexAttribs1dvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs1dvNV\0"
   "\0"
   /* _mesa_function_pool[45294]: VertexAttribs2dvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs2dvNV\0"
   "\0"
   /* _mesa_function_pool[45320]: VertexAttribs3dvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs3dvNV\0"
   "\0"
   /* _mesa_function_pool[45346]: VertexAttribs4dvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs4dvNV\0"
   "\0"
   /* _mesa_function_pool[45372]: VertexAttribs4ubvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs4ubvNV\0"
   "\0"
   /* _mesa_function_pool[45399]: TexBumpParameterfvATI (will be remapped) */
   "ip\0"
   "glTexBumpParameterfvATI\0"
   "\0"
   /* _mesa_function_pool[45427]: TexBumpParameterivATI (will be remapped) */
   "ip\0"
   "glTexBumpParameterivATI\0"
   "\0"
   /* _mesa_function_pool[45455]: GetTexBumpParameterfvATI (will be remapped) */
   "ip\0"
   "glGetTexBumpParameterfvATI\0"
   "\0"
   /* _mesa_function_pool[45486]: GetTexBumpParameterivATI (will be remapped) */
   "ip\0"
   "glGetTexBumpParameterivATI\0"
   "\0"
   /* _mesa_function_pool[45517]: GenFragmentShadersATI (will be remapped) */
   "i\0"
   "glGenFragmentShadersATI\0"
   "\0"
   /* _mesa_function_pool[45544]: BindFragmentShaderATI (will be remapped) */
   "i\0"
   "glBindFragmentShaderATI\0"
   "\0"
   /* _mesa_function_pool[45571]: DeleteFragmentShaderATI (will be remapped) */
   "i\0"
   "glDeleteFragmentShaderATI\0"
   "\0"
   /* _mesa_function_pool[45600]: BeginFragmentShaderATI (will be remapped) */
   "\0"
   "glBeginFragmentShaderATI\0"
   "\0"
   /* _mesa_function_pool[45627]: EndFragmentShaderATI (will be remapped) */
   "\0"
   "glEndFragmentShaderATI\0"
   "\0"
   /* _mesa_function_pool[45652]: PassTexCoordATI (will be remapped) */
   "iii\0"
   "glPassTexCoordATI\0"
   "\0"
   /* _mesa_function_pool[45675]: SampleMapATI (will be remapped) */
   "iii\0"
   "glSampleMapATI\0"
   "\0"
   /* _mesa_function_pool[45695]: ColorFragmentOp1ATI (will be remapped) */
   "iiiiiii\0"
   "glColorFragmentOp1ATI\0"
   "\0"
   /* _mesa_function_pool[45726]: ColorFragmentOp2ATI (will be remapped) */
   "iiiiiiiiii\0"
   "glColorFragmentOp2ATI\0"
   "\0"
   /* _mesa_function_pool[45760]: ColorFragmentOp3ATI (will be remapped) */
   "iiiiiiiiiiiii\0"
   "glColorFragmentOp3ATI\0"
   "\0"
   /* _mesa_function_pool[45797]: AlphaFragmentOp1ATI (will be remapped) */
   "iiiiii\0"
   "glAlphaFragmentOp1ATI\0"
   "\0"
   /* _mesa_function_pool[45827]: AlphaFragmentOp2ATI (will be remapped) */
   "iiiiiiiii\0"
   "glAlphaFragmentOp2ATI\0"
   "\0"
   /* _mesa_function_pool[45860]: AlphaFragmentOp3ATI (will be remapped) */
   "iiiiiiiiiiii\0"
   "glAlphaFragmentOp3ATI\0"
   "\0"
   /* _mesa_function_pool[45896]: SetFragmentShaderConstantATI (will be remapped) */
   "ip\0"
   "glSetFragmentShaderConstantATI\0"
   "\0"
   /* _mesa_function_pool[45931]: DrawMeshArraysSUN (dynamic) */
   "iiii\0"
   "glDrawMeshArraysSUN\0"
   "\0"
   /* _mesa_function_pool[45957]: ActiveStencilFaceEXT (will be remapped) */
   "i\0"
   "glActiveStencilFaceEXT\0"
   "\0"
   /* _mesa_function_pool[45983]: ObjectPurgeableAPPLE (will be remapped) */
   "iii\0"
   "glObjectPurgeableAPPLE\0"
   "\0"
   /* _mesa_function_pool[46011]: ObjectUnpurgeableAPPLE (will be remapped) */
   "iii\0"
   "glObjectUnpurgeableAPPLE\0"
   "\0"
   /* _mesa_function_pool[46041]: GetObjectParameterivAPPLE (will be remapped) */
   "iiip\0"
   "glGetObjectParameterivAPPLE\0"
   "\0"
   /* _mesa_function_pool[46075]: BindVertexArrayAPPLE (dynamic) */
   "i\0"
   "glBindVertexArrayAPPLE\0"
   "\0"
   /* _mesa_function_pool[46101]: DeleteVertexArraysAPPLE (dynamic) */
   "ip\0"
   "glDeleteVertexArraysAPPLE\0"
   "\0"
   /* _mesa_function_pool[46131]: GenVertexArraysAPPLE (dynamic) */
   "ip\0"
   "glGenVertexArraysAPPLE\0"
   "\0"
   /* _mesa_function_pool[46158]: IsVertexArrayAPPLE (dynamic) */
   "i\0"
   "glIsVertexArrayAPPLE\0"
   "\0"
   /* _mesa_function_pool[46182]: ProgramNamedParameter4fNV (will be remapped) */
   "iipffff\0"
   "glProgramNamedParameter4fNV\0"
   "\0"
   /* _mesa_function_pool[46219]: ProgramNamedParameter4dNV (will be remapped) */
   "iipdddd\0"
   "glProgramNamedParameter4dNV\0"
   "\0"
   /* _mesa_function_pool[46256]: ProgramNamedParameter4fvNV (will be remapped) */
   "iipp\0"
   "glProgramNamedParameter4fvNV\0"
   "\0"
   /* _mesa_function_pool[46291]: ProgramNamedParameter4dvNV (will be remapped) */
   "iipp\0"
   "glProgramNamedParameter4dvNV\0"
   "\0"
   /* _mesa_function_pool[46326]: GetProgramNamedParameterfvNV (will be remapped) */
   "iipp\0"
   "glGetProgramNamedParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[46363]: GetProgramNamedParameterdvNV (will be remapped) */
   "iipp\0"
   "glGetProgramNamedParameterdvNV\0"
   "\0"
   /* _mesa_function_pool[46400]: DepthBoundsEXT (will be remapped) */
   "dd\0"
   "glDepthBoundsEXT\0"
   "\0"
   /* _mesa_function_pool[46421]: BindRenderbufferEXT (will be remapped) */
   "ii\0"
   "glBindRenderbufferEXT\0"
   "\0"
   /* _mesa_function_pool[46447]: BindFramebufferEXT (will be remapped) */
   "ii\0"
   "glBindFramebufferEXT\0"
   "\0"
   /* _mesa_function_pool[46472]: StringMarkerGREMEDY (will be remapped) */
   "ip\0"
   "glStringMarkerGREMEDY\0"
   "\0"
   /* _mesa_function_pool[46498]: ProvokingVertex (will be remapped) */
   "i\0"
   "glProvokingVertexEXT\0"
   "glProvokingVertex\0"
   "\0"
   /* _mesa_function_pool[46540]: ColorMaski (will be remapped) */
   "iiiii\0"
   "glColorMaskIndexedEXT\0"
   "glColorMaski\0"
   "glColorMaskiEXT\0"
   "glColorMaskiOES\0"
   "\0"
   /* _mesa_function_pool[46614]: GetBooleani_v (will be remapped) */
   "iip\0"
   "glGetBooleanIndexedvEXT\0"
   "glGetBooleani_v\0"
   "\0"
   /* _mesa_function_pool[46659]: GetIntegeri_v (will be remapped) */
   "iip\0"
   "glGetIntegerIndexedvEXT\0"
   "glGetIntegeri_v\0"
   "\0"
   /* _mesa_function_pool[46704]: Enablei (will be remapped) */
   "ii\0"
   "glEnableIndexedEXT\0"
   "glEnablei\0"
   "glEnableiEXT\0"
   "glEnableiOES\0"
   "\0"
   /* _mesa_function_pool[46763]: Disablei (will be remapped) */
   "ii\0"
   "glDisableIndexedEXT\0"
   "glDisablei\0"
   "glDisableiEXT\0"
   "glDisableiOES\0"
   "\0"
   /* _mesa_function_pool[46826]: IsEnabledi (will be remapped) */
   "ii\0"
   "glIsEnabledIndexedEXT\0"
   "glIsEnabledi\0"
   "glIsEnablediEXT\0"
   "glIsEnablediOES\0"
   "\0"
   /* _mesa_function_pool[46897]: BufferParameteriAPPLE (will be remapped) */
   "iii\0"
   "glBufferParameteriAPPLE\0"
   "\0"
   /* _mesa_function_pool[46926]: FlushMappedBufferRangeAPPLE (will be remapped) */
   "iii\0"
   "glFlushMappedBufferRangeAPPLE\0"
   "\0"
   /* _mesa_function_pool[46961]: GetPerfMonitorGroupsAMD (will be remapped) */
   "pip\0"
   "glGetPerfMonitorGroupsAMD\0"
   "\0"
   /* _mesa_function_pool[46992]: GetPerfMonitorCountersAMD (will be remapped) */
   "ippip\0"
   "glGetPerfMonitorCountersAMD\0"
   "\0"
   /* _mesa_function_pool[47027]: GetPerfMonitorGroupStringAMD (will be remapped) */
   "iipp\0"
   "glGetPerfMonitorGroupStringAMD\0"
   "\0"
   /* _mesa_function_pool[47064]: GetPerfMonitorCounterStringAMD (will be remapped) */
   "iiipp\0"
   "glGetPerfMonitorCounterStringAMD\0"
   "\0"
   /* _mesa_function_pool[47104]: GetPerfMonitorCounterInfoAMD (will be remapped) */
   "iiip\0"
   "glGetPerfMonitorCounterInfoAMD\0"
   "\0"
   /* _mesa_function_pool[47141]: GenPerfMonitorsAMD (will be remapped) */
   "ip\0"
   "glGenPerfMonitorsAMD\0"
   "\0"
   /* _mesa_function_pool[47166]: DeletePerfMonitorsAMD (will be remapped) */
   "ip\0"
   "glDeletePerfMonitorsAMD\0"
   "\0"
   /* _mesa_function_pool[47194]: SelectPerfMonitorCountersAMD (will be remapped) */
   "iiiip\0"
   "glSelectPerfMonitorCountersAMD\0"
   "\0"
   /* _mesa_function_pool[47232]: BeginPerfMonitorAMD (will be remapped) */
   "i\0"
   "glBeginPerfMonitorAMD\0"
   "\0"
   /* _mesa_function_pool[47257]: EndPerfMonitorAMD (will be remapped) */
   "i\0"
   "glEndPerfMonitorAMD\0"
   "\0"
   /* _mesa_function_pool[47280]: GetPerfMonitorCounterDataAMD (will be remapped) */
   "iiipp\0"
   "glGetPerfMonitorCounterDataAMD\0"
   "\0"
   /* _mesa_function_pool[47318]: TextureRangeAPPLE (dynamic) */
   "iip\0"
   "glTextureRangeAPPLE\0"
   "\0"
   /* _mesa_function_pool[47343]: GetTexParameterPointervAPPLE (dynamic) */
   "iip\0"
   "glGetTexParameterPointervAPPLE\0"
   "\0"
   /* _mesa_function_pool[47379]: UseShaderProgramEXT (will be remapped) */
   "ii\0"
   "glUseShaderProgramEXT\0"
   "\0"
   /* _mesa_function_pool[47405]: ActiveProgramEXT (will be remapped) */
   "i\0"
   "glActiveProgramEXT\0"
   "\0"
   /* _mesa_function_pool[47427]: CreateShaderProgramEXT (will be remapped) */
   "ip\0"
   "glCreateShaderProgramEXT\0"
   "\0"
   /* _mesa_function_pool[47456]: SubpixelPrecisionBiasNV (will be remapped) */
   "ii\0"
   "glSubpixelPrecisionBiasNV\0"
   "\0"
   /* _mesa_function_pool[47486]: ConservativeRasterParameterfNV (will be remapped) */
   "if\0"
   "glConservativeRasterParameterfNV\0"
   "\0"
   /* _mesa_function_pool[47523]: ConservativeRasterParameteriNV (will be remapped) */
   "ii\0"
   "glConservativeRasterParameteriNV\0"
   "\0"
   /* _mesa_function_pool[47560]: GetFirstPerfQueryIdINTEL (will be remapped) */
   "p\0"
   "glGetFirstPerfQueryIdINTEL\0"
   "\0"
   /* _mesa_function_pool[47590]: GetNextPerfQueryIdINTEL (will be remapped) */
   "ip\0"
   "glGetNextPerfQueryIdINTEL\0"
   "\0"
   /* _mesa_function_pool[47620]: GetPerfQueryIdByNameINTEL (will be remapped) */
   "pp\0"
   "glGetPerfQueryIdByNameINTEL\0"
   "\0"
   /* _mesa_function_pool[47652]: GetPerfQueryInfoINTEL (will be remapped) */
   "iippppp\0"
   "glGetPerfQueryInfoINTEL\0"
   "\0"
   /* _mesa_function_pool[47685]: GetPerfCounterInfoINTEL (will be remapped) */
   "iiipipppppp\0"
   "glGetPerfCounterInfoINTEL\0"
   "\0"
   /* _mesa_function_pool[47724]: CreatePerfQueryINTEL (will be remapped) */
   "ip\0"
   "glCreatePerfQueryINTEL\0"
   "\0"
   /* _mesa_function_pool[47751]: DeletePerfQueryINTEL (will be remapped) */
   "i\0"
   "glDeletePerfQueryINTEL\0"
   "\0"
   /* _mesa_function_pool[47777]: BeginPerfQueryINTEL (will be remapped) */
   "i\0"
   "glBeginPerfQueryINTEL\0"
   "\0"
   /* _mesa_function_pool[47802]: EndPerfQueryINTEL (will be remapped) */
   "i\0"
   "glEndPerfQueryINTEL\0"
   "\0"
   /* _mesa_function_pool[47825]: GetPerfQueryDataINTEL (will be remapped) */
   "iiipp\0"
   "glGetPerfQueryDataINTEL\0"
   "\0"
   /* _mesa_function_pool[47856]: PolygonOffsetClampEXT (will be remapped) */
   "fff\0"
   "glPolygonOffsetClampEXT\0"
   "glPolygonOffsetClamp\0"
   "\0"
   /* _mesa_function_pool[47906]: WindowRectanglesEXT (will be remapped) */
   "iip\0"
   "glWindowRectanglesEXT\0"
   "\0"
   /* _mesa_function_pool[47933]: FramebufferFetchBarrierEXT (will be remapped) */
   "\0"
   "glFramebufferFetchBarrierEXT\0"
   "\0"
   /* _mesa_function_pool[47964]: StencilFuncSeparateATI (will be remapped) */
   "iiii\0"
   "glStencilFuncSeparateATI\0"
   "\0"
   /* _mesa_function_pool[47995]: ProgramEnvParameters4fvEXT (will be remapped) */
   "iiip\0"
   "glProgramEnvParameters4fvEXT\0"
   "\0"
   /* _mesa_function_pool[48030]: ProgramLocalParameters4fvEXT (will be remapped) */
   "iiip\0"
   "glProgramLocalParameters4fvEXT\0"
   "\0"
   /* _mesa_function_pool[48067]: IglooInterfaceSGIX (dynamic) */
   "ip\0"
   "glIglooInterfaceSGIX\0"
   "\0"
   /* _mesa_function_pool[48092]: DeformationMap3dSGIX (dynamic) */
   "iddiiddiiddiip\0"
   "glDeformationMap3dSGIX\0"
   "\0"
   /* _mesa_function_pool[48131]: DeformationMap3fSGIX (dynamic) */
   "iffiiffiiffiip\0"
   "glDeformationMap3fSGIX\0"
   "\0"
   /* _mesa_function_pool[48170]: DeformSGIX (dynamic) */
   "i\0"
   "glDeformSGIX\0"
   "\0"
   /* _mesa_function_pool[48186]: LoadIdentityDeformationMapSGIX (dynamic) */
   "i\0"
   "glLoadIdentityDeformationMapSGIX\0"
   "\0"
   /* _mesa_function_pool[48222]: EGLImageTargetTexture2DOES (will be remapped) */
   "ip\0"
   "glEGLImageTargetTexture2DOES\0"
   "\0"
   /* _mesa_function_pool[48255]: EGLImageTargetRenderbufferStorageOES (will be remapped) */
   "ip\0"
   "glEGLImageTargetRenderbufferStorageOES\0"
   "\0"
   /* _mesa_function_pool[48298]: ClearColorIiEXT (will be remapped) */
   "iiii\0"
   "glClearColorIiEXT\0"
   "\0"
   /* _mesa_function_pool[48322]: ClearColorIuiEXT (will be remapped) */
   "iiii\0"
   "glClearColorIuiEXT\0"
   "\0"
   /* _mesa_function_pool[48347]: TexParameterIiv (will be remapped) */
   "iip\0"
   "glTexParameterIivEXT\0"
   "glTexParameterIiv\0"
   "glTexParameterIivOES\0"
   "\0"
   /* _mesa_function_pool[48412]: TexParameterIuiv (will be remapped) */
   "iip\0"
   "glTexParameterIuivEXT\0"
   "glTexParameterIuiv\0"
   "glTexParameterIuivOES\0"
   "\0"
   /* _mesa_function_pool[48480]: GetTexParameterIiv (will be remapped) */
   "iip\0"
   "glGetTexParameterIivEXT\0"
   "glGetTexParameterIiv\0"
   "glGetTexParameterIivOES\0"
   "\0"
   /* _mesa_function_pool[48554]: GetTexParameterIuiv (will be remapped) */
   "iip\0"
   "glGetTexParameterIuivEXT\0"
   "glGetTexParameterIuiv\0"
   "glGetTexParameterIuivOES\0"
   "\0"
   /* _mesa_function_pool[48631]: VertexAttribI1iEXT (will be remapped) */
   "ii\0"
   "glVertexAttribI1iEXT\0"
   "glVertexAttribI1i\0"
   "\0"
   /* _mesa_function_pool[48674]: VertexAttribI2iEXT (will be remapped) */
   "iii\0"
   "glVertexAttribI2iEXT\0"
   "glVertexAttribI2i\0"
   "\0"
   /* _mesa_function_pool[48718]: VertexAttribI3iEXT (will be remapped) */
   "iiii\0"
   "glVertexAttribI3iEXT\0"
   "glVertexAttribI3i\0"
   "\0"
   /* _mesa_function_pool[48763]: VertexAttribI4iEXT (will be remapped) */
   "iiiii\0"
   "glVertexAttribI4iEXT\0"
   "glVertexAttribI4i\0"
   "\0"
   /* _mesa_function_pool[48809]: VertexAttribI1uiEXT (will be remapped) */
   "ii\0"
   "glVertexAttribI1uiEXT\0"
   "glVertexAttribI1ui\0"
   "\0"
   /* _mesa_function_pool[48854]: VertexAttribI2uiEXT (will be remapped) */
   "iii\0"
   "glVertexAttribI2uiEXT\0"
   "glVertexAttribI2ui\0"
   "\0"
   /* _mesa_function_pool[48900]: VertexAttribI3uiEXT (will be remapped) */
   "iiii\0"
   "glVertexAttribI3uiEXT\0"
   "glVertexAttribI3ui\0"
   "\0"
   /* _mesa_function_pool[48947]: VertexAttribI4uiEXT (will be remapped) */
   "iiiii\0"
   "glVertexAttribI4uiEXT\0"
   "glVertexAttribI4ui\0"
   "\0"
   /* _mesa_function_pool[48995]: VertexAttribI1iv (will be remapped) */
   "ip\0"
   "glVertexAttribI1ivEXT\0"
   "glVertexAttribI1iv\0"
   "\0"
   /* _mesa_function_pool[49040]: VertexAttribI2ivEXT (will be remapped) */
   "ip\0"
   "glVertexAttribI2ivEXT\0"
   "glVertexAttribI2iv\0"
   "\0"
   /* _mesa_function_pool[49085]: VertexAttribI3ivEXT (will be remapped) */
   "ip\0"
   "glVertexAttribI3ivEXT\0"
   "glVertexAttribI3iv\0"
   "\0"
   /* _mesa_function_pool[49130]: VertexAttribI4ivEXT (will be remapped) */
   "ip\0"
   "glVertexAttribI4ivEXT\0"
   "glVertexAttribI4iv\0"
   "\0"
   /* _mesa_function_pool[49175]: VertexAttribI1uiv (will be remapped) */
   "ip\0"
   "glVertexAttribI1uivEXT\0"
   "glVertexAttribI1uiv\0"
   "\0"
   /* _mesa_function_pool[49222]: VertexAttribI2uivEXT (will be remapped) */
   "ip\0"
   "glVertexAttribI2uivEXT\0"
   "glVertexAttribI2uiv\0"
   "\0"
   /* _mesa_function_pool[49269]: VertexAttribI3uivEXT (will be remapped) */
   "ip\0"
   "glVertexAttribI3uivEXT\0"
   "glVertexAttribI3uiv\0"
   "\0"
   /* _mesa_function_pool[49316]: VertexAttribI4uivEXT (will be remapped) */
   "ip\0"
   "glVertexAttribI4uivEXT\0"
   "glVertexAttribI4uiv\0"
   "\0"
   /* _mesa_function_pool[49363]: VertexAttribI4bv (will be remapped) */
   "ip\0"
   "glVertexAttribI4bvEXT\0"
   "glVertexAttribI4bv\0"
   "\0"
   /* _mesa_function_pool[49408]: VertexAttribI4sv (will be remapped) */
   "ip\0"
   "glVertexAttribI4svEXT\0"
   "glVertexAttribI4sv\0"
   "\0"
   /* _mesa_function_pool[49453]: VertexAttribI4ubv (will be remapped) */
   "ip\0"
   "glVertexAttribI4ubvEXT\0"
   "glVertexAttribI4ubv\0"
   "\0"
   /* _mesa_function_pool[49500]: VertexAttribI4usv (will be remapped) */
   "ip\0"
   "glVertexAttribI4usvEXT\0"
   "glVertexAttribI4usv\0"
   "\0"
   /* _mesa_function_pool[49547]: VertexAttribIPointer (will be remapped) */
   "iiiip\0"
   "glVertexAttribIPointerEXT\0"
   "glVertexAttribIPointer\0"
   "\0"
   /* _mesa_function_pool[49603]: GetVertexAttribIiv (will be remapped) */
   "iip\0"
   "glGetVertexAttribIivEXT\0"
   "glGetVertexAttribIiv\0"
   "\0"
   /* _mesa_function_pool[49653]: GetVertexAttribIuiv (will be remapped) */
   "iip\0"
   "glGetVertexAttribIuivEXT\0"
   "glGetVertexAttribIuiv\0"
   "\0"
   /* _mesa_function_pool[49705]: Uniform1ui (will be remapped) */
   "ii\0"
   "glUniform1uiEXT\0"
   "glUniform1ui\0"
   "\0"
   /* _mesa_function_pool[49738]: Uniform2ui (will be remapped) */
   "iii\0"
   "glUniform2uiEXT\0"
   "glUniform2ui\0"
   "\0"
   /* _mesa_function_pool[49772]: Uniform3ui (will be remapped) */
   "iiii\0"
   "glUniform3uiEXT\0"
   "glUniform3ui\0"
   "\0"
   /* _mesa_function_pool[49807]: Uniform4ui (will be remapped) */
   "iiiii\0"
   "glUniform4uiEXT\0"
   "glUniform4ui\0"
   "\0"
   /* _mesa_function_pool[49843]: Uniform1uiv (will be remapped) */
   "iip\0"
   "glUniform1uivEXT\0"
   "glUniform1uiv\0"
   "\0"
   /* _mesa_function_pool[49879]: Uniform2uiv (will be remapped) */
   "iip\0"
   "glUniform2uivEXT\0"
   "glUniform2uiv\0"
   "\0"
   /* _mesa_function_pool[49915]: Uniform3uiv (will be remapped) */
   "iip\0"
   "glUniform3uivEXT\0"
   "glUniform3uiv\0"
   "\0"
   /* _mesa_function_pool[49951]: Uniform4uiv (will be remapped) */
   "iip\0"
   "glUniform4uivEXT\0"
   "glUniform4uiv\0"
   "\0"
   /* _mesa_function_pool[49987]: GetUniformuiv (will be remapped) */
   "iip\0"
   "glGetUniformuivEXT\0"
   "glGetUniformuiv\0"
   "\0"
   /* _mesa_function_pool[50027]: BindFragDataLocation (will be remapped) */
   "iip\0"
   "glBindFragDataLocationEXT\0"
   "glBindFragDataLocation\0"
   "\0"
   /* _mesa_function_pool[50081]: GetFragDataLocation (will be remapped) */
   "ip\0"
   "glGetFragDataLocationEXT\0"
   "glGetFragDataLocation\0"
   "\0"
   /* _mesa_function_pool[50132]: ClearBufferiv (will be remapped) */
   "iip\0"
   "glClearBufferiv\0"
   "\0"
   /* _mesa_function_pool[50153]: ClearBufferuiv (will be remapped) */
   "iip\0"
   "glClearBufferuiv\0"
   "\0"
   /* _mesa_function_pool[50175]: ClearBufferfv (will be remapped) */
   "iip\0"
   "glClearBufferfv\0"
   "\0"
   /* _mesa_function_pool[50196]: ClearBufferfi (will be remapped) */
   "iifi\0"
   "glClearBufferfi\0"
   "\0"
   /* _mesa_function_pool[50218]: GetStringi (will be remapped) */
   "ii\0"
   "glGetStringi\0"
   "\0"
   /* _mesa_function_pool[50235]: BeginTransformFeedback (will be remapped) */
   "i\0"
   "glBeginTransformFeedback\0"
   "glBeginTransformFeedbackEXT\0"
   "\0"
   /* _mesa_function_pool[50291]: EndTransformFeedback (will be remapped) */
   "\0"
   "glEndTransformFeedback\0"
   "glEndTransformFeedbackEXT\0"
   "\0"
   /* _mesa_function_pool[50342]: BindBufferRange (will be remapped) */
   "iiiii\0"
   "glBindBufferRange\0"
   "glBindBufferRangeEXT\0"
   "\0"
   /* _mesa_function_pool[50388]: BindBufferBase (will be remapped) */
   "iii\0"
   "glBindBufferBase\0"
   "glBindBufferBaseEXT\0"
   "\0"
   /* _mesa_function_pool[50430]: TransformFeedbackVaryings (will be remapped) */
   "iipi\0"
   "glTransformFeedbackVaryings\0"
   "glTransformFeedbackVaryingsEXT\0"
   "\0"
   /* _mesa_function_pool[50495]: GetTransformFeedbackVarying (will be remapped) */
   "iiipppp\0"
   "glGetTransformFeedbackVarying\0"
   "glGetTransformFeedbackVaryingEXT\0"
   "\0"
   /* _mesa_function_pool[50567]: BeginConditionalRender (will be remapped) */
   "ii\0"
   "glBeginConditionalRender\0"
   "glBeginConditionalRenderNV\0"
   "\0"
   /* _mesa_function_pool[50623]: EndConditionalRender (will be remapped) */
   "\0"
   "glEndConditionalRender\0"
   "glEndConditionalRenderNV\0"
   "\0"
   /* _mesa_function_pool[50673]: PrimitiveRestartIndex (will be remapped) */
   "i\0"
   "glPrimitiveRestartIndex\0"
   "glPrimitiveRestartIndexNV\0"
   "\0"
   /* _mesa_function_pool[50726]: GetInteger64i_v (will be remapped) */
   "iip\0"
   "glGetInteger64i_v\0"
   "\0"
   /* _mesa_function_pool[50749]: GetBufferParameteri64v (will be remapped) */
   "iip\0"
   "glGetBufferParameteri64v\0"
   "\0"
   /* _mesa_function_pool[50779]: FramebufferTexture (will be remapped) */
   "iiii\0"
   "glFramebufferTexture\0"
   "glFramebufferTextureEXT\0"
   "glFramebufferTextureOES\0"
   "\0"
   /* _mesa_function_pool[50854]: PrimitiveRestartNV (will be remapped) */
   "\0"
   "glPrimitiveRestartNV\0"
   "\0"
   /* _mesa_function_pool[50877]: BindBufferOffsetEXT (will be remapped) */
   "iiii\0"
   "glBindBufferOffsetEXT\0"
   "\0"
   /* _mesa_function_pool[50905]: BindTransformFeedback (will be remapped) */
   "ii\0"
   "glBindTransformFeedback\0"
   "\0"
   /* _mesa_function_pool[50933]: DeleteTransformFeedbacks (will be remapped) */
   "ip\0"
   "glDeleteTransformFeedbacks\0"
   "\0"
   /* _mesa_function_pool[50964]: GenTransformFeedbacks (will be remapped) */
   "ip\0"
   "glGenTransformFeedbacks\0"
   "\0"
   /* _mesa_function_pool[50992]: IsTransformFeedback (will be remapped) */
   "i\0"
   "glIsTransformFeedback\0"
   "\0"
   /* _mesa_function_pool[51017]: PauseTransformFeedback (will be remapped) */
   "\0"
   "glPauseTransformFeedback\0"
   "\0"
   /* _mesa_function_pool[51044]: ResumeTransformFeedback (will be remapped) */
   "\0"
   "glResumeTransformFeedback\0"
   "\0"
   /* _mesa_function_pool[51072]: DrawTransformFeedback (will be remapped) */
   "ii\0"
   "glDrawTransformFeedback\0"
   "\0"
   /* _mesa_function_pool[51100]: VDPAUInitNV (will be remapped) */
   "pp\0"
   "glVDPAUInitNV\0"
   "\0"
   /* _mesa_function_pool[51118]: VDPAUFiniNV (will be remapped) */
   "\0"
   "glVDPAUFiniNV\0"
   "\0"
   /* _mesa_function_pool[51134]: VDPAURegisterVideoSurfaceNV (will be remapped) */
   "piip\0"
   "glVDPAURegisterVideoSurfaceNV\0"
   "\0"
   /* _mesa_function_pool[51170]: VDPAURegisterOutputSurfaceNV (will be remapped) */
   "piip\0"
   "glVDPAURegisterOutputSurfaceNV\0"
   "\0"
   /* _mesa_function_pool[51207]: VDPAUIsSurfaceNV (will be remapped) */
   "i\0"
   "glVDPAUIsSurfaceNV\0"
   "\0"
   /* _mesa_function_pool[51229]: VDPAUUnregisterSurfaceNV (will be remapped) */
   "i\0"
   "glVDPAUUnregisterSurfaceNV\0"
   "\0"
   /* _mesa_function_pool[51259]: VDPAUGetSurfaceivNV (will be remapped) */
   "iiipp\0"
   "glVDPAUGetSurfaceivNV\0"
   "\0"
   /* _mesa_function_pool[51288]: VDPAUSurfaceAccessNV (will be remapped) */
   "ii\0"
   "glVDPAUSurfaceAccessNV\0"
   "\0"
   /* _mesa_function_pool[51315]: VDPAUMapSurfacesNV (will be remapped) */
   "ip\0"
   "glVDPAUMapSurfacesNV\0"
   "\0"
   /* _mesa_function_pool[51340]: VDPAUUnmapSurfacesNV (will be remapped) */
   "ip\0"
   "glVDPAUUnmapSurfacesNV\0"
   "\0"
   /* _mesa_function_pool[51367]: GetUnsignedBytevEXT (will be remapped) */
   "ip\0"
   "glGetUnsignedBytevEXT\0"
   "\0"
   /* _mesa_function_pool[51393]: GetUnsignedBytei_vEXT (will be remapped) */
   "iip\0"
   "glGetUnsignedBytei_vEXT\0"
   "\0"
   /* _mesa_function_pool[51422]: DeleteMemoryObjectsEXT (will be remapped) */
   "ip\0"
   "glDeleteMemoryObjectsEXT\0"
   "\0"
   /* _mesa_function_pool[51451]: IsMemoryObjectEXT (will be remapped) */
   "i\0"
   "glIsMemoryObjectEXT\0"
   "\0"
   /* _mesa_function_pool[51474]: CreateMemoryObjectsEXT (will be remapped) */
   "ip\0"
   "glCreateMemoryObjectsEXT\0"
   "\0"
   /* _mesa_function_pool[51503]: MemoryObjectParameterivEXT (will be remapped) */
   "iip\0"
   "glMemoryObjectParameterivEXT\0"
   "\0"
   /* _mesa_function_pool[51537]: GetMemoryObjectParameterivEXT (will be remapped) */
   "iip\0"
   "glGetMemoryObjectParameterivEXT\0"
   "\0"
   /* _mesa_function_pool[51574]: TexStorageMem2DEXT (will be remapped) */
   "iiiiiii\0"
   "glTexStorageMem2DEXT\0"
   "\0"
   /* _mesa_function_pool[51604]: TexStorageMem2DMultisampleEXT (will be remapped) */
   "iiiiiiii\0"
   "glTexStorageMem2DMultisampleEXT\0"
   "\0"
   /* _mesa_function_pool[51646]: TexStorageMem3DEXT (will be remapped) */
   "iiiiiiii\0"
   "glTexStorageMem3DEXT\0"
   "\0"
   /* _mesa_function_pool[51677]: TexStorageMem3DMultisampleEXT (will be remapped) */
   "iiiiiiiii\0"
   "glTexStorageMem3DMultisampleEXT\0"
   "\0"
   /* _mesa_function_pool[51720]: BufferStorageMemEXT (will be remapped) */
   "iiii\0"
   "glBufferStorageMemEXT\0"
   "\0"
   /* _mesa_function_pool[51748]: TextureStorageMem2DEXT (will be remapped) */
   "iiiiiii\0"
   "glTextureStorageMem2DEXT\0"
   "\0"
   /* _mesa_function_pool[51782]: TextureStorageMem2DMultisampleEXT (will be remapped) */
   "iiiiiiii\0"
   "glTextureStorageMem2DMultisampleEXT\0"
   "\0"
   /* _mesa_function_pool[51828]: TextureStorageMem3DEXT (will be remapped) */
   "iiiiiiii\0"
   "glTextureStorageMem3DEXT\0"
   "\0"
   /* _mesa_function_pool[51863]: TextureStorageMem3DMultisampleEXT (will be remapped) */
   "iiiiiiiii\0"
   "glTextureStorageMem3DMultisampleEXT\0"
   "\0"
   /* _mesa_function_pool[51910]: NamedBufferStorageMemEXT (will be remapped) */
   "iiii\0"
   "glNamedBufferStorageMemEXT\0"
   "\0"
   /* _mesa_function_pool[51943]: TexStorageMem1DEXT (will be remapped) */
   "iiiiii\0"
   "glTexStorageMem1DEXT\0"
   "\0"
   /* _mesa_function_pool[51972]: TextureStorageMem1DEXT (will be remapped) */
   "iiiiii\0"
   "glTextureStorageMem1DEXT\0"
   "\0"
   /* _mesa_function_pool[52005]: GenSemaphoresEXT (will be remapped) */
   "ip\0"
   "glGenSemaphoresEXT\0"
   "\0"
   /* _mesa_function_pool[52028]: DeleteSemaphoresEXT (will be remapped) */
   "ip\0"
   "glDeleteSemaphoresEXT\0"
   "\0"
   /* _mesa_function_pool[52054]: IsSemaphoreEXT (will be remapped) */
   "i\0"
   "glIsSemaphoreEXT\0"
   "\0"
   /* _mesa_function_pool[52074]: SemaphoreParameterui64vEXT (will be remapped) */
   "iip\0"
   "glSemaphoreParameterui64vEXT\0"
   "\0"
   /* _mesa_function_pool[52108]: GetSemaphoreParameterui64vEXT (will be remapped) */
   "iip\0"
   "glGetSemaphoreParameterui64vEXT\0"
   "\0"
   /* _mesa_function_pool[52145]: WaitSemaphoreEXT (will be remapped) */
   "iipipp\0"
   "glWaitSemaphoreEXT\0"
   "\0"
   /* _mesa_function_pool[52172]: SignalSemaphoreEXT (will be remapped) */
   "iipipp\0"
   "glSignalSemaphoreEXT\0"
   "\0"
   /* _mesa_function_pool[52201]: ImportMemoryFdEXT (will be remapped) */
   "iiii\0"
   "glImportMemoryFdEXT\0"
   "\0"
   /* _mesa_function_pool[52227]: ImportSemaphoreFdEXT (will be remapped) */
   "iii\0"
   "glImportSemaphoreFdEXT\0"
   "\0"
   /* _mesa_function_pool[52255]: MemoryBarrierByRegion (will be remapped) */
   "i\0"
   "glMemoryBarrierByRegion\0"
   "\0"
   /* _mesa_function_pool[52282]: AlphaFuncx (will be remapped) */
   "ii\0"
   "glAlphaFuncxOES\0"
   "glAlphaFuncx\0"
   "\0"
   /* _mesa_function_pool[52315]: ClearColorx (will be remapped) */
   "iiii\0"
   "glClearColorxOES\0"
   "glClearColorx\0"
   "\0"
   /* _mesa_function_pool[52352]: ClearDepthx (will be remapped) */
   "i\0"
   "glClearDepthxOES\0"
   "glClearDepthx\0"
   "\0"
   /* _mesa_function_pool[52386]: Color4x (will be remapped) */
   "iiii\0"
   "glColor4xOES\0"
   "glColor4x\0"
   "\0"
   /* _mesa_function_pool[52415]: DepthRangex (will be remapped) */
   "ii\0"
   "glDepthRangexOES\0"
   "glDepthRangex\0"
   "\0"
   /* _mesa_function_pool[52450]: Fogx (will be remapped) */
   "ii\0"
   "glFogxOES\0"
   "glFogx\0"
   "\0"
   /* _mesa_function_pool[52471]: Fogxv (will be remapped) */
   "ip\0"
   "glFogxvOES\0"
   "glFogxv\0"
   "\0"
   /* _mesa_function_pool[52494]: Frustumx (will be remapped) */
   "iiiiii\0"
   "glFrustumxOES\0"
   "glFrustumx\0"
   "\0"
   /* _mesa_function_pool[52527]: LightModelx (will be remapped) */
   "ii\0"
   "glLightModelxOES\0"
   "glLightModelx\0"
   "\0"
   /* _mesa_function_pool[52562]: LightModelxv (will be remapped) */
   "ip\0"
   "glLightModelxvOES\0"
   "glLightModelxv\0"
   "\0"
   /* _mesa_function_pool[52599]: Lightx (will be remapped) */
   "iii\0"
   "glLightxOES\0"
   "glLightx\0"
   "\0"
   /* _mesa_function_pool[52625]: Lightxv (will be remapped) */
   "iip\0"
   "glLightxvOES\0"
   "glLightxv\0"
   "\0"
   /* _mesa_function_pool[52653]: LineWidthx (will be remapped) */
   "i\0"
   "glLineWidthxOES\0"
   "glLineWidthx\0"
   "\0"
   /* _mesa_function_pool[52685]: LoadMatrixx (will be remapped) */
   "p\0"
   "glLoadMatrixxOES\0"
   "glLoadMatrixx\0"
   "\0"
   /* _mesa_function_pool[52719]: Materialx (will be remapped) */
   "iii\0"
   "glMaterialxOES\0"
   "glMaterialx\0"
   "\0"
   /* _mesa_function_pool[52751]: Materialxv (will be remapped) */
   "iip\0"
   "glMaterialxvOES\0"
   "glMaterialxv\0"
   "\0"
   /* _mesa_function_pool[52785]: MultMatrixx (will be remapped) */
   "p\0"
   "glMultMatrixxOES\0"
   "glMultMatrixx\0"
   "\0"
   /* _mesa_function_pool[52819]: MultiTexCoord4x (will be remapped) */
   "iiiii\0"
   "glMultiTexCoord4xOES\0"
   "glMultiTexCoord4x\0"
   "\0"
   /* _mesa_function_pool[52865]: Normal3x (will be remapped) */
   "iii\0"
   "glNormal3xOES\0"
   "glNormal3x\0"
   "\0"
   /* _mesa_function_pool[52895]: Orthox (will be remapped) */
   "iiiiii\0"
   "glOrthoxOES\0"
   "glOrthox\0"
   "\0"
   /* _mesa_function_pool[52924]: PointSizex (will be remapped) */
   "i\0"
   "glPointSizexOES\0"
   "glPointSizex\0"
   "\0"
   /* _mesa_function_pool[52956]: PolygonOffsetx (will be remapped) */
   "ii\0"
   "glPolygonOffsetxOES\0"
   "glPolygonOffsetx\0"
   "\0"
   /* _mesa_function_pool[52997]: Rotatex (will be remapped) */
   "iiii\0"
   "glRotatexOES\0"
   "glRotatex\0"
   "\0"
   /* _mesa_function_pool[53026]: SampleCoveragex (will be remapped) */
   "ii\0"
   "glSampleCoveragexOES\0"
   "glSampleCoveragex\0"
   "\0"
   /* _mesa_function_pool[53069]: Scalex (will be remapped) */
   "iii\0"
   "glScalexOES\0"
   "glScalex\0"
   "\0"
   /* _mesa_function_pool[53095]: TexEnvx (will be remapped) */
   "iii\0"
   "glTexEnvxOES\0"
   "glTexEnvx\0"
   "\0"
   /* _mesa_function_pool[53123]: TexEnvxv (will be remapped) */
   "iip\0"
   "glTexEnvxvOES\0"
   "glTexEnvxv\0"
   "\0"
   /* _mesa_function_pool[53153]: TexParameterx (will be remapped) */
   "iii\0"
   "glTexParameterxOES\0"
   "glTexParameterx\0"
   "\0"
   /* _mesa_function_pool[53193]: Translatex (will be remapped) */
   "iii\0"
   "glTranslatexOES\0"
   "glTranslatex\0"
   "\0"
   /* _mesa_function_pool[53227]: ClipPlanex (will be remapped) */
   "ip\0"
   "glClipPlanexOES\0"
   "glClipPlanex\0"
   "\0"
   /* _mesa_function_pool[53260]: GetClipPlanex (will be remapped) */
   "ip\0"
   "glGetClipPlanexOES\0"
   "glGetClipPlanex\0"
   "\0"
   /* _mesa_function_pool[53299]: GetFixedv (will be remapped) */
   "ip\0"
   "glGetFixedvOES\0"
   "glGetFixedv\0"
   "\0"
   /* _mesa_function_pool[53330]: GetLightxv (will be remapped) */
   "iip\0"
   "glGetLightxvOES\0"
   "glGetLightxv\0"
   "\0"
   /* _mesa_function_pool[53364]: GetMaterialxv (will be remapped) */
   "iip\0"
   "glGetMaterialxvOES\0"
   "glGetMaterialxv\0"
   "\0"
   /* _mesa_function_pool[53404]: GetTexEnvxv (will be remapped) */
   "iip\0"
   "glGetTexEnvxvOES\0"
   "glGetTexEnvxv\0"
   "\0"
   /* _mesa_function_pool[53440]: GetTexParameterxv (will be remapped) */
   "iip\0"
   "glGetTexParameterxvOES\0"
   "glGetTexParameterxv\0"
   "\0"
   /* _mesa_function_pool[53488]: PointParameterx (will be remapped) */
   "ii\0"
   "glPointParameterxOES\0"
   "glPointParameterx\0"
   "\0"
   /* _mesa_function_pool[53531]: PointParameterxv (will be remapped) */
   "ip\0"
   "glPointParameterxvOES\0"
   "glPointParameterxv\0"
   "\0"
   /* _mesa_function_pool[53576]: TexParameterxv (will be remapped) */
   "iip\0"
   "glTexParameterxvOES\0"
   "glTexParameterxv\0"
   "\0"
   /* _mesa_function_pool[53618]: GetTexGenxvOES (will be remapped) */
   "iip\0"
   "glGetTexGenxvOES\0"
   "\0"
   /* _mesa_function_pool[53640]: TexGenxOES (will be remapped) */
   "iii\0"
   "glTexGenxOES\0"
   "\0"
   /* _mesa_function_pool[53658]: TexGenxvOES (will be remapped) */
   "iip\0"
   "glTexGenxvOES\0"
   "\0"
   /* _mesa_function_pool[53677]: ClipPlanef (will be remapped) */
   "ip\0"
   "glClipPlanefOES\0"
   "glClipPlanef\0"
   "\0"
   /* _mesa_function_pool[53710]: GetClipPlanef (will be remapped) */
   "ip\0"
   "glGetClipPlanefOES\0"
   "glGetClipPlanef\0"
   "\0"
   /* _mesa_function_pool[53749]: Frustumf (will be remapped) */
   "ffffff\0"
   "glFrustumfOES\0"
   "glFrustumf\0"
   "\0"
   /* _mesa_function_pool[53782]: Orthof (will be remapped) */
   "ffffff\0"
   "glOrthofOES\0"
   "glOrthof\0"
   "\0"
   /* _mesa_function_pool[53811]: DrawTexiOES (will be remapped) */
   "iiiii\0"
   "glDrawTexiOES\0"
   "\0"
   /* _mesa_function_pool[53832]: DrawTexivOES (will be remapped) */
   "p\0"
   "glDrawTexivOES\0"
   "\0"
   /* _mesa_function_pool[53850]: DrawTexfOES (will be remapped) */
   "fffff\0"
   "glDrawTexfOES\0"
   "\0"
   /* _mesa_function_pool[53871]: DrawTexfvOES (will be remapped) */
   "p\0"
   "glDrawTexfvOES\0"
   "\0"
   /* _mesa_function_pool[53889]: DrawTexsOES (will be remapped) */
   "iiiii\0"
   "glDrawTexsOES\0"
   "\0"
   /* _mesa_function_pool[53910]: DrawTexsvOES (will be remapped) */
   "p\0"
   "glDrawTexsvOES\0"
   "\0"
   /* _mesa_function_pool[53928]: DrawTexxOES (will be remapped) */
   "iiiii\0"
   "glDrawTexxOES\0"
   "\0"
   /* _mesa_function_pool[53949]: DrawTexxvOES (will be remapped) */
   "p\0"
   "glDrawTexxvOES\0"
   "\0"
   /* _mesa_function_pool[53967]: LoadPaletteFromModelViewMatrixOES (dynamic) */
   "\0"
   "glLoadPaletteFromModelViewMatrixOES\0"
   "\0"
   /* _mesa_function_pool[54005]: PointSizePointerOES (will be remapped) */
   "iip\0"
   "glPointSizePointerOES\0"
   "\0"
   /* _mesa_function_pool[54032]: QueryMatrixxOES (will be remapped) */
   "pp\0"
   "glQueryMatrixxOES\0"
   "\0"
   /* _mesa_function_pool[54054]: DiscardFramebufferEXT (will be remapped) */
   "iip\0"
   "glDiscardFramebufferEXT\0"
   "\0"
   /* _mesa_function_pool[54083]: DepthRangeArrayfvOES (will be remapped) */
   "iip\0"
   "glDepthRangeArrayfvOES\0"
   "\0"
   /* _mesa_function_pool[54111]: DepthRangeIndexedfOES (will be remapped) */
   "iff\0"
   "glDepthRangeIndexedfOES\0"
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
   { 50567, BeginConditionalRender_remap_index },
   { 50235, BeginTransformFeedback_remap_index },
   { 50388, BindBufferBase_remap_index },
   { 50342, BindBufferRange_remap_index },
   { 50027, BindFragDataLocation_remap_index },
   { 18971, ClampColor_remap_index },
   { 50196, ClearBufferfi_remap_index },
   { 50175, ClearBufferfv_remap_index },
   { 50132, ClearBufferiv_remap_index },
   { 50153, ClearBufferuiv_remap_index },
   { 46540, ColorMaski_remap_index },
   { 46763, Disablei_remap_index },
   { 46704, Enablei_remap_index },
   { 50623, EndConditionalRender_remap_index },
   { 50291, EndTransformFeedback_remap_index },
   { 46614, GetBooleani_v_remap_index },
   { 50081, GetFragDataLocation_remap_index },
   { 46659, GetIntegeri_v_remap_index },
   { 50218, GetStringi_remap_index },
   { 48480, GetTexParameterIiv_remap_index },
   { 48554, GetTexParameterIuiv_remap_index },
   { 50495, GetTransformFeedbackVarying_remap_index },
   { 49987, GetUniformuiv_remap_index },
   { 49603, GetVertexAttribIiv_remap_index },
   { 49653, GetVertexAttribIuiv_remap_index },
   { 46826, IsEnabledi_remap_index },
   { 48347, TexParameterIiv_remap_index },
   { 48412, TexParameterIuiv_remap_index },
   { 50430, TransformFeedbackVaryings_remap_index },
   { 49705, Uniform1ui_remap_index },
   { 49843, Uniform1uiv_remap_index },
   { 49738, Uniform2ui_remap_index },
   { 49879, Uniform2uiv_remap_index },
   { 49772, Uniform3ui_remap_index },
   { 49915, Uniform3uiv_remap_index },
   { 49807, Uniform4ui_remap_index },
   { 49951, Uniform4uiv_remap_index },
   { 48995, VertexAttribI1iv_remap_index },
   { 49175, VertexAttribI1uiv_remap_index },
   { 49363, VertexAttribI4bv_remap_index },
   { 49408, VertexAttribI4sv_remap_index },
   { 49453, VertexAttribI4ubv_remap_index },
   { 49500, VertexAttribI4usv_remap_index },
   { 49547, VertexAttribIPointer_remap_index },
   { 50673, PrimitiveRestartIndex_remap_index },
   { 20750, TexBuffer_remap_index },
   { 50779, FramebufferTexture_remap_index },
   { 50749, GetBufferParameteri64v_remap_index },
   { 50726, GetInteger64i_v_remap_index },
   { 20598, VertexAttribDivisor_remap_index },
   { 22298, MinSampleShading_remap_index },
   { 52255, MemoryBarrierByRegion_remap_index },
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
   { 20692, FlushMappedBufferRange_remap_index },
   { 20649, MapBufferRange_remap_index },
   { 20812, BindVertexArray_remap_index },
   { 20854, DeleteVertexArrays_remap_index },
   { 20903, GenVertexArrays_remap_index },
   { 20946, IsVertexArray_remap_index },
   { 21128, GetActiveUniformBlockName_remap_index },
   { 21096, GetActiveUniformBlockiv_remap_index },
   { 21039, GetActiveUniformName_remap_index },
   { 21010, GetActiveUniformsiv_remap_index },
   { 21069, GetUniformBlockIndex_remap_index },
   { 20984, GetUniformIndices_remap_index },
   { 21163, UniformBlockBinding_remap_index },
   { 21190, CopyBufferSubData_remap_index },
   { 21641, ClientWaitSync_remap_index },
   { 21625, DeleteSync_remap_index },
   { 21597, FenceSync_remap_index },
   { 21679, GetInteger64v_remap_index },
   { 21699, GetSynciv_remap_index },
   { 21613, IsSync_remap_index },
   { 21663, WaitSync_remap_index },
   { 21217, DrawElementsBaseVertex_remap_index },
   { 21481, DrawElementsInstancedBaseVertex_remap_index },
   { 21305, DrawRangeElementsBaseVertex_remap_index },
   { 21410, MultiDrawElementsBaseVertex_remap_index },
   { 46498, ProvokingVertex_remap_index },
   { 21783, GetMultisamplefv_remap_index },
   { 21807, SampleMaski_remap_index },
   { 21718, TexImage2DMultisample_remap_index },
   { 21750, TexImage3DMultisample_remap_index },
   { 21932, BlendEquationSeparateiARB_remap_index },
   { 21825, BlendEquationiARB_remap_index },
   { 22168, BlendFuncSeparateiARB_remap_index },
   { 22080, BlendFunciARB_remap_index },
   { 22364, BindFragDataLocationIndexed_remap_index },
   { 22433, GetFragDataIndex_remap_index },
   { 22532, BindSampler_remap_index },
   { 22496, DeleteSamplers_remap_index },
   { 22478, GenSamplers_remap_index },
   { 22867, GetSamplerParameterIiv_remap_index },
   { 22953, GetSamplerParameterIuiv_remap_index },
   { 22838, GetSamplerParameterfv_remap_index },
   { 22809, GetSamplerParameteriv_remap_index },
   { 22517, IsSampler_remap_index },
   { 22652, SamplerParameterIiv_remap_index },
   { 22729, SamplerParameterIuiv_remap_index },
   { 22575, SamplerParameterf_remap_index },
   { 22626, SamplerParameterfv_remap_index },
   { 22550, SamplerParameteri_remap_index },
   { 22600, SamplerParameteriv_remap_index },
   { 23042, GetQueryObjecti64v_remap_index },
   { 23092, GetQueryObjectui64v_remap_index },
   { 23144, QueryCounter_remap_index },
   { 23681, ColorP3ui_remap_index },
   { 23713, ColorP3uiv_remap_index },
   { 23697, ColorP4ui_remap_index },
   { 23730, ColorP4uiv_remap_index },
   { 23442, MultiTexCoordP1ui_remap_index },
   { 23542, MultiTexCoordP1uiv_remap_index },
   { 23467, MultiTexCoordP2ui_remap_index },
   { 23568, MultiTexCoordP2uiv_remap_index },
   { 23492, MultiTexCoordP3ui_remap_index },
   { 23594, MultiTexCoordP3uiv_remap_index },
   { 23517, MultiTexCoordP4ui_remap_index },
   { 23620, MultiTexCoordP4uiv_remap_index },
   { 23646, NormalP3ui_remap_index },
   { 23663, NormalP3uiv_remap_index },
   { 23747, SecondaryColorP3ui_remap_index },
   { 23772, SecondaryColorP3uiv_remap_index },
   { 23286, TexCoordP1ui_remap_index },
   { 23362, TexCoordP1uiv_remap_index },
   { 23305, TexCoordP2ui_remap_index },
   { 23382, TexCoordP2uiv_remap_index },
   { 23324, TexCoordP3ui_remap_index },
   { 23402, TexCoordP3uiv_remap_index },
   { 23343, TexCoordP4ui_remap_index },
   { 23422, TexCoordP4uiv_remap_index },
   { 23798, VertexAttribP1ui_remap_index },
   { 23898, VertexAttribP1uiv_remap_index },
   { 23823, VertexAttribP2ui_remap_index },
   { 23924, VertexAttribP2uiv_remap_index },
   { 23848, VertexAttribP3ui_remap_index },
   { 23950, VertexAttribP3uiv_remap_index },
   { 23873, VertexAttribP4ui_remap_index },
   { 23976, VertexAttribP4uiv_remap_index },
   { 23181, VertexP2ui_remap_index },
   { 23232, VertexP2uiv_remap_index },
   { 23198, VertexP3ui_remap_index },
   { 23250, VertexP3uiv_remap_index },
   { 23215, VertexP4ui_remap_index },
   { 23268, VertexP4uiv_remap_index },
   { 24350, DrawArraysIndirect_remap_index },
   { 24375, DrawElementsIndirect_remap_index },
   { 24849, GetUniformdv_remap_index },
   { 24470, Uniform1d_remap_index },
   { 24540, Uniform1dv_remap_index },
   { 24486, Uniform2d_remap_index },
   { 24558, Uniform2dv_remap_index },
   { 24503, Uniform3d_remap_index },
   { 24576, Uniform3dv_remap_index },
   { 24521, Uniform4d_remap_index },
   { 24594, Uniform4dv_remap_index },
   { 24612, UniformMatrix2dv_remap_index },
   { 24687, UniformMatrix2x3dv_remap_index },
   { 24714, UniformMatrix2x4dv_remap_index },
   { 24637, UniformMatrix3dv_remap_index },
   { 24741, UniformMatrix3x2dv_remap_index },
   { 24768, UniformMatrix3x4dv_remap_index },
   { 24662, UniformMatrix4dv_remap_index },
   { 24795, UniformMatrix4x2dv_remap_index },
   { 24822, UniformMatrix4x3dv_remap_index },
   { 24143, GetActiveSubroutineName_remap_index },
   { 24102, GetActiveSubroutineUniformName_remap_index },
   { 24064, GetActiveSubroutineUniformiv_remap_index },
   { 24237, GetProgramStageiv_remap_index },
   { 24038, GetSubroutineIndex_remap_index },
   { 24002, GetSubroutineUniformLocation_remap_index },
   { 24206, GetUniformSubroutineuiv_remap_index },
   { 24177, UniformSubroutinesuiv_remap_index },
   { 24327, PatchParameterfv_remap_index },
   { 24263, PatchParameteri_remap_index },
   { 50905, BindTransformFeedback_remap_index },
   { 50933, DeleteTransformFeedbacks_remap_index },
   { 51072, DrawTransformFeedback_remap_index },
   { 50964, GenTransformFeedbacks_remap_index },
   { 50992, IsTransformFeedback_remap_index },
   { 51017, PauseTransformFeedback_remap_index },
   { 51044, ResumeTransformFeedback_remap_index },
   { 24904, BeginQueryIndexed_remap_index },
   { 24869, DrawTransformFeedbackStream_remap_index },
   { 24929, EndQueryIndexed_remap_index },
   { 24951, GetQueryIndexediv_remap_index },
   { 28103, ClearDepthf_remap_index },
   { 28137, DepthRangef_remap_index },
   { 28022, GetShaderPrecisionFormat_remap_index },
   { 28055, ReleaseShaderCompiler_remap_index },
   { 28081, ShaderBinary_remap_index },
   { 28172, GetProgramBinary_remap_index },
   { 28220, ProgramBinary_remap_index },
   { 28261, ProgramParameteri_remap_index },
   { 27996, GetVertexAttribLdv_remap_index },
   { 27780, VertexAttribL1d_remap_index },
   { 27874, VertexAttribL1dv_remap_index },
   { 27802, VertexAttribL2d_remap_index },
   { 27897, VertexAttribL2dv_remap_index },
   { 27825, VertexAttribL3d_remap_index },
   { 27920, VertexAttribL3dv_remap_index },
   { 27849, VertexAttribL4d_remap_index },
   { 27943, VertexAttribL4dv_remap_index },
   { 27966, VertexAttribLPointer_remap_index },
   { 38506, DepthRangeArrayv_remap_index },
   { 38530, DepthRangeIndexed_remap_index },
   { 38591, GetDoublei_v_remap_index },
   { 38555, GetFloati_v_remap_index },
   { 38379, ScissorArrayv_remap_index },
   { 38419, ScissorIndexed_remap_index },
   { 38463, ScissorIndexedv_remap_index },
   { 38242, ViewportArrayv_remap_index },
   { 38284, ViewportIndexedf_remap_index },
   { 38332, ViewportIndexedfv_remap_index },
   { 28624, GetGraphicsResetStatusARB_remap_index },
   { 28959, GetnColorTableARB_remap_index },
   { 29106, GetnCompressedTexImageARB_remap_index },
   { 28986, GetnConvolutionFilterARB_remap_index },
   { 29055, GetnHistogramARB_remap_index },
   { 28707, GetnMapdvARB_remap_index },
   { 28728, GetnMapfvARB_remap_index },
   { 28749, GetnMapivARB_remap_index },
   { 29082, GetnMinmaxARB_remap_index },
   { 28770, GetnPixelMapfvARB_remap_index },
   { 28795, GetnPixelMapuivARB_remap_index },
   { 28821, GetnPixelMapusvARB_remap_index },
   { 28847, GetnPolygonStippleARB_remap_index },
   { 29020, GetnSeparableFilterARB_remap_index },
   { 28875, GetnTexImageARB_remap_index },
   { 29323, GetnUniformdvARB_remap_index },
   { 29140, GetnUniformfvARB_remap_index },
   { 29200, GetnUniformivARB_remap_index },
   { 29260, GetnUniformuivARB_remap_index },
   { 28901, ReadnPixelsARB_remap_index },
   { 29348, DrawArraysInstancedBaseInstance_remap_index },
   { 29426, DrawElementsInstancedBaseInstance_remap_index },
   { 29509, DrawElementsInstancedBaseVertexBaseInstance_remap_index },
   { 29613, DrawTransformFeedbackInstanced_remap_index },
   { 29651, DrawTransformFeedbackStreamInstanced_remap_index },
   { 29696, GetInternalformativ_remap_index },
   { 29725, GetActiveAtomicCounterBufferiv_remap_index },
   { 29764, BindImageTexture_remap_index },
   { 29792, MemoryBarrier_remap_index },
   { 29811, TexStorage1D_remap_index },
   { 29832, TexStorage2D_remap_index },
   { 29854, TexStorage3D_remap_index },
   { 29877, TextureStorage1DEXT_remap_index },
   { 29906, TextureStorage2DEXT_remap_index },
   { 29936, TextureStorage3DEXT_remap_index },
   { 30219, ClearBufferData_remap_index },
   { 30244, ClearBufferSubData_remap_index },
   { 30274, DispatchCompute_remap_index },
   { 30297, DispatchComputeIndirect_remap_index },
   { 30326, CopyImageSubData_remap_index },
   { 30406, TextureView_remap_index },
   { 30447, BindVertexBuffer_remap_index },
   { 30556, VertexAttribBinding_remap_index },
   { 30472, VertexAttribFormat_remap_index },
   { 30500, VertexAttribIFormat_remap_index },
   { 30528, VertexAttribLFormat_remap_index },
   { 30582, VertexBindingDivisor_remap_index },
   { 30609, FramebufferParameteri_remap_index },
   { 30638, GetFramebufferParameteriv_remap_index },
   { 30671, GetInternalformati64v_remap_index },
   { 24403, MultiDrawArraysIndirect_remap_index },
   { 24435, MultiDrawElementsIndirect_remap_index },
   { 30883, GetProgramInterfaceiv_remap_index },
   { 30913, GetProgramResourceIndex_remap_index },
   { 31010, GetProgramResourceLocation_remap_index },
   { 31044, GetProgramResourceLocationIndex_remap_index },
   { 30944, GetProgramResourceName_remap_index },
   { 30977, GetProgramResourceiv_remap_index },
   { 31120, ShaderStorageBlockBinding_remap_index },
   { 31153, TexBufferRange_remap_index },
   { 31217, TexStorage2DMultisample_remap_index },
   { 31251, TexStorage3DMultisample_remap_index },
   { 31315, BufferStorage_remap_index },
   { 31356, ClearTexImage_remap_index },
   { 31379, ClearTexSubImage_remap_index },
   { 31411, BindBuffersBase_remap_index },
   { 31435, BindBuffersRange_remap_index },
   { 31502, BindImageTextures_remap_index },
   { 31482, BindSamplers_remap_index },
   { 31462, BindTextures_remap_index },
   { 31527, BindVertexBuffers_remap_index },
   { 31683, GetImageHandleARB_remap_index },
   { 31554, GetTextureHandleARB_remap_index },
   { 31579, GetTextureSamplerHandleARB_remap_index },
   { 32025, GetVertexAttribLui64vARB_remap_index },
   { 31938, IsImageHandleResidentARB_remap_index },
   { 31906, IsTextureHandleResidentARB_remap_index },
   { 31743, MakeImageHandleNonResidentARB_remap_index },
   { 31710, MakeImageHandleResidentARB_remap_index },
   { 31646, MakeTextureHandleNonResidentARB_remap_index },
   { 31612, MakeTextureHandleResidentARB_remap_index },
   { 31834, ProgramUniformHandleui64ARB_remap_index },
   { 31869, ProgramUniformHandleui64vARB_remap_index },
   { 31778, UniformHandleui64ARB_remap_index },
   { 31805, UniformHandleui64vARB_remap_index },
   { 31968, VertexAttribL1ui64ARB_remap_index },
   { 31996, VertexAttribL1ui64vARB_remap_index },
   { 32057, DispatchComputeGroupSizeARB_remap_index },
   { 32095, MultiDrawArraysIndirectCountARB_remap_index },
   { 32167, MultiDrawElementsIndirectCountARB_remap_index },
   { 32244, ClipControl_remap_index },
   { 34399, BindTextureUnit_remap_index },
   { 33371, BlitNamedFramebuffer_remap_index },
   { 33408, CheckNamedFramebufferStatus_remap_index },
   { 32592, ClearNamedBufferData_remap_index },
   { 32622, ClearNamedBufferSubData_remap_index },
   { 33338, ClearNamedFramebufferfi_remap_index },
   { 33306, ClearNamedFramebufferfv_remap_index },
   { 33241, ClearNamedFramebufferiv_remap_index },
   { 33273, ClearNamedFramebufferuiv_remap_index },
   { 33991, CompressedTextureSubImage1D_remap_index },
   { 34030, CompressedTextureSubImage2D_remap_index },
   { 34071, CompressedTextureSubImage3D_remap_index },
   { 32560, CopyNamedBufferSubData_remap_index },
   { 34114, CopyTextureSubImage1D_remap_index },
   { 34146, CopyTextureSubImage2D_remap_index },
   { 34180, CopyTextureSubImage3D_remap_index },
   { 32462, CreateBuffers_remap_index },
   { 32891, CreateFramebuffers_remap_index },
   { 35133, CreateProgramPipelines_remap_index },
   { 35162, CreateQueries_remap_index },
   { 33529, CreateRenderbuffers_remap_index },
   { 35112, CreateSamplers_remap_index },
   { 33672, CreateTextures_remap_index },
   { 32262, CreateTransformFeedbacks_remap_index },
   { 34670, CreateVertexArrays_remap_index },
   { 34695, DisableVertexArrayAttrib_remap_index },
   { 34726, EnableVertexArrayAttrib_remap_index },
   { 32728, FlushMappedNamedBufferRange_remap_index },
   { 34372, GenerateTextureMipmap_remap_index },
   { 34447, GetCompressedTextureImage_remap_index },
   { 32796, GetNamedBufferParameteri64v_remap_index },
   { 32763, GetNamedBufferParameteriv_remap_index },
   { 32831, GetNamedBufferPointerv_remap_index },
   { 32861, GetNamedBufferSubData_remap_index },
   { 33480, GetNamedFramebufferAttachmentParameteriv_remap_index },
   { 33442, GetNamedFramebufferParameteriv_remap_index },
   { 33633, GetNamedRenderbufferParameteriv_remap_index },
   { 35246, GetQueryBufferObjecti64v_remap_index },
   { 35183, GetQueryBufferObjectiv_remap_index },
   { 35279, GetQueryBufferObjectui64v_remap_index },
   { 35214, GetQueryBufferObjectuiv_remap_index },
   { 34421, GetTextureImage_remap_index },
   { 34481, GetTextureLevelParameterfv_remap_index },
   { 34516, GetTextureLevelParameteriv_remap_index },
   { 34580, GetTextureParameterIiv_remap_index },
   { 34610, GetTextureParameterIuiv_remap_index },
   { 34551, GetTextureParameterfv_remap_index },
   { 34641, GetTextureParameteriv_remap_index },
   { 32428, GetTransformFeedbacki64_v_remap_index },
   { 32396, GetTransformFeedbacki_v_remap_index },
   { 32366, GetTransformFeedbackiv_remap_index },
   { 35078, GetVertexArrayIndexed64iv_remap_index },
   { 35046, GetVertexArrayIndexediv_remap_index },
   { 35022, GetVertexArrayiv_remap_index },
   { 33158, InvalidateNamedFramebufferData_remap_index },
   { 33196, InvalidateNamedFramebufferSubData_remap_index },
   { 32657, MapNamedBuffer_remap_index },
   { 32678, MapNamedBufferRange_remap_index },
   { 32509, NamedBufferData_remap_index },
   { 32482, NamedBufferStorage_remap_index },
   { 32533, NamedBufferSubData_remap_index },
   { 33057, NamedFramebufferDrawBuffer_remap_index },
   { 33090, NamedFramebufferDrawBuffers_remap_index },
   { 32953, NamedFramebufferParameteri_remap_index },
   { 33125, NamedFramebufferReadBuffer_remap_index },
   { 32916, NamedFramebufferRenderbuffer_remap_index },
   { 32987, NamedFramebufferTexture_remap_index },
   { 33019, NamedFramebufferTextureLayer_remap_index },
   { 33555, NamedRenderbufferStorage_remap_index },
   { 33588, NamedRenderbufferStorageMultisample_remap_index },
   { 33694, TextureBuffer_remap_index },
   { 33715, TextureBufferRange_remap_index },
   { 34291, TextureParameterIiv_remap_index },
   { 34318, TextureParameterIuiv_remap_index },
   { 34215, TextureParameterf_remap_index },
   { 34240, TextureParameterfv_remap_index },
   { 34266, TextureParameteri_remap_index },
   { 34346, TextureParameteriv_remap_index },
   { 33743, TextureStorage1D_remap_index },
   { 33768, TextureStorage2D_remap_index },
   { 33821, TextureStorage2DMultisample_remap_index },
   { 33794, TextureStorage3D_remap_index },
   { 33859, TextureStorage3DMultisample_remap_index },
   { 33898, TextureSubImage1D_remap_index },
   { 33927, TextureSubImage2D_remap_index },
   { 33958, TextureSubImage3D_remap_index },
   { 32293, TransformFeedbackBufferBase_remap_index },
   { 32328, TransformFeedbackBufferRange_remap_index },
   { 32706, UnmapNamedBuffer_remap_index },
   { 34957, VertexArrayAttribBinding_remap_index },
   { 34855, VertexArrayAttribFormat_remap_index },
   { 34889, VertexArrayAttribIFormat_remap_index },
   { 34923, VertexArrayAttribLFormat_remap_index },
   { 34989, VertexArrayBindingDivisor_remap_index },
   { 34756, VertexArrayElementBuffer_remap_index },
   { 34787, VertexArrayVertexBuffer_remap_index },
   { 34820, VertexArrayVertexBuffers_remap_index },
   { 35348, GetCompressedTextureSubImage_remap_index },
   { 35313, GetTextureSubImage_remap_index },
   { 35429, BufferPageCommitmentARB_remap_index },
   { 35461, NamedBufferPageCommitmentARB_remap_index },
   { 36016, GetUniformi64vARB_remap_index },
   { 36041, GetUniformui64vARB_remap_index },
   { 36067, GetnUniformi64vARB_remap_index },
   { 36094, GetnUniformui64vARB_remap_index },
   { 36122, ProgramUniform1i64ARB_remap_index },
   { 36244, ProgramUniform1i64vARB_remap_index },
   { 36368, ProgramUniform1ui64ARB_remap_index },
   { 36494, ProgramUniform1ui64vARB_remap_index },
   { 36151, ProgramUniform2i64ARB_remap_index },
   { 36275, ProgramUniform2i64vARB_remap_index },
   { 36398, ProgramUniform2ui64ARB_remap_index },
   { 36526, ProgramUniform2ui64vARB_remap_index },
   { 36181, ProgramUniform3i64ARB_remap_index },
   { 36306, ProgramUniform3i64vARB_remap_index },
   { 36429, ProgramUniform3ui64ARB_remap_index },
   { 36558, ProgramUniform3ui64vARB_remap_index },
   { 36212, ProgramUniform4i64ARB_remap_index },
   { 36337, ProgramUniform4i64vARB_remap_index },
   { 36461, ProgramUniform4ui64ARB_remap_index },
   { 36590, ProgramUniform4ui64vARB_remap_index },
   { 35644, Uniform1i64ARB_remap_index },
   { 35734, Uniform1i64vARB_remap_index },
   { 35826, Uniform1ui64ARB_remap_index },
   { 35920, Uniform1ui64vARB_remap_index },
   { 35665, Uniform2i64ARB_remap_index },
   { 35757, Uniform2i64vARB_remap_index },
   { 35848, Uniform2ui64ARB_remap_index },
   { 35944, Uniform2ui64vARB_remap_index },
   { 35687, Uniform3i64ARB_remap_index },
   { 35780, Uniform3i64vARB_remap_index },
   { 35871, Uniform3ui64ARB_remap_index },
   { 35968, Uniform3ui64vARB_remap_index },
   { 35710, Uniform4i64ARB_remap_index },
   { 35803, Uniform4i64vARB_remap_index },
   { 35895, Uniform4ui64ARB_remap_index },
   { 35992, Uniform4ui64vARB_remap_index },
   { 42349, EvaluateDepthValuesARB_remap_index },
   { 42193, FramebufferSampleLocationsfvARB_remap_index },
   { 42266, NamedFramebufferSampleLocationsfvARB_remap_index },
   { 36622, SpecializeShaderARB_remap_index },
   { 30792, InvalidateBufferData_remap_index },
   { 30761, InvalidateBufferSubData_remap_index },
   { 30854, InvalidateFramebuffer_remap_index },
   { 30818, InvalidateSubFramebuffer_remap_index },
   { 30736, InvalidateTexImage_remap_index },
   { 30702, InvalidateTexSubImage_remap_index },
   { 53850, DrawTexfOES_remap_index },
   { 53871, DrawTexfvOES_remap_index },
   { 53811, DrawTexiOES_remap_index },
   { 53832, DrawTexivOES_remap_index },
   { 53889, DrawTexsOES_remap_index },
   { 53910, DrawTexsvOES_remap_index },
   { 53928, DrawTexxOES_remap_index },
   { 53949, DrawTexxvOES_remap_index },
   { 54005, PointSizePointerOES_remap_index },
   { 54032, QueryMatrixxOES_remap_index },
   { 37097, SampleMaskSGIS_remap_index },
   { 37134, SamplePatternSGIS_remap_index },
   { 37176, ColorPointerEXT_remap_index },
   { 37201, EdgeFlagPointerEXT_remap_index },
   { 37227, IndexPointerEXT_remap_index },
   { 37251, NormalPointerEXT_remap_index },
   { 37276, TexCoordPointerEXT_remap_index },
   { 37304, VertexPointerEXT_remap_index },
   { 54054, DiscardFramebufferEXT_remap_index },
   { 25023, ActiveShaderProgram_remap_index },
   { 25128, BindProgramPipeline_remap_index },
   { 25074, CreateShaderProgramv_remap_index },
   { 25178, DeleteProgramPipelines_remap_index },
   { 25235, GenProgramPipelines_remap_index },
   { 27200, GetProgramPipelineInfoLog_remap_index },
   { 25332, GetProgramPipelineiv_remap_index },
   { 25286, IsProgramPipeline_remap_index },
   { 38152, LockArraysEXT_remap_index },
   { 27265, ProgramUniform1d_remap_index },
   { 27676, ProgramUniform1dv_remap_index },
   { 25774, ProgramUniform1f_remap_index },
   { 26364, ProgramUniform1fv_remap_index },
   { 25386, ProgramUniform1i_remap_index },
   { 25964, ProgramUniform1iv_remap_index },
   { 25576, ProgramUniform1ui_remap_index },
   { 26160, ProgramUniform1uiv_remap_index },
   { 27289, ProgramUniform2d_remap_index },
   { 27702, ProgramUniform2dv_remap_index },
   { 25820, ProgramUniform2f_remap_index },
   { 26413, ProgramUniform2fv_remap_index },
   { 25432, ProgramUniform2i_remap_index },
   { 26013, ProgramUniform2iv_remap_index },
   { 25624, ProgramUniform2ui_remap_index },
   { 26211, ProgramUniform2uiv_remap_index },
   { 27314, ProgramUniform3d_remap_index },
   { 27728, ProgramUniform3dv_remap_index },
   { 25867, ProgramUniform3f_remap_index },
   { 26462, ProgramUniform3fv_remap_index },
   { 25479, ProgramUniform3i_remap_index },
   { 26062, ProgramUniform3iv_remap_index },
   { 25673, ProgramUniform3ui_remap_index },
   { 26262, ProgramUniform3uiv_remap_index },
   { 27340, ProgramUniform4d_remap_index },
   { 27754, ProgramUniform4dv_remap_index },
   { 25915, ProgramUniform4f_remap_index },
   { 26511, ProgramUniform4fv_remap_index },
   { 25527, ProgramUniform4i_remap_index },
   { 26111, ProgramUniform4iv_remap_index },
   { 25723, ProgramUniform4ui_remap_index },
   { 26313, ProgramUniform4uiv_remap_index },
   { 27577, ProgramUniformMatrix2dv_remap_index },
   { 26560, ProgramUniformMatrix2fv_remap_index },
   { 27367, ProgramUniformMatrix2x3dv_remap_index },
   { 26746, ProgramUniformMatrix2x3fv_remap_index },
   { 27437, ProgramUniformMatrix2x4dv_remap_index },
   { 26878, ProgramUniformMatrix2x4fv_remap_index },
   { 27610, ProgramUniformMatrix3dv_remap_index },
   { 26622, ProgramUniformMatrix3fv_remap_index },
   { 27402, ProgramUniformMatrix3x2dv_remap_index },
   { 26812, ProgramUniformMatrix3x2fv_remap_index },
   { 27507, ProgramUniformMatrix3x4dv_remap_index },
   { 27010, ProgramUniformMatrix3x4fv_remap_index },
   { 27643, ProgramUniformMatrix4dv_remap_index },
   { 26684, ProgramUniformMatrix4fv_remap_index },
   { 27472, ProgramUniformMatrix4x2dv_remap_index },
   { 26944, ProgramUniformMatrix4x2fv_remap_index },
   { 27542, ProgramUniformMatrix4x3dv_remap_index },
   { 27076, ProgramUniformMatrix4x3fv_remap_index },
   { 38172, UnlockArraysEXT_remap_index },
   { 24977, UseProgramStages_remap_index },
   { 27142, ValidateProgramPipeline_remap_index },
   { 28466, DebugMessageCallback_remap_index },
   { 28309, DebugMessageControl_remap_index },
   { 28389, DebugMessageInsert_remap_index },
   { 28545, GetDebugMessageLog_remap_index },
   { 30084, GetObjectLabel_remap_index },
   { 30170, GetObjectPtrLabel_remap_index },
   { 30047, ObjectLabel_remap_index },
   { 30128, ObjectPtrLabel_remap_index },
   { 30010, PopDebugGroup_remap_index },
   { 29967, PushDebugGroup_remap_index },
   { 11272, SecondaryColor3fEXT_remap_index },
   { 11318, SecondaryColor3fvEXT_remap_index },
   { 10776, MultiDrawElementsEXT_remap_index },
   { 10563, FogCoordfEXT_remap_index },
   { 10593, FogCoordfvEXT_remap_index },
   { 42964, ResizeBuffersMESA_remap_index },
   { 42986, WindowPos4dMESA_remap_index },
   { 43010, WindowPos4dvMESA_remap_index },
   { 43032, WindowPos4fMESA_remap_index },
   { 43056, WindowPos4fvMESA_remap_index },
   { 43078, WindowPos4iMESA_remap_index },
   { 43102, WindowPos4ivMESA_remap_index },
   { 43124, WindowPos4sMESA_remap_index },
   { 43148, WindowPos4svMESA_remap_index },
   { 43170, MultiModeDrawArraysIBM_remap_index },
   { 43202, MultiModeDrawElementsIBM_remap_index },
   { 43984, AreProgramsResidentNV_remap_index },
   { 44013, ExecuteProgramNV_remap_index },
   { 44037, GetProgramParameterdvNV_remap_index },
   { 44069, GetProgramParameterfvNV_remap_index },
   { 44123, GetProgramStringNV_remap_index },
   { 44101, GetProgramivNV_remap_index },
   { 44149, GetTrackMatrixivNV_remap_index },
   { 44176, GetVertexAttribdvNV_remap_index },
   { 44203, GetVertexAttribfvNV_remap_index },
   { 44230, GetVertexAttribivNV_remap_index },
   { 44257, LoadProgramNV_remap_index },
   { 44279, ProgramParameters4dvNV_remap_index },
   { 44310, ProgramParameters4fvNV_remap_index },
   { 44341, RequestResidentProgramsNV_remap_index },
   { 44373, TrackMatrixNV_remap_index },
   { 44814, VertexAttrib1dNV_remap_index },
   { 44837, VertexAttrib1dvNV_remap_index },
   { 44620, VertexAttrib1fNV_remap_index },
   { 44643, VertexAttrib1fvNV_remap_index },
   { 44426, VertexAttrib1sNV_remap_index },
   { 44449, VertexAttrib1svNV_remap_index },
   { 44861, VertexAttrib2dNV_remap_index },
   { 44885, VertexAttrib2dvNV_remap_index },
   { 44667, VertexAttrib2fNV_remap_index },
   { 44691, VertexAttrib2fvNV_remap_index },
   { 44473, VertexAttrib2sNV_remap_index },
   { 44497, VertexAttrib2svNV_remap_index },
   { 44909, VertexAttrib3dNV_remap_index },
   { 44934, VertexAttrib3dvNV_remap_index },
   { 44715, VertexAttrib3fNV_remap_index },
   { 44740, VertexAttrib3fvNV_remap_index },
   { 44521, VertexAttrib3sNV_remap_index },
   { 44546, VertexAttrib3svNV_remap_index },
   { 44958, VertexAttrib4dNV_remap_index },
   { 44984, VertexAttrib4dvNV_remap_index },
   { 44764, VertexAttrib4fNV_remap_index },
   { 44790, VertexAttrib4fvNV_remap_index },
   { 44570, VertexAttrib4sNV_remap_index },
   { 44596, VertexAttrib4svNV_remap_index },
   { 45008, VertexAttrib4ubNV_remap_index },
   { 45035, VertexAttrib4ubvNV_remap_index },
   { 44395, VertexAttribPointerNV_remap_index },
   { 45268, VertexAttribs1dvNV_remap_index },
   { 45164, VertexAttribs1fvNV_remap_index },
   { 45060, VertexAttribs1svNV_remap_index },
   { 45294, VertexAttribs2dvNV_remap_index },
   { 45190, VertexAttribs2fvNV_remap_index },
   { 45086, VertexAttribs2svNV_remap_index },
   { 45320, VertexAttribs3dvNV_remap_index },
   { 45216, VertexAttribs3fvNV_remap_index },
   { 45112, VertexAttribs3svNV_remap_index },
   { 45346, VertexAttribs4dvNV_remap_index },
   { 45242, VertexAttribs4fvNV_remap_index },
   { 45138, VertexAttribs4svNV_remap_index },
   { 45372, VertexAttribs4ubvNV_remap_index },
   { 45455, GetTexBumpParameterfvATI_remap_index },
   { 45486, GetTexBumpParameterivATI_remap_index },
   { 45399, TexBumpParameterfvATI_remap_index },
   { 45427, TexBumpParameterivATI_remap_index },
   { 45797, AlphaFragmentOp1ATI_remap_index },
   { 45827, AlphaFragmentOp2ATI_remap_index },
   { 45860, AlphaFragmentOp3ATI_remap_index },
   { 45600, BeginFragmentShaderATI_remap_index },
   { 45544, BindFragmentShaderATI_remap_index },
   { 45695, ColorFragmentOp1ATI_remap_index },
   { 45726, ColorFragmentOp2ATI_remap_index },
   { 45760, ColorFragmentOp3ATI_remap_index },
   { 45571, DeleteFragmentShaderATI_remap_index },
   { 45627, EndFragmentShaderATI_remap_index },
   { 45517, GenFragmentShadersATI_remap_index },
   { 45652, PassTexCoordATI_remap_index },
   { 45675, SampleMapATI_remap_index },
   { 45896, SetFragmentShaderConstantATI_remap_index },
   { 54083, DepthRangeArrayfvOES_remap_index },
   { 54111, DepthRangeIndexedfOES_remap_index },
   { 45957, ActiveStencilFaceEXT_remap_index },
   { 46363, GetProgramNamedParameterdvNV_remap_index },
   { 46326, GetProgramNamedParameterfvNV_remap_index },
   { 46219, ProgramNamedParameter4dNV_remap_index },
   { 46291, ProgramNamedParameter4dvNV_remap_index },
   { 46182, ProgramNamedParameter4fNV_remap_index },
   { 46256, ProgramNamedParameter4fvNV_remap_index },
   { 50854, PrimitiveRestartNV_remap_index },
   { 53618, GetTexGenxvOES_remap_index },
   { 53640, TexGenxOES_remap_index },
   { 53658, TexGenxvOES_remap_index },
   { 46400, DepthBoundsEXT_remap_index },
   { 46447, BindFramebufferEXT_remap_index },
   { 46421, BindRenderbufferEXT_remap_index },
   { 46472, StringMarkerGREMEDY_remap_index },
   { 46897, BufferParameteriAPPLE_remap_index },
   { 46926, FlushMappedBufferRangeAPPLE_remap_index },
   { 48631, VertexAttribI1iEXT_remap_index },
   { 48809, VertexAttribI1uiEXT_remap_index },
   { 48674, VertexAttribI2iEXT_remap_index },
   { 49040, VertexAttribI2ivEXT_remap_index },
   { 48854, VertexAttribI2uiEXT_remap_index },
   { 49222, VertexAttribI2uivEXT_remap_index },
   { 48718, VertexAttribI3iEXT_remap_index },
   { 49085, VertexAttribI3ivEXT_remap_index },
   { 48900, VertexAttribI3uiEXT_remap_index },
   { 49269, VertexAttribI3uivEXT_remap_index },
   { 48763, VertexAttribI4iEXT_remap_index },
   { 49130, VertexAttribI4ivEXT_remap_index },
   { 48947, VertexAttribI4uiEXT_remap_index },
   { 49316, VertexAttribI4uivEXT_remap_index },
   { 48298, ClearColorIiEXT_remap_index },
   { 48322, ClearColorIuiEXT_remap_index },
   { 50877, BindBufferOffsetEXT_remap_index },
   { 47232, BeginPerfMonitorAMD_remap_index },
   { 47166, DeletePerfMonitorsAMD_remap_index },
   { 47257, EndPerfMonitorAMD_remap_index },
   { 47141, GenPerfMonitorsAMD_remap_index },
   { 47280, GetPerfMonitorCounterDataAMD_remap_index },
   { 47104, GetPerfMonitorCounterInfoAMD_remap_index },
   { 47064, GetPerfMonitorCounterStringAMD_remap_index },
   { 46992, GetPerfMonitorCountersAMD_remap_index },
   { 47027, GetPerfMonitorGroupStringAMD_remap_index },
   { 46961, GetPerfMonitorGroupsAMD_remap_index },
   { 47194, SelectPerfMonitorCountersAMD_remap_index },
   { 46041, GetObjectParameterivAPPLE_remap_index },
   { 45983, ObjectPurgeableAPPLE_remap_index },
   { 46011, ObjectUnpurgeableAPPLE_remap_index },
   { 47405, ActiveProgramEXT_remap_index },
   { 47427, CreateShaderProgramEXT_remap_index },
   { 47379, UseShaderProgramEXT_remap_index },
   { 35391, TextureBarrierNV_remap_index },
   { 51118, VDPAUFiniNV_remap_index },
   { 51259, VDPAUGetSurfaceivNV_remap_index },
   { 51100, VDPAUInitNV_remap_index },
   { 51207, VDPAUIsSurfaceNV_remap_index },
   { 51315, VDPAUMapSurfacesNV_remap_index },
   { 51170, VDPAURegisterOutputSurfaceNV_remap_index },
   { 51134, VDPAURegisterVideoSurfaceNV_remap_index },
   { 51288, VDPAUSurfaceAccessNV_remap_index },
   { 51340, VDPAUUnmapSurfacesNV_remap_index },
   { 51229, VDPAUUnregisterSurfaceNV_remap_index },
   { 47777, BeginPerfQueryINTEL_remap_index },
   { 47724, CreatePerfQueryINTEL_remap_index },
   { 47751, DeletePerfQueryINTEL_remap_index },
   { 47802, EndPerfQueryINTEL_remap_index },
   { 47560, GetFirstPerfQueryIdINTEL_remap_index },
   { 47590, GetNextPerfQueryIdINTEL_remap_index },
   { 47685, GetPerfCounterInfoINTEL_remap_index },
   { 47825, GetPerfQueryDataINTEL_remap_index },
   { 47620, GetPerfQueryIdByNameINTEL_remap_index },
   { 47652, GetPerfQueryInfoINTEL_remap_index },
   { 47856, PolygonOffsetClampEXT_remap_index },
   { 47456, SubpixelPrecisionBiasNV_remap_index },
   { 47486, ConservativeRasterParameterfNV_remap_index },
   { 47523, ConservativeRasterParameteriNV_remap_index },
   { 47906, WindowRectanglesEXT_remap_index },
   { 51720, BufferStorageMemEXT_remap_index },
   { 51474, CreateMemoryObjectsEXT_remap_index },
   { 51422, DeleteMemoryObjectsEXT_remap_index },
   { 52028, DeleteSemaphoresEXT_remap_index },
   { 52005, GenSemaphoresEXT_remap_index },
   { 51537, GetMemoryObjectParameterivEXT_remap_index },
   { 52108, GetSemaphoreParameterui64vEXT_remap_index },
   { 51393, GetUnsignedBytei_vEXT_remap_index },
   { 51367, GetUnsignedBytevEXT_remap_index },
   { 51451, IsMemoryObjectEXT_remap_index },
   { 52054, IsSemaphoreEXT_remap_index },
   { 51503, MemoryObjectParameterivEXT_remap_index },
   { 51910, NamedBufferStorageMemEXT_remap_index },
   { 52074, SemaphoreParameterui64vEXT_remap_index },
   { 52172, SignalSemaphoreEXT_remap_index },
   { 51943, TexStorageMem1DEXT_remap_index },
   { 51574, TexStorageMem2DEXT_remap_index },
   { 51604, TexStorageMem2DMultisampleEXT_remap_index },
   { 51646, TexStorageMem3DEXT_remap_index },
   { 51677, TexStorageMem3DMultisampleEXT_remap_index },
   { 51972, TextureStorageMem1DEXT_remap_index },
   { 51748, TextureStorageMem2DEXT_remap_index },
   { 51782, TextureStorageMem2DMultisampleEXT_remap_index },
   { 51828, TextureStorageMem3DEXT_remap_index },
   { 51863, TextureStorageMem3DMultisampleEXT_remap_index },
   { 52145, WaitSemaphoreEXT_remap_index },
   { 52201, ImportMemoryFdEXT_remap_index },
   { 52227, ImportSemaphoreFdEXT_remap_index },
   { 47933, FramebufferFetchBarrierEXT_remap_index },
   { 47964, StencilFuncSeparateATI_remap_index },
   { 47995, ProgramEnvParameters4fvEXT_remap_index },
   { 48030, ProgramLocalParameters4fvEXT_remap_index },
   { 48255, EGLImageTargetRenderbufferStorageOES_remap_index },
   { 48222, EGLImageTargetTexture2DOES_remap_index },
   { 52282, AlphaFuncx_remap_index },
   { 52315, ClearColorx_remap_index },
   { 52352, ClearDepthx_remap_index },
   { 52386, Color4x_remap_index },
   { 52415, DepthRangex_remap_index },
   { 52450, Fogx_remap_index },
   { 52471, Fogxv_remap_index },
   { 53749, Frustumf_remap_index },
   { 52494, Frustumx_remap_index },
   { 52527, LightModelx_remap_index },
   { 52562, LightModelxv_remap_index },
   { 52599, Lightx_remap_index },
   { 52625, Lightxv_remap_index },
   { 52653, LineWidthx_remap_index },
   { 52685, LoadMatrixx_remap_index },
   { 52719, Materialx_remap_index },
   { 52751, Materialxv_remap_index },
   { 52785, MultMatrixx_remap_index },
   { 52819, MultiTexCoord4x_remap_index },
   { 52865, Normal3x_remap_index },
   { 53782, Orthof_remap_index },
   { 52895, Orthox_remap_index },
   { 52924, PointSizex_remap_index },
   { 52956, PolygonOffsetx_remap_index },
   { 52997, Rotatex_remap_index },
   { 53026, SampleCoveragex_remap_index },
   { 53069, Scalex_remap_index },
   { 53095, TexEnvx_remap_index },
   { 53123, TexEnvxv_remap_index },
   { 53153, TexParameterx_remap_index },
   { 53193, Translatex_remap_index },
   { 53677, ClipPlanef_remap_index },
   { 53227, ClipPlanex_remap_index },
   { 53710, GetClipPlanef_remap_index },
   { 53260, GetClipPlanex_remap_index },
   { 53299, GetFixedv_remap_index },
   { 53330, GetLightxv_remap_index },
   { 53364, GetMaterialxv_remap_index },
   { 53404, GetTexEnvxv_remap_index },
   { 53440, GetTexParameterxv_remap_index },
   { 53488, PointParameterx_remap_index },
   { 53531, PointParameterxv_remap_index },
   { 53576, TexParameterxv_remap_index },
   { 35609, BlendBarrier_remap_index },
   { 35498, PrimitiveBoundingBox_remap_index },
   {    -1, -1 }
};

