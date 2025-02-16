/* DO NOT EDIT - This file generated automatically by gl_table.py (from Mesa) script */

/*
 * (C) Copyright IBM Corporation 2005
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
 * IBM,
 * AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#if !defined( _DISPATCH_H_ )
#  define _DISPATCH_H_


#include "glapitable.h"
/**
 * \file main/dispatch.h
 * Macros for handling GL dispatch tables.
 *
 * For each known GL function, there are 3 macros in this file.  The first
 * macro is named CALL_FuncName and is used to call that GL function using
 * the specified dispatch table.  The other 2 macros, called GET_FuncName
 * can SET_FuncName, are used to get and set the dispatch pointer for the
 * named function in the specified dispatch table.
 */

#include "glheader.h"

#ifdef _MSC_VER
#ifndef INLINE
#define INLINE __inline
#endif
#endif
#define CALL_by_offset(disp, cast, offset, parameters) \
    (*(cast (GET_by_offset(disp, offset)))) parameters
#define GET_by_offset(disp, offset) \
    (offset >= 0) ? (((_glapi_proc *)(disp))[offset]) : NULL
#define SET_by_offset(disp, offset, fn) \
    do { \
        if ( (offset) < 0 ) { \
            /* fprintf( stderr, "[%s:%u] SET_by_offset(%p, %d, %s)!\n", */ \
            /*         __func__, __LINE__, disp, offset, # fn); */ \
            /* abort(); */ \
        } \
        else { \
            ( (_glapi_proc *) (disp) )[offset] = (_glapi_proc) fn; \
        } \
    } while(0)

/* total number of offsets below */
#define _gloffset_COUNT 1678

#define _gloffset_NewList 0
#define _gloffset_EndList 1
#define _gloffset_CallList 2
#define _gloffset_CallLists 3
#define _gloffset_DeleteLists 4
#define _gloffset_GenLists 5
#define _gloffset_ListBase 6
#define _gloffset_Begin 7
#define _gloffset_Bitmap 8
#define _gloffset_Color3b 9
#define _gloffset_Color3bv 10
#define _gloffset_Color3d 11
#define _gloffset_Color3dv 12
#define _gloffset_Color3f 13
#define _gloffset_Color3fv 14
#define _gloffset_Color3i 15
#define _gloffset_Color3iv 16
#define _gloffset_Color3s 17
#define _gloffset_Color3sv 18
#define _gloffset_Color3ub 19
#define _gloffset_Color3ubv 20
#define _gloffset_Color3ui 21
#define _gloffset_Color3uiv 22
#define _gloffset_Color3us 23
#define _gloffset_Color3usv 24
#define _gloffset_Color4b 25
#define _gloffset_Color4bv 26
#define _gloffset_Color4d 27
#define _gloffset_Color4dv 28
#define _gloffset_Color4f 29
#define _gloffset_Color4fv 30
#define _gloffset_Color4i 31
#define _gloffset_Color4iv 32
#define _gloffset_Color4s 33
#define _gloffset_Color4sv 34
#define _gloffset_Color4ub 35
#define _gloffset_Color4ubv 36
#define _gloffset_Color4ui 37
#define _gloffset_Color4uiv 38
#define _gloffset_Color4us 39
#define _gloffset_Color4usv 40
#define _gloffset_EdgeFlag 41
#define _gloffset_EdgeFlagv 42
#define _gloffset_End 43
#define _gloffset_Indexd 44
#define _gloffset_Indexdv 45
#define _gloffset_Indexf 46
#define _gloffset_Indexfv 47
#define _gloffset_Indexi 48
#define _gloffset_Indexiv 49
#define _gloffset_Indexs 50
#define _gloffset_Indexsv 51
#define _gloffset_Normal3b 52
#define _gloffset_Normal3bv 53
#define _gloffset_Normal3d 54
#define _gloffset_Normal3dv 55
#define _gloffset_Normal3f 56
#define _gloffset_Normal3fv 57
#define _gloffset_Normal3i 58
#define _gloffset_Normal3iv 59
#define _gloffset_Normal3s 60
#define _gloffset_Normal3sv 61
#define _gloffset_RasterPos2d 62
#define _gloffset_RasterPos2dv 63
#define _gloffset_RasterPos2f 64
#define _gloffset_RasterPos2fv 65
#define _gloffset_RasterPos2i 66
#define _gloffset_RasterPos2iv 67
#define _gloffset_RasterPos2s 68
#define _gloffset_RasterPos2sv 69
#define _gloffset_RasterPos3d 70
#define _gloffset_RasterPos3dv 71
#define _gloffset_RasterPos3f 72
#define _gloffset_RasterPos3fv 73
#define _gloffset_RasterPos3i 74
#define _gloffset_RasterPos3iv 75
#define _gloffset_RasterPos3s 76
#define _gloffset_RasterPos3sv 77
#define _gloffset_RasterPos4d 78
#define _gloffset_RasterPos4dv 79
#define _gloffset_RasterPos4f 80
#define _gloffset_RasterPos4fv 81
#define _gloffset_RasterPos4i 82
#define _gloffset_RasterPos4iv 83
#define _gloffset_RasterPos4s 84
#define _gloffset_RasterPos4sv 85
#define _gloffset_Rectd 86
#define _gloffset_Rectdv 87
#define _gloffset_Rectf 88
#define _gloffset_Rectfv 89
#define _gloffset_Recti 90
#define _gloffset_Rectiv 91
#define _gloffset_Rects 92
#define _gloffset_Rectsv 93
#define _gloffset_TexCoord1d 94
#define _gloffset_TexCoord1dv 95
#define _gloffset_TexCoord1f 96
#define _gloffset_TexCoord1fv 97
#define _gloffset_TexCoord1i 98
#define _gloffset_TexCoord1iv 99
#define _gloffset_TexCoord1s 100
#define _gloffset_TexCoord1sv 101
#define _gloffset_TexCoord2d 102
#define _gloffset_TexCoord2dv 103
#define _gloffset_TexCoord2f 104
#define _gloffset_TexCoord2fv 105
#define _gloffset_TexCoord2i 106
#define _gloffset_TexCoord2iv 107
#define _gloffset_TexCoord2s 108
#define _gloffset_TexCoord2sv 109
#define _gloffset_TexCoord3d 110
#define _gloffset_TexCoord3dv 111
#define _gloffset_TexCoord3f 112
#define _gloffset_TexCoord3fv 113
#define _gloffset_TexCoord3i 114
#define _gloffset_TexCoord3iv 115
#define _gloffset_TexCoord3s 116
#define _gloffset_TexCoord3sv 117
#define _gloffset_TexCoord4d 118
#define _gloffset_TexCoord4dv 119
#define _gloffset_TexCoord4f 120
#define _gloffset_TexCoord4fv 121
#define _gloffset_TexCoord4i 122
#define _gloffset_TexCoord4iv 123
#define _gloffset_TexCoord4s 124
#define _gloffset_TexCoord4sv 125
#define _gloffset_Vertex2d 126
#define _gloffset_Vertex2dv 127
#define _gloffset_Vertex2f 128
#define _gloffset_Vertex2fv 129
#define _gloffset_Vertex2i 130
#define _gloffset_Vertex2iv 131
#define _gloffset_Vertex2s 132
#define _gloffset_Vertex2sv 133
#define _gloffset_Vertex3d 134
#define _gloffset_Vertex3dv 135
#define _gloffset_Vertex3f 136
#define _gloffset_Vertex3fv 137
#define _gloffset_Vertex3i 138
#define _gloffset_Vertex3iv 139
#define _gloffset_Vertex3s 140
#define _gloffset_Vertex3sv 141
#define _gloffset_Vertex4d 142
#define _gloffset_Vertex4dv 143
#define _gloffset_Vertex4f 144
#define _gloffset_Vertex4fv 145
#define _gloffset_Vertex4i 146
#define _gloffset_Vertex4iv 147
#define _gloffset_Vertex4s 148
#define _gloffset_Vertex4sv 149
#define _gloffset_ClipPlane 150
#define _gloffset_ColorMaterial 151
#define _gloffset_CullFace 152
#define _gloffset_Fogf 153
#define _gloffset_Fogfv 154
#define _gloffset_Fogi 155
#define _gloffset_Fogiv 156
#define _gloffset_FrontFace 157
#define _gloffset_Hint 158
#define _gloffset_Lightf 159
#define _gloffset_Lightfv 160
#define _gloffset_Lighti 161
#define _gloffset_Lightiv 162
#define _gloffset_LightModelf 163
#define _gloffset_LightModelfv 164
#define _gloffset_LightModeli 165
#define _gloffset_LightModeliv 166
#define _gloffset_LineStipple 167
#define _gloffset_LineWidth 168
#define _gloffset_Materialf 169
#define _gloffset_Materialfv 170
#define _gloffset_Materiali 171
#define _gloffset_Materialiv 172
#define _gloffset_PointSize 173
#define _gloffset_PolygonMode 174
#define _gloffset_PolygonStipple 175
#define _gloffset_Scissor 176
#define _gloffset_ShadeModel 177
#define _gloffset_TexParameterf 178
#define _gloffset_TexParameterfv 179
#define _gloffset_TexParameteri 180
#define _gloffset_TexParameteriv 181
#define _gloffset_TexImage1D 182
#define _gloffset_TexImage2D 183
#define _gloffset_TexEnvf 184
#define _gloffset_TexEnvfv 185
#define _gloffset_TexEnvi 186
#define _gloffset_TexEnviv 187
#define _gloffset_TexGend 188
#define _gloffset_TexGendv 189
#define _gloffset_TexGenf 190
#define _gloffset_TexGenfv 191
#define _gloffset_TexGeni 192
#define _gloffset_TexGeniv 193
#define _gloffset_FeedbackBuffer 194
#define _gloffset_SelectBuffer 195
#define _gloffset_RenderMode 196
#define _gloffset_InitNames 197
#define _gloffset_LoadName 198
#define _gloffset_PassThrough 199
#define _gloffset_PopName 200
#define _gloffset_PushName 201
#define _gloffset_DrawBuffer 202
#define _gloffset_Clear 203
#define _gloffset_ClearAccum 204
#define _gloffset_ClearIndex 205
#define _gloffset_ClearColor 206
#define _gloffset_ClearStencil 207
#define _gloffset_ClearDepth 208
#define _gloffset_StencilMask 209
#define _gloffset_ColorMask 210
#define _gloffset_DepthMask 211
#define _gloffset_IndexMask 212
#define _gloffset_Accum 213
#define _gloffset_Disable 214
#define _gloffset_Enable 215
#define _gloffset_Finish 216
#define _gloffset_Flush 217
#define _gloffset_PopAttrib 218
#define _gloffset_PushAttrib 219
#define _gloffset_Map1d 220
#define _gloffset_Map1f 221
#define _gloffset_Map2d 222
#define _gloffset_Map2f 223
#define _gloffset_MapGrid1d 224
#define _gloffset_MapGrid1f 225
#define _gloffset_MapGrid2d 226
#define _gloffset_MapGrid2f 227
#define _gloffset_EvalCoord1d 228
#define _gloffset_EvalCoord1dv 229
#define _gloffset_EvalCoord1f 230
#define _gloffset_EvalCoord1fv 231
#define _gloffset_EvalCoord2d 232
#define _gloffset_EvalCoord2dv 233
#define _gloffset_EvalCoord2f 234
#define _gloffset_EvalCoord2fv 235
#define _gloffset_EvalMesh1 236
#define _gloffset_EvalPoint1 237
#define _gloffset_EvalMesh2 238
#define _gloffset_EvalPoint2 239
#define _gloffset_AlphaFunc 240
#define _gloffset_BlendFunc 241
#define _gloffset_LogicOp 242
#define _gloffset_StencilFunc 243
#define _gloffset_StencilOp 244
#define _gloffset_DepthFunc 245
#define _gloffset_PixelZoom 246
#define _gloffset_PixelTransferf 247
#define _gloffset_PixelTransferi 248
#define _gloffset_PixelStoref 249
#define _gloffset_PixelStorei 250
#define _gloffset_PixelMapfv 251
#define _gloffset_PixelMapuiv 252
#define _gloffset_PixelMapusv 253
#define _gloffset_ReadBuffer 254
#define _gloffset_CopyPixels 255
#define _gloffset_ReadPixels 256
#define _gloffset_DrawPixels 257
#define _gloffset_GetBooleanv 258
#define _gloffset_GetClipPlane 259
#define _gloffset_GetDoublev 260
#define _gloffset_GetError 261
#define _gloffset_GetFloatv 262
#define _gloffset_GetIntegerv 263
#define _gloffset_GetLightfv 264
#define _gloffset_GetLightiv 265
#define _gloffset_GetMapdv 266
#define _gloffset_GetMapfv 267
#define _gloffset_GetMapiv 268
#define _gloffset_GetMaterialfv 269
#define _gloffset_GetMaterialiv 270
#define _gloffset_GetPixelMapfv 271
#define _gloffset_GetPixelMapuiv 272
#define _gloffset_GetPixelMapusv 273
#define _gloffset_GetPolygonStipple 274
#define _gloffset_GetString 275
#define _gloffset_GetTexEnvfv 276
#define _gloffset_GetTexEnviv 277
#define _gloffset_GetTexGendv 278
#define _gloffset_GetTexGenfv 279
#define _gloffset_GetTexGeniv 280
#define _gloffset_GetTexImage 281
#define _gloffset_GetTexParameterfv 282
#define _gloffset_GetTexParameteriv 283
#define _gloffset_GetTexLevelParameterfv 284
#define _gloffset_GetTexLevelParameteriv 285
#define _gloffset_IsEnabled 286
#define _gloffset_IsList 287
#define _gloffset_DepthRange 288
#define _gloffset_Frustum 289
#define _gloffset_LoadIdentity 290
#define _gloffset_LoadMatrixf 291
#define _gloffset_LoadMatrixd 292
#define _gloffset_MatrixMode 293
#define _gloffset_MultMatrixf 294
#define _gloffset_MultMatrixd 295
#define _gloffset_Ortho 296
#define _gloffset_PopMatrix 297
#define _gloffset_PushMatrix 298
#define _gloffset_Rotated 299
#define _gloffset_Rotatef 300
#define _gloffset_Scaled 301
#define _gloffset_Scalef 302
#define _gloffset_Translated 303
#define _gloffset_Translatef 304
#define _gloffset_Viewport 305
#define _gloffset_ArrayElement 306
#define _gloffset_BindTexture 307
#define _gloffset_ColorPointer 308
#define _gloffset_DisableClientState 309
#define _gloffset_DrawArrays 310
#define _gloffset_DrawElements 311
#define _gloffset_EdgeFlagPointer 312
#define _gloffset_EnableClientState 313
#define _gloffset_IndexPointer 314
#define _gloffset_Indexub 315
#define _gloffset_Indexubv 316
#define _gloffset_InterleavedArrays 317
#define _gloffset_NormalPointer 318
#define _gloffset_PolygonOffset 319
#define _gloffset_TexCoordPointer 320
#define _gloffset_VertexPointer 321
#define _gloffset_AreTexturesResident 322
#define _gloffset_CopyTexImage1D 323
#define _gloffset_CopyTexImage2D 324
#define _gloffset_CopyTexSubImage1D 325
#define _gloffset_CopyTexSubImage2D 326
#define _gloffset_DeleteTextures 327
#define _gloffset_GenTextures 328
#define _gloffset_GetPointerv 329
#define _gloffset_IsTexture 330
#define _gloffset_PrioritizeTextures 331
#define _gloffset_TexSubImage1D 332
#define _gloffset_TexSubImage2D 333
#define _gloffset_PopClientAttrib 334
#define _gloffset_PushClientAttrib 335
#define _gloffset_BlendColor 336
#define _gloffset_BlendEquation 337
#define _gloffset_DrawRangeElements 338
#define _gloffset_ColorTable 339
#define _gloffset_ColorTableParameterfv 340
#define _gloffset_ColorTableParameteriv 341
#define _gloffset_CopyColorTable 342
#define _gloffset_GetColorTable 343
#define _gloffset_GetColorTableParameterfv 344
#define _gloffset_GetColorTableParameteriv 345
#define _gloffset_ColorSubTable 346
#define _gloffset_CopyColorSubTable 347
#define _gloffset_ConvolutionFilter1D 348
#define _gloffset_ConvolutionFilter2D 349
#define _gloffset_ConvolutionParameterf 350
#define _gloffset_ConvolutionParameterfv 351
#define _gloffset_ConvolutionParameteri 352
#define _gloffset_ConvolutionParameteriv 353
#define _gloffset_CopyConvolutionFilter1D 354
#define _gloffset_CopyConvolutionFilter2D 355
#define _gloffset_GetConvolutionFilter 356
#define _gloffset_GetConvolutionParameterfv 357
#define _gloffset_GetConvolutionParameteriv 358
#define _gloffset_GetSeparableFilter 359
#define _gloffset_SeparableFilter2D 360
#define _gloffset_GetHistogram 361
#define _gloffset_GetHistogramParameterfv 362
#define _gloffset_GetHistogramParameteriv 363
#define _gloffset_GetMinmax 364
#define _gloffset_GetMinmaxParameterfv 365
#define _gloffset_GetMinmaxParameteriv 366
#define _gloffset_Histogram 367
#define _gloffset_Minmax 368
#define _gloffset_ResetHistogram 369
#define _gloffset_ResetMinmax 370
#define _gloffset_TexImage3D 371
#define _gloffset_TexSubImage3D 372
#define _gloffset_CopyTexSubImage3D 373
#define _gloffset_ActiveTexture 374
#define _gloffset_ClientActiveTexture 375
#define _gloffset_MultiTexCoord1d 376
#define _gloffset_MultiTexCoord1dv 377
#define _gloffset_MultiTexCoord1fARB 378
#define _gloffset_MultiTexCoord1fvARB 379
#define _gloffset_MultiTexCoord1i 380
#define _gloffset_MultiTexCoord1iv 381
#define _gloffset_MultiTexCoord1s 382
#define _gloffset_MultiTexCoord1sv 383
#define _gloffset_MultiTexCoord2d 384
#define _gloffset_MultiTexCoord2dv 385
#define _gloffset_MultiTexCoord2fARB 386
#define _gloffset_MultiTexCoord2fvARB 387
#define _gloffset_MultiTexCoord2i 388
#define _gloffset_MultiTexCoord2iv 389
#define _gloffset_MultiTexCoord2s 390
#define _gloffset_MultiTexCoord2sv 391
#define _gloffset_MultiTexCoord3d 392
#define _gloffset_MultiTexCoord3dv 393
#define _gloffset_MultiTexCoord3fARB 394
#define _gloffset_MultiTexCoord3fvARB 395
#define _gloffset_MultiTexCoord3i 396
#define _gloffset_MultiTexCoord3iv 397
#define _gloffset_MultiTexCoord3s 398
#define _gloffset_MultiTexCoord3sv 399
#define _gloffset_MultiTexCoord4d 400
#define _gloffset_MultiTexCoord4dv 401
#define _gloffset_MultiTexCoord4fARB 402
#define _gloffset_MultiTexCoord4fvARB 403
#define _gloffset_MultiTexCoord4i 404
#define _gloffset_MultiTexCoord4iv 405
#define _gloffset_MultiTexCoord4s 406
#define _gloffset_MultiTexCoord4sv 407
#define _gloffset_CompressedTexImage1D 408
#define _gloffset_CompressedTexImage2D 409
#define _gloffset_CompressedTexImage3D 410
#define _gloffset_CompressedTexSubImage1D 411
#define _gloffset_CompressedTexSubImage2D 412
#define _gloffset_CompressedTexSubImage3D 413
#define _gloffset_GetCompressedTexImage 414
#define _gloffset_LoadTransposeMatrixd 415
#define _gloffset_LoadTransposeMatrixf 416
#define _gloffset_MultTransposeMatrixd 417
#define _gloffset_MultTransposeMatrixf 418
#define _gloffset_SampleCoverage 419
#define _gloffset_BlendFuncSeparate 420
#define _gloffset_FogCoordPointer 421
#define _gloffset_FogCoordd 422
#define _gloffset_FogCoorddv 423
#define _gloffset_MultiDrawArrays 424
#define _gloffset_PointParameterf 425
#define _gloffset_PointParameterfv 426
#define _gloffset_PointParameteri 427
#define _gloffset_PointParameteriv 428
#define _gloffset_SecondaryColor3b 429
#define _gloffset_SecondaryColor3bv 430
#define _gloffset_SecondaryColor3d 431
#define _gloffset_SecondaryColor3dv 432
#define _gloffset_SecondaryColor3i 433
#define _gloffset_SecondaryColor3iv 434
#define _gloffset_SecondaryColor3s 435
#define _gloffset_SecondaryColor3sv 436
#define _gloffset_SecondaryColor3ub 437
#define _gloffset_SecondaryColor3ubv 438
#define _gloffset_SecondaryColor3ui 439
#define _gloffset_SecondaryColor3uiv 440
#define _gloffset_SecondaryColor3us 441
#define _gloffset_SecondaryColor3usv 442
#define _gloffset_SecondaryColorPointer 443
#define _gloffset_WindowPos2d 444
#define _gloffset_WindowPos2dv 445
#define _gloffset_WindowPos2f 446
#define _gloffset_WindowPos2fv 447
#define _gloffset_WindowPos2i 448
#define _gloffset_WindowPos2iv 449
#define _gloffset_WindowPos2s 450
#define _gloffset_WindowPos2sv 451
#define _gloffset_WindowPos3d 452
#define _gloffset_WindowPos3dv 453
#define _gloffset_WindowPos3f 454
#define _gloffset_WindowPos3fv 455
#define _gloffset_WindowPos3i 456
#define _gloffset_WindowPos3iv 457
#define _gloffset_WindowPos3s 458
#define _gloffset_WindowPos3sv 459
#define _gloffset_BeginQuery 460
#define _gloffset_BindBuffer 461
#define _gloffset_BufferData 462
#define _gloffset_BufferSubData 463
#define _gloffset_DeleteBuffers 464
#define _gloffset_DeleteQueries 465
#define _gloffset_EndQuery 466
#define _gloffset_GenBuffers 467
#define _gloffset_GenQueries 468
#define _gloffset_GetBufferParameteriv 469
#define _gloffset_GetBufferPointerv 470
#define _gloffset_GetBufferSubData 471
#define _gloffset_GetQueryObjectiv 472
#define _gloffset_GetQueryObjectuiv 473
#define _gloffset_GetQueryiv 474
#define _gloffset_IsBuffer 475
#define _gloffset_IsQuery 476
#define _gloffset_MapBuffer 477
#define _gloffset_UnmapBuffer 478
#define _gloffset_AttachShader 479
#define _gloffset_BindAttribLocation 480
#define _gloffset_BlendEquationSeparate 481
#define _gloffset_CompileShader 482
#define _gloffset_CreateProgram 483
#define _gloffset_CreateShader 484
#define _gloffset_DeleteProgram 485
#define _gloffset_DeleteShader 486
#define _gloffset_DetachShader 487
#define _gloffset_DisableVertexAttribArray 488
#define _gloffset_DrawBuffers 489
#define _gloffset_EnableVertexAttribArray 490
#define _gloffset_GetActiveAttrib 491
#define _gloffset_GetActiveUniform 492
#define _gloffset_GetAttachedShaders 493
#define _gloffset_GetAttribLocation 494
#define _gloffset_GetProgramInfoLog 495
#define _gloffset_GetProgramiv 496
#define _gloffset_GetShaderInfoLog 497
#define _gloffset_GetShaderSource 498
#define _gloffset_GetShaderiv 499
#define _gloffset_GetUniformLocation 500
#define _gloffset_GetUniformfv 501
#define _gloffset_GetUniformiv 502
#define _gloffset_GetVertexAttribPointerv 503
#define _gloffset_GetVertexAttribdv 504
#define _gloffset_GetVertexAttribfv 505
#define _gloffset_GetVertexAttribiv 506
#define _gloffset_IsProgram 507
#define _gloffset_IsShader 508
#define _gloffset_LinkProgram 509
#define _gloffset_ShaderSource 510
#define _gloffset_StencilFuncSeparate 511
#define _gloffset_StencilMaskSeparate 512
#define _gloffset_StencilOpSeparate 513
#define _gloffset_Uniform1f 514
#define _gloffset_Uniform1fv 515
#define _gloffset_Uniform1i 516
#define _gloffset_Uniform1iv 517
#define _gloffset_Uniform2f 518
#define _gloffset_Uniform2fv 519
#define _gloffset_Uniform2i 520
#define _gloffset_Uniform2iv 521
#define _gloffset_Uniform3f 522
#define _gloffset_Uniform3fv 523
#define _gloffset_Uniform3i 524
#define _gloffset_Uniform3iv 525
#define _gloffset_Uniform4f 526
#define _gloffset_Uniform4fv 527
#define _gloffset_Uniform4i 528
#define _gloffset_Uniform4iv 529
#define _gloffset_UniformMatrix2fv 530
#define _gloffset_UniformMatrix3fv 531
#define _gloffset_UniformMatrix4fv 532
#define _gloffset_UseProgram 533
#define _gloffset_ValidateProgram 534
#define _gloffset_VertexAttrib1d 535
#define _gloffset_VertexAttrib1dv 536
#define _gloffset_VertexAttrib1s 537
#define _gloffset_VertexAttrib1sv 538
#define _gloffset_VertexAttrib2d 539
#define _gloffset_VertexAttrib2dv 540
#define _gloffset_VertexAttrib2s 541
#define _gloffset_VertexAttrib2sv 542
#define _gloffset_VertexAttrib3d 543
#define _gloffset_VertexAttrib3dv 544
#define _gloffset_VertexAttrib3s 545
#define _gloffset_VertexAttrib3sv 546
#define _gloffset_VertexAttrib4Nbv 547
#define _gloffset_VertexAttrib4Niv 548
#define _gloffset_VertexAttrib4Nsv 549
#define _gloffset_VertexAttrib4Nub 550
#define _gloffset_VertexAttrib4Nubv 551
#define _gloffset_VertexAttrib4Nuiv 552
#define _gloffset_VertexAttrib4Nusv 553
#define _gloffset_VertexAttrib4bv 554
#define _gloffset_VertexAttrib4d 555
#define _gloffset_VertexAttrib4dv 556
#define _gloffset_VertexAttrib4iv 557
#define _gloffset_VertexAttrib4s 558
#define _gloffset_VertexAttrib4sv 559
#define _gloffset_VertexAttrib4ubv 560
#define _gloffset_VertexAttrib4uiv 561
#define _gloffset_VertexAttrib4usv 562
#define _gloffset_VertexAttribPointer 563
#define _gloffset_UniformMatrix2x3fv 564
#define _gloffset_UniformMatrix2x4fv 565
#define _gloffset_UniformMatrix3x2fv 566
#define _gloffset_UniformMatrix3x4fv 567
#define _gloffset_UniformMatrix4x2fv 568
#define _gloffset_UniformMatrix4x3fv 569
#define _gloffset_BeginConditionalRender 570
#define _gloffset_BeginTransformFeedback 571
#define _gloffset_BindBufferBase 572
#define _gloffset_BindBufferRange 573
#define _gloffset_BindFragDataLocation 574
#define _gloffset_ClampColor 575
#define _gloffset_ClearBufferfi 576
#define _gloffset_ClearBufferfv 577
#define _gloffset_ClearBufferiv 578
#define _gloffset_ClearBufferuiv 579
#define _gloffset_ColorMaski 580
#define _gloffset_Disablei 581
#define _gloffset_Enablei 582
#define _gloffset_EndConditionalRender 583
#define _gloffset_EndTransformFeedback 584
#define _gloffset_GetBooleani_v 585
#define _gloffset_GetFragDataLocation 586
#define _gloffset_GetIntegeri_v 587
#define _gloffset_GetStringi 588
#define _gloffset_GetTexParameterIiv 589
#define _gloffset_GetTexParameterIuiv 590
#define _gloffset_GetTransformFeedbackVarying 591
#define _gloffset_GetUniformuiv 592
#define _gloffset_GetVertexAttribIiv 593
#define _gloffset_GetVertexAttribIuiv 594
#define _gloffset_IsEnabledi 595
#define _gloffset_TexParameterIiv 596
#define _gloffset_TexParameterIuiv 597
#define _gloffset_TransformFeedbackVaryings 598
#define _gloffset_Uniform1ui 599
#define _gloffset_Uniform1uiv 600
#define _gloffset_Uniform2ui 601
#define _gloffset_Uniform2uiv 602
#define _gloffset_Uniform3ui 603
#define _gloffset_Uniform3uiv 604
#define _gloffset_Uniform4ui 605
#define _gloffset_Uniform4uiv 606
#define _gloffset_VertexAttribI1iv 607
#define _gloffset_VertexAttribI1uiv 608
#define _gloffset_VertexAttribI4bv 609
#define _gloffset_VertexAttribI4sv 610
#define _gloffset_VertexAttribI4ubv 611
#define _gloffset_VertexAttribI4usv 612
#define _gloffset_VertexAttribIPointer 613
#define _gloffset_PrimitiveRestartIndex 614
#define _gloffset_TexBuffer 615
#define _gloffset_FramebufferTexture 616
#define _gloffset_GetBufferParameteri64v 617
#define _gloffset_GetInteger64i_v 618
#define _gloffset_VertexAttribDivisor 619
#define _gloffset_MinSampleShading 620
#define _gloffset_MemoryBarrierByRegion 621
#define _gloffset_BindProgramARB 622
#define _gloffset_DeleteProgramsARB 623
#define _gloffset_GenProgramsARB 624
#define _gloffset_GetProgramEnvParameterdvARB 625
#define _gloffset_GetProgramEnvParameterfvARB 626
#define _gloffset_GetProgramLocalParameterdvARB 627
#define _gloffset_GetProgramLocalParameterfvARB 628
#define _gloffset_GetProgramStringARB 629
#define _gloffset_GetProgramivARB 630
#define _gloffset_IsProgramARB 631
#define _gloffset_ProgramEnvParameter4dARB 632
#define _gloffset_ProgramEnvParameter4dvARB 633
#define _gloffset_ProgramEnvParameter4fARB 634
#define _gloffset_ProgramEnvParameter4fvARB 635
#define _gloffset_ProgramLocalParameter4dARB 636
#define _gloffset_ProgramLocalParameter4dvARB 637
#define _gloffset_ProgramLocalParameter4fARB 638
#define _gloffset_ProgramLocalParameter4fvARB 639
#define _gloffset_ProgramStringARB 640
#define _gloffset_VertexAttrib1fARB 641
#define _gloffset_VertexAttrib1fvARB 642
#define _gloffset_VertexAttrib2fARB 643
#define _gloffset_VertexAttrib2fvARB 644
#define _gloffset_VertexAttrib3fARB 645
#define _gloffset_VertexAttrib3fvARB 646
#define _gloffset_VertexAttrib4fARB 647
#define _gloffset_VertexAttrib4fvARB 648
#define _gloffset_AttachObjectARB 649
#define _gloffset_CreateProgramObjectARB 650
#define _gloffset_CreateShaderObjectARB 651
#define _gloffset_DeleteObjectARB 652
#define _gloffset_DetachObjectARB 653
#define _gloffset_GetAttachedObjectsARB 654
#define _gloffset_GetHandleARB 655
#define _gloffset_GetInfoLogARB 656
#define _gloffset_GetObjectParameterfvARB 657
#define _gloffset_GetObjectParameterivARB 658
#define _gloffset_DrawArraysInstanced 659
#define _gloffset_DrawElementsInstanced 660
#define _gloffset_BindFramebuffer 661
#define _gloffset_BindRenderbuffer 662
#define _gloffset_BlitFramebuffer 663
#define _gloffset_CheckFramebufferStatus 664
#define _gloffset_DeleteFramebuffers 665
#define _gloffset_DeleteRenderbuffers 666
#define _gloffset_FramebufferRenderbuffer 667
#define _gloffset_FramebufferTexture1D 668
#define _gloffset_FramebufferTexture2D 669
#define _gloffset_FramebufferTexture3D 670
#define _gloffset_FramebufferTextureLayer 671
#define _gloffset_GenFramebuffers 672
#define _gloffset_GenRenderbuffers 673
#define _gloffset_GenerateMipmap 674
#define _gloffset_GetFramebufferAttachmentParameteriv 675
#define _gloffset_GetRenderbufferParameteriv 676
#define _gloffset_IsFramebuffer 677
#define _gloffset_IsRenderbuffer 678
#define _gloffset_RenderbufferStorage 679
#define _gloffset_RenderbufferStorageMultisample 680
#define _gloffset_FlushMappedBufferRange 681
#define _gloffset_MapBufferRange 682
#define _gloffset_BindVertexArray 683
#define _gloffset_DeleteVertexArrays 684
#define _gloffset_GenVertexArrays 685
#define _gloffset_IsVertexArray 686
#define _gloffset_GetActiveUniformBlockName 687
#define _gloffset_GetActiveUniformBlockiv 688
#define _gloffset_GetActiveUniformName 689
#define _gloffset_GetActiveUniformsiv 690
#define _gloffset_GetUniformBlockIndex 691
#define _gloffset_GetUniformIndices 692
#define _gloffset_UniformBlockBinding 693
#define _gloffset_CopyBufferSubData 694
#define _gloffset_ClientWaitSync 695
#define _gloffset_DeleteSync 696
#define _gloffset_FenceSync 697
#define _gloffset_GetInteger64v 698
#define _gloffset_GetSynciv 699
#define _gloffset_IsSync 700
#define _gloffset_WaitSync 701
#define _gloffset_DrawElementsBaseVertex 702
#define _gloffset_DrawElementsInstancedBaseVertex 703
#define _gloffset_DrawRangeElementsBaseVertex 704
#define _gloffset_MultiDrawElementsBaseVertex 705
#define _gloffset_ProvokingVertex 706
#define _gloffset_GetMultisamplefv 707
#define _gloffset_SampleMaski 708
#define _gloffset_TexImage2DMultisample 709
#define _gloffset_TexImage3DMultisample 710
#define _gloffset_BlendEquationSeparateiARB 711
#define _gloffset_BlendEquationiARB 712
#define _gloffset_BlendFuncSeparateiARB 713
#define _gloffset_BlendFunciARB 714
#define _gloffset_BindFragDataLocationIndexed 715
#define _gloffset_GetFragDataIndex 716
#define _gloffset_BindSampler 717
#define _gloffset_DeleteSamplers 718
#define _gloffset_GenSamplers 719
#define _gloffset_GetSamplerParameterIiv 720
#define _gloffset_GetSamplerParameterIuiv 721
#define _gloffset_GetSamplerParameterfv 722
#define _gloffset_GetSamplerParameteriv 723
#define _gloffset_IsSampler 724
#define _gloffset_SamplerParameterIiv 725
#define _gloffset_SamplerParameterIuiv 726
#define _gloffset_SamplerParameterf 727
#define _gloffset_SamplerParameterfv 728
#define _gloffset_SamplerParameteri 729
#define _gloffset_SamplerParameteriv 730
#define _gloffset_GetQueryObjecti64v 731
#define _gloffset_GetQueryObjectui64v 732
#define _gloffset_QueryCounter 733
#define _gloffset_ColorP3ui 734
#define _gloffset_ColorP3uiv 735
#define _gloffset_ColorP4ui 736
#define _gloffset_ColorP4uiv 737
#define _gloffset_MultiTexCoordP1ui 738
#define _gloffset_MultiTexCoordP1uiv 739
#define _gloffset_MultiTexCoordP2ui 740
#define _gloffset_MultiTexCoordP2uiv 741
#define _gloffset_MultiTexCoordP3ui 742
#define _gloffset_MultiTexCoordP3uiv 743
#define _gloffset_MultiTexCoordP4ui 744
#define _gloffset_MultiTexCoordP4uiv 745
#define _gloffset_NormalP3ui 746
#define _gloffset_NormalP3uiv 747
#define _gloffset_SecondaryColorP3ui 748
#define _gloffset_SecondaryColorP3uiv 749
#define _gloffset_TexCoordP1ui 750
#define _gloffset_TexCoordP1uiv 751
#define _gloffset_TexCoordP2ui 752
#define _gloffset_TexCoordP2uiv 753
#define _gloffset_TexCoordP3ui 754
#define _gloffset_TexCoordP3uiv 755
#define _gloffset_TexCoordP4ui 756
#define _gloffset_TexCoordP4uiv 757
#define _gloffset_VertexAttribP1ui 758
#define _gloffset_VertexAttribP1uiv 759
#define _gloffset_VertexAttribP2ui 760
#define _gloffset_VertexAttribP2uiv 761
#define _gloffset_VertexAttribP3ui 762
#define _gloffset_VertexAttribP3uiv 763
#define _gloffset_VertexAttribP4ui 764
#define _gloffset_VertexAttribP4uiv 765
#define _gloffset_VertexP2ui 766
#define _gloffset_VertexP2uiv 767
#define _gloffset_VertexP3ui 768
#define _gloffset_VertexP3uiv 769
#define _gloffset_VertexP4ui 770
#define _gloffset_VertexP4uiv 771
#define _gloffset_DrawArraysIndirect 772
#define _gloffset_DrawElementsIndirect 773
#define _gloffset_GetUniformdv 774
#define _gloffset_Uniform1d 775
#define _gloffset_Uniform1dv 776
#define _gloffset_Uniform2d 777
#define _gloffset_Uniform2dv 778
#define _gloffset_Uniform3d 779
#define _gloffset_Uniform3dv 780
#define _gloffset_Uniform4d 781
#define _gloffset_Uniform4dv 782
#define _gloffset_UniformMatrix2dv 783
#define _gloffset_UniformMatrix2x3dv 784
#define _gloffset_UniformMatrix2x4dv 785
#define _gloffset_UniformMatrix3dv 786
#define _gloffset_UniformMatrix3x2dv 787
#define _gloffset_UniformMatrix3x4dv 788
#define _gloffset_UniformMatrix4dv 789
#define _gloffset_UniformMatrix4x2dv 790
#define _gloffset_UniformMatrix4x3dv 791
#define _gloffset_GetActiveSubroutineName 792
#define _gloffset_GetActiveSubroutineUniformName 793
#define _gloffset_GetActiveSubroutineUniformiv 794
#define _gloffset_GetProgramStageiv 795
#define _gloffset_GetSubroutineIndex 796
#define _gloffset_GetSubroutineUniformLocation 797
#define _gloffset_GetUniformSubroutineuiv 798
#define _gloffset_UniformSubroutinesuiv 799
#define _gloffset_PatchParameterfv 800
#define _gloffset_PatchParameteri 801
#define _gloffset_BindTransformFeedback 802
#define _gloffset_DeleteTransformFeedbacks 803
#define _gloffset_DrawTransformFeedback 804
#define _gloffset_GenTransformFeedbacks 805
#define _gloffset_IsTransformFeedback 806
#define _gloffset_PauseTransformFeedback 807
#define _gloffset_ResumeTransformFeedback 808
#define _gloffset_BeginQueryIndexed 809
#define _gloffset_DrawTransformFeedbackStream 810
#define _gloffset_EndQueryIndexed 811
#define _gloffset_GetQueryIndexediv 812
#define _gloffset_ClearDepthf 813
#define _gloffset_DepthRangef 814
#define _gloffset_GetShaderPrecisionFormat 815
#define _gloffset_ReleaseShaderCompiler 816
#define _gloffset_ShaderBinary 817
#define _gloffset_GetProgramBinary 818
#define _gloffset_ProgramBinary 819
#define _gloffset_ProgramParameteri 820
#define _gloffset_GetVertexAttribLdv 821
#define _gloffset_VertexAttribL1d 822
#define _gloffset_VertexAttribL1dv 823
#define _gloffset_VertexAttribL2d 824
#define _gloffset_VertexAttribL2dv 825
#define _gloffset_VertexAttribL3d 826
#define _gloffset_VertexAttribL3dv 827
#define _gloffset_VertexAttribL4d 828
#define _gloffset_VertexAttribL4dv 829
#define _gloffset_VertexAttribLPointer 830
#define _gloffset_DepthRangeArrayv 831
#define _gloffset_DepthRangeIndexed 832
#define _gloffset_GetDoublei_v 833
#define _gloffset_GetFloati_v 834
#define _gloffset_ScissorArrayv 835
#define _gloffset_ScissorIndexed 836
#define _gloffset_ScissorIndexedv 837
#define _gloffset_ViewportArrayv 838
#define _gloffset_ViewportIndexedf 839
#define _gloffset_ViewportIndexedfv 840
#define _gloffset_GetGraphicsResetStatusARB 841
#define _gloffset_GetnColorTableARB 842
#define _gloffset_GetnCompressedTexImageARB 843
#define _gloffset_GetnConvolutionFilterARB 844
#define _gloffset_GetnHistogramARB 845
#define _gloffset_GetnMapdvARB 846
#define _gloffset_GetnMapfvARB 847
#define _gloffset_GetnMapivARB 848
#define _gloffset_GetnMinmaxARB 849
#define _gloffset_GetnPixelMapfvARB 850
#define _gloffset_GetnPixelMapuivARB 851
#define _gloffset_GetnPixelMapusvARB 852
#define _gloffset_GetnPolygonStippleARB 853
#define _gloffset_GetnSeparableFilterARB 854
#define _gloffset_GetnTexImageARB 855
#define _gloffset_GetnUniformdvARB 856
#define _gloffset_GetnUniformfvARB 857
#define _gloffset_GetnUniformivARB 858
#define _gloffset_GetnUniformuivARB 859
#define _gloffset_ReadnPixelsARB 860
#define _gloffset_DrawArraysInstancedBaseInstance 861
#define _gloffset_DrawElementsInstancedBaseInstance 862
#define _gloffset_DrawElementsInstancedBaseVertexBaseInstance 863
#define _gloffset_DrawTransformFeedbackInstanced 864
#define _gloffset_DrawTransformFeedbackStreamInstanced 865
#define _gloffset_GetInternalformativ 866
#define _gloffset_GetActiveAtomicCounterBufferiv 867
#define _gloffset_BindImageTexture 868
#define _gloffset_MemoryBarrier 869
#define _gloffset_TexStorage1D 870
#define _gloffset_TexStorage2D 871
#define _gloffset_TexStorage3D 872
#define _gloffset_TextureStorage1DEXT 873
#define _gloffset_TextureStorage2DEXT 874
#define _gloffset_TextureStorage3DEXT 875
#define _gloffset_ClearBufferData 876
#define _gloffset_ClearBufferSubData 877
#define _gloffset_DispatchCompute 878
#define _gloffset_DispatchComputeIndirect 879
#define _gloffset_CopyImageSubData 880
#define _gloffset_TextureView 881
#define _gloffset_BindVertexBuffer 882
#define _gloffset_VertexAttribBinding 883
#define _gloffset_VertexAttribFormat 884
#define _gloffset_VertexAttribIFormat 885
#define _gloffset_VertexAttribLFormat 886
#define _gloffset_VertexBindingDivisor 887
#define _gloffset_FramebufferParameteri 888
#define _gloffset_GetFramebufferParameteriv 889
#define _gloffset_GetInternalformati64v 890
#define _gloffset_MultiDrawArraysIndirect 891
#define _gloffset_MultiDrawElementsIndirect 892
#define _gloffset_GetProgramInterfaceiv 893
#define _gloffset_GetProgramResourceIndex 894
#define _gloffset_GetProgramResourceLocation 895
#define _gloffset_GetProgramResourceLocationIndex 896
#define _gloffset_GetProgramResourceName 897
#define _gloffset_GetProgramResourceiv 898
#define _gloffset_ShaderStorageBlockBinding 899
#define _gloffset_TexBufferRange 900
#define _gloffset_TexStorage2DMultisample 901
#define _gloffset_TexStorage3DMultisample 902
#define _gloffset_BufferStorage 903
#define _gloffset_ClearTexImage 904
#define _gloffset_ClearTexSubImage 905
#define _gloffset_BindBuffersBase 906
#define _gloffset_BindBuffersRange 907
#define _gloffset_BindImageTextures 908
#define _gloffset_BindSamplers 909
#define _gloffset_BindTextures 910
#define _gloffset_BindVertexBuffers 911
#define _gloffset_GetImageHandleARB 912
#define _gloffset_GetTextureHandleARB 913
#define _gloffset_GetTextureSamplerHandleARB 914
#define _gloffset_GetVertexAttribLui64vARB 915
#define _gloffset_IsImageHandleResidentARB 916
#define _gloffset_IsTextureHandleResidentARB 917
#define _gloffset_MakeImageHandleNonResidentARB 918
#define _gloffset_MakeImageHandleResidentARB 919
#define _gloffset_MakeTextureHandleNonResidentARB 920
#define _gloffset_MakeTextureHandleResidentARB 921
#define _gloffset_ProgramUniformHandleui64ARB 922
#define _gloffset_ProgramUniformHandleui64vARB 923
#define _gloffset_UniformHandleui64ARB 924
#define _gloffset_UniformHandleui64vARB 925
#define _gloffset_VertexAttribL1ui64ARB 926
#define _gloffset_VertexAttribL1ui64vARB 927
#define _gloffset_DispatchComputeGroupSizeARB 928
#define _gloffset_MultiDrawArraysIndirectCountARB 929
#define _gloffset_MultiDrawElementsIndirectCountARB 930
#define _gloffset_ClipControl 931
#define _gloffset_BindTextureUnit 932
#define _gloffset_BlitNamedFramebuffer 933
#define _gloffset_CheckNamedFramebufferStatus 934
#define _gloffset_ClearNamedBufferData 935
#define _gloffset_ClearNamedBufferSubData 936
#define _gloffset_ClearNamedFramebufferfi 937
#define _gloffset_ClearNamedFramebufferfv 938
#define _gloffset_ClearNamedFramebufferiv 939
#define _gloffset_ClearNamedFramebufferuiv 940
#define _gloffset_CompressedTextureSubImage1D 941
#define _gloffset_CompressedTextureSubImage2D 942
#define _gloffset_CompressedTextureSubImage3D 943
#define _gloffset_CopyNamedBufferSubData 944
#define _gloffset_CopyTextureSubImage1D 945
#define _gloffset_CopyTextureSubImage2D 946
#define _gloffset_CopyTextureSubImage3D 947
#define _gloffset_CreateBuffers 948
#define _gloffset_CreateFramebuffers 949
#define _gloffset_CreateProgramPipelines 950
#define _gloffset_CreateQueries 951
#define _gloffset_CreateRenderbuffers 952
#define _gloffset_CreateSamplers 953
#define _gloffset_CreateTextures 954
#define _gloffset_CreateTransformFeedbacks 955
#define _gloffset_CreateVertexArrays 956
#define _gloffset_DisableVertexArrayAttrib 957
#define _gloffset_EnableVertexArrayAttrib 958
#define _gloffset_FlushMappedNamedBufferRange 959
#define _gloffset_GenerateTextureMipmap 960
#define _gloffset_GetCompressedTextureImage 961
#define _gloffset_GetNamedBufferParameteri64v 962
#define _gloffset_GetNamedBufferParameteriv 963
#define _gloffset_GetNamedBufferPointerv 964
#define _gloffset_GetNamedBufferSubData 965
#define _gloffset_GetNamedFramebufferAttachmentParameteriv 966
#define _gloffset_GetNamedFramebufferParameteriv 967
#define _gloffset_GetNamedRenderbufferParameteriv 968
#define _gloffset_GetQueryBufferObjecti64v 969
#define _gloffset_GetQueryBufferObjectiv 970
#define _gloffset_GetQueryBufferObjectui64v 971
#define _gloffset_GetQueryBufferObjectuiv 972
#define _gloffset_GetTextureImage 973
#define _gloffset_GetTextureLevelParameterfv 974
#define _gloffset_GetTextureLevelParameteriv 975
#define _gloffset_GetTextureParameterIiv 976
#define _gloffset_GetTextureParameterIuiv 977
#define _gloffset_GetTextureParameterfv 978
#define _gloffset_GetTextureParameteriv 979
#define _gloffset_GetTransformFeedbacki64_v 980
#define _gloffset_GetTransformFeedbacki_v 981
#define _gloffset_GetTransformFeedbackiv 982
#define _gloffset_GetVertexArrayIndexed64iv 983
#define _gloffset_GetVertexArrayIndexediv 984
#define _gloffset_GetVertexArrayiv 985
#define _gloffset_InvalidateNamedFramebufferData 986
#define _gloffset_InvalidateNamedFramebufferSubData 987
#define _gloffset_MapNamedBuffer 988
#define _gloffset_MapNamedBufferRange 989
#define _gloffset_NamedBufferData 990
#define _gloffset_NamedBufferStorage 991
#define _gloffset_NamedBufferSubData 992
#define _gloffset_NamedFramebufferDrawBuffer 993
#define _gloffset_NamedFramebufferDrawBuffers 994
#define _gloffset_NamedFramebufferParameteri 995
#define _gloffset_NamedFramebufferReadBuffer 996
#define _gloffset_NamedFramebufferRenderbuffer 997
#define _gloffset_NamedFramebufferTexture 998
#define _gloffset_NamedFramebufferTextureLayer 999
#define _gloffset_NamedRenderbufferStorage 1000
#define _gloffset_NamedRenderbufferStorageMultisample 1001
#define _gloffset_TextureBuffer 1002
#define _gloffset_TextureBufferRange 1003
#define _gloffset_TextureParameterIiv 1004
#define _gloffset_TextureParameterIuiv 1005
#define _gloffset_TextureParameterf 1006
#define _gloffset_TextureParameterfv 1007
#define _gloffset_TextureParameteri 1008
#define _gloffset_TextureParameteriv 1009
#define _gloffset_TextureStorage1D 1010
#define _gloffset_TextureStorage2D 1011
#define _gloffset_TextureStorage2DMultisample 1012
#define _gloffset_TextureStorage3D 1013
#define _gloffset_TextureStorage3DMultisample 1014
#define _gloffset_TextureSubImage1D 1015
#define _gloffset_TextureSubImage2D 1016
#define _gloffset_TextureSubImage3D 1017
#define _gloffset_TransformFeedbackBufferBase 1018
#define _gloffset_TransformFeedbackBufferRange 1019
#define _gloffset_UnmapNamedBufferEXT 1020
#define _gloffset_VertexArrayAttribBinding 1021
#define _gloffset_VertexArrayAttribFormat 1022
#define _gloffset_VertexArrayAttribIFormat 1023
#define _gloffset_VertexArrayAttribLFormat 1024
#define _gloffset_VertexArrayBindingDivisor 1025
#define _gloffset_VertexArrayElementBuffer 1026
#define _gloffset_VertexArrayVertexBuffer 1027
#define _gloffset_VertexArrayVertexBuffers 1028
#define _gloffset_GetCompressedTextureSubImage 1029
#define _gloffset_GetTextureSubImage 1030
#define _gloffset_BufferPageCommitmentARB 1031
#define _gloffset_NamedBufferPageCommitmentARB 1032
#define _gloffset_GetUniformi64vARB 1033
#define _gloffset_GetUniformui64vARB 1034
#define _gloffset_GetnUniformi64vARB 1035
#define _gloffset_GetnUniformui64vARB 1036
#define _gloffset_ProgramUniform1i64ARB 1037
#define _gloffset_ProgramUniform1i64vARB 1038
#define _gloffset_ProgramUniform1ui64ARB 1039
#define _gloffset_ProgramUniform1ui64vARB 1040
#define _gloffset_ProgramUniform2i64ARB 1041
#define _gloffset_ProgramUniform2i64vARB 1042
#define _gloffset_ProgramUniform2ui64ARB 1043
#define _gloffset_ProgramUniform2ui64vARB 1044
#define _gloffset_ProgramUniform3i64ARB 1045
#define _gloffset_ProgramUniform3i64vARB 1046
#define _gloffset_ProgramUniform3ui64ARB 1047
#define _gloffset_ProgramUniform3ui64vARB 1048
#define _gloffset_ProgramUniform4i64ARB 1049
#define _gloffset_ProgramUniform4i64vARB 1050
#define _gloffset_ProgramUniform4ui64ARB 1051
#define _gloffset_ProgramUniform4ui64vARB 1052
#define _gloffset_Uniform1i64ARB 1053
#define _gloffset_Uniform1i64vARB 1054
#define _gloffset_Uniform1ui64ARB 1055
#define _gloffset_Uniform1ui64vARB 1056
#define _gloffset_Uniform2i64ARB 1057
#define _gloffset_Uniform2i64vARB 1058
#define _gloffset_Uniform2ui64ARB 1059
#define _gloffset_Uniform2ui64vARB 1060
#define _gloffset_Uniform3i64ARB 1061
#define _gloffset_Uniform3i64vARB 1062
#define _gloffset_Uniform3ui64ARB 1063
#define _gloffset_Uniform3ui64vARB 1064
#define _gloffset_Uniform4i64ARB 1065
#define _gloffset_Uniform4i64vARB 1066
#define _gloffset_Uniform4ui64ARB 1067
#define _gloffset_Uniform4ui64vARB 1068
#define _gloffset_EvaluateDepthValuesARB 1069
#define _gloffset_FramebufferSampleLocationsfvARB 1070
#define _gloffset_NamedFramebufferSampleLocationsfvARB 1071
#define _gloffset_SpecializeShaderARB 1072
#define _gloffset_InvalidateBufferData 1073
#define _gloffset_InvalidateBufferSubData 1074
#define _gloffset_InvalidateFramebuffer 1075
#define _gloffset_InvalidateSubFramebuffer 1076
#define _gloffset_InvalidateTexImage 1077
#define _gloffset_InvalidateTexSubImage 1078
#define _gloffset_DrawTexfOES 1079
#define _gloffset_DrawTexfvOES 1080
#define _gloffset_DrawTexiOES 1081
#define _gloffset_DrawTexivOES 1082
#define _gloffset_DrawTexsOES 1083
#define _gloffset_DrawTexsvOES 1084
#define _gloffset_DrawTexxOES 1085
#define _gloffset_DrawTexxvOES 1086
#define _gloffset_PointSizePointerOES 1087
#define _gloffset_QueryMatrixxOES 1088
#define _gloffset_SampleMaskSGIS 1089
#define _gloffset_SamplePatternSGIS 1090
#define _gloffset_ColorPointerEXT 1091
#define _gloffset_EdgeFlagPointerEXT 1092
#define _gloffset_IndexPointerEXT 1093
#define _gloffset_NormalPointerEXT 1094
#define _gloffset_TexCoordPointerEXT 1095
#define _gloffset_VertexPointerEXT 1096
#define _gloffset_DiscardFramebufferEXT 1097
#define _gloffset_ActiveShaderProgram 1098
#define _gloffset_BindProgramPipeline 1099
#define _gloffset_CreateShaderProgramv 1100
#define _gloffset_DeleteProgramPipelines 1101
#define _gloffset_GenProgramPipelines 1102
#define _gloffset_GetProgramPipelineInfoLog 1103
#define _gloffset_GetProgramPipelineiv 1104
#define _gloffset_IsProgramPipeline 1105
#define _gloffset_LockArraysEXT 1106
#define _gloffset_ProgramUniform1d 1107
#define _gloffset_ProgramUniform1dv 1108
#define _gloffset_ProgramUniform1f 1109
#define _gloffset_ProgramUniform1fv 1110
#define _gloffset_ProgramUniform1i 1111
#define _gloffset_ProgramUniform1iv 1112
#define _gloffset_ProgramUniform1ui 1113
#define _gloffset_ProgramUniform1uiv 1114
#define _gloffset_ProgramUniform2d 1115
#define _gloffset_ProgramUniform2dv 1116
#define _gloffset_ProgramUniform2f 1117
#define _gloffset_ProgramUniform2fv 1118
#define _gloffset_ProgramUniform2i 1119
#define _gloffset_ProgramUniform2iv 1120
#define _gloffset_ProgramUniform2ui 1121
#define _gloffset_ProgramUniform2uiv 1122
#define _gloffset_ProgramUniform3d 1123
#define _gloffset_ProgramUniform3dv 1124
#define _gloffset_ProgramUniform3f 1125
#define _gloffset_ProgramUniform3fv 1126
#define _gloffset_ProgramUniform3i 1127
#define _gloffset_ProgramUniform3iv 1128
#define _gloffset_ProgramUniform3ui 1129
#define _gloffset_ProgramUniform3uiv 1130
#define _gloffset_ProgramUniform4d 1131
#define _gloffset_ProgramUniform4dv 1132
#define _gloffset_ProgramUniform4f 1133
#define _gloffset_ProgramUniform4fv 1134
#define _gloffset_ProgramUniform4i 1135
#define _gloffset_ProgramUniform4iv 1136
#define _gloffset_ProgramUniform4ui 1137
#define _gloffset_ProgramUniform4uiv 1138
#define _gloffset_ProgramUniformMatrix2dv 1139
#define _gloffset_ProgramUniformMatrix2fv 1140
#define _gloffset_ProgramUniformMatrix2x3dv 1141
#define _gloffset_ProgramUniformMatrix2x3fv 1142
#define _gloffset_ProgramUniformMatrix2x4dv 1143
#define _gloffset_ProgramUniformMatrix2x4fv 1144
#define _gloffset_ProgramUniformMatrix3dv 1145
#define _gloffset_ProgramUniformMatrix3fv 1146
#define _gloffset_ProgramUniformMatrix3x2dv 1147
#define _gloffset_ProgramUniformMatrix3x2fv 1148
#define _gloffset_ProgramUniformMatrix3x4dv 1149
#define _gloffset_ProgramUniformMatrix3x4fv 1150
#define _gloffset_ProgramUniformMatrix4dv 1151
#define _gloffset_ProgramUniformMatrix4fv 1152
#define _gloffset_ProgramUniformMatrix4x2dv 1153
#define _gloffset_ProgramUniformMatrix4x2fv 1154
#define _gloffset_ProgramUniformMatrix4x3dv 1155
#define _gloffset_ProgramUniformMatrix4x3fv 1156
#define _gloffset_UnlockArraysEXT 1157
#define _gloffset_UseProgramStages 1158
#define _gloffset_ValidateProgramPipeline 1159
#define _gloffset_FramebufferTexture2DMultisampleEXT 1160
#define _gloffset_DebugMessageCallback 1161
#define _gloffset_DebugMessageControl 1162
#define _gloffset_DebugMessageInsert 1163
#define _gloffset_GetDebugMessageLog 1164
#define _gloffset_GetObjectLabel 1165
#define _gloffset_GetObjectPtrLabel 1166
#define _gloffset_ObjectLabel 1167
#define _gloffset_ObjectPtrLabel 1168
#define _gloffset_PopDebugGroup 1169
#define _gloffset_PushDebugGroup 1170
#define _gloffset_SecondaryColor3fEXT 1171
#define _gloffset_SecondaryColor3fvEXT 1172
#define _gloffset_MultiDrawElements 1173
#define _gloffset_FogCoordfEXT 1174
#define _gloffset_FogCoordfvEXT 1175
#define _gloffset_ResizeBuffersMESA 1176
#define _gloffset_WindowPos4dMESA 1177
#define _gloffset_WindowPos4dvMESA 1178
#define _gloffset_WindowPos4fMESA 1179
#define _gloffset_WindowPos4fvMESA 1180
#define _gloffset_WindowPos4iMESA 1181
#define _gloffset_WindowPos4ivMESA 1182
#define _gloffset_WindowPos4sMESA 1183
#define _gloffset_WindowPos4svMESA 1184
#define _gloffset_MultiModeDrawArraysIBM 1185
#define _gloffset_MultiModeDrawElementsIBM 1186
#define _gloffset_AreProgramsResidentNV 1187
#define _gloffset_ExecuteProgramNV 1188
#define _gloffset_GetProgramParameterdvNV 1189
#define _gloffset_GetProgramParameterfvNV 1190
#define _gloffset_GetProgramStringNV 1191
#define _gloffset_GetProgramivNV 1192
#define _gloffset_GetTrackMatrixivNV 1193
#define _gloffset_GetVertexAttribdvNV 1194
#define _gloffset_GetVertexAttribfvNV 1195
#define _gloffset_GetVertexAttribivNV 1196
#define _gloffset_LoadProgramNV 1197
#define _gloffset_ProgramParameters4dvNV 1198
#define _gloffset_ProgramParameters4fvNV 1199
#define _gloffset_RequestResidentProgramsNV 1200
#define _gloffset_TrackMatrixNV 1201
#define _gloffset_VertexAttrib1dNV 1202
#define _gloffset_VertexAttrib1dvNV 1203
#define _gloffset_VertexAttrib1fNV 1204
#define _gloffset_VertexAttrib1fvNV 1205
#define _gloffset_VertexAttrib1sNV 1206
#define _gloffset_VertexAttrib1svNV 1207
#define _gloffset_VertexAttrib2dNV 1208
#define _gloffset_VertexAttrib2dvNV 1209
#define _gloffset_VertexAttrib2fNV 1210
#define _gloffset_VertexAttrib2fvNV 1211
#define _gloffset_VertexAttrib2sNV 1212
#define _gloffset_VertexAttrib2svNV 1213
#define _gloffset_VertexAttrib3dNV 1214
#define _gloffset_VertexAttrib3dvNV 1215
#define _gloffset_VertexAttrib3fNV 1216
#define _gloffset_VertexAttrib3fvNV 1217
#define _gloffset_VertexAttrib3sNV 1218
#define _gloffset_VertexAttrib3svNV 1219
#define _gloffset_VertexAttrib4dNV 1220
#define _gloffset_VertexAttrib4dvNV 1221
#define _gloffset_VertexAttrib4fNV 1222
#define _gloffset_VertexAttrib4fvNV 1223
#define _gloffset_VertexAttrib4sNV 1224
#define _gloffset_VertexAttrib4svNV 1225
#define _gloffset_VertexAttrib4ubNV 1226
#define _gloffset_VertexAttrib4ubvNV 1227
#define _gloffset_VertexAttribPointerNV 1228
#define _gloffset_VertexAttribs1dvNV 1229
#define _gloffset_VertexAttribs1fvNV 1230
#define _gloffset_VertexAttribs1svNV 1231
#define _gloffset_VertexAttribs2dvNV 1232
#define _gloffset_VertexAttribs2fvNV 1233
#define _gloffset_VertexAttribs2svNV 1234
#define _gloffset_VertexAttribs3dvNV 1235
#define _gloffset_VertexAttribs3fvNV 1236
#define _gloffset_VertexAttribs3svNV 1237
#define _gloffset_VertexAttribs4dvNV 1238
#define _gloffset_VertexAttribs4fvNV 1239
#define _gloffset_VertexAttribs4svNV 1240
#define _gloffset_VertexAttribs4ubvNV 1241
#define _gloffset_GetTexBumpParameterfvATI 1242
#define _gloffset_GetTexBumpParameterivATI 1243
#define _gloffset_TexBumpParameterfvATI 1244
#define _gloffset_TexBumpParameterivATI 1245
#define _gloffset_AlphaFragmentOp1ATI 1246
#define _gloffset_AlphaFragmentOp2ATI 1247
#define _gloffset_AlphaFragmentOp3ATI 1248
#define _gloffset_BeginFragmentShaderATI 1249
#define _gloffset_BindFragmentShaderATI 1250
#define _gloffset_ColorFragmentOp1ATI 1251
#define _gloffset_ColorFragmentOp2ATI 1252
#define _gloffset_ColorFragmentOp3ATI 1253
#define _gloffset_DeleteFragmentShaderATI 1254
#define _gloffset_EndFragmentShaderATI 1255
#define _gloffset_GenFragmentShadersATI 1256
#define _gloffset_PassTexCoordATI 1257
#define _gloffset_SampleMapATI 1258
#define _gloffset_SetFragmentShaderConstantATI 1259
#define _gloffset_DepthRangeArrayfvOES 1260
#define _gloffset_DepthRangeIndexedfOES 1261
#define _gloffset_ActiveStencilFaceEXT 1262
#define _gloffset_GetProgramNamedParameterdvNV 1263
#define _gloffset_GetProgramNamedParameterfvNV 1264
#define _gloffset_ProgramNamedParameter4dNV 1265
#define _gloffset_ProgramNamedParameter4dvNV 1266
#define _gloffset_ProgramNamedParameter4fNV 1267
#define _gloffset_ProgramNamedParameter4fvNV 1268
#define _gloffset_PrimitiveRestartNV 1269
#define _gloffset_GetTexGenxvOES 1270
#define _gloffset_TexGenxOES 1271
#define _gloffset_TexGenxvOES 1272
#define _gloffset_DepthBoundsEXT 1273
#define _gloffset_BindFramebufferEXT 1274
#define _gloffset_BindRenderbufferEXT 1275
#define _gloffset_StringMarkerGREMEDY 1276
#define _gloffset_BufferParameteriAPPLE 1277
#define _gloffset_FlushMappedBufferRangeAPPLE 1278
#define _gloffset_VertexAttribI1iEXT 1279
#define _gloffset_VertexAttribI1uiEXT 1280
#define _gloffset_VertexAttribI2iEXT 1281
#define _gloffset_VertexAttribI2ivEXT 1282
#define _gloffset_VertexAttribI2uiEXT 1283
#define _gloffset_VertexAttribI2uivEXT 1284
#define _gloffset_VertexAttribI3iEXT 1285
#define _gloffset_VertexAttribI3ivEXT 1286
#define _gloffset_VertexAttribI3uiEXT 1287
#define _gloffset_VertexAttribI3uivEXT 1288
#define _gloffset_VertexAttribI4iEXT 1289
#define _gloffset_VertexAttribI4ivEXT 1290
#define _gloffset_VertexAttribI4uiEXT 1291
#define _gloffset_VertexAttribI4uivEXT 1292
#define _gloffset_ClearColorIiEXT 1293
#define _gloffset_ClearColorIuiEXT 1294
#define _gloffset_BindBufferOffsetEXT 1295
#define _gloffset_BeginPerfMonitorAMD 1296
#define _gloffset_DeletePerfMonitorsAMD 1297
#define _gloffset_EndPerfMonitorAMD 1298
#define _gloffset_GenPerfMonitorsAMD 1299
#define _gloffset_GetPerfMonitorCounterDataAMD 1300
#define _gloffset_GetPerfMonitorCounterInfoAMD 1301
#define _gloffset_GetPerfMonitorCounterStringAMD 1302
#define _gloffset_GetPerfMonitorCountersAMD 1303
#define _gloffset_GetPerfMonitorGroupStringAMD 1304
#define _gloffset_GetPerfMonitorGroupsAMD 1305
#define _gloffset_SelectPerfMonitorCountersAMD 1306
#define _gloffset_GetObjectParameterivAPPLE 1307
#define _gloffset_ObjectPurgeableAPPLE 1308
#define _gloffset_ObjectUnpurgeableAPPLE 1309
#define _gloffset_ActiveProgramEXT 1310
#define _gloffset_CreateShaderProgramEXT 1311
#define _gloffset_UseShaderProgramEXT 1312
#define _gloffset_TextureBarrierNV 1313
#define _gloffset_VDPAUFiniNV 1314
#define _gloffset_VDPAUGetSurfaceivNV 1315
#define _gloffset_VDPAUInitNV 1316
#define _gloffset_VDPAUIsSurfaceNV 1317
#define _gloffset_VDPAUMapSurfacesNV 1318
#define _gloffset_VDPAURegisterOutputSurfaceNV 1319
#define _gloffset_VDPAURegisterVideoSurfaceNV 1320
#define _gloffset_VDPAUSurfaceAccessNV 1321
#define _gloffset_VDPAUUnmapSurfacesNV 1322
#define _gloffset_VDPAUUnregisterSurfaceNV 1323
#define _gloffset_BeginPerfQueryINTEL 1324
#define _gloffset_CreatePerfQueryINTEL 1325
#define _gloffset_DeletePerfQueryINTEL 1326
#define _gloffset_EndPerfQueryINTEL 1327
#define _gloffset_GetFirstPerfQueryIdINTEL 1328
#define _gloffset_GetNextPerfQueryIdINTEL 1329
#define _gloffset_GetPerfCounterInfoINTEL 1330
#define _gloffset_GetPerfQueryDataINTEL 1331
#define _gloffset_GetPerfQueryIdByNameINTEL 1332
#define _gloffset_GetPerfQueryInfoINTEL 1333
#define _gloffset_PolygonOffsetClampEXT 1334
#define _gloffset_SubpixelPrecisionBiasNV 1335
#define _gloffset_ConservativeRasterParameterfNV 1336
#define _gloffset_ConservativeRasterParameteriNV 1337
#define _gloffset_WindowRectanglesEXT 1338
#define _gloffset_BufferStorageMemEXT 1339
#define _gloffset_CreateMemoryObjectsEXT 1340
#define _gloffset_DeleteMemoryObjectsEXT 1341
#define _gloffset_DeleteSemaphoresEXT 1342
#define _gloffset_GenSemaphoresEXT 1343
#define _gloffset_GetMemoryObjectParameterivEXT 1344
#define _gloffset_GetSemaphoreParameterui64vEXT 1345
#define _gloffset_GetUnsignedBytei_vEXT 1346
#define _gloffset_GetUnsignedBytevEXT 1347
#define _gloffset_IsMemoryObjectEXT 1348
#define _gloffset_IsSemaphoreEXT 1349
#define _gloffset_MemoryObjectParameterivEXT 1350
#define _gloffset_NamedBufferStorageMemEXT 1351
#define _gloffset_SemaphoreParameterui64vEXT 1352
#define _gloffset_SignalSemaphoreEXT 1353
#define _gloffset_TexStorageMem1DEXT 1354
#define _gloffset_TexStorageMem2DEXT 1355
#define _gloffset_TexStorageMem2DMultisampleEXT 1356
#define _gloffset_TexStorageMem3DEXT 1357
#define _gloffset_TexStorageMem3DMultisampleEXT 1358
#define _gloffset_TextureStorageMem1DEXT 1359
#define _gloffset_TextureStorageMem2DEXT 1360
#define _gloffset_TextureStorageMem2DMultisampleEXT 1361
#define _gloffset_TextureStorageMem3DEXT 1362
#define _gloffset_TextureStorageMem3DMultisampleEXT 1363
#define _gloffset_WaitSemaphoreEXT 1364
#define _gloffset_ImportMemoryFdEXT 1365
#define _gloffset_ImportSemaphoreFdEXT 1366
#define _gloffset_FramebufferFetchBarrierEXT 1367
#define _gloffset_NamedRenderbufferStorageMultisampleAdvancedAMD 1368
#define _gloffset_RenderbufferStorageMultisampleAdvancedAMD 1369
#define _gloffset_StencilFuncSeparateATI 1370
#define _gloffset_ProgramEnvParameters4fvEXT 1371
#define _gloffset_ProgramLocalParameters4fvEXT 1372
#define _gloffset_EGLImageTargetRenderbufferStorageOES 1373
#define _gloffset_EGLImageTargetTexture2DOES 1374
#define _gloffset_AlphaFuncx 1375
#define _gloffset_ClearColorx 1376
#define _gloffset_ClearDepthx 1377
#define _gloffset_Color4x 1378
#define _gloffset_DepthRangex 1379
#define _gloffset_Fogx 1380
#define _gloffset_Fogxv 1381
#define _gloffset_Frustumf 1382
#define _gloffset_Frustumx 1383
#define _gloffset_LightModelx 1384
#define _gloffset_LightModelxv 1385
#define _gloffset_Lightx 1386
#define _gloffset_Lightxv 1387
#define _gloffset_LineWidthx 1388
#define _gloffset_LoadMatrixx 1389
#define _gloffset_Materialx 1390
#define _gloffset_Materialxv 1391
#define _gloffset_MultMatrixx 1392
#define _gloffset_MultiTexCoord4x 1393
#define _gloffset_Normal3x 1394
#define _gloffset_Orthof 1395
#define _gloffset_Orthox 1396
#define _gloffset_PointSizex 1397
#define _gloffset_PolygonOffsetx 1398
#define _gloffset_Rotatex 1399
#define _gloffset_SampleCoveragex 1400
#define _gloffset_Scalex 1401
#define _gloffset_TexEnvx 1402
#define _gloffset_TexEnvxv 1403
#define _gloffset_TexParameterx 1404
#define _gloffset_Translatex 1405
#define _gloffset_ClipPlanef 1406
#define _gloffset_ClipPlanex 1407
#define _gloffset_GetClipPlanef 1408
#define _gloffset_GetClipPlanex 1409
#define _gloffset_GetFixedv 1410
#define _gloffset_GetLightxv 1411
#define _gloffset_GetMaterialxv 1412
#define _gloffset_GetTexEnvxv 1413
#define _gloffset_GetTexParameterxv 1414
#define _gloffset_PointParameterx 1415
#define _gloffset_PointParameterxv 1416
#define _gloffset_TexParameterxv 1417
#define _gloffset_BlendBarrier 1418
#define _gloffset_PrimitiveBoundingBox 1419
#define _gloffset_MaxShaderCompilerThreadsKHR 1420
#define _gloffset_MatrixLoadfEXT 1421
#define _gloffset_MatrixLoaddEXT 1422
#define _gloffset_MatrixMultfEXT 1423
#define _gloffset_MatrixMultdEXT 1424
#define _gloffset_MatrixLoadIdentityEXT 1425
#define _gloffset_MatrixRotatefEXT 1426
#define _gloffset_MatrixRotatedEXT 1427
#define _gloffset_MatrixScalefEXT 1428
#define _gloffset_MatrixScaledEXT 1429
#define _gloffset_MatrixTranslatefEXT 1430
#define _gloffset_MatrixTranslatedEXT 1431
#define _gloffset_MatrixOrthoEXT 1432
#define _gloffset_MatrixFrustumEXT 1433
#define _gloffset_MatrixPushEXT 1434
#define _gloffset_MatrixPopEXT 1435
#define _gloffset_MatrixLoadTransposefEXT 1436
#define _gloffset_MatrixLoadTransposedEXT 1437
#define _gloffset_MatrixMultTransposefEXT 1438
#define _gloffset_MatrixMultTransposedEXT 1439
#define _gloffset_BindMultiTextureEXT 1440
#define _gloffset_NamedBufferDataEXT 1441
#define _gloffset_NamedBufferSubDataEXT 1442
#define _gloffset_NamedBufferStorageEXT 1443
#define _gloffset_MapNamedBufferRangeEXT 1444
#define _gloffset_TextureImage1DEXT 1445
#define _gloffset_TextureImage2DEXT 1446
#define _gloffset_TextureImage3DEXT 1447
#define _gloffset_TextureSubImage1DEXT 1448
#define _gloffset_TextureSubImage2DEXT 1449
#define _gloffset_TextureSubImage3DEXT 1450
#define _gloffset_CopyTextureImage1DEXT 1451
#define _gloffset_CopyTextureImage2DEXT 1452
#define _gloffset_CopyTextureSubImage1DEXT 1453
#define _gloffset_CopyTextureSubImage2DEXT 1454
#define _gloffset_CopyTextureSubImage3DEXT 1455
#define _gloffset_MapNamedBufferEXT 1456
#define _gloffset_GetTextureParameterivEXT 1457
#define _gloffset_GetTextureParameterfvEXT 1458
#define _gloffset_TextureParameteriEXT 1459
#define _gloffset_TextureParameterivEXT 1460
#define _gloffset_TextureParameterfEXT 1461
#define _gloffset_TextureParameterfvEXT 1462
#define _gloffset_GetTextureImageEXT 1463
#define _gloffset_GetTextureLevelParameterivEXT 1464
#define _gloffset_GetTextureLevelParameterfvEXT 1465
#define _gloffset_GetNamedBufferSubDataEXT 1466
#define _gloffset_GetNamedBufferPointervEXT 1467
#define _gloffset_GetNamedBufferParameterivEXT 1468
#define _gloffset_FlushMappedNamedBufferRangeEXT 1469
#define _gloffset_FramebufferDrawBufferEXT 1470
#define _gloffset_FramebufferDrawBuffersEXT 1471
#define _gloffset_FramebufferReadBufferEXT 1472
#define _gloffset_GetFramebufferParameterivEXT 1473
#define _gloffset_CheckNamedFramebufferStatusEXT 1474
#define _gloffset_NamedFramebufferTexture1DEXT 1475
#define _gloffset_NamedFramebufferTexture2DEXT 1476
#define _gloffset_NamedFramebufferTexture3DEXT 1477
#define _gloffset_NamedFramebufferRenderbufferEXT 1478
#define _gloffset_GetNamedFramebufferAttachmentParameterivEXT 1479
#define _gloffset_EnableClientStateiEXT 1480
#define _gloffset_DisableClientStateiEXT 1481
#define _gloffset_GetPointerIndexedvEXT 1482
#define _gloffset_MultiTexEnviEXT 1483
#define _gloffset_MultiTexEnvivEXT 1484
#define _gloffset_MultiTexEnvfEXT 1485
#define _gloffset_MultiTexEnvfvEXT 1486
#define _gloffset_GetMultiTexEnvivEXT 1487
#define _gloffset_GetMultiTexEnvfvEXT 1488
#define _gloffset_MultiTexParameteriEXT 1489
#define _gloffset_MultiTexParameterivEXT 1490
#define _gloffset_MultiTexParameterfEXT 1491
#define _gloffset_MultiTexParameterfvEXT 1492
#define _gloffset_GetMultiTexImageEXT 1493
#define _gloffset_MultiTexImage1DEXT 1494
#define _gloffset_MultiTexImage2DEXT 1495
#define _gloffset_MultiTexImage3DEXT 1496
#define _gloffset_MultiTexSubImage1DEXT 1497
#define _gloffset_MultiTexSubImage2DEXT 1498
#define _gloffset_MultiTexSubImage3DEXT 1499
#define _gloffset_GetMultiTexParameterivEXT 1500
#define _gloffset_GetMultiTexParameterfvEXT 1501
#define _gloffset_CopyMultiTexImage1DEXT 1502
#define _gloffset_CopyMultiTexImage2DEXT 1503
#define _gloffset_CopyMultiTexSubImage1DEXT 1504
#define _gloffset_CopyMultiTexSubImage2DEXT 1505
#define _gloffset_CopyMultiTexSubImage3DEXT 1506
#define _gloffset_MultiTexGendEXT 1507
#define _gloffset_MultiTexGendvEXT 1508
#define _gloffset_MultiTexGenfEXT 1509
#define _gloffset_MultiTexGenfvEXT 1510
#define _gloffset_MultiTexGeniEXT 1511
#define _gloffset_MultiTexGenivEXT 1512
#define _gloffset_GetMultiTexGendvEXT 1513
#define _gloffset_GetMultiTexGenfvEXT 1514
#define _gloffset_GetMultiTexGenivEXT 1515
#define _gloffset_MultiTexCoordPointerEXT 1516
#define _gloffset_BindImageTextureEXT 1517
#define _gloffset_CompressedTextureImage1DEXT 1518
#define _gloffset_CompressedTextureImage2DEXT 1519
#define _gloffset_CompressedTextureImage3DEXT 1520
#define _gloffset_CompressedTextureSubImage1DEXT 1521
#define _gloffset_CompressedTextureSubImage2DEXT 1522
#define _gloffset_CompressedTextureSubImage3DEXT 1523
#define _gloffset_GetCompressedTextureImageEXT 1524
#define _gloffset_CompressedMultiTexImage1DEXT 1525
#define _gloffset_CompressedMultiTexImage2DEXT 1526
#define _gloffset_CompressedMultiTexImage3DEXT 1527
#define _gloffset_CompressedMultiTexSubImage1DEXT 1528
#define _gloffset_CompressedMultiTexSubImage2DEXT 1529
#define _gloffset_CompressedMultiTexSubImage3DEXT 1530
#define _gloffset_GetCompressedMultiTexImageEXT 1531
#define _gloffset_GetMultiTexLevelParameterivEXT 1532
#define _gloffset_GetMultiTexLevelParameterfvEXT 1533
#define _gloffset_FramebufferParameteriMESA 1534
#define _gloffset_GetFramebufferParameterivMESA 1535
#define _gloffset_NamedRenderbufferStorageEXT 1536
#define _gloffset_GetNamedRenderbufferParameterivEXT 1537
#define _gloffset_ClientAttribDefaultEXT 1538
#define _gloffset_PushClientAttribDefaultEXT 1539
#define _gloffset_NamedProgramStringEXT 1540
#define _gloffset_GetNamedProgramStringEXT 1541
#define _gloffset_NamedProgramLocalParameter4fEXT 1542
#define _gloffset_NamedProgramLocalParameter4fvEXT 1543
#define _gloffset_GetNamedProgramLocalParameterfvEXT 1544
#define _gloffset_NamedProgramLocalParameter4dEXT 1545
#define _gloffset_NamedProgramLocalParameter4dvEXT 1546
#define _gloffset_GetNamedProgramLocalParameterdvEXT 1547
#define _gloffset_GetNamedProgramivEXT 1548
#define _gloffset_TextureBufferEXT 1549
#define _gloffset_MultiTexBufferEXT 1550
#define _gloffset_TextureParameterIivEXT 1551
#define _gloffset_TextureParameterIuivEXT 1552
#define _gloffset_GetTextureParameterIivEXT 1553
#define _gloffset_GetTextureParameterIuivEXT 1554
#define _gloffset_MultiTexParameterIivEXT 1555
#define _gloffset_MultiTexParameterIuivEXT 1556
#define _gloffset_GetMultiTexParameterIivEXT 1557
#define _gloffset_GetMultiTexParameterIuivEXT 1558
#define _gloffset_NamedProgramLocalParameters4fvEXT 1559
#define _gloffset_GenerateTextureMipmapEXT 1560
#define _gloffset_GenerateMultiTexMipmapEXT 1561
#define _gloffset_NamedRenderbufferStorageMultisampleEXT 1562
#define _gloffset_NamedCopyBufferSubDataEXT 1563
#define _gloffset_VertexArrayVertexOffsetEXT 1564
#define _gloffset_VertexArrayColorOffsetEXT 1565
#define _gloffset_VertexArrayEdgeFlagOffsetEXT 1566
#define _gloffset_VertexArrayIndexOffsetEXT 1567
#define _gloffset_VertexArrayNormalOffsetEXT 1568
#define _gloffset_VertexArrayTexCoordOffsetEXT 1569
#define _gloffset_VertexArrayMultiTexCoordOffsetEXT 1570
#define _gloffset_VertexArrayFogCoordOffsetEXT 1571
#define _gloffset_VertexArraySecondaryColorOffsetEXT 1572
#define _gloffset_VertexArrayVertexAttribOffsetEXT 1573
#define _gloffset_VertexArrayVertexAttribIOffsetEXT 1574
#define _gloffset_EnableVertexArrayEXT 1575
#define _gloffset_DisableVertexArrayEXT 1576
#define _gloffset_EnableVertexArrayAttribEXT 1577
#define _gloffset_DisableVertexArrayAttribEXT 1578
#define _gloffset_GetVertexArrayIntegervEXT 1579
#define _gloffset_GetVertexArrayPointervEXT 1580
#define _gloffset_GetVertexArrayIntegeri_vEXT 1581
#define _gloffset_GetVertexArrayPointeri_vEXT 1582
#define _gloffset_ClearNamedBufferDataEXT 1583
#define _gloffset_ClearNamedBufferSubDataEXT 1584
#define _gloffset_NamedFramebufferParameteriEXT 1585
#define _gloffset_GetNamedFramebufferParameterivEXT 1586
#define _gloffset_VertexArrayVertexAttribLOffsetEXT 1587
#define _gloffset_VertexArrayVertexAttribDivisorEXT 1588
#define _gloffset_TextureBufferRangeEXT 1589
#define _gloffset_TextureStorage2DMultisampleEXT 1590
#define _gloffset_TextureStorage3DMultisampleEXT 1591
#define _gloffset_VertexArrayBindVertexBufferEXT 1592
#define _gloffset_VertexArrayVertexAttribFormatEXT 1593
#define _gloffset_VertexArrayVertexAttribIFormatEXT 1594
#define _gloffset_VertexArrayVertexAttribLFormatEXT 1595
#define _gloffset_VertexArrayVertexAttribBindingEXT 1596
#define _gloffset_VertexArrayVertexBindingDivisorEXT 1597
#define _gloffset_NamedBufferPageCommitmentEXT 1598
#define _gloffset_NamedStringARB 1599
#define _gloffset_DeleteNamedStringARB 1600
#define _gloffset_CompileShaderIncludeARB 1601
#define _gloffset_IsNamedStringARB 1602
#define _gloffset_GetNamedStringARB 1603
#define _gloffset_GetNamedStringivARB 1604
#define _gloffset_EGLImageTargetTexStorageEXT 1605
#define _gloffset_EGLImageTargetTextureStorageEXT 1606
#define _gloffset_CopyImageSubDataNV 1607
#define _gloffset_ViewportSwizzleNV 1608
#define _gloffset_AlphaToCoverageDitherControlNV 1609
#define _gloffset_InternalBufferSubDataCopyMESA 1610
#define _gloffset_Vertex2hNV 1611
#define _gloffset_Vertex2hvNV 1612
#define _gloffset_Vertex3hNV 1613
#define _gloffset_Vertex3hvNV 1614
#define _gloffset_Vertex4hNV 1615
#define _gloffset_Vertex4hvNV 1616
#define _gloffset_Normal3hNV 1617
#define _gloffset_Normal3hvNV 1618
#define _gloffset_Color3hNV 1619
#define _gloffset_Color3hvNV 1620
#define _gloffset_Color4hNV 1621
#define _gloffset_Color4hvNV 1622
#define _gloffset_TexCoord1hNV 1623
#define _gloffset_TexCoord1hvNV 1624
#define _gloffset_TexCoord2hNV 1625
#define _gloffset_TexCoord2hvNV 1626
#define _gloffset_TexCoord3hNV 1627
#define _gloffset_TexCoord3hvNV 1628
#define _gloffset_TexCoord4hNV 1629
#define _gloffset_TexCoord4hvNV 1630
#define _gloffset_MultiTexCoord1hNV 1631
#define _gloffset_MultiTexCoord1hvNV 1632
#define _gloffset_MultiTexCoord2hNV 1633
#define _gloffset_MultiTexCoord2hvNV 1634
#define _gloffset_MultiTexCoord3hNV 1635
#define _gloffset_MultiTexCoord3hvNV 1636
#define _gloffset_MultiTexCoord4hNV 1637
#define _gloffset_MultiTexCoord4hvNV 1638
#define _gloffset_FogCoordhNV 1639
#define _gloffset_FogCoordhvNV 1640
#define _gloffset_SecondaryColor3hNV 1641
#define _gloffset_SecondaryColor3hvNV 1642
#define _gloffset_InternalSetError 1643
#define _gloffset_VertexAttrib1hNV 1644
#define _gloffset_VertexAttrib1hvNV 1645
#define _gloffset_VertexAttrib2hNV 1646
#define _gloffset_VertexAttrib2hvNV 1647
#define _gloffset_VertexAttrib3hNV 1648
#define _gloffset_VertexAttrib3hvNV 1649
#define _gloffset_VertexAttrib4hNV 1650
#define _gloffset_VertexAttrib4hvNV 1651
#define _gloffset_VertexAttribs1hvNV 1652
#define _gloffset_VertexAttribs2hvNV 1653
#define _gloffset_VertexAttribs3hvNV 1654
#define _gloffset_VertexAttribs4hvNV 1655
#define _gloffset_TexPageCommitmentARB 1656
#define _gloffset_TexturePageCommitmentEXT 1657
#define _gloffset_ImportMemoryWin32HandleEXT 1658
#define _gloffset_ImportSemaphoreWin32HandleEXT 1659
#define _gloffset_ImportMemoryWin32NameEXT 1660
#define _gloffset_ImportSemaphoreWin32NameEXT 1661
#define _gloffset_GetObjectLabelEXT 1662
#define _gloffset_LabelObjectEXT 1663
#define _gloffset_DrawArraysUserBuf 1664
#define _gloffset_DrawElementsUserBuf 1665
#define _gloffset_MultiDrawArraysUserBuf 1666
#define _gloffset_MultiDrawElementsUserBuf 1667
#define _gloffset_DrawArraysInstancedBaseInstanceDrawID 1668
#define _gloffset_DrawElementsInstancedBaseVertexBaseInstanceDrawID 1669
#define _gloffset_InternalInvalidateFramebufferAncillaryMESA 1670
#define _gloffset_DrawElementsPacked 1671
#define _gloffset_DrawElementsUserBufPacked 1672
#define _gloffset_TexStorageAttribs2DEXT 1673
#define _gloffset_TexStorageAttribs3DEXT 1674
#define _gloffset_FramebufferTextureMultiviewOVR 1675
#define _gloffset_NamedFramebufferTextureMultiviewOVR 1676
#define _gloffset_FramebufferTextureMultisampleMultiviewOVR 1677

typedef void (GLAPIENTRYP _glptr_NewList)(GLuint, GLenum);
#define CALL_NewList(disp, parameters) (* GET_NewList(disp)) parameters
#define GET_NewList(disp) ((_glptr_NewList)(GET_by_offset((disp), _gloffset_NewList)))
#define SET_NewList(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum) = func; \
   SET_by_offset(disp, _gloffset_NewList, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_EndList)(void);
#define CALL_EndList(disp, parameters) (* GET_EndList(disp)) parameters
#define GET_EndList(disp) ((_glptr_EndList)(GET_by_offset((disp), _gloffset_EndList)))
#define SET_EndList(disp, func) do { \
   void (GLAPIENTRYP fn)(void) = func; \
   SET_by_offset(disp, _gloffset_EndList, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CallList)(GLuint);
#define CALL_CallList(disp, parameters) (* GET_CallList(disp)) parameters
#define GET_CallList(disp) ((_glptr_CallList)(GET_by_offset((disp), _gloffset_CallList)))
#define SET_CallList(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint) = func; \
   SET_by_offset(disp, _gloffset_CallList, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CallLists)(GLsizei, GLenum, const GLvoid *);
#define CALL_CallLists(disp, parameters) (* GET_CallLists(disp)) parameters
#define GET_CallLists(disp) ((_glptr_CallLists)(GET_by_offset((disp), _gloffset_CallLists)))
#define SET_CallLists(disp, func) do { \
   void (GLAPIENTRYP fn)(GLsizei, GLenum, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_CallLists, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DeleteLists)(GLuint, GLsizei);
#define CALL_DeleteLists(disp, parameters) (* GET_DeleteLists(disp)) parameters
#define GET_DeleteLists(disp) ((_glptr_DeleteLists)(GET_by_offset((disp), _gloffset_DeleteLists)))
#define SET_DeleteLists(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_DeleteLists, fn); \
} while (0)

typedef GLuint (GLAPIENTRYP _glptr_GenLists)(GLsizei);
#define CALL_GenLists(disp, parameters) (* GET_GenLists(disp)) parameters
#define GET_GenLists(disp) ((_glptr_GenLists)(GET_by_offset((disp), _gloffset_GenLists)))
#define SET_GenLists(disp, func) do { \
   GLuint (GLAPIENTRYP fn)(GLsizei) = func; \
   SET_by_offset(disp, _gloffset_GenLists, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ListBase)(GLuint);
#define CALL_ListBase(disp, parameters) (* GET_ListBase(disp)) parameters
#define GET_ListBase(disp) ((_glptr_ListBase)(GET_by_offset((disp), _gloffset_ListBase)))
#define SET_ListBase(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint) = func; \
   SET_by_offset(disp, _gloffset_ListBase, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Begin)(GLenum);
#define CALL_Begin(disp, parameters) (* GET_Begin(disp)) parameters
#define GET_Begin(disp) ((_glptr_Begin)(GET_by_offset((disp), _gloffset_Begin)))
#define SET_Begin(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum) = func; \
   SET_by_offset(disp, _gloffset_Begin, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Bitmap)(GLsizei, GLsizei, GLfloat, GLfloat, GLfloat, GLfloat, const GLubyte *);
#define CALL_Bitmap(disp, parameters) (* GET_Bitmap(disp)) parameters
#define GET_Bitmap(disp) ((_glptr_Bitmap)(GET_by_offset((disp), _gloffset_Bitmap)))
#define SET_Bitmap(disp, func) do { \
   void (GLAPIENTRYP fn)(GLsizei, GLsizei, GLfloat, GLfloat, GLfloat, GLfloat, const GLubyte *) = func; \
   SET_by_offset(disp, _gloffset_Bitmap, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Color3b)(GLbyte, GLbyte, GLbyte);
#define CALL_Color3b(disp, parameters) (* GET_Color3b(disp)) parameters
#define GET_Color3b(disp) ((_glptr_Color3b)(GET_by_offset((disp), _gloffset_Color3b)))
#define SET_Color3b(disp, func) do { \
   void (GLAPIENTRYP fn)(GLbyte, GLbyte, GLbyte) = func; \
   SET_by_offset(disp, _gloffset_Color3b, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Color3bv)(const GLbyte *);
#define CALL_Color3bv(disp, parameters) (* GET_Color3bv(disp)) parameters
#define GET_Color3bv(disp) ((_glptr_Color3bv)(GET_by_offset((disp), _gloffset_Color3bv)))
#define SET_Color3bv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLbyte *) = func; \
   SET_by_offset(disp, _gloffset_Color3bv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Color3d)(GLdouble, GLdouble, GLdouble);
#define CALL_Color3d(disp, parameters) (* GET_Color3d(disp)) parameters
#define GET_Color3d(disp) ((_glptr_Color3d)(GET_by_offset((disp), _gloffset_Color3d)))
#define SET_Color3d(disp, func) do { \
   void (GLAPIENTRYP fn)(GLdouble, GLdouble, GLdouble) = func; \
   SET_by_offset(disp, _gloffset_Color3d, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Color3dv)(const GLdouble *);
#define CALL_Color3dv(disp, parameters) (* GET_Color3dv(disp)) parameters
#define GET_Color3dv(disp) ((_glptr_Color3dv)(GET_by_offset((disp), _gloffset_Color3dv)))
#define SET_Color3dv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_Color3dv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Color3f)(GLfloat, GLfloat, GLfloat);
#define CALL_Color3f(disp, parameters) (* GET_Color3f(disp)) parameters
#define GET_Color3f(disp) ((_glptr_Color3f)(GET_by_offset((disp), _gloffset_Color3f)))
#define SET_Color3f(disp, func) do { \
   void (GLAPIENTRYP fn)(GLfloat, GLfloat, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_Color3f, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Color3fv)(const GLfloat *);
#define CALL_Color3fv(disp, parameters) (* GET_Color3fv(disp)) parameters
#define GET_Color3fv(disp) ((_glptr_Color3fv)(GET_by_offset((disp), _gloffset_Color3fv)))
#define SET_Color3fv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_Color3fv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Color3i)(GLint, GLint, GLint);
#define CALL_Color3i(disp, parameters) (* GET_Color3i(disp)) parameters
#define GET_Color3i(disp) ((_glptr_Color3i)(GET_by_offset((disp), _gloffset_Color3i)))
#define SET_Color3i(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLint, GLint) = func; \
   SET_by_offset(disp, _gloffset_Color3i, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Color3iv)(const GLint *);
#define CALL_Color3iv(disp, parameters) (* GET_Color3iv(disp)) parameters
#define GET_Color3iv(disp) ((_glptr_Color3iv)(GET_by_offset((disp), _gloffset_Color3iv)))
#define SET_Color3iv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLint *) = func; \
   SET_by_offset(disp, _gloffset_Color3iv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Color3s)(GLshort, GLshort, GLshort);
#define CALL_Color3s(disp, parameters) (* GET_Color3s(disp)) parameters
#define GET_Color3s(disp) ((_glptr_Color3s)(GET_by_offset((disp), _gloffset_Color3s)))
#define SET_Color3s(disp, func) do { \
   void (GLAPIENTRYP fn)(GLshort, GLshort, GLshort) = func; \
   SET_by_offset(disp, _gloffset_Color3s, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Color3sv)(const GLshort *);
#define CALL_Color3sv(disp, parameters) (* GET_Color3sv(disp)) parameters
#define GET_Color3sv(disp) ((_glptr_Color3sv)(GET_by_offset((disp), _gloffset_Color3sv)))
#define SET_Color3sv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLshort *) = func; \
   SET_by_offset(disp, _gloffset_Color3sv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Color3ub)(GLubyte, GLubyte, GLubyte);
#define CALL_Color3ub(disp, parameters) (* GET_Color3ub(disp)) parameters
#define GET_Color3ub(disp) ((_glptr_Color3ub)(GET_by_offset((disp), _gloffset_Color3ub)))
#define SET_Color3ub(disp, func) do { \
   void (GLAPIENTRYP fn)(GLubyte, GLubyte, GLubyte) = func; \
   SET_by_offset(disp, _gloffset_Color3ub, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Color3ubv)(const GLubyte *);
#define CALL_Color3ubv(disp, parameters) (* GET_Color3ubv(disp)) parameters
#define GET_Color3ubv(disp) ((_glptr_Color3ubv)(GET_by_offset((disp), _gloffset_Color3ubv)))
#define SET_Color3ubv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLubyte *) = func; \
   SET_by_offset(disp, _gloffset_Color3ubv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Color3ui)(GLuint, GLuint, GLuint);
#define CALL_Color3ui(disp, parameters) (* GET_Color3ui(disp)) parameters
#define GET_Color3ui(disp) ((_glptr_Color3ui)(GET_by_offset((disp), _gloffset_Color3ui)))
#define SET_Color3ui(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLuint) = func; \
   SET_by_offset(disp, _gloffset_Color3ui, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Color3uiv)(const GLuint *);
#define CALL_Color3uiv(disp, parameters) (* GET_Color3uiv(disp)) parameters
#define GET_Color3uiv(disp) ((_glptr_Color3uiv)(GET_by_offset((disp), _gloffset_Color3uiv)))
#define SET_Color3uiv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_Color3uiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Color3us)(GLushort, GLushort, GLushort);
#define CALL_Color3us(disp, parameters) (* GET_Color3us(disp)) parameters
#define GET_Color3us(disp) ((_glptr_Color3us)(GET_by_offset((disp), _gloffset_Color3us)))
#define SET_Color3us(disp, func) do { \
   void (GLAPIENTRYP fn)(GLushort, GLushort, GLushort) = func; \
   SET_by_offset(disp, _gloffset_Color3us, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Color3usv)(const GLushort *);
#define CALL_Color3usv(disp, parameters) (* GET_Color3usv(disp)) parameters
#define GET_Color3usv(disp) ((_glptr_Color3usv)(GET_by_offset((disp), _gloffset_Color3usv)))
#define SET_Color3usv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLushort *) = func; \
   SET_by_offset(disp, _gloffset_Color3usv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Color4b)(GLbyte, GLbyte, GLbyte, GLbyte);
#define CALL_Color4b(disp, parameters) (* GET_Color4b(disp)) parameters
#define GET_Color4b(disp) ((_glptr_Color4b)(GET_by_offset((disp), _gloffset_Color4b)))
#define SET_Color4b(disp, func) do { \
   void (GLAPIENTRYP fn)(GLbyte, GLbyte, GLbyte, GLbyte) = func; \
   SET_by_offset(disp, _gloffset_Color4b, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Color4bv)(const GLbyte *);
#define CALL_Color4bv(disp, parameters) (* GET_Color4bv(disp)) parameters
#define GET_Color4bv(disp) ((_glptr_Color4bv)(GET_by_offset((disp), _gloffset_Color4bv)))
#define SET_Color4bv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLbyte *) = func; \
   SET_by_offset(disp, _gloffset_Color4bv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Color4d)(GLdouble, GLdouble, GLdouble, GLdouble);
#define CALL_Color4d(disp, parameters) (* GET_Color4d(disp)) parameters
#define GET_Color4d(disp) ((_glptr_Color4d)(GET_by_offset((disp), _gloffset_Color4d)))
#define SET_Color4d(disp, func) do { \
   void (GLAPIENTRYP fn)(GLdouble, GLdouble, GLdouble, GLdouble) = func; \
   SET_by_offset(disp, _gloffset_Color4d, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Color4dv)(const GLdouble *);
#define CALL_Color4dv(disp, parameters) (* GET_Color4dv(disp)) parameters
#define GET_Color4dv(disp) ((_glptr_Color4dv)(GET_by_offset((disp), _gloffset_Color4dv)))
#define SET_Color4dv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_Color4dv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Color4f)(GLfloat, GLfloat, GLfloat, GLfloat);
#define CALL_Color4f(disp, parameters) (* GET_Color4f(disp)) parameters
#define GET_Color4f(disp) ((_glptr_Color4f)(GET_by_offset((disp), _gloffset_Color4f)))
#define SET_Color4f(disp, func) do { \
   void (GLAPIENTRYP fn)(GLfloat, GLfloat, GLfloat, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_Color4f, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Color4fv)(const GLfloat *);
#define CALL_Color4fv(disp, parameters) (* GET_Color4fv(disp)) parameters
#define GET_Color4fv(disp) ((_glptr_Color4fv)(GET_by_offset((disp), _gloffset_Color4fv)))
#define SET_Color4fv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_Color4fv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Color4i)(GLint, GLint, GLint, GLint);
#define CALL_Color4i(disp, parameters) (* GET_Color4i(disp)) parameters
#define GET_Color4i(disp) ((_glptr_Color4i)(GET_by_offset((disp), _gloffset_Color4i)))
#define SET_Color4i(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLint, GLint, GLint) = func; \
   SET_by_offset(disp, _gloffset_Color4i, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Color4iv)(const GLint *);
#define CALL_Color4iv(disp, parameters) (* GET_Color4iv(disp)) parameters
#define GET_Color4iv(disp) ((_glptr_Color4iv)(GET_by_offset((disp), _gloffset_Color4iv)))
#define SET_Color4iv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLint *) = func; \
   SET_by_offset(disp, _gloffset_Color4iv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Color4s)(GLshort, GLshort, GLshort, GLshort);
#define CALL_Color4s(disp, parameters) (* GET_Color4s(disp)) parameters
#define GET_Color4s(disp) ((_glptr_Color4s)(GET_by_offset((disp), _gloffset_Color4s)))
#define SET_Color4s(disp, func) do { \
   void (GLAPIENTRYP fn)(GLshort, GLshort, GLshort, GLshort) = func; \
   SET_by_offset(disp, _gloffset_Color4s, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Color4sv)(const GLshort *);
#define CALL_Color4sv(disp, parameters) (* GET_Color4sv(disp)) parameters
#define GET_Color4sv(disp) ((_glptr_Color4sv)(GET_by_offset((disp), _gloffset_Color4sv)))
#define SET_Color4sv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLshort *) = func; \
   SET_by_offset(disp, _gloffset_Color4sv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Color4ub)(GLubyte, GLubyte, GLubyte, GLubyte);
#define CALL_Color4ub(disp, parameters) (* GET_Color4ub(disp)) parameters
#define GET_Color4ub(disp) ((_glptr_Color4ub)(GET_by_offset((disp), _gloffset_Color4ub)))
#define SET_Color4ub(disp, func) do { \
   void (GLAPIENTRYP fn)(GLubyte, GLubyte, GLubyte, GLubyte) = func; \
   SET_by_offset(disp, _gloffset_Color4ub, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Color4ubv)(const GLubyte *);
#define CALL_Color4ubv(disp, parameters) (* GET_Color4ubv(disp)) parameters
#define GET_Color4ubv(disp) ((_glptr_Color4ubv)(GET_by_offset((disp), _gloffset_Color4ubv)))
#define SET_Color4ubv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLubyte *) = func; \
   SET_by_offset(disp, _gloffset_Color4ubv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Color4ui)(GLuint, GLuint, GLuint, GLuint);
#define CALL_Color4ui(disp, parameters) (* GET_Color4ui(disp)) parameters
#define GET_Color4ui(disp) ((_glptr_Color4ui)(GET_by_offset((disp), _gloffset_Color4ui)))
#define SET_Color4ui(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLuint, GLuint) = func; \
   SET_by_offset(disp, _gloffset_Color4ui, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Color4uiv)(const GLuint *);
#define CALL_Color4uiv(disp, parameters) (* GET_Color4uiv(disp)) parameters
#define GET_Color4uiv(disp) ((_glptr_Color4uiv)(GET_by_offset((disp), _gloffset_Color4uiv)))
#define SET_Color4uiv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_Color4uiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Color4us)(GLushort, GLushort, GLushort, GLushort);
#define CALL_Color4us(disp, parameters) (* GET_Color4us(disp)) parameters
#define GET_Color4us(disp) ((_glptr_Color4us)(GET_by_offset((disp), _gloffset_Color4us)))
#define SET_Color4us(disp, func) do { \
   void (GLAPIENTRYP fn)(GLushort, GLushort, GLushort, GLushort) = func; \
   SET_by_offset(disp, _gloffset_Color4us, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Color4usv)(const GLushort *);
#define CALL_Color4usv(disp, parameters) (* GET_Color4usv(disp)) parameters
#define GET_Color4usv(disp) ((_glptr_Color4usv)(GET_by_offset((disp), _gloffset_Color4usv)))
#define SET_Color4usv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLushort *) = func; \
   SET_by_offset(disp, _gloffset_Color4usv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_EdgeFlag)(GLboolean);
#define CALL_EdgeFlag(disp, parameters) (* GET_EdgeFlag(disp)) parameters
#define GET_EdgeFlag(disp) ((_glptr_EdgeFlag)(GET_by_offset((disp), _gloffset_EdgeFlag)))
#define SET_EdgeFlag(disp, func) do { \
   void (GLAPIENTRYP fn)(GLboolean) = func; \
   SET_by_offset(disp, _gloffset_EdgeFlag, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_EdgeFlagv)(const GLboolean *);
#define CALL_EdgeFlagv(disp, parameters) (* GET_EdgeFlagv(disp)) parameters
#define GET_EdgeFlagv(disp) ((_glptr_EdgeFlagv)(GET_by_offset((disp), _gloffset_EdgeFlagv)))
#define SET_EdgeFlagv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLboolean *) = func; \
   SET_by_offset(disp, _gloffset_EdgeFlagv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_End)(void);
#define CALL_End(disp, parameters) (* GET_End(disp)) parameters
#define GET_End(disp) ((_glptr_End)(GET_by_offset((disp), _gloffset_End)))
#define SET_End(disp, func) do { \
   void (GLAPIENTRYP fn)(void) = func; \
   SET_by_offset(disp, _gloffset_End, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Indexd)(GLdouble);
#define CALL_Indexd(disp, parameters) (* GET_Indexd(disp)) parameters
#define GET_Indexd(disp) ((_glptr_Indexd)(GET_by_offset((disp), _gloffset_Indexd)))
#define SET_Indexd(disp, func) do { \
   void (GLAPIENTRYP fn)(GLdouble) = func; \
   SET_by_offset(disp, _gloffset_Indexd, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Indexdv)(const GLdouble *);
#define CALL_Indexdv(disp, parameters) (* GET_Indexdv(disp)) parameters
#define GET_Indexdv(disp) ((_glptr_Indexdv)(GET_by_offset((disp), _gloffset_Indexdv)))
#define SET_Indexdv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_Indexdv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Indexf)(GLfloat);
#define CALL_Indexf(disp, parameters) (* GET_Indexf(disp)) parameters
#define GET_Indexf(disp) ((_glptr_Indexf)(GET_by_offset((disp), _gloffset_Indexf)))
#define SET_Indexf(disp, func) do { \
   void (GLAPIENTRYP fn)(GLfloat) = func; \
   SET_by_offset(disp, _gloffset_Indexf, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Indexfv)(const GLfloat *);
#define CALL_Indexfv(disp, parameters) (* GET_Indexfv(disp)) parameters
#define GET_Indexfv(disp) ((_glptr_Indexfv)(GET_by_offset((disp), _gloffset_Indexfv)))
#define SET_Indexfv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_Indexfv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Indexi)(GLint);
#define CALL_Indexi(disp, parameters) (* GET_Indexi(disp)) parameters
#define GET_Indexi(disp) ((_glptr_Indexi)(GET_by_offset((disp), _gloffset_Indexi)))
#define SET_Indexi(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint) = func; \
   SET_by_offset(disp, _gloffset_Indexi, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Indexiv)(const GLint *);
#define CALL_Indexiv(disp, parameters) (* GET_Indexiv(disp)) parameters
#define GET_Indexiv(disp) ((_glptr_Indexiv)(GET_by_offset((disp), _gloffset_Indexiv)))
#define SET_Indexiv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLint *) = func; \
   SET_by_offset(disp, _gloffset_Indexiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Indexs)(GLshort);
#define CALL_Indexs(disp, parameters) (* GET_Indexs(disp)) parameters
#define GET_Indexs(disp) ((_glptr_Indexs)(GET_by_offset((disp), _gloffset_Indexs)))
#define SET_Indexs(disp, func) do { \
   void (GLAPIENTRYP fn)(GLshort) = func; \
   SET_by_offset(disp, _gloffset_Indexs, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Indexsv)(const GLshort *);
#define CALL_Indexsv(disp, parameters) (* GET_Indexsv(disp)) parameters
#define GET_Indexsv(disp) ((_glptr_Indexsv)(GET_by_offset((disp), _gloffset_Indexsv)))
#define SET_Indexsv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLshort *) = func; \
   SET_by_offset(disp, _gloffset_Indexsv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Normal3b)(GLbyte, GLbyte, GLbyte);
#define CALL_Normal3b(disp, parameters) (* GET_Normal3b(disp)) parameters
#define GET_Normal3b(disp) ((_glptr_Normal3b)(GET_by_offset((disp), _gloffset_Normal3b)))
#define SET_Normal3b(disp, func) do { \
   void (GLAPIENTRYP fn)(GLbyte, GLbyte, GLbyte) = func; \
   SET_by_offset(disp, _gloffset_Normal3b, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Normal3bv)(const GLbyte *);
#define CALL_Normal3bv(disp, parameters) (* GET_Normal3bv(disp)) parameters
#define GET_Normal3bv(disp) ((_glptr_Normal3bv)(GET_by_offset((disp), _gloffset_Normal3bv)))
#define SET_Normal3bv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLbyte *) = func; \
   SET_by_offset(disp, _gloffset_Normal3bv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Normal3d)(GLdouble, GLdouble, GLdouble);
#define CALL_Normal3d(disp, parameters) (* GET_Normal3d(disp)) parameters
#define GET_Normal3d(disp) ((_glptr_Normal3d)(GET_by_offset((disp), _gloffset_Normal3d)))
#define SET_Normal3d(disp, func) do { \
   void (GLAPIENTRYP fn)(GLdouble, GLdouble, GLdouble) = func; \
   SET_by_offset(disp, _gloffset_Normal3d, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Normal3dv)(const GLdouble *);
#define CALL_Normal3dv(disp, parameters) (* GET_Normal3dv(disp)) parameters
#define GET_Normal3dv(disp) ((_glptr_Normal3dv)(GET_by_offset((disp), _gloffset_Normal3dv)))
#define SET_Normal3dv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_Normal3dv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Normal3f)(GLfloat, GLfloat, GLfloat);
#define CALL_Normal3f(disp, parameters) (* GET_Normal3f(disp)) parameters
#define GET_Normal3f(disp) ((_glptr_Normal3f)(GET_by_offset((disp), _gloffset_Normal3f)))
#define SET_Normal3f(disp, func) do { \
   void (GLAPIENTRYP fn)(GLfloat, GLfloat, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_Normal3f, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Normal3fv)(const GLfloat *);
#define CALL_Normal3fv(disp, parameters) (* GET_Normal3fv(disp)) parameters
#define GET_Normal3fv(disp) ((_glptr_Normal3fv)(GET_by_offset((disp), _gloffset_Normal3fv)))
#define SET_Normal3fv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_Normal3fv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Normal3i)(GLint, GLint, GLint);
#define CALL_Normal3i(disp, parameters) (* GET_Normal3i(disp)) parameters
#define GET_Normal3i(disp) ((_glptr_Normal3i)(GET_by_offset((disp), _gloffset_Normal3i)))
#define SET_Normal3i(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLint, GLint) = func; \
   SET_by_offset(disp, _gloffset_Normal3i, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Normal3iv)(const GLint *);
#define CALL_Normal3iv(disp, parameters) (* GET_Normal3iv(disp)) parameters
#define GET_Normal3iv(disp) ((_glptr_Normal3iv)(GET_by_offset((disp), _gloffset_Normal3iv)))
#define SET_Normal3iv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLint *) = func; \
   SET_by_offset(disp, _gloffset_Normal3iv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Normal3s)(GLshort, GLshort, GLshort);
#define CALL_Normal3s(disp, parameters) (* GET_Normal3s(disp)) parameters
#define GET_Normal3s(disp) ((_glptr_Normal3s)(GET_by_offset((disp), _gloffset_Normal3s)))
#define SET_Normal3s(disp, func) do { \
   void (GLAPIENTRYP fn)(GLshort, GLshort, GLshort) = func; \
   SET_by_offset(disp, _gloffset_Normal3s, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Normal3sv)(const GLshort *);
#define CALL_Normal3sv(disp, parameters) (* GET_Normal3sv(disp)) parameters
#define GET_Normal3sv(disp) ((_glptr_Normal3sv)(GET_by_offset((disp), _gloffset_Normal3sv)))
#define SET_Normal3sv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLshort *) = func; \
   SET_by_offset(disp, _gloffset_Normal3sv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_RasterPos2d)(GLdouble, GLdouble);
#define CALL_RasterPos2d(disp, parameters) (* GET_RasterPos2d(disp)) parameters
#define GET_RasterPos2d(disp) ((_glptr_RasterPos2d)(GET_by_offset((disp), _gloffset_RasterPos2d)))
#define SET_RasterPos2d(disp, func) do { \
   void (GLAPIENTRYP fn)(GLdouble, GLdouble) = func; \
   SET_by_offset(disp, _gloffset_RasterPos2d, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_RasterPos2dv)(const GLdouble *);
#define CALL_RasterPos2dv(disp, parameters) (* GET_RasterPos2dv(disp)) parameters
#define GET_RasterPos2dv(disp) ((_glptr_RasterPos2dv)(GET_by_offset((disp), _gloffset_RasterPos2dv)))
#define SET_RasterPos2dv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_RasterPos2dv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_RasterPos2f)(GLfloat, GLfloat);
#define CALL_RasterPos2f(disp, parameters) (* GET_RasterPos2f(disp)) parameters
#define GET_RasterPos2f(disp) ((_glptr_RasterPos2f)(GET_by_offset((disp), _gloffset_RasterPos2f)))
#define SET_RasterPos2f(disp, func) do { \
   void (GLAPIENTRYP fn)(GLfloat, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_RasterPos2f, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_RasterPos2fv)(const GLfloat *);
#define CALL_RasterPos2fv(disp, parameters) (* GET_RasterPos2fv(disp)) parameters
#define GET_RasterPos2fv(disp) ((_glptr_RasterPos2fv)(GET_by_offset((disp), _gloffset_RasterPos2fv)))
#define SET_RasterPos2fv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_RasterPos2fv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_RasterPos2i)(GLint, GLint);
#define CALL_RasterPos2i(disp, parameters) (* GET_RasterPos2i(disp)) parameters
#define GET_RasterPos2i(disp) ((_glptr_RasterPos2i)(GET_by_offset((disp), _gloffset_RasterPos2i)))
#define SET_RasterPos2i(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLint) = func; \
   SET_by_offset(disp, _gloffset_RasterPos2i, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_RasterPos2iv)(const GLint *);
#define CALL_RasterPos2iv(disp, parameters) (* GET_RasterPos2iv(disp)) parameters
#define GET_RasterPos2iv(disp) ((_glptr_RasterPos2iv)(GET_by_offset((disp), _gloffset_RasterPos2iv)))
#define SET_RasterPos2iv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLint *) = func; \
   SET_by_offset(disp, _gloffset_RasterPos2iv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_RasterPos2s)(GLshort, GLshort);
#define CALL_RasterPos2s(disp, parameters) (* GET_RasterPos2s(disp)) parameters
#define GET_RasterPos2s(disp) ((_glptr_RasterPos2s)(GET_by_offset((disp), _gloffset_RasterPos2s)))
#define SET_RasterPos2s(disp, func) do { \
   void (GLAPIENTRYP fn)(GLshort, GLshort) = func; \
   SET_by_offset(disp, _gloffset_RasterPos2s, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_RasterPos2sv)(const GLshort *);
#define CALL_RasterPos2sv(disp, parameters) (* GET_RasterPos2sv(disp)) parameters
#define GET_RasterPos2sv(disp) ((_glptr_RasterPos2sv)(GET_by_offset((disp), _gloffset_RasterPos2sv)))
#define SET_RasterPos2sv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLshort *) = func; \
   SET_by_offset(disp, _gloffset_RasterPos2sv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_RasterPos3d)(GLdouble, GLdouble, GLdouble);
#define CALL_RasterPos3d(disp, parameters) (* GET_RasterPos3d(disp)) parameters
#define GET_RasterPos3d(disp) ((_glptr_RasterPos3d)(GET_by_offset((disp), _gloffset_RasterPos3d)))
#define SET_RasterPos3d(disp, func) do { \
   void (GLAPIENTRYP fn)(GLdouble, GLdouble, GLdouble) = func; \
   SET_by_offset(disp, _gloffset_RasterPos3d, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_RasterPos3dv)(const GLdouble *);
#define CALL_RasterPos3dv(disp, parameters) (* GET_RasterPos3dv(disp)) parameters
#define GET_RasterPos3dv(disp) ((_glptr_RasterPos3dv)(GET_by_offset((disp), _gloffset_RasterPos3dv)))
#define SET_RasterPos3dv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_RasterPos3dv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_RasterPos3f)(GLfloat, GLfloat, GLfloat);
#define CALL_RasterPos3f(disp, parameters) (* GET_RasterPos3f(disp)) parameters
#define GET_RasterPos3f(disp) ((_glptr_RasterPos3f)(GET_by_offset((disp), _gloffset_RasterPos3f)))
#define SET_RasterPos3f(disp, func) do { \
   void (GLAPIENTRYP fn)(GLfloat, GLfloat, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_RasterPos3f, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_RasterPos3fv)(const GLfloat *);
#define CALL_RasterPos3fv(disp, parameters) (* GET_RasterPos3fv(disp)) parameters
#define GET_RasterPos3fv(disp) ((_glptr_RasterPos3fv)(GET_by_offset((disp), _gloffset_RasterPos3fv)))
#define SET_RasterPos3fv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_RasterPos3fv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_RasterPos3i)(GLint, GLint, GLint);
#define CALL_RasterPos3i(disp, parameters) (* GET_RasterPos3i(disp)) parameters
#define GET_RasterPos3i(disp) ((_glptr_RasterPos3i)(GET_by_offset((disp), _gloffset_RasterPos3i)))
#define SET_RasterPos3i(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLint, GLint) = func; \
   SET_by_offset(disp, _gloffset_RasterPos3i, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_RasterPos3iv)(const GLint *);
#define CALL_RasterPos3iv(disp, parameters) (* GET_RasterPos3iv(disp)) parameters
#define GET_RasterPos3iv(disp) ((_glptr_RasterPos3iv)(GET_by_offset((disp), _gloffset_RasterPos3iv)))
#define SET_RasterPos3iv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLint *) = func; \
   SET_by_offset(disp, _gloffset_RasterPos3iv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_RasterPos3s)(GLshort, GLshort, GLshort);
#define CALL_RasterPos3s(disp, parameters) (* GET_RasterPos3s(disp)) parameters
#define GET_RasterPos3s(disp) ((_glptr_RasterPos3s)(GET_by_offset((disp), _gloffset_RasterPos3s)))
#define SET_RasterPos3s(disp, func) do { \
   void (GLAPIENTRYP fn)(GLshort, GLshort, GLshort) = func; \
   SET_by_offset(disp, _gloffset_RasterPos3s, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_RasterPos3sv)(const GLshort *);
#define CALL_RasterPos3sv(disp, parameters) (* GET_RasterPos3sv(disp)) parameters
#define GET_RasterPos3sv(disp) ((_glptr_RasterPos3sv)(GET_by_offset((disp), _gloffset_RasterPos3sv)))
#define SET_RasterPos3sv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLshort *) = func; \
   SET_by_offset(disp, _gloffset_RasterPos3sv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_RasterPos4d)(GLdouble, GLdouble, GLdouble, GLdouble);
#define CALL_RasterPos4d(disp, parameters) (* GET_RasterPos4d(disp)) parameters
#define GET_RasterPos4d(disp) ((_glptr_RasterPos4d)(GET_by_offset((disp), _gloffset_RasterPos4d)))
#define SET_RasterPos4d(disp, func) do { \
   void (GLAPIENTRYP fn)(GLdouble, GLdouble, GLdouble, GLdouble) = func; \
   SET_by_offset(disp, _gloffset_RasterPos4d, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_RasterPos4dv)(const GLdouble *);
#define CALL_RasterPos4dv(disp, parameters) (* GET_RasterPos4dv(disp)) parameters
#define GET_RasterPos4dv(disp) ((_glptr_RasterPos4dv)(GET_by_offset((disp), _gloffset_RasterPos4dv)))
#define SET_RasterPos4dv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_RasterPos4dv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_RasterPos4f)(GLfloat, GLfloat, GLfloat, GLfloat);
#define CALL_RasterPos4f(disp, parameters) (* GET_RasterPos4f(disp)) parameters
#define GET_RasterPos4f(disp) ((_glptr_RasterPos4f)(GET_by_offset((disp), _gloffset_RasterPos4f)))
#define SET_RasterPos4f(disp, func) do { \
   void (GLAPIENTRYP fn)(GLfloat, GLfloat, GLfloat, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_RasterPos4f, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_RasterPos4fv)(const GLfloat *);
#define CALL_RasterPos4fv(disp, parameters) (* GET_RasterPos4fv(disp)) parameters
#define GET_RasterPos4fv(disp) ((_glptr_RasterPos4fv)(GET_by_offset((disp), _gloffset_RasterPos4fv)))
#define SET_RasterPos4fv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_RasterPos4fv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_RasterPos4i)(GLint, GLint, GLint, GLint);
#define CALL_RasterPos4i(disp, parameters) (* GET_RasterPos4i(disp)) parameters
#define GET_RasterPos4i(disp) ((_glptr_RasterPos4i)(GET_by_offset((disp), _gloffset_RasterPos4i)))
#define SET_RasterPos4i(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLint, GLint, GLint) = func; \
   SET_by_offset(disp, _gloffset_RasterPos4i, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_RasterPos4iv)(const GLint *);
#define CALL_RasterPos4iv(disp, parameters) (* GET_RasterPos4iv(disp)) parameters
#define GET_RasterPos4iv(disp) ((_glptr_RasterPos4iv)(GET_by_offset((disp), _gloffset_RasterPos4iv)))
#define SET_RasterPos4iv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLint *) = func; \
   SET_by_offset(disp, _gloffset_RasterPos4iv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_RasterPos4s)(GLshort, GLshort, GLshort, GLshort);
#define CALL_RasterPos4s(disp, parameters) (* GET_RasterPos4s(disp)) parameters
#define GET_RasterPos4s(disp) ((_glptr_RasterPos4s)(GET_by_offset((disp), _gloffset_RasterPos4s)))
#define SET_RasterPos4s(disp, func) do { \
   void (GLAPIENTRYP fn)(GLshort, GLshort, GLshort, GLshort) = func; \
   SET_by_offset(disp, _gloffset_RasterPos4s, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_RasterPos4sv)(const GLshort *);
#define CALL_RasterPos4sv(disp, parameters) (* GET_RasterPos4sv(disp)) parameters
#define GET_RasterPos4sv(disp) ((_glptr_RasterPos4sv)(GET_by_offset((disp), _gloffset_RasterPos4sv)))
#define SET_RasterPos4sv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLshort *) = func; \
   SET_by_offset(disp, _gloffset_RasterPos4sv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Rectd)(GLdouble, GLdouble, GLdouble, GLdouble);
#define CALL_Rectd(disp, parameters) (* GET_Rectd(disp)) parameters
#define GET_Rectd(disp) ((_glptr_Rectd)(GET_by_offset((disp), _gloffset_Rectd)))
#define SET_Rectd(disp, func) do { \
   void (GLAPIENTRYP fn)(GLdouble, GLdouble, GLdouble, GLdouble) = func; \
   SET_by_offset(disp, _gloffset_Rectd, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Rectdv)(const GLdouble *, const GLdouble *);
#define CALL_Rectdv(disp, parameters) (* GET_Rectdv(disp)) parameters
#define GET_Rectdv(disp) ((_glptr_Rectdv)(GET_by_offset((disp), _gloffset_Rectdv)))
#define SET_Rectdv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLdouble *, const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_Rectdv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Rectf)(GLfloat, GLfloat, GLfloat, GLfloat);
#define CALL_Rectf(disp, parameters) (* GET_Rectf(disp)) parameters
#define GET_Rectf(disp) ((_glptr_Rectf)(GET_by_offset((disp), _gloffset_Rectf)))
#define SET_Rectf(disp, func) do { \
   void (GLAPIENTRYP fn)(GLfloat, GLfloat, GLfloat, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_Rectf, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Rectfv)(const GLfloat *, const GLfloat *);
#define CALL_Rectfv(disp, parameters) (* GET_Rectfv(disp)) parameters
#define GET_Rectfv(disp) ((_glptr_Rectfv)(GET_by_offset((disp), _gloffset_Rectfv)))
#define SET_Rectfv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLfloat *, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_Rectfv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Recti)(GLint, GLint, GLint, GLint);
#define CALL_Recti(disp, parameters) (* GET_Recti(disp)) parameters
#define GET_Recti(disp) ((_glptr_Recti)(GET_by_offset((disp), _gloffset_Recti)))
#define SET_Recti(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLint, GLint, GLint) = func; \
   SET_by_offset(disp, _gloffset_Recti, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Rectiv)(const GLint *, const GLint *);
#define CALL_Rectiv(disp, parameters) (* GET_Rectiv(disp)) parameters
#define GET_Rectiv(disp) ((_glptr_Rectiv)(GET_by_offset((disp), _gloffset_Rectiv)))
#define SET_Rectiv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLint *, const GLint *) = func; \
   SET_by_offset(disp, _gloffset_Rectiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Rects)(GLshort, GLshort, GLshort, GLshort);
#define CALL_Rects(disp, parameters) (* GET_Rects(disp)) parameters
#define GET_Rects(disp) ((_glptr_Rects)(GET_by_offset((disp), _gloffset_Rects)))
#define SET_Rects(disp, func) do { \
   void (GLAPIENTRYP fn)(GLshort, GLshort, GLshort, GLshort) = func; \
   SET_by_offset(disp, _gloffset_Rects, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Rectsv)(const GLshort *, const GLshort *);
#define CALL_Rectsv(disp, parameters) (* GET_Rectsv(disp)) parameters
#define GET_Rectsv(disp) ((_glptr_Rectsv)(GET_by_offset((disp), _gloffset_Rectsv)))
#define SET_Rectsv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLshort *, const GLshort *) = func; \
   SET_by_offset(disp, _gloffset_Rectsv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexCoord1d)(GLdouble);
#define CALL_TexCoord1d(disp, parameters) (* GET_TexCoord1d(disp)) parameters
#define GET_TexCoord1d(disp) ((_glptr_TexCoord1d)(GET_by_offset((disp), _gloffset_TexCoord1d)))
#define SET_TexCoord1d(disp, func) do { \
   void (GLAPIENTRYP fn)(GLdouble) = func; \
   SET_by_offset(disp, _gloffset_TexCoord1d, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexCoord1dv)(const GLdouble *);
#define CALL_TexCoord1dv(disp, parameters) (* GET_TexCoord1dv(disp)) parameters
#define GET_TexCoord1dv(disp) ((_glptr_TexCoord1dv)(GET_by_offset((disp), _gloffset_TexCoord1dv)))
#define SET_TexCoord1dv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_TexCoord1dv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexCoord1f)(GLfloat);
#define CALL_TexCoord1f(disp, parameters) (* GET_TexCoord1f(disp)) parameters
#define GET_TexCoord1f(disp) ((_glptr_TexCoord1f)(GET_by_offset((disp), _gloffset_TexCoord1f)))
#define SET_TexCoord1f(disp, func) do { \
   void (GLAPIENTRYP fn)(GLfloat) = func; \
   SET_by_offset(disp, _gloffset_TexCoord1f, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexCoord1fv)(const GLfloat *);
#define CALL_TexCoord1fv(disp, parameters) (* GET_TexCoord1fv(disp)) parameters
#define GET_TexCoord1fv(disp) ((_glptr_TexCoord1fv)(GET_by_offset((disp), _gloffset_TexCoord1fv)))
#define SET_TexCoord1fv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_TexCoord1fv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexCoord1i)(GLint);
#define CALL_TexCoord1i(disp, parameters) (* GET_TexCoord1i(disp)) parameters
#define GET_TexCoord1i(disp) ((_glptr_TexCoord1i)(GET_by_offset((disp), _gloffset_TexCoord1i)))
#define SET_TexCoord1i(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint) = func; \
   SET_by_offset(disp, _gloffset_TexCoord1i, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexCoord1iv)(const GLint *);
#define CALL_TexCoord1iv(disp, parameters) (* GET_TexCoord1iv(disp)) parameters
#define GET_TexCoord1iv(disp) ((_glptr_TexCoord1iv)(GET_by_offset((disp), _gloffset_TexCoord1iv)))
#define SET_TexCoord1iv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLint *) = func; \
   SET_by_offset(disp, _gloffset_TexCoord1iv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexCoord1s)(GLshort);
#define CALL_TexCoord1s(disp, parameters) (* GET_TexCoord1s(disp)) parameters
#define GET_TexCoord1s(disp) ((_glptr_TexCoord1s)(GET_by_offset((disp), _gloffset_TexCoord1s)))
#define SET_TexCoord1s(disp, func) do { \
   void (GLAPIENTRYP fn)(GLshort) = func; \
   SET_by_offset(disp, _gloffset_TexCoord1s, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexCoord1sv)(const GLshort *);
#define CALL_TexCoord1sv(disp, parameters) (* GET_TexCoord1sv(disp)) parameters
#define GET_TexCoord1sv(disp) ((_glptr_TexCoord1sv)(GET_by_offset((disp), _gloffset_TexCoord1sv)))
#define SET_TexCoord1sv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLshort *) = func; \
   SET_by_offset(disp, _gloffset_TexCoord1sv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexCoord2d)(GLdouble, GLdouble);
#define CALL_TexCoord2d(disp, parameters) (* GET_TexCoord2d(disp)) parameters
#define GET_TexCoord2d(disp) ((_glptr_TexCoord2d)(GET_by_offset((disp), _gloffset_TexCoord2d)))
#define SET_TexCoord2d(disp, func) do { \
   void (GLAPIENTRYP fn)(GLdouble, GLdouble) = func; \
   SET_by_offset(disp, _gloffset_TexCoord2d, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexCoord2dv)(const GLdouble *);
#define CALL_TexCoord2dv(disp, parameters) (* GET_TexCoord2dv(disp)) parameters
#define GET_TexCoord2dv(disp) ((_glptr_TexCoord2dv)(GET_by_offset((disp), _gloffset_TexCoord2dv)))
#define SET_TexCoord2dv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_TexCoord2dv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexCoord2f)(GLfloat, GLfloat);
#define CALL_TexCoord2f(disp, parameters) (* GET_TexCoord2f(disp)) parameters
#define GET_TexCoord2f(disp) ((_glptr_TexCoord2f)(GET_by_offset((disp), _gloffset_TexCoord2f)))
#define SET_TexCoord2f(disp, func) do { \
   void (GLAPIENTRYP fn)(GLfloat, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_TexCoord2f, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexCoord2fv)(const GLfloat *);
#define CALL_TexCoord2fv(disp, parameters) (* GET_TexCoord2fv(disp)) parameters
#define GET_TexCoord2fv(disp) ((_glptr_TexCoord2fv)(GET_by_offset((disp), _gloffset_TexCoord2fv)))
#define SET_TexCoord2fv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_TexCoord2fv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexCoord2i)(GLint, GLint);
#define CALL_TexCoord2i(disp, parameters) (* GET_TexCoord2i(disp)) parameters
#define GET_TexCoord2i(disp) ((_glptr_TexCoord2i)(GET_by_offset((disp), _gloffset_TexCoord2i)))
#define SET_TexCoord2i(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLint) = func; \
   SET_by_offset(disp, _gloffset_TexCoord2i, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexCoord2iv)(const GLint *);
#define CALL_TexCoord2iv(disp, parameters) (* GET_TexCoord2iv(disp)) parameters
#define GET_TexCoord2iv(disp) ((_glptr_TexCoord2iv)(GET_by_offset((disp), _gloffset_TexCoord2iv)))
#define SET_TexCoord2iv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLint *) = func; \
   SET_by_offset(disp, _gloffset_TexCoord2iv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexCoord2s)(GLshort, GLshort);
#define CALL_TexCoord2s(disp, parameters) (* GET_TexCoord2s(disp)) parameters
#define GET_TexCoord2s(disp) ((_glptr_TexCoord2s)(GET_by_offset((disp), _gloffset_TexCoord2s)))
#define SET_TexCoord2s(disp, func) do { \
   void (GLAPIENTRYP fn)(GLshort, GLshort) = func; \
   SET_by_offset(disp, _gloffset_TexCoord2s, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexCoord2sv)(const GLshort *);
#define CALL_TexCoord2sv(disp, parameters) (* GET_TexCoord2sv(disp)) parameters
#define GET_TexCoord2sv(disp) ((_glptr_TexCoord2sv)(GET_by_offset((disp), _gloffset_TexCoord2sv)))
#define SET_TexCoord2sv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLshort *) = func; \
   SET_by_offset(disp, _gloffset_TexCoord2sv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexCoord3d)(GLdouble, GLdouble, GLdouble);
#define CALL_TexCoord3d(disp, parameters) (* GET_TexCoord3d(disp)) parameters
#define GET_TexCoord3d(disp) ((_glptr_TexCoord3d)(GET_by_offset((disp), _gloffset_TexCoord3d)))
#define SET_TexCoord3d(disp, func) do { \
   void (GLAPIENTRYP fn)(GLdouble, GLdouble, GLdouble) = func; \
   SET_by_offset(disp, _gloffset_TexCoord3d, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexCoord3dv)(const GLdouble *);
#define CALL_TexCoord3dv(disp, parameters) (* GET_TexCoord3dv(disp)) parameters
#define GET_TexCoord3dv(disp) ((_glptr_TexCoord3dv)(GET_by_offset((disp), _gloffset_TexCoord3dv)))
#define SET_TexCoord3dv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_TexCoord3dv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexCoord3f)(GLfloat, GLfloat, GLfloat);
#define CALL_TexCoord3f(disp, parameters) (* GET_TexCoord3f(disp)) parameters
#define GET_TexCoord3f(disp) ((_glptr_TexCoord3f)(GET_by_offset((disp), _gloffset_TexCoord3f)))
#define SET_TexCoord3f(disp, func) do { \
   void (GLAPIENTRYP fn)(GLfloat, GLfloat, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_TexCoord3f, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexCoord3fv)(const GLfloat *);
#define CALL_TexCoord3fv(disp, parameters) (* GET_TexCoord3fv(disp)) parameters
#define GET_TexCoord3fv(disp) ((_glptr_TexCoord3fv)(GET_by_offset((disp), _gloffset_TexCoord3fv)))
#define SET_TexCoord3fv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_TexCoord3fv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexCoord3i)(GLint, GLint, GLint);
#define CALL_TexCoord3i(disp, parameters) (* GET_TexCoord3i(disp)) parameters
#define GET_TexCoord3i(disp) ((_glptr_TexCoord3i)(GET_by_offset((disp), _gloffset_TexCoord3i)))
#define SET_TexCoord3i(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLint, GLint) = func; \
   SET_by_offset(disp, _gloffset_TexCoord3i, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexCoord3iv)(const GLint *);
#define CALL_TexCoord3iv(disp, parameters) (* GET_TexCoord3iv(disp)) parameters
#define GET_TexCoord3iv(disp) ((_glptr_TexCoord3iv)(GET_by_offset((disp), _gloffset_TexCoord3iv)))
#define SET_TexCoord3iv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLint *) = func; \
   SET_by_offset(disp, _gloffset_TexCoord3iv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexCoord3s)(GLshort, GLshort, GLshort);
#define CALL_TexCoord3s(disp, parameters) (* GET_TexCoord3s(disp)) parameters
#define GET_TexCoord3s(disp) ((_glptr_TexCoord3s)(GET_by_offset((disp), _gloffset_TexCoord3s)))
#define SET_TexCoord3s(disp, func) do { \
   void (GLAPIENTRYP fn)(GLshort, GLshort, GLshort) = func; \
   SET_by_offset(disp, _gloffset_TexCoord3s, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexCoord3sv)(const GLshort *);
#define CALL_TexCoord3sv(disp, parameters) (* GET_TexCoord3sv(disp)) parameters
#define GET_TexCoord3sv(disp) ((_glptr_TexCoord3sv)(GET_by_offset((disp), _gloffset_TexCoord3sv)))
#define SET_TexCoord3sv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLshort *) = func; \
   SET_by_offset(disp, _gloffset_TexCoord3sv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexCoord4d)(GLdouble, GLdouble, GLdouble, GLdouble);
#define CALL_TexCoord4d(disp, parameters) (* GET_TexCoord4d(disp)) parameters
#define GET_TexCoord4d(disp) ((_glptr_TexCoord4d)(GET_by_offset((disp), _gloffset_TexCoord4d)))
#define SET_TexCoord4d(disp, func) do { \
   void (GLAPIENTRYP fn)(GLdouble, GLdouble, GLdouble, GLdouble) = func; \
   SET_by_offset(disp, _gloffset_TexCoord4d, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexCoord4dv)(const GLdouble *);
#define CALL_TexCoord4dv(disp, parameters) (* GET_TexCoord4dv(disp)) parameters
#define GET_TexCoord4dv(disp) ((_glptr_TexCoord4dv)(GET_by_offset((disp), _gloffset_TexCoord4dv)))
#define SET_TexCoord4dv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_TexCoord4dv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexCoord4f)(GLfloat, GLfloat, GLfloat, GLfloat);
#define CALL_TexCoord4f(disp, parameters) (* GET_TexCoord4f(disp)) parameters
#define GET_TexCoord4f(disp) ((_glptr_TexCoord4f)(GET_by_offset((disp), _gloffset_TexCoord4f)))
#define SET_TexCoord4f(disp, func) do { \
   void (GLAPIENTRYP fn)(GLfloat, GLfloat, GLfloat, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_TexCoord4f, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexCoord4fv)(const GLfloat *);
#define CALL_TexCoord4fv(disp, parameters) (* GET_TexCoord4fv(disp)) parameters
#define GET_TexCoord4fv(disp) ((_glptr_TexCoord4fv)(GET_by_offset((disp), _gloffset_TexCoord4fv)))
#define SET_TexCoord4fv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_TexCoord4fv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexCoord4i)(GLint, GLint, GLint, GLint);
#define CALL_TexCoord4i(disp, parameters) (* GET_TexCoord4i(disp)) parameters
#define GET_TexCoord4i(disp) ((_glptr_TexCoord4i)(GET_by_offset((disp), _gloffset_TexCoord4i)))
#define SET_TexCoord4i(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLint, GLint, GLint) = func; \
   SET_by_offset(disp, _gloffset_TexCoord4i, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexCoord4iv)(const GLint *);
#define CALL_TexCoord4iv(disp, parameters) (* GET_TexCoord4iv(disp)) parameters
#define GET_TexCoord4iv(disp) ((_glptr_TexCoord4iv)(GET_by_offset((disp), _gloffset_TexCoord4iv)))
#define SET_TexCoord4iv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLint *) = func; \
   SET_by_offset(disp, _gloffset_TexCoord4iv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexCoord4s)(GLshort, GLshort, GLshort, GLshort);
#define CALL_TexCoord4s(disp, parameters) (* GET_TexCoord4s(disp)) parameters
#define GET_TexCoord4s(disp) ((_glptr_TexCoord4s)(GET_by_offset((disp), _gloffset_TexCoord4s)))
#define SET_TexCoord4s(disp, func) do { \
   void (GLAPIENTRYP fn)(GLshort, GLshort, GLshort, GLshort) = func; \
   SET_by_offset(disp, _gloffset_TexCoord4s, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexCoord4sv)(const GLshort *);
#define CALL_TexCoord4sv(disp, parameters) (* GET_TexCoord4sv(disp)) parameters
#define GET_TexCoord4sv(disp) ((_glptr_TexCoord4sv)(GET_by_offset((disp), _gloffset_TexCoord4sv)))
#define SET_TexCoord4sv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLshort *) = func; \
   SET_by_offset(disp, _gloffset_TexCoord4sv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Vertex2d)(GLdouble, GLdouble);
#define CALL_Vertex2d(disp, parameters) (* GET_Vertex2d(disp)) parameters
#define GET_Vertex2d(disp) ((_glptr_Vertex2d)(GET_by_offset((disp), _gloffset_Vertex2d)))
#define SET_Vertex2d(disp, func) do { \
   void (GLAPIENTRYP fn)(GLdouble, GLdouble) = func; \
   SET_by_offset(disp, _gloffset_Vertex2d, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Vertex2dv)(const GLdouble *);
#define CALL_Vertex2dv(disp, parameters) (* GET_Vertex2dv(disp)) parameters
#define GET_Vertex2dv(disp) ((_glptr_Vertex2dv)(GET_by_offset((disp), _gloffset_Vertex2dv)))
#define SET_Vertex2dv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_Vertex2dv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Vertex2f)(GLfloat, GLfloat);
#define CALL_Vertex2f(disp, parameters) (* GET_Vertex2f(disp)) parameters
#define GET_Vertex2f(disp) ((_glptr_Vertex2f)(GET_by_offset((disp), _gloffset_Vertex2f)))
#define SET_Vertex2f(disp, func) do { \
   void (GLAPIENTRYP fn)(GLfloat, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_Vertex2f, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Vertex2fv)(const GLfloat *);
#define CALL_Vertex2fv(disp, parameters) (* GET_Vertex2fv(disp)) parameters
#define GET_Vertex2fv(disp) ((_glptr_Vertex2fv)(GET_by_offset((disp), _gloffset_Vertex2fv)))
#define SET_Vertex2fv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_Vertex2fv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Vertex2i)(GLint, GLint);
#define CALL_Vertex2i(disp, parameters) (* GET_Vertex2i(disp)) parameters
#define GET_Vertex2i(disp) ((_glptr_Vertex2i)(GET_by_offset((disp), _gloffset_Vertex2i)))
#define SET_Vertex2i(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLint) = func; \
   SET_by_offset(disp, _gloffset_Vertex2i, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Vertex2iv)(const GLint *);
#define CALL_Vertex2iv(disp, parameters) (* GET_Vertex2iv(disp)) parameters
#define GET_Vertex2iv(disp) ((_glptr_Vertex2iv)(GET_by_offset((disp), _gloffset_Vertex2iv)))
#define SET_Vertex2iv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLint *) = func; \
   SET_by_offset(disp, _gloffset_Vertex2iv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Vertex2s)(GLshort, GLshort);
#define CALL_Vertex2s(disp, parameters) (* GET_Vertex2s(disp)) parameters
#define GET_Vertex2s(disp) ((_glptr_Vertex2s)(GET_by_offset((disp), _gloffset_Vertex2s)))
#define SET_Vertex2s(disp, func) do { \
   void (GLAPIENTRYP fn)(GLshort, GLshort) = func; \
   SET_by_offset(disp, _gloffset_Vertex2s, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Vertex2sv)(const GLshort *);
#define CALL_Vertex2sv(disp, parameters) (* GET_Vertex2sv(disp)) parameters
#define GET_Vertex2sv(disp) ((_glptr_Vertex2sv)(GET_by_offset((disp), _gloffset_Vertex2sv)))
#define SET_Vertex2sv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLshort *) = func; \
   SET_by_offset(disp, _gloffset_Vertex2sv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Vertex3d)(GLdouble, GLdouble, GLdouble);
#define CALL_Vertex3d(disp, parameters) (* GET_Vertex3d(disp)) parameters
#define GET_Vertex3d(disp) ((_glptr_Vertex3d)(GET_by_offset((disp), _gloffset_Vertex3d)))
#define SET_Vertex3d(disp, func) do { \
   void (GLAPIENTRYP fn)(GLdouble, GLdouble, GLdouble) = func; \
   SET_by_offset(disp, _gloffset_Vertex3d, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Vertex3dv)(const GLdouble *);
#define CALL_Vertex3dv(disp, parameters) (* GET_Vertex3dv(disp)) parameters
#define GET_Vertex3dv(disp) ((_glptr_Vertex3dv)(GET_by_offset((disp), _gloffset_Vertex3dv)))
#define SET_Vertex3dv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_Vertex3dv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Vertex3f)(GLfloat, GLfloat, GLfloat);
#define CALL_Vertex3f(disp, parameters) (* GET_Vertex3f(disp)) parameters
#define GET_Vertex3f(disp) ((_glptr_Vertex3f)(GET_by_offset((disp), _gloffset_Vertex3f)))
#define SET_Vertex3f(disp, func) do { \
   void (GLAPIENTRYP fn)(GLfloat, GLfloat, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_Vertex3f, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Vertex3fv)(const GLfloat *);
#define CALL_Vertex3fv(disp, parameters) (* GET_Vertex3fv(disp)) parameters
#define GET_Vertex3fv(disp) ((_glptr_Vertex3fv)(GET_by_offset((disp), _gloffset_Vertex3fv)))
#define SET_Vertex3fv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_Vertex3fv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Vertex3i)(GLint, GLint, GLint);
#define CALL_Vertex3i(disp, parameters) (* GET_Vertex3i(disp)) parameters
#define GET_Vertex3i(disp) ((_glptr_Vertex3i)(GET_by_offset((disp), _gloffset_Vertex3i)))
#define SET_Vertex3i(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLint, GLint) = func; \
   SET_by_offset(disp, _gloffset_Vertex3i, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Vertex3iv)(const GLint *);
#define CALL_Vertex3iv(disp, parameters) (* GET_Vertex3iv(disp)) parameters
#define GET_Vertex3iv(disp) ((_glptr_Vertex3iv)(GET_by_offset((disp), _gloffset_Vertex3iv)))
#define SET_Vertex3iv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLint *) = func; \
   SET_by_offset(disp, _gloffset_Vertex3iv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Vertex3s)(GLshort, GLshort, GLshort);
#define CALL_Vertex3s(disp, parameters) (* GET_Vertex3s(disp)) parameters
#define GET_Vertex3s(disp) ((_glptr_Vertex3s)(GET_by_offset((disp), _gloffset_Vertex3s)))
#define SET_Vertex3s(disp, func) do { \
   void (GLAPIENTRYP fn)(GLshort, GLshort, GLshort) = func; \
   SET_by_offset(disp, _gloffset_Vertex3s, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Vertex3sv)(const GLshort *);
#define CALL_Vertex3sv(disp, parameters) (* GET_Vertex3sv(disp)) parameters
#define GET_Vertex3sv(disp) ((_glptr_Vertex3sv)(GET_by_offset((disp), _gloffset_Vertex3sv)))
#define SET_Vertex3sv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLshort *) = func; \
   SET_by_offset(disp, _gloffset_Vertex3sv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Vertex4d)(GLdouble, GLdouble, GLdouble, GLdouble);
#define CALL_Vertex4d(disp, parameters) (* GET_Vertex4d(disp)) parameters
#define GET_Vertex4d(disp) ((_glptr_Vertex4d)(GET_by_offset((disp), _gloffset_Vertex4d)))
#define SET_Vertex4d(disp, func) do { \
   void (GLAPIENTRYP fn)(GLdouble, GLdouble, GLdouble, GLdouble) = func; \
   SET_by_offset(disp, _gloffset_Vertex4d, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Vertex4dv)(const GLdouble *);
#define CALL_Vertex4dv(disp, parameters) (* GET_Vertex4dv(disp)) parameters
#define GET_Vertex4dv(disp) ((_glptr_Vertex4dv)(GET_by_offset((disp), _gloffset_Vertex4dv)))
#define SET_Vertex4dv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_Vertex4dv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Vertex4f)(GLfloat, GLfloat, GLfloat, GLfloat);
#define CALL_Vertex4f(disp, parameters) (* GET_Vertex4f(disp)) parameters
#define GET_Vertex4f(disp) ((_glptr_Vertex4f)(GET_by_offset((disp), _gloffset_Vertex4f)))
#define SET_Vertex4f(disp, func) do { \
   void (GLAPIENTRYP fn)(GLfloat, GLfloat, GLfloat, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_Vertex4f, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Vertex4fv)(const GLfloat *);
#define CALL_Vertex4fv(disp, parameters) (* GET_Vertex4fv(disp)) parameters
#define GET_Vertex4fv(disp) ((_glptr_Vertex4fv)(GET_by_offset((disp), _gloffset_Vertex4fv)))
#define SET_Vertex4fv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_Vertex4fv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Vertex4i)(GLint, GLint, GLint, GLint);
#define CALL_Vertex4i(disp, parameters) (* GET_Vertex4i(disp)) parameters
#define GET_Vertex4i(disp) ((_glptr_Vertex4i)(GET_by_offset((disp), _gloffset_Vertex4i)))
#define SET_Vertex4i(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLint, GLint, GLint) = func; \
   SET_by_offset(disp, _gloffset_Vertex4i, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Vertex4iv)(const GLint *);
#define CALL_Vertex4iv(disp, parameters) (* GET_Vertex4iv(disp)) parameters
#define GET_Vertex4iv(disp) ((_glptr_Vertex4iv)(GET_by_offset((disp), _gloffset_Vertex4iv)))
#define SET_Vertex4iv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLint *) = func; \
   SET_by_offset(disp, _gloffset_Vertex4iv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Vertex4s)(GLshort, GLshort, GLshort, GLshort);
#define CALL_Vertex4s(disp, parameters) (* GET_Vertex4s(disp)) parameters
#define GET_Vertex4s(disp) ((_glptr_Vertex4s)(GET_by_offset((disp), _gloffset_Vertex4s)))
#define SET_Vertex4s(disp, func) do { \
   void (GLAPIENTRYP fn)(GLshort, GLshort, GLshort, GLshort) = func; \
   SET_by_offset(disp, _gloffset_Vertex4s, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Vertex4sv)(const GLshort *);
#define CALL_Vertex4sv(disp, parameters) (* GET_Vertex4sv(disp)) parameters
#define GET_Vertex4sv(disp) ((_glptr_Vertex4sv)(GET_by_offset((disp), _gloffset_Vertex4sv)))
#define SET_Vertex4sv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLshort *) = func; \
   SET_by_offset(disp, _gloffset_Vertex4sv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ClipPlane)(GLenum, const GLdouble *);
#define CALL_ClipPlane(disp, parameters) (* GET_ClipPlane(disp)) parameters
#define GET_ClipPlane(disp) ((_glptr_ClipPlane)(GET_by_offset((disp), _gloffset_ClipPlane)))
#define SET_ClipPlane(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_ClipPlane, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ColorMaterial)(GLenum, GLenum);
#define CALL_ColorMaterial(disp, parameters) (* GET_ColorMaterial(disp)) parameters
#define GET_ColorMaterial(disp) ((_glptr_ColorMaterial)(GET_by_offset((disp), _gloffset_ColorMaterial)))
#define SET_ColorMaterial(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum) = func; \
   SET_by_offset(disp, _gloffset_ColorMaterial, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CullFace)(GLenum);
#define CALL_CullFace(disp, parameters) (* GET_CullFace(disp)) parameters
#define GET_CullFace(disp) ((_glptr_CullFace)(GET_by_offset((disp), _gloffset_CullFace)))
#define SET_CullFace(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum) = func; \
   SET_by_offset(disp, _gloffset_CullFace, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Fogf)(GLenum, GLfloat);
#define CALL_Fogf(disp, parameters) (* GET_Fogf(disp)) parameters
#define GET_Fogf(disp) ((_glptr_Fogf)(GET_by_offset((disp), _gloffset_Fogf)))
#define SET_Fogf(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_Fogf, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Fogfv)(GLenum, const GLfloat *);
#define CALL_Fogfv(disp, parameters) (* GET_Fogfv(disp)) parameters
#define GET_Fogfv(disp) ((_glptr_Fogfv)(GET_by_offset((disp), _gloffset_Fogfv)))
#define SET_Fogfv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_Fogfv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Fogi)(GLenum, GLint);
#define CALL_Fogi(disp, parameters) (* GET_Fogi(disp)) parameters
#define GET_Fogi(disp) ((_glptr_Fogi)(GET_by_offset((disp), _gloffset_Fogi)))
#define SET_Fogi(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLint) = func; \
   SET_by_offset(disp, _gloffset_Fogi, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Fogiv)(GLenum, const GLint *);
#define CALL_Fogiv(disp, parameters) (* GET_Fogiv(disp)) parameters
#define GET_Fogiv(disp) ((_glptr_Fogiv)(GET_by_offset((disp), _gloffset_Fogiv)))
#define SET_Fogiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, const GLint *) = func; \
   SET_by_offset(disp, _gloffset_Fogiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_FrontFace)(GLenum);
#define CALL_FrontFace(disp, parameters) (* GET_FrontFace(disp)) parameters
#define GET_FrontFace(disp) ((_glptr_FrontFace)(GET_by_offset((disp), _gloffset_FrontFace)))
#define SET_FrontFace(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum) = func; \
   SET_by_offset(disp, _gloffset_FrontFace, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Hint)(GLenum, GLenum);
#define CALL_Hint(disp, parameters) (* GET_Hint(disp)) parameters
#define GET_Hint(disp) ((_glptr_Hint)(GET_by_offset((disp), _gloffset_Hint)))
#define SET_Hint(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum) = func; \
   SET_by_offset(disp, _gloffset_Hint, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Lightf)(GLenum, GLenum, GLfloat);
#define CALL_Lightf(disp, parameters) (* GET_Lightf(disp)) parameters
#define GET_Lightf(disp) ((_glptr_Lightf)(GET_by_offset((disp), _gloffset_Lightf)))
#define SET_Lightf(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_Lightf, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Lightfv)(GLenum, GLenum, const GLfloat *);
#define CALL_Lightfv(disp, parameters) (* GET_Lightfv(disp)) parameters
#define GET_Lightfv(disp) ((_glptr_Lightfv)(GET_by_offset((disp), _gloffset_Lightfv)))
#define SET_Lightfv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_Lightfv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Lighti)(GLenum, GLenum, GLint);
#define CALL_Lighti(disp, parameters) (* GET_Lighti(disp)) parameters
#define GET_Lighti(disp) ((_glptr_Lighti)(GET_by_offset((disp), _gloffset_Lighti)))
#define SET_Lighti(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLint) = func; \
   SET_by_offset(disp, _gloffset_Lighti, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Lightiv)(GLenum, GLenum, const GLint *);
#define CALL_Lightiv(disp, parameters) (* GET_Lightiv(disp)) parameters
#define GET_Lightiv(disp) ((_glptr_Lightiv)(GET_by_offset((disp), _gloffset_Lightiv)))
#define SET_Lightiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, const GLint *) = func; \
   SET_by_offset(disp, _gloffset_Lightiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_LightModelf)(GLenum, GLfloat);
#define CALL_LightModelf(disp, parameters) (* GET_LightModelf(disp)) parameters
#define GET_LightModelf(disp) ((_glptr_LightModelf)(GET_by_offset((disp), _gloffset_LightModelf)))
#define SET_LightModelf(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_LightModelf, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_LightModelfv)(GLenum, const GLfloat *);
#define CALL_LightModelfv(disp, parameters) (* GET_LightModelfv(disp)) parameters
#define GET_LightModelfv(disp) ((_glptr_LightModelfv)(GET_by_offset((disp), _gloffset_LightModelfv)))
#define SET_LightModelfv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_LightModelfv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_LightModeli)(GLenum, GLint);
#define CALL_LightModeli(disp, parameters) (* GET_LightModeli(disp)) parameters
#define GET_LightModeli(disp) ((_glptr_LightModeli)(GET_by_offset((disp), _gloffset_LightModeli)))
#define SET_LightModeli(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLint) = func; \
   SET_by_offset(disp, _gloffset_LightModeli, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_LightModeliv)(GLenum, const GLint *);
#define CALL_LightModeliv(disp, parameters) (* GET_LightModeliv(disp)) parameters
#define GET_LightModeliv(disp) ((_glptr_LightModeliv)(GET_by_offset((disp), _gloffset_LightModeliv)))
#define SET_LightModeliv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, const GLint *) = func; \
   SET_by_offset(disp, _gloffset_LightModeliv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_LineStipple)(GLint, GLushort);
#define CALL_LineStipple(disp, parameters) (* GET_LineStipple(disp)) parameters
#define GET_LineStipple(disp) ((_glptr_LineStipple)(GET_by_offset((disp), _gloffset_LineStipple)))
#define SET_LineStipple(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLushort) = func; \
   SET_by_offset(disp, _gloffset_LineStipple, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_LineWidth)(GLfloat);
#define CALL_LineWidth(disp, parameters) (* GET_LineWidth(disp)) parameters
#define GET_LineWidth(disp) ((_glptr_LineWidth)(GET_by_offset((disp), _gloffset_LineWidth)))
#define SET_LineWidth(disp, func) do { \
   void (GLAPIENTRYP fn)(GLfloat) = func; \
   SET_by_offset(disp, _gloffset_LineWidth, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Materialf)(GLenum, GLenum, GLfloat);
#define CALL_Materialf(disp, parameters) (* GET_Materialf(disp)) parameters
#define GET_Materialf(disp) ((_glptr_Materialf)(GET_by_offset((disp), _gloffset_Materialf)))
#define SET_Materialf(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_Materialf, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Materialfv)(GLenum, GLenum, const GLfloat *);
#define CALL_Materialfv(disp, parameters) (* GET_Materialfv(disp)) parameters
#define GET_Materialfv(disp) ((_glptr_Materialfv)(GET_by_offset((disp), _gloffset_Materialfv)))
#define SET_Materialfv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_Materialfv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Materiali)(GLenum, GLenum, GLint);
#define CALL_Materiali(disp, parameters) (* GET_Materiali(disp)) parameters
#define GET_Materiali(disp) ((_glptr_Materiali)(GET_by_offset((disp), _gloffset_Materiali)))
#define SET_Materiali(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLint) = func; \
   SET_by_offset(disp, _gloffset_Materiali, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Materialiv)(GLenum, GLenum, const GLint *);
#define CALL_Materialiv(disp, parameters) (* GET_Materialiv(disp)) parameters
#define GET_Materialiv(disp) ((_glptr_Materialiv)(GET_by_offset((disp), _gloffset_Materialiv)))
#define SET_Materialiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, const GLint *) = func; \
   SET_by_offset(disp, _gloffset_Materialiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_PointSize)(GLfloat);
#define CALL_PointSize(disp, parameters) (* GET_PointSize(disp)) parameters
#define GET_PointSize(disp) ((_glptr_PointSize)(GET_by_offset((disp), _gloffset_PointSize)))
#define SET_PointSize(disp, func) do { \
   void (GLAPIENTRYP fn)(GLfloat) = func; \
   SET_by_offset(disp, _gloffset_PointSize, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_PolygonMode)(GLenum, GLenum);
#define CALL_PolygonMode(disp, parameters) (* GET_PolygonMode(disp)) parameters
#define GET_PolygonMode(disp) ((_glptr_PolygonMode)(GET_by_offset((disp), _gloffset_PolygonMode)))
#define SET_PolygonMode(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum) = func; \
   SET_by_offset(disp, _gloffset_PolygonMode, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_PolygonStipple)(const GLubyte *);
#define CALL_PolygonStipple(disp, parameters) (* GET_PolygonStipple(disp)) parameters
#define GET_PolygonStipple(disp) ((_glptr_PolygonStipple)(GET_by_offset((disp), _gloffset_PolygonStipple)))
#define SET_PolygonStipple(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLubyte *) = func; \
   SET_by_offset(disp, _gloffset_PolygonStipple, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Scissor)(GLint, GLint, GLsizei, GLsizei);
#define CALL_Scissor(disp, parameters) (* GET_Scissor(disp)) parameters
#define GET_Scissor(disp) ((_glptr_Scissor)(GET_by_offset((disp), _gloffset_Scissor)))
#define SET_Scissor(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLint, GLsizei, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_Scissor, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ShadeModel)(GLenum);
#define CALL_ShadeModel(disp, parameters) (* GET_ShadeModel(disp)) parameters
#define GET_ShadeModel(disp) ((_glptr_ShadeModel)(GET_by_offset((disp), _gloffset_ShadeModel)))
#define SET_ShadeModel(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum) = func; \
   SET_by_offset(disp, _gloffset_ShadeModel, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexParameterf)(GLenum, GLenum, GLfloat);
#define CALL_TexParameterf(disp, parameters) (* GET_TexParameterf(disp)) parameters
#define GET_TexParameterf(disp) ((_glptr_TexParameterf)(GET_by_offset((disp), _gloffset_TexParameterf)))
#define SET_TexParameterf(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_TexParameterf, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexParameterfv)(GLenum, GLenum, const GLfloat *);
#define CALL_TexParameterfv(disp, parameters) (* GET_TexParameterfv(disp)) parameters
#define GET_TexParameterfv(disp) ((_glptr_TexParameterfv)(GET_by_offset((disp), _gloffset_TexParameterfv)))
#define SET_TexParameterfv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_TexParameterfv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexParameteri)(GLenum, GLenum, GLint);
#define CALL_TexParameteri(disp, parameters) (* GET_TexParameteri(disp)) parameters
#define GET_TexParameteri(disp) ((_glptr_TexParameteri)(GET_by_offset((disp), _gloffset_TexParameteri)))
#define SET_TexParameteri(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLint) = func; \
   SET_by_offset(disp, _gloffset_TexParameteri, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexParameteriv)(GLenum, GLenum, const GLint *);
#define CALL_TexParameteriv(disp, parameters) (* GET_TexParameteriv(disp)) parameters
#define GET_TexParameteriv(disp) ((_glptr_TexParameteriv)(GET_by_offset((disp), _gloffset_TexParameteriv)))
#define SET_TexParameteriv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, const GLint *) = func; \
   SET_by_offset(disp, _gloffset_TexParameteriv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexImage1D)(GLenum, GLint, GLint, GLsizei, GLint, GLenum, GLenum, const GLvoid *);
#define CALL_TexImage1D(disp, parameters) (* GET_TexImage1D(disp)) parameters
#define GET_TexImage1D(disp) ((_glptr_TexImage1D)(GET_by_offset((disp), _gloffset_TexImage1D)))
#define SET_TexImage1D(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLint, GLint, GLsizei, GLint, GLenum, GLenum, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_TexImage1D, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const GLvoid *);
#define CALL_TexImage2D(disp, parameters) (* GET_TexImage2D(disp)) parameters
#define GET_TexImage2D(disp) ((_glptr_TexImage2D)(GET_by_offset((disp), _gloffset_TexImage2D)))
#define SET_TexImage2D(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_TexImage2D, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexEnvf)(GLenum, GLenum, GLfloat);
#define CALL_TexEnvf(disp, parameters) (* GET_TexEnvf(disp)) parameters
#define GET_TexEnvf(disp) ((_glptr_TexEnvf)(GET_by_offset((disp), _gloffset_TexEnvf)))
#define SET_TexEnvf(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_TexEnvf, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexEnvfv)(GLenum, GLenum, const GLfloat *);
#define CALL_TexEnvfv(disp, parameters) (* GET_TexEnvfv(disp)) parameters
#define GET_TexEnvfv(disp) ((_glptr_TexEnvfv)(GET_by_offset((disp), _gloffset_TexEnvfv)))
#define SET_TexEnvfv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_TexEnvfv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexEnvi)(GLenum, GLenum, GLint);
#define CALL_TexEnvi(disp, parameters) (* GET_TexEnvi(disp)) parameters
#define GET_TexEnvi(disp) ((_glptr_TexEnvi)(GET_by_offset((disp), _gloffset_TexEnvi)))
#define SET_TexEnvi(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLint) = func; \
   SET_by_offset(disp, _gloffset_TexEnvi, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexEnviv)(GLenum, GLenum, const GLint *);
#define CALL_TexEnviv(disp, parameters) (* GET_TexEnviv(disp)) parameters
#define GET_TexEnviv(disp) ((_glptr_TexEnviv)(GET_by_offset((disp), _gloffset_TexEnviv)))
#define SET_TexEnviv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, const GLint *) = func; \
   SET_by_offset(disp, _gloffset_TexEnviv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexGend)(GLenum, GLenum, GLdouble);
#define CALL_TexGend(disp, parameters) (* GET_TexGend(disp)) parameters
#define GET_TexGend(disp) ((_glptr_TexGend)(GET_by_offset((disp), _gloffset_TexGend)))
#define SET_TexGend(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLdouble) = func; \
   SET_by_offset(disp, _gloffset_TexGend, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexGendv)(GLenum, GLenum, const GLdouble *);
#define CALL_TexGendv(disp, parameters) (* GET_TexGendv(disp)) parameters
#define GET_TexGendv(disp) ((_glptr_TexGendv)(GET_by_offset((disp), _gloffset_TexGendv)))
#define SET_TexGendv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_TexGendv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexGenf)(GLenum, GLenum, GLfloat);
#define CALL_TexGenf(disp, parameters) (* GET_TexGenf(disp)) parameters
#define GET_TexGenf(disp) ((_glptr_TexGenf)(GET_by_offset((disp), _gloffset_TexGenf)))
#define SET_TexGenf(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_TexGenf, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexGenfv)(GLenum, GLenum, const GLfloat *);
#define CALL_TexGenfv(disp, parameters) (* GET_TexGenfv(disp)) parameters
#define GET_TexGenfv(disp) ((_glptr_TexGenfv)(GET_by_offset((disp), _gloffset_TexGenfv)))
#define SET_TexGenfv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_TexGenfv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexGeni)(GLenum, GLenum, GLint);
#define CALL_TexGeni(disp, parameters) (* GET_TexGeni(disp)) parameters
#define GET_TexGeni(disp) ((_glptr_TexGeni)(GET_by_offset((disp), _gloffset_TexGeni)))
#define SET_TexGeni(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLint) = func; \
   SET_by_offset(disp, _gloffset_TexGeni, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexGeniv)(GLenum, GLenum, const GLint *);
#define CALL_TexGeniv(disp, parameters) (* GET_TexGeniv(disp)) parameters
#define GET_TexGeniv(disp) ((_glptr_TexGeniv)(GET_by_offset((disp), _gloffset_TexGeniv)))
#define SET_TexGeniv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, const GLint *) = func; \
   SET_by_offset(disp, _gloffset_TexGeniv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_FeedbackBuffer)(GLsizei, GLenum, GLfloat *);
#define CALL_FeedbackBuffer(disp, parameters) (* GET_FeedbackBuffer(disp)) parameters
#define GET_FeedbackBuffer(disp) ((_glptr_FeedbackBuffer)(GET_by_offset((disp), _gloffset_FeedbackBuffer)))
#define SET_FeedbackBuffer(disp, func) do { \
   void (GLAPIENTRYP fn)(GLsizei, GLenum, GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_FeedbackBuffer, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_SelectBuffer)(GLsizei, GLuint *);
#define CALL_SelectBuffer(disp, parameters) (* GET_SelectBuffer(disp)) parameters
#define GET_SelectBuffer(disp) ((_glptr_SelectBuffer)(GET_by_offset((disp), _gloffset_SelectBuffer)))
#define SET_SelectBuffer(disp, func) do { \
   void (GLAPIENTRYP fn)(GLsizei, GLuint *) = func; \
   SET_by_offset(disp, _gloffset_SelectBuffer, fn); \
} while (0)

typedef GLint (GLAPIENTRYP _glptr_RenderMode)(GLenum);
#define CALL_RenderMode(disp, parameters) (* GET_RenderMode(disp)) parameters
#define GET_RenderMode(disp) ((_glptr_RenderMode)(GET_by_offset((disp), _gloffset_RenderMode)))
#define SET_RenderMode(disp, func) do { \
   GLint (GLAPIENTRYP fn)(GLenum) = func; \
   SET_by_offset(disp, _gloffset_RenderMode, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_InitNames)(void);
#define CALL_InitNames(disp, parameters) (* GET_InitNames(disp)) parameters
#define GET_InitNames(disp) ((_glptr_InitNames)(GET_by_offset((disp), _gloffset_InitNames)))
#define SET_InitNames(disp, func) do { \
   void (GLAPIENTRYP fn)(void) = func; \
   SET_by_offset(disp, _gloffset_InitNames, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_LoadName)(GLuint);
#define CALL_LoadName(disp, parameters) (* GET_LoadName(disp)) parameters
#define GET_LoadName(disp) ((_glptr_LoadName)(GET_by_offset((disp), _gloffset_LoadName)))
#define SET_LoadName(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint) = func; \
   SET_by_offset(disp, _gloffset_LoadName, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_PassThrough)(GLfloat);
#define CALL_PassThrough(disp, parameters) (* GET_PassThrough(disp)) parameters
#define GET_PassThrough(disp) ((_glptr_PassThrough)(GET_by_offset((disp), _gloffset_PassThrough)))
#define SET_PassThrough(disp, func) do { \
   void (GLAPIENTRYP fn)(GLfloat) = func; \
   SET_by_offset(disp, _gloffset_PassThrough, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_PopName)(void);
#define CALL_PopName(disp, parameters) (* GET_PopName(disp)) parameters
#define GET_PopName(disp) ((_glptr_PopName)(GET_by_offset((disp), _gloffset_PopName)))
#define SET_PopName(disp, func) do { \
   void (GLAPIENTRYP fn)(void) = func; \
   SET_by_offset(disp, _gloffset_PopName, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_PushName)(GLuint);
#define CALL_PushName(disp, parameters) (* GET_PushName(disp)) parameters
#define GET_PushName(disp) ((_glptr_PushName)(GET_by_offset((disp), _gloffset_PushName)))
#define SET_PushName(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint) = func; \
   SET_by_offset(disp, _gloffset_PushName, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DrawBuffer)(GLenum);
#define CALL_DrawBuffer(disp, parameters) (* GET_DrawBuffer(disp)) parameters
#define GET_DrawBuffer(disp) ((_glptr_DrawBuffer)(GET_by_offset((disp), _gloffset_DrawBuffer)))
#define SET_DrawBuffer(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum) = func; \
   SET_by_offset(disp, _gloffset_DrawBuffer, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Clear)(GLbitfield);
#define CALL_Clear(disp, parameters) (* GET_Clear(disp)) parameters
#define GET_Clear(disp) ((_glptr_Clear)(GET_by_offset((disp), _gloffset_Clear)))
#define SET_Clear(disp, func) do { \
   void (GLAPIENTRYP fn)(GLbitfield) = func; \
   SET_by_offset(disp, _gloffset_Clear, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ClearAccum)(GLfloat, GLfloat, GLfloat, GLfloat);
#define CALL_ClearAccum(disp, parameters) (* GET_ClearAccum(disp)) parameters
#define GET_ClearAccum(disp) ((_glptr_ClearAccum)(GET_by_offset((disp), _gloffset_ClearAccum)))
#define SET_ClearAccum(disp, func) do { \
   void (GLAPIENTRYP fn)(GLfloat, GLfloat, GLfloat, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_ClearAccum, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ClearIndex)(GLfloat);
#define CALL_ClearIndex(disp, parameters) (* GET_ClearIndex(disp)) parameters
#define GET_ClearIndex(disp) ((_glptr_ClearIndex)(GET_by_offset((disp), _gloffset_ClearIndex)))
#define SET_ClearIndex(disp, func) do { \
   void (GLAPIENTRYP fn)(GLfloat) = func; \
   SET_by_offset(disp, _gloffset_ClearIndex, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ClearColor)(GLclampf, GLclampf, GLclampf, GLclampf);
#define CALL_ClearColor(disp, parameters) (* GET_ClearColor(disp)) parameters
#define GET_ClearColor(disp) ((_glptr_ClearColor)(GET_by_offset((disp), _gloffset_ClearColor)))
#define SET_ClearColor(disp, func) do { \
   void (GLAPIENTRYP fn)(GLclampf, GLclampf, GLclampf, GLclampf) = func; \
   SET_by_offset(disp, _gloffset_ClearColor, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ClearStencil)(GLint);
#define CALL_ClearStencil(disp, parameters) (* GET_ClearStencil(disp)) parameters
#define GET_ClearStencil(disp) ((_glptr_ClearStencil)(GET_by_offset((disp), _gloffset_ClearStencil)))
#define SET_ClearStencil(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint) = func; \
   SET_by_offset(disp, _gloffset_ClearStencil, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ClearDepth)(GLclampd);
#define CALL_ClearDepth(disp, parameters) (* GET_ClearDepth(disp)) parameters
#define GET_ClearDepth(disp) ((_glptr_ClearDepth)(GET_by_offset((disp), _gloffset_ClearDepth)))
#define SET_ClearDepth(disp, func) do { \
   void (GLAPIENTRYP fn)(GLclampd) = func; \
   SET_by_offset(disp, _gloffset_ClearDepth, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_StencilMask)(GLuint);
#define CALL_StencilMask(disp, parameters) (* GET_StencilMask(disp)) parameters
#define GET_StencilMask(disp) ((_glptr_StencilMask)(GET_by_offset((disp), _gloffset_StencilMask)))
#define SET_StencilMask(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint) = func; \
   SET_by_offset(disp, _gloffset_StencilMask, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ColorMask)(GLboolean, GLboolean, GLboolean, GLboolean);
#define CALL_ColorMask(disp, parameters) (* GET_ColorMask(disp)) parameters
#define GET_ColorMask(disp) ((_glptr_ColorMask)(GET_by_offset((disp), _gloffset_ColorMask)))
#define SET_ColorMask(disp, func) do { \
   void (GLAPIENTRYP fn)(GLboolean, GLboolean, GLboolean, GLboolean) = func; \
   SET_by_offset(disp, _gloffset_ColorMask, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DepthMask)(GLboolean);
#define CALL_DepthMask(disp, parameters) (* GET_DepthMask(disp)) parameters
#define GET_DepthMask(disp) ((_glptr_DepthMask)(GET_by_offset((disp), _gloffset_DepthMask)))
#define SET_DepthMask(disp, func) do { \
   void (GLAPIENTRYP fn)(GLboolean) = func; \
   SET_by_offset(disp, _gloffset_DepthMask, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_IndexMask)(GLuint);
#define CALL_IndexMask(disp, parameters) (* GET_IndexMask(disp)) parameters
#define GET_IndexMask(disp) ((_glptr_IndexMask)(GET_by_offset((disp), _gloffset_IndexMask)))
#define SET_IndexMask(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint) = func; \
   SET_by_offset(disp, _gloffset_IndexMask, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Accum)(GLenum, GLfloat);
#define CALL_Accum(disp, parameters) (* GET_Accum(disp)) parameters
#define GET_Accum(disp) ((_glptr_Accum)(GET_by_offset((disp), _gloffset_Accum)))
#define SET_Accum(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_Accum, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Disable)(GLenum);
#define CALL_Disable(disp, parameters) (* GET_Disable(disp)) parameters
#define GET_Disable(disp) ((_glptr_Disable)(GET_by_offset((disp), _gloffset_Disable)))
#define SET_Disable(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum) = func; \
   SET_by_offset(disp, _gloffset_Disable, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Enable)(GLenum);
#define CALL_Enable(disp, parameters) (* GET_Enable(disp)) parameters
#define GET_Enable(disp) ((_glptr_Enable)(GET_by_offset((disp), _gloffset_Enable)))
#define SET_Enable(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum) = func; \
   SET_by_offset(disp, _gloffset_Enable, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Finish)(void);
#define CALL_Finish(disp, parameters) (* GET_Finish(disp)) parameters
#define GET_Finish(disp) ((_glptr_Finish)(GET_by_offset((disp), _gloffset_Finish)))
#define SET_Finish(disp, func) do { \
   void (GLAPIENTRYP fn)(void) = func; \
   SET_by_offset(disp, _gloffset_Finish, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Flush)(void);
#define CALL_Flush(disp, parameters) (* GET_Flush(disp)) parameters
#define GET_Flush(disp) ((_glptr_Flush)(GET_by_offset((disp), _gloffset_Flush)))
#define SET_Flush(disp, func) do { \
   void (GLAPIENTRYP fn)(void) = func; \
   SET_by_offset(disp, _gloffset_Flush, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_PopAttrib)(void);
#define CALL_PopAttrib(disp, parameters) (* GET_PopAttrib(disp)) parameters
#define GET_PopAttrib(disp) ((_glptr_PopAttrib)(GET_by_offset((disp), _gloffset_PopAttrib)))
#define SET_PopAttrib(disp, func) do { \
   void (GLAPIENTRYP fn)(void) = func; \
   SET_by_offset(disp, _gloffset_PopAttrib, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_PushAttrib)(GLbitfield);
#define CALL_PushAttrib(disp, parameters) (* GET_PushAttrib(disp)) parameters
#define GET_PushAttrib(disp) ((_glptr_PushAttrib)(GET_by_offset((disp), _gloffset_PushAttrib)))
#define SET_PushAttrib(disp, func) do { \
   void (GLAPIENTRYP fn)(GLbitfield) = func; \
   SET_by_offset(disp, _gloffset_PushAttrib, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Map1d)(GLenum, GLdouble, GLdouble, GLint, GLint, const GLdouble *);
#define CALL_Map1d(disp, parameters) (* GET_Map1d(disp)) parameters
#define GET_Map1d(disp) ((_glptr_Map1d)(GET_by_offset((disp), _gloffset_Map1d)))
#define SET_Map1d(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLdouble, GLdouble, GLint, GLint, const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_Map1d, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Map1f)(GLenum, GLfloat, GLfloat, GLint, GLint, const GLfloat *);
#define CALL_Map1f(disp, parameters) (* GET_Map1f(disp)) parameters
#define GET_Map1f(disp) ((_glptr_Map1f)(GET_by_offset((disp), _gloffset_Map1f)))
#define SET_Map1f(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLfloat, GLfloat, GLint, GLint, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_Map1f, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Map2d)(GLenum, GLdouble, GLdouble, GLint, GLint, GLdouble, GLdouble, GLint, GLint, const GLdouble *);
#define CALL_Map2d(disp, parameters) (* GET_Map2d(disp)) parameters
#define GET_Map2d(disp) ((_glptr_Map2d)(GET_by_offset((disp), _gloffset_Map2d)))
#define SET_Map2d(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLdouble, GLdouble, GLint, GLint, GLdouble, GLdouble, GLint, GLint, const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_Map2d, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Map2f)(GLenum, GLfloat, GLfloat, GLint, GLint, GLfloat, GLfloat, GLint, GLint, const GLfloat *);
#define CALL_Map2f(disp, parameters) (* GET_Map2f(disp)) parameters
#define GET_Map2f(disp) ((_glptr_Map2f)(GET_by_offset((disp), _gloffset_Map2f)))
#define SET_Map2f(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLfloat, GLfloat, GLint, GLint, GLfloat, GLfloat, GLint, GLint, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_Map2f, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MapGrid1d)(GLint, GLdouble, GLdouble);
#define CALL_MapGrid1d(disp, parameters) (* GET_MapGrid1d(disp)) parameters
#define GET_MapGrid1d(disp) ((_glptr_MapGrid1d)(GET_by_offset((disp), _gloffset_MapGrid1d)))
#define SET_MapGrid1d(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLdouble, GLdouble) = func; \
   SET_by_offset(disp, _gloffset_MapGrid1d, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MapGrid1f)(GLint, GLfloat, GLfloat);
#define CALL_MapGrid1f(disp, parameters) (* GET_MapGrid1f(disp)) parameters
#define GET_MapGrid1f(disp) ((_glptr_MapGrid1f)(GET_by_offset((disp), _gloffset_MapGrid1f)))
#define SET_MapGrid1f(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLfloat, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_MapGrid1f, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MapGrid2d)(GLint, GLdouble, GLdouble, GLint, GLdouble, GLdouble);
#define CALL_MapGrid2d(disp, parameters) (* GET_MapGrid2d(disp)) parameters
#define GET_MapGrid2d(disp) ((_glptr_MapGrid2d)(GET_by_offset((disp), _gloffset_MapGrid2d)))
#define SET_MapGrid2d(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLdouble, GLdouble, GLint, GLdouble, GLdouble) = func; \
   SET_by_offset(disp, _gloffset_MapGrid2d, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MapGrid2f)(GLint, GLfloat, GLfloat, GLint, GLfloat, GLfloat);
#define CALL_MapGrid2f(disp, parameters) (* GET_MapGrid2f(disp)) parameters
#define GET_MapGrid2f(disp) ((_glptr_MapGrid2f)(GET_by_offset((disp), _gloffset_MapGrid2f)))
#define SET_MapGrid2f(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLfloat, GLfloat, GLint, GLfloat, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_MapGrid2f, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_EvalCoord1d)(GLdouble);
#define CALL_EvalCoord1d(disp, parameters) (* GET_EvalCoord1d(disp)) parameters
#define GET_EvalCoord1d(disp) ((_glptr_EvalCoord1d)(GET_by_offset((disp), _gloffset_EvalCoord1d)))
#define SET_EvalCoord1d(disp, func) do { \
   void (GLAPIENTRYP fn)(GLdouble) = func; \
   SET_by_offset(disp, _gloffset_EvalCoord1d, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_EvalCoord1dv)(const GLdouble *);
#define CALL_EvalCoord1dv(disp, parameters) (* GET_EvalCoord1dv(disp)) parameters
#define GET_EvalCoord1dv(disp) ((_glptr_EvalCoord1dv)(GET_by_offset((disp), _gloffset_EvalCoord1dv)))
#define SET_EvalCoord1dv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_EvalCoord1dv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_EvalCoord1f)(GLfloat);
#define CALL_EvalCoord1f(disp, parameters) (* GET_EvalCoord1f(disp)) parameters
#define GET_EvalCoord1f(disp) ((_glptr_EvalCoord1f)(GET_by_offset((disp), _gloffset_EvalCoord1f)))
#define SET_EvalCoord1f(disp, func) do { \
   void (GLAPIENTRYP fn)(GLfloat) = func; \
   SET_by_offset(disp, _gloffset_EvalCoord1f, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_EvalCoord1fv)(const GLfloat *);
#define CALL_EvalCoord1fv(disp, parameters) (* GET_EvalCoord1fv(disp)) parameters
#define GET_EvalCoord1fv(disp) ((_glptr_EvalCoord1fv)(GET_by_offset((disp), _gloffset_EvalCoord1fv)))
#define SET_EvalCoord1fv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_EvalCoord1fv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_EvalCoord2d)(GLdouble, GLdouble);
#define CALL_EvalCoord2d(disp, parameters) (* GET_EvalCoord2d(disp)) parameters
#define GET_EvalCoord2d(disp) ((_glptr_EvalCoord2d)(GET_by_offset((disp), _gloffset_EvalCoord2d)))
#define SET_EvalCoord2d(disp, func) do { \
   void (GLAPIENTRYP fn)(GLdouble, GLdouble) = func; \
   SET_by_offset(disp, _gloffset_EvalCoord2d, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_EvalCoord2dv)(const GLdouble *);
#define CALL_EvalCoord2dv(disp, parameters) (* GET_EvalCoord2dv(disp)) parameters
#define GET_EvalCoord2dv(disp) ((_glptr_EvalCoord2dv)(GET_by_offset((disp), _gloffset_EvalCoord2dv)))
#define SET_EvalCoord2dv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_EvalCoord2dv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_EvalCoord2f)(GLfloat, GLfloat);
#define CALL_EvalCoord2f(disp, parameters) (* GET_EvalCoord2f(disp)) parameters
#define GET_EvalCoord2f(disp) ((_glptr_EvalCoord2f)(GET_by_offset((disp), _gloffset_EvalCoord2f)))
#define SET_EvalCoord2f(disp, func) do { \
   void (GLAPIENTRYP fn)(GLfloat, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_EvalCoord2f, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_EvalCoord2fv)(const GLfloat *);
#define CALL_EvalCoord2fv(disp, parameters) (* GET_EvalCoord2fv(disp)) parameters
#define GET_EvalCoord2fv(disp) ((_glptr_EvalCoord2fv)(GET_by_offset((disp), _gloffset_EvalCoord2fv)))
#define SET_EvalCoord2fv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_EvalCoord2fv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_EvalMesh1)(GLenum, GLint, GLint);
#define CALL_EvalMesh1(disp, parameters) (* GET_EvalMesh1(disp)) parameters
#define GET_EvalMesh1(disp) ((_glptr_EvalMesh1)(GET_by_offset((disp), _gloffset_EvalMesh1)))
#define SET_EvalMesh1(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLint, GLint) = func; \
   SET_by_offset(disp, _gloffset_EvalMesh1, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_EvalPoint1)(GLint);
#define CALL_EvalPoint1(disp, parameters) (* GET_EvalPoint1(disp)) parameters
#define GET_EvalPoint1(disp) ((_glptr_EvalPoint1)(GET_by_offset((disp), _gloffset_EvalPoint1)))
#define SET_EvalPoint1(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint) = func; \
   SET_by_offset(disp, _gloffset_EvalPoint1, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_EvalMesh2)(GLenum, GLint, GLint, GLint, GLint);
#define CALL_EvalMesh2(disp, parameters) (* GET_EvalMesh2(disp)) parameters
#define GET_EvalMesh2(disp) ((_glptr_EvalMesh2)(GET_by_offset((disp), _gloffset_EvalMesh2)))
#define SET_EvalMesh2(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLint, GLint, GLint, GLint) = func; \
   SET_by_offset(disp, _gloffset_EvalMesh2, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_EvalPoint2)(GLint, GLint);
#define CALL_EvalPoint2(disp, parameters) (* GET_EvalPoint2(disp)) parameters
#define GET_EvalPoint2(disp) ((_glptr_EvalPoint2)(GET_by_offset((disp), _gloffset_EvalPoint2)))
#define SET_EvalPoint2(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLint) = func; \
   SET_by_offset(disp, _gloffset_EvalPoint2, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_AlphaFunc)(GLenum, GLclampf);
#define CALL_AlphaFunc(disp, parameters) (* GET_AlphaFunc(disp)) parameters
#define GET_AlphaFunc(disp) ((_glptr_AlphaFunc)(GET_by_offset((disp), _gloffset_AlphaFunc)))
#define SET_AlphaFunc(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLclampf) = func; \
   SET_by_offset(disp, _gloffset_AlphaFunc, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_BlendFunc)(GLenum, GLenum);
#define CALL_BlendFunc(disp, parameters) (* GET_BlendFunc(disp)) parameters
#define GET_BlendFunc(disp) ((_glptr_BlendFunc)(GET_by_offset((disp), _gloffset_BlendFunc)))
#define SET_BlendFunc(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum) = func; \
   SET_by_offset(disp, _gloffset_BlendFunc, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_LogicOp)(GLenum);
#define CALL_LogicOp(disp, parameters) (* GET_LogicOp(disp)) parameters
#define GET_LogicOp(disp) ((_glptr_LogicOp)(GET_by_offset((disp), _gloffset_LogicOp)))
#define SET_LogicOp(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum) = func; \
   SET_by_offset(disp, _gloffset_LogicOp, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_StencilFunc)(GLenum, GLint, GLuint);
#define CALL_StencilFunc(disp, parameters) (* GET_StencilFunc(disp)) parameters
#define GET_StencilFunc(disp) ((_glptr_StencilFunc)(GET_by_offset((disp), _gloffset_StencilFunc)))
#define SET_StencilFunc(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLint, GLuint) = func; \
   SET_by_offset(disp, _gloffset_StencilFunc, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_StencilOp)(GLenum, GLenum, GLenum);
#define CALL_StencilOp(disp, parameters) (* GET_StencilOp(disp)) parameters
#define GET_StencilOp(disp) ((_glptr_StencilOp)(GET_by_offset((disp), _gloffset_StencilOp)))
#define SET_StencilOp(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLenum) = func; \
   SET_by_offset(disp, _gloffset_StencilOp, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DepthFunc)(GLenum);
#define CALL_DepthFunc(disp, parameters) (* GET_DepthFunc(disp)) parameters
#define GET_DepthFunc(disp) ((_glptr_DepthFunc)(GET_by_offset((disp), _gloffset_DepthFunc)))
#define SET_DepthFunc(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum) = func; \
   SET_by_offset(disp, _gloffset_DepthFunc, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_PixelZoom)(GLfloat, GLfloat);
#define CALL_PixelZoom(disp, parameters) (* GET_PixelZoom(disp)) parameters
#define GET_PixelZoom(disp) ((_glptr_PixelZoom)(GET_by_offset((disp), _gloffset_PixelZoom)))
#define SET_PixelZoom(disp, func) do { \
   void (GLAPIENTRYP fn)(GLfloat, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_PixelZoom, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_PixelTransferf)(GLenum, GLfloat);
#define CALL_PixelTransferf(disp, parameters) (* GET_PixelTransferf(disp)) parameters
#define GET_PixelTransferf(disp) ((_glptr_PixelTransferf)(GET_by_offset((disp), _gloffset_PixelTransferf)))
#define SET_PixelTransferf(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_PixelTransferf, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_PixelTransferi)(GLenum, GLint);
#define CALL_PixelTransferi(disp, parameters) (* GET_PixelTransferi(disp)) parameters
#define GET_PixelTransferi(disp) ((_glptr_PixelTransferi)(GET_by_offset((disp), _gloffset_PixelTransferi)))
#define SET_PixelTransferi(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLint) = func; \
   SET_by_offset(disp, _gloffset_PixelTransferi, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_PixelStoref)(GLenum, GLfloat);
#define CALL_PixelStoref(disp, parameters) (* GET_PixelStoref(disp)) parameters
#define GET_PixelStoref(disp) ((_glptr_PixelStoref)(GET_by_offset((disp), _gloffset_PixelStoref)))
#define SET_PixelStoref(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_PixelStoref, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_PixelStorei)(GLenum, GLint);
#define CALL_PixelStorei(disp, parameters) (* GET_PixelStorei(disp)) parameters
#define GET_PixelStorei(disp) ((_glptr_PixelStorei)(GET_by_offset((disp), _gloffset_PixelStorei)))
#define SET_PixelStorei(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLint) = func; \
   SET_by_offset(disp, _gloffset_PixelStorei, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_PixelMapfv)(GLenum, GLsizei, const GLfloat *);
#define CALL_PixelMapfv(disp, parameters) (* GET_PixelMapfv(disp)) parameters
#define GET_PixelMapfv(disp) ((_glptr_PixelMapfv)(GET_by_offset((disp), _gloffset_PixelMapfv)))
#define SET_PixelMapfv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLsizei, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_PixelMapfv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_PixelMapuiv)(GLenum, GLsizei, const GLuint *);
#define CALL_PixelMapuiv(disp, parameters) (* GET_PixelMapuiv(disp)) parameters
#define GET_PixelMapuiv(disp) ((_glptr_PixelMapuiv)(GET_by_offset((disp), _gloffset_PixelMapuiv)))
#define SET_PixelMapuiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLsizei, const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_PixelMapuiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_PixelMapusv)(GLenum, GLsizei, const GLushort *);
#define CALL_PixelMapusv(disp, parameters) (* GET_PixelMapusv(disp)) parameters
#define GET_PixelMapusv(disp) ((_glptr_PixelMapusv)(GET_by_offset((disp), _gloffset_PixelMapusv)))
#define SET_PixelMapusv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLsizei, const GLushort *) = func; \
   SET_by_offset(disp, _gloffset_PixelMapusv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ReadBuffer)(GLenum);
#define CALL_ReadBuffer(disp, parameters) (* GET_ReadBuffer(disp)) parameters
#define GET_ReadBuffer(disp) ((_glptr_ReadBuffer)(GET_by_offset((disp), _gloffset_ReadBuffer)))
#define SET_ReadBuffer(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum) = func; \
   SET_by_offset(disp, _gloffset_ReadBuffer, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CopyPixels)(GLint, GLint, GLsizei, GLsizei, GLenum);
#define CALL_CopyPixels(disp, parameters) (* GET_CopyPixels(disp)) parameters
#define GET_CopyPixels(disp) ((_glptr_CopyPixels)(GET_by_offset((disp), _gloffset_CopyPixels)))
#define SET_CopyPixels(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLint, GLsizei, GLsizei, GLenum) = func; \
   SET_by_offset(disp, _gloffset_CopyPixels, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, GLvoid *);
#define CALL_ReadPixels(disp, parameters) (* GET_ReadPixels(disp)) parameters
#define GET_ReadPixels(disp) ((_glptr_ReadPixels)(GET_by_offset((disp), _gloffset_ReadPixels)))
#define SET_ReadPixels(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_ReadPixels, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DrawPixels)(GLsizei, GLsizei, GLenum, GLenum, const GLvoid *);
#define CALL_DrawPixels(disp, parameters) (* GET_DrawPixels(disp)) parameters
#define GET_DrawPixels(disp) ((_glptr_DrawPixels)(GET_by_offset((disp), _gloffset_DrawPixels)))
#define SET_DrawPixels(disp, func) do { \
   void (GLAPIENTRYP fn)(GLsizei, GLsizei, GLenum, GLenum, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_DrawPixels, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetBooleanv)(GLenum, GLboolean *);
#define CALL_GetBooleanv(disp, parameters) (* GET_GetBooleanv(disp)) parameters
#define GET_GetBooleanv(disp) ((_glptr_GetBooleanv)(GET_by_offset((disp), _gloffset_GetBooleanv)))
#define SET_GetBooleanv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLboolean *) = func; \
   SET_by_offset(disp, _gloffset_GetBooleanv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetClipPlane)(GLenum, GLdouble *);
#define CALL_GetClipPlane(disp, parameters) (* GET_GetClipPlane(disp)) parameters
#define GET_GetClipPlane(disp) ((_glptr_GetClipPlane)(GET_by_offset((disp), _gloffset_GetClipPlane)))
#define SET_GetClipPlane(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_GetClipPlane, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetDoublev)(GLenum, GLdouble *);
#define CALL_GetDoublev(disp, parameters) (* GET_GetDoublev(disp)) parameters
#define GET_GetDoublev(disp) ((_glptr_GetDoublev)(GET_by_offset((disp), _gloffset_GetDoublev)))
#define SET_GetDoublev(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_GetDoublev, fn); \
} while (0)

typedef GLenum (GLAPIENTRYP _glptr_GetError)(void);
#define CALL_GetError(disp, parameters) (* GET_GetError(disp)) parameters
#define GET_GetError(disp) ((_glptr_GetError)(GET_by_offset((disp), _gloffset_GetError)))
#define SET_GetError(disp, func) do { \
   GLenum (GLAPIENTRYP fn)(void) = func; \
   SET_by_offset(disp, _gloffset_GetError, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetFloatv)(GLenum, GLfloat *);
#define CALL_GetFloatv(disp, parameters) (* GET_GetFloatv(disp)) parameters
#define GET_GetFloatv(disp) ((_glptr_GetFloatv)(GET_by_offset((disp), _gloffset_GetFloatv)))
#define SET_GetFloatv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_GetFloatv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetIntegerv)(GLenum, GLint *);
#define CALL_GetIntegerv(disp, parameters) (* GET_GetIntegerv(disp)) parameters
#define GET_GetIntegerv(disp) ((_glptr_GetIntegerv)(GET_by_offset((disp), _gloffset_GetIntegerv)))
#define SET_GetIntegerv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetIntegerv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetLightfv)(GLenum, GLenum, GLfloat *);
#define CALL_GetLightfv(disp, parameters) (* GET_GetLightfv(disp)) parameters
#define GET_GetLightfv(disp) ((_glptr_GetLightfv)(GET_by_offset((disp), _gloffset_GetLightfv)))
#define SET_GetLightfv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_GetLightfv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetLightiv)(GLenum, GLenum, GLint *);
#define CALL_GetLightiv(disp, parameters) (* GET_GetLightiv(disp)) parameters
#define GET_GetLightiv(disp) ((_glptr_GetLightiv)(GET_by_offset((disp), _gloffset_GetLightiv)))
#define SET_GetLightiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetLightiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetMapdv)(GLenum, GLenum, GLdouble *);
#define CALL_GetMapdv(disp, parameters) (* GET_GetMapdv(disp)) parameters
#define GET_GetMapdv(disp) ((_glptr_GetMapdv)(GET_by_offset((disp), _gloffset_GetMapdv)))
#define SET_GetMapdv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_GetMapdv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetMapfv)(GLenum, GLenum, GLfloat *);
#define CALL_GetMapfv(disp, parameters) (* GET_GetMapfv(disp)) parameters
#define GET_GetMapfv(disp) ((_glptr_GetMapfv)(GET_by_offset((disp), _gloffset_GetMapfv)))
#define SET_GetMapfv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_GetMapfv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetMapiv)(GLenum, GLenum, GLint *);
#define CALL_GetMapiv(disp, parameters) (* GET_GetMapiv(disp)) parameters
#define GET_GetMapiv(disp) ((_glptr_GetMapiv)(GET_by_offset((disp), _gloffset_GetMapiv)))
#define SET_GetMapiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetMapiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetMaterialfv)(GLenum, GLenum, GLfloat *);
#define CALL_GetMaterialfv(disp, parameters) (* GET_GetMaterialfv(disp)) parameters
#define GET_GetMaterialfv(disp) ((_glptr_GetMaterialfv)(GET_by_offset((disp), _gloffset_GetMaterialfv)))
#define SET_GetMaterialfv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_GetMaterialfv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetMaterialiv)(GLenum, GLenum, GLint *);
#define CALL_GetMaterialiv(disp, parameters) (* GET_GetMaterialiv(disp)) parameters
#define GET_GetMaterialiv(disp) ((_glptr_GetMaterialiv)(GET_by_offset((disp), _gloffset_GetMaterialiv)))
#define SET_GetMaterialiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetMaterialiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetPixelMapfv)(GLenum, GLfloat *);
#define CALL_GetPixelMapfv(disp, parameters) (* GET_GetPixelMapfv(disp)) parameters
#define GET_GetPixelMapfv(disp) ((_glptr_GetPixelMapfv)(GET_by_offset((disp), _gloffset_GetPixelMapfv)))
#define SET_GetPixelMapfv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_GetPixelMapfv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetPixelMapuiv)(GLenum, GLuint *);
#define CALL_GetPixelMapuiv(disp, parameters) (* GET_GetPixelMapuiv(disp)) parameters
#define GET_GetPixelMapuiv(disp) ((_glptr_GetPixelMapuiv)(GET_by_offset((disp), _gloffset_GetPixelMapuiv)))
#define SET_GetPixelMapuiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint *) = func; \
   SET_by_offset(disp, _gloffset_GetPixelMapuiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetPixelMapusv)(GLenum, GLushort *);
#define CALL_GetPixelMapusv(disp, parameters) (* GET_GetPixelMapusv(disp)) parameters
#define GET_GetPixelMapusv(disp) ((_glptr_GetPixelMapusv)(GET_by_offset((disp), _gloffset_GetPixelMapusv)))
#define SET_GetPixelMapusv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLushort *) = func; \
   SET_by_offset(disp, _gloffset_GetPixelMapusv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetPolygonStipple)(GLubyte *);
#define CALL_GetPolygonStipple(disp, parameters) (* GET_GetPolygonStipple(disp)) parameters
#define GET_GetPolygonStipple(disp) ((_glptr_GetPolygonStipple)(GET_by_offset((disp), _gloffset_GetPolygonStipple)))
#define SET_GetPolygonStipple(disp, func) do { \
   void (GLAPIENTRYP fn)(GLubyte *) = func; \
   SET_by_offset(disp, _gloffset_GetPolygonStipple, fn); \
} while (0)

typedef const GLubyte * (GLAPIENTRYP _glptr_GetString)(GLenum);
#define CALL_GetString(disp, parameters) (* GET_GetString(disp)) parameters
#define GET_GetString(disp) ((_glptr_GetString)(GET_by_offset((disp), _gloffset_GetString)))
#define SET_GetString(disp, func) do { \
   const GLubyte * (GLAPIENTRYP fn)(GLenum) = func; \
   SET_by_offset(disp, _gloffset_GetString, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetTexEnvfv)(GLenum, GLenum, GLfloat *);
#define CALL_GetTexEnvfv(disp, parameters) (* GET_GetTexEnvfv(disp)) parameters
#define GET_GetTexEnvfv(disp) ((_glptr_GetTexEnvfv)(GET_by_offset((disp), _gloffset_GetTexEnvfv)))
#define SET_GetTexEnvfv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_GetTexEnvfv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetTexEnviv)(GLenum, GLenum, GLint *);
#define CALL_GetTexEnviv(disp, parameters) (* GET_GetTexEnviv(disp)) parameters
#define GET_GetTexEnviv(disp) ((_glptr_GetTexEnviv)(GET_by_offset((disp), _gloffset_GetTexEnviv)))
#define SET_GetTexEnviv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetTexEnviv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetTexGendv)(GLenum, GLenum, GLdouble *);
#define CALL_GetTexGendv(disp, parameters) (* GET_GetTexGendv(disp)) parameters
#define GET_GetTexGendv(disp) ((_glptr_GetTexGendv)(GET_by_offset((disp), _gloffset_GetTexGendv)))
#define SET_GetTexGendv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_GetTexGendv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetTexGenfv)(GLenum, GLenum, GLfloat *);
#define CALL_GetTexGenfv(disp, parameters) (* GET_GetTexGenfv(disp)) parameters
#define GET_GetTexGenfv(disp) ((_glptr_GetTexGenfv)(GET_by_offset((disp), _gloffset_GetTexGenfv)))
#define SET_GetTexGenfv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_GetTexGenfv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetTexGeniv)(GLenum, GLenum, GLint *);
#define CALL_GetTexGeniv(disp, parameters) (* GET_GetTexGeniv(disp)) parameters
#define GET_GetTexGeniv(disp) ((_glptr_GetTexGeniv)(GET_by_offset((disp), _gloffset_GetTexGeniv)))
#define SET_GetTexGeniv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetTexGeniv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetTexImage)(GLenum, GLint, GLenum, GLenum, GLvoid *);
#define CALL_GetTexImage(disp, parameters) (* GET_GetTexImage(disp)) parameters
#define GET_GetTexImage(disp) ((_glptr_GetTexImage)(GET_by_offset((disp), _gloffset_GetTexImage)))
#define SET_GetTexImage(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLint, GLenum, GLenum, GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_GetTexImage, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetTexParameterfv)(GLenum, GLenum, GLfloat *);
#define CALL_GetTexParameterfv(disp, parameters) (* GET_GetTexParameterfv(disp)) parameters
#define GET_GetTexParameterfv(disp) ((_glptr_GetTexParameterfv)(GET_by_offset((disp), _gloffset_GetTexParameterfv)))
#define SET_GetTexParameterfv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_GetTexParameterfv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetTexParameteriv)(GLenum, GLenum, GLint *);
#define CALL_GetTexParameteriv(disp, parameters) (* GET_GetTexParameteriv(disp)) parameters
#define GET_GetTexParameteriv(disp) ((_glptr_GetTexParameteriv)(GET_by_offset((disp), _gloffset_GetTexParameteriv)))
#define SET_GetTexParameteriv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetTexParameteriv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetTexLevelParameterfv)(GLenum, GLint, GLenum, GLfloat *);
#define CALL_GetTexLevelParameterfv(disp, parameters) (* GET_GetTexLevelParameterfv(disp)) parameters
#define GET_GetTexLevelParameterfv(disp) ((_glptr_GetTexLevelParameterfv)(GET_by_offset((disp), _gloffset_GetTexLevelParameterfv)))
#define SET_GetTexLevelParameterfv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLint, GLenum, GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_GetTexLevelParameterfv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetTexLevelParameteriv)(GLenum, GLint, GLenum, GLint *);
#define CALL_GetTexLevelParameteriv(disp, parameters) (* GET_GetTexLevelParameteriv(disp)) parameters
#define GET_GetTexLevelParameteriv(disp) ((_glptr_GetTexLevelParameteriv)(GET_by_offset((disp), _gloffset_GetTexLevelParameteriv)))
#define SET_GetTexLevelParameteriv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLint, GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetTexLevelParameteriv, fn); \
} while (0)

typedef GLboolean (GLAPIENTRYP _glptr_IsEnabled)(GLenum);
#define CALL_IsEnabled(disp, parameters) (* GET_IsEnabled(disp)) parameters
#define GET_IsEnabled(disp) ((_glptr_IsEnabled)(GET_by_offset((disp), _gloffset_IsEnabled)))
#define SET_IsEnabled(disp, func) do { \
   GLboolean (GLAPIENTRYP fn)(GLenum) = func; \
   SET_by_offset(disp, _gloffset_IsEnabled, fn); \
} while (0)

typedef GLboolean (GLAPIENTRYP _glptr_IsList)(GLuint);
#define CALL_IsList(disp, parameters) (* GET_IsList(disp)) parameters
#define GET_IsList(disp) ((_glptr_IsList)(GET_by_offset((disp), _gloffset_IsList)))
#define SET_IsList(disp, func) do { \
   GLboolean (GLAPIENTRYP fn)(GLuint) = func; \
   SET_by_offset(disp, _gloffset_IsList, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DepthRange)(GLclampd, GLclampd);
#define CALL_DepthRange(disp, parameters) (* GET_DepthRange(disp)) parameters
#define GET_DepthRange(disp) ((_glptr_DepthRange)(GET_by_offset((disp), _gloffset_DepthRange)))
#define SET_DepthRange(disp, func) do { \
   void (GLAPIENTRYP fn)(GLclampd, GLclampd) = func; \
   SET_by_offset(disp, _gloffset_DepthRange, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Frustum)(GLdouble, GLdouble, GLdouble, GLdouble, GLdouble, GLdouble);
#define CALL_Frustum(disp, parameters) (* GET_Frustum(disp)) parameters
#define GET_Frustum(disp) ((_glptr_Frustum)(GET_by_offset((disp), _gloffset_Frustum)))
#define SET_Frustum(disp, func) do { \
   void (GLAPIENTRYP fn)(GLdouble, GLdouble, GLdouble, GLdouble, GLdouble, GLdouble) = func; \
   SET_by_offset(disp, _gloffset_Frustum, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_LoadIdentity)(void);
#define CALL_LoadIdentity(disp, parameters) (* GET_LoadIdentity(disp)) parameters
#define GET_LoadIdentity(disp) ((_glptr_LoadIdentity)(GET_by_offset((disp), _gloffset_LoadIdentity)))
#define SET_LoadIdentity(disp, func) do { \
   void (GLAPIENTRYP fn)(void) = func; \
   SET_by_offset(disp, _gloffset_LoadIdentity, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_LoadMatrixf)(const GLfloat *);
#define CALL_LoadMatrixf(disp, parameters) (* GET_LoadMatrixf(disp)) parameters
#define GET_LoadMatrixf(disp) ((_glptr_LoadMatrixf)(GET_by_offset((disp), _gloffset_LoadMatrixf)))
#define SET_LoadMatrixf(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_LoadMatrixf, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_LoadMatrixd)(const GLdouble *);
#define CALL_LoadMatrixd(disp, parameters) (* GET_LoadMatrixd(disp)) parameters
#define GET_LoadMatrixd(disp) ((_glptr_LoadMatrixd)(GET_by_offset((disp), _gloffset_LoadMatrixd)))
#define SET_LoadMatrixd(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_LoadMatrixd, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MatrixMode)(GLenum);
#define CALL_MatrixMode(disp, parameters) (* GET_MatrixMode(disp)) parameters
#define GET_MatrixMode(disp) ((_glptr_MatrixMode)(GET_by_offset((disp), _gloffset_MatrixMode)))
#define SET_MatrixMode(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum) = func; \
   SET_by_offset(disp, _gloffset_MatrixMode, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultMatrixf)(const GLfloat *);
#define CALL_MultMatrixf(disp, parameters) (* GET_MultMatrixf(disp)) parameters
#define GET_MultMatrixf(disp) ((_glptr_MultMatrixf)(GET_by_offset((disp), _gloffset_MultMatrixf)))
#define SET_MultMatrixf(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_MultMatrixf, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultMatrixd)(const GLdouble *);
#define CALL_MultMatrixd(disp, parameters) (* GET_MultMatrixd(disp)) parameters
#define GET_MultMatrixd(disp) ((_glptr_MultMatrixd)(GET_by_offset((disp), _gloffset_MultMatrixd)))
#define SET_MultMatrixd(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_MultMatrixd, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Ortho)(GLdouble, GLdouble, GLdouble, GLdouble, GLdouble, GLdouble);
#define CALL_Ortho(disp, parameters) (* GET_Ortho(disp)) parameters
#define GET_Ortho(disp) ((_glptr_Ortho)(GET_by_offset((disp), _gloffset_Ortho)))
#define SET_Ortho(disp, func) do { \
   void (GLAPIENTRYP fn)(GLdouble, GLdouble, GLdouble, GLdouble, GLdouble, GLdouble) = func; \
   SET_by_offset(disp, _gloffset_Ortho, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_PopMatrix)(void);
#define CALL_PopMatrix(disp, parameters) (* GET_PopMatrix(disp)) parameters
#define GET_PopMatrix(disp) ((_glptr_PopMatrix)(GET_by_offset((disp), _gloffset_PopMatrix)))
#define SET_PopMatrix(disp, func) do { \
   void (GLAPIENTRYP fn)(void) = func; \
   SET_by_offset(disp, _gloffset_PopMatrix, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_PushMatrix)(void);
#define CALL_PushMatrix(disp, parameters) (* GET_PushMatrix(disp)) parameters
#define GET_PushMatrix(disp) ((_glptr_PushMatrix)(GET_by_offset((disp), _gloffset_PushMatrix)))
#define SET_PushMatrix(disp, func) do { \
   void (GLAPIENTRYP fn)(void) = func; \
   SET_by_offset(disp, _gloffset_PushMatrix, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Rotated)(GLdouble, GLdouble, GLdouble, GLdouble);
#define CALL_Rotated(disp, parameters) (* GET_Rotated(disp)) parameters
#define GET_Rotated(disp) ((_glptr_Rotated)(GET_by_offset((disp), _gloffset_Rotated)))
#define SET_Rotated(disp, func) do { \
   void (GLAPIENTRYP fn)(GLdouble, GLdouble, GLdouble, GLdouble) = func; \
   SET_by_offset(disp, _gloffset_Rotated, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Rotatef)(GLfloat, GLfloat, GLfloat, GLfloat);
#define CALL_Rotatef(disp, parameters) (* GET_Rotatef(disp)) parameters
#define GET_Rotatef(disp) ((_glptr_Rotatef)(GET_by_offset((disp), _gloffset_Rotatef)))
#define SET_Rotatef(disp, func) do { \
   void (GLAPIENTRYP fn)(GLfloat, GLfloat, GLfloat, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_Rotatef, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Scaled)(GLdouble, GLdouble, GLdouble);
#define CALL_Scaled(disp, parameters) (* GET_Scaled(disp)) parameters
#define GET_Scaled(disp) ((_glptr_Scaled)(GET_by_offset((disp), _gloffset_Scaled)))
#define SET_Scaled(disp, func) do { \
   void (GLAPIENTRYP fn)(GLdouble, GLdouble, GLdouble) = func; \
   SET_by_offset(disp, _gloffset_Scaled, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Scalef)(GLfloat, GLfloat, GLfloat);
#define CALL_Scalef(disp, parameters) (* GET_Scalef(disp)) parameters
#define GET_Scalef(disp) ((_glptr_Scalef)(GET_by_offset((disp), _gloffset_Scalef)))
#define SET_Scalef(disp, func) do { \
   void (GLAPIENTRYP fn)(GLfloat, GLfloat, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_Scalef, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Translated)(GLdouble, GLdouble, GLdouble);
#define CALL_Translated(disp, parameters) (* GET_Translated(disp)) parameters
#define GET_Translated(disp) ((_glptr_Translated)(GET_by_offset((disp), _gloffset_Translated)))
#define SET_Translated(disp, func) do { \
   void (GLAPIENTRYP fn)(GLdouble, GLdouble, GLdouble) = func; \
   SET_by_offset(disp, _gloffset_Translated, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Translatef)(GLfloat, GLfloat, GLfloat);
#define CALL_Translatef(disp, parameters) (* GET_Translatef(disp)) parameters
#define GET_Translatef(disp) ((_glptr_Translatef)(GET_by_offset((disp), _gloffset_Translatef)))
#define SET_Translatef(disp, func) do { \
   void (GLAPIENTRYP fn)(GLfloat, GLfloat, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_Translatef, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Viewport)(GLint, GLint, GLsizei, GLsizei);
#define CALL_Viewport(disp, parameters) (* GET_Viewport(disp)) parameters
#define GET_Viewport(disp) ((_glptr_Viewport)(GET_by_offset((disp), _gloffset_Viewport)))
#define SET_Viewport(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLint, GLsizei, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_Viewport, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ArrayElement)(GLint);
#define CALL_ArrayElement(disp, parameters) (* GET_ArrayElement(disp)) parameters
#define GET_ArrayElement(disp) ((_glptr_ArrayElement)(GET_by_offset((disp), _gloffset_ArrayElement)))
#define SET_ArrayElement(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint) = func; \
   SET_by_offset(disp, _gloffset_ArrayElement, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_BindTexture)(GLenum, GLuint);
#define CALL_BindTexture(disp, parameters) (* GET_BindTexture(disp)) parameters
#define GET_BindTexture(disp) ((_glptr_BindTexture)(GET_by_offset((disp), _gloffset_BindTexture)))
#define SET_BindTexture(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint) = func; \
   SET_by_offset(disp, _gloffset_BindTexture, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ColorPointer)(GLint, GLenum, GLsizei, const GLvoid *);
#define CALL_ColorPointer(disp, parameters) (* GET_ColorPointer(disp)) parameters
#define GET_ColorPointer(disp) ((_glptr_ColorPointer)(GET_by_offset((disp), _gloffset_ColorPointer)))
#define SET_ColorPointer(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLenum, GLsizei, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_ColorPointer, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DisableClientState)(GLenum);
#define CALL_DisableClientState(disp, parameters) (* GET_DisableClientState(disp)) parameters
#define GET_DisableClientState(disp) ((_glptr_DisableClientState)(GET_by_offset((disp), _gloffset_DisableClientState)))
#define SET_DisableClientState(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum) = func; \
   SET_by_offset(disp, _gloffset_DisableClientState, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DrawArrays)(GLenum, GLint, GLsizei);
#define CALL_DrawArrays(disp, parameters) (* GET_DrawArrays(disp)) parameters
#define GET_DrawArrays(disp) ((_glptr_DrawArrays)(GET_by_offset((disp), _gloffset_DrawArrays)))
#define SET_DrawArrays(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLint, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_DrawArrays, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DrawElements)(GLenum, GLsizei, GLenum, const GLvoid *);
#define CALL_DrawElements(disp, parameters) (* GET_DrawElements(disp)) parameters
#define GET_DrawElements(disp) ((_glptr_DrawElements)(GET_by_offset((disp), _gloffset_DrawElements)))
#define SET_DrawElements(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLsizei, GLenum, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_DrawElements, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_EdgeFlagPointer)(GLsizei, const GLvoid *);
#define CALL_EdgeFlagPointer(disp, parameters) (* GET_EdgeFlagPointer(disp)) parameters
#define GET_EdgeFlagPointer(disp) ((_glptr_EdgeFlagPointer)(GET_by_offset((disp), _gloffset_EdgeFlagPointer)))
#define SET_EdgeFlagPointer(disp, func) do { \
   void (GLAPIENTRYP fn)(GLsizei, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_EdgeFlagPointer, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_EnableClientState)(GLenum);
#define CALL_EnableClientState(disp, parameters) (* GET_EnableClientState(disp)) parameters
#define GET_EnableClientState(disp) ((_glptr_EnableClientState)(GET_by_offset((disp), _gloffset_EnableClientState)))
#define SET_EnableClientState(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum) = func; \
   SET_by_offset(disp, _gloffset_EnableClientState, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_IndexPointer)(GLenum, GLsizei, const GLvoid *);
#define CALL_IndexPointer(disp, parameters) (* GET_IndexPointer(disp)) parameters
#define GET_IndexPointer(disp) ((_glptr_IndexPointer)(GET_by_offset((disp), _gloffset_IndexPointer)))
#define SET_IndexPointer(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLsizei, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_IndexPointer, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Indexub)(GLubyte);
#define CALL_Indexub(disp, parameters) (* GET_Indexub(disp)) parameters
#define GET_Indexub(disp) ((_glptr_Indexub)(GET_by_offset((disp), _gloffset_Indexub)))
#define SET_Indexub(disp, func) do { \
   void (GLAPIENTRYP fn)(GLubyte) = func; \
   SET_by_offset(disp, _gloffset_Indexub, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Indexubv)(const GLubyte *);
#define CALL_Indexubv(disp, parameters) (* GET_Indexubv(disp)) parameters
#define GET_Indexubv(disp) ((_glptr_Indexubv)(GET_by_offset((disp), _gloffset_Indexubv)))
#define SET_Indexubv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLubyte *) = func; \
   SET_by_offset(disp, _gloffset_Indexubv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_InterleavedArrays)(GLenum, GLsizei, const GLvoid *);
#define CALL_InterleavedArrays(disp, parameters) (* GET_InterleavedArrays(disp)) parameters
#define GET_InterleavedArrays(disp) ((_glptr_InterleavedArrays)(GET_by_offset((disp), _gloffset_InterleavedArrays)))
#define SET_InterleavedArrays(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLsizei, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_InterleavedArrays, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_NormalPointer)(GLenum, GLsizei, const GLvoid *);
#define CALL_NormalPointer(disp, parameters) (* GET_NormalPointer(disp)) parameters
#define GET_NormalPointer(disp) ((_glptr_NormalPointer)(GET_by_offset((disp), _gloffset_NormalPointer)))
#define SET_NormalPointer(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLsizei, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_NormalPointer, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_PolygonOffset)(GLfloat, GLfloat);
#define CALL_PolygonOffset(disp, parameters) (* GET_PolygonOffset(disp)) parameters
#define GET_PolygonOffset(disp) ((_glptr_PolygonOffset)(GET_by_offset((disp), _gloffset_PolygonOffset)))
#define SET_PolygonOffset(disp, func) do { \
   void (GLAPIENTRYP fn)(GLfloat, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_PolygonOffset, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexCoordPointer)(GLint, GLenum, GLsizei, const GLvoid *);
#define CALL_TexCoordPointer(disp, parameters) (* GET_TexCoordPointer(disp)) parameters
#define GET_TexCoordPointer(disp) ((_glptr_TexCoordPointer)(GET_by_offset((disp), _gloffset_TexCoordPointer)))
#define SET_TexCoordPointer(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLenum, GLsizei, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_TexCoordPointer, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexPointer)(GLint, GLenum, GLsizei, const GLvoid *);
#define CALL_VertexPointer(disp, parameters) (* GET_VertexPointer(disp)) parameters
#define GET_VertexPointer(disp) ((_glptr_VertexPointer)(GET_by_offset((disp), _gloffset_VertexPointer)))
#define SET_VertexPointer(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLenum, GLsizei, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_VertexPointer, fn); \
} while (0)

typedef GLboolean (GLAPIENTRYP _glptr_AreTexturesResident)(GLsizei, const GLuint *, GLboolean *);
#define CALL_AreTexturesResident(disp, parameters) (* GET_AreTexturesResident(disp)) parameters
#define GET_AreTexturesResident(disp) ((_glptr_AreTexturesResident)(GET_by_offset((disp), _gloffset_AreTexturesResident)))
#define SET_AreTexturesResident(disp, func) do { \
   GLboolean (GLAPIENTRYP fn)(GLsizei, const GLuint *, GLboolean *) = func; \
   SET_by_offset(disp, _gloffset_AreTexturesResident, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CopyTexImage1D)(GLenum, GLint, GLenum, GLint, GLint, GLsizei, GLint);
#define CALL_CopyTexImage1D(disp, parameters) (* GET_CopyTexImage1D(disp)) parameters
#define GET_CopyTexImage1D(disp) ((_glptr_CopyTexImage1D)(GET_by_offset((disp), _gloffset_CopyTexImage1D)))
#define SET_CopyTexImage1D(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLint, GLenum, GLint, GLint, GLsizei, GLint) = func; \
   SET_by_offset(disp, _gloffset_CopyTexImage1D, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CopyTexImage2D)(GLenum, GLint, GLenum, GLint, GLint, GLsizei, GLsizei, GLint);
#define CALL_CopyTexImage2D(disp, parameters) (* GET_CopyTexImage2D(disp)) parameters
#define GET_CopyTexImage2D(disp) ((_glptr_CopyTexImage2D)(GET_by_offset((disp), _gloffset_CopyTexImage2D)))
#define SET_CopyTexImage2D(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLint, GLenum, GLint, GLint, GLsizei, GLsizei, GLint) = func; \
   SET_by_offset(disp, _gloffset_CopyTexImage2D, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CopyTexSubImage1D)(GLenum, GLint, GLint, GLint, GLint, GLsizei);
#define CALL_CopyTexSubImage1D(disp, parameters) (* GET_CopyTexSubImage1D(disp)) parameters
#define GET_CopyTexSubImage1D(disp) ((_glptr_CopyTexSubImage1D)(GET_by_offset((disp), _gloffset_CopyTexSubImage1D)))
#define SET_CopyTexSubImage1D(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLint, GLint, GLint, GLint, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_CopyTexSubImage1D, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CopyTexSubImage2D)(GLenum, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei);
#define CALL_CopyTexSubImage2D(disp, parameters) (* GET_CopyTexSubImage2D(disp)) parameters
#define GET_CopyTexSubImage2D(disp) ((_glptr_CopyTexSubImage2D)(GET_by_offset((disp), _gloffset_CopyTexSubImage2D)))
#define SET_CopyTexSubImage2D(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_CopyTexSubImage2D, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DeleteTextures)(GLsizei, const GLuint *);
#define CALL_DeleteTextures(disp, parameters) (* GET_DeleteTextures(disp)) parameters
#define GET_DeleteTextures(disp) ((_glptr_DeleteTextures)(GET_by_offset((disp), _gloffset_DeleteTextures)))
#define SET_DeleteTextures(disp, func) do { \
   void (GLAPIENTRYP fn)(GLsizei, const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_DeleteTextures, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GenTextures)(GLsizei, GLuint *);
#define CALL_GenTextures(disp, parameters) (* GET_GenTextures(disp)) parameters
#define GET_GenTextures(disp) ((_glptr_GenTextures)(GET_by_offset((disp), _gloffset_GenTextures)))
#define SET_GenTextures(disp, func) do { \
   void (GLAPIENTRYP fn)(GLsizei, GLuint *) = func; \
   SET_by_offset(disp, _gloffset_GenTextures, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetPointerv)(GLenum, GLvoid **);
#define CALL_GetPointerv(disp, parameters) (* GET_GetPointerv(disp)) parameters
#define GET_GetPointerv(disp) ((_glptr_GetPointerv)(GET_by_offset((disp), _gloffset_GetPointerv)))
#define SET_GetPointerv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLvoid **) = func; \
   SET_by_offset(disp, _gloffset_GetPointerv, fn); \
} while (0)

typedef GLboolean (GLAPIENTRYP _glptr_IsTexture)(GLuint);
#define CALL_IsTexture(disp, parameters) (* GET_IsTexture(disp)) parameters
#define GET_IsTexture(disp) ((_glptr_IsTexture)(GET_by_offset((disp), _gloffset_IsTexture)))
#define SET_IsTexture(disp, func) do { \
   GLboolean (GLAPIENTRYP fn)(GLuint) = func; \
   SET_by_offset(disp, _gloffset_IsTexture, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_PrioritizeTextures)(GLsizei, const GLuint *, const GLclampf *);
#define CALL_PrioritizeTextures(disp, parameters) (* GET_PrioritizeTextures(disp)) parameters
#define GET_PrioritizeTextures(disp) ((_glptr_PrioritizeTextures)(GET_by_offset((disp), _gloffset_PrioritizeTextures)))
#define SET_PrioritizeTextures(disp, func) do { \
   void (GLAPIENTRYP fn)(GLsizei, const GLuint *, const GLclampf *) = func; \
   SET_by_offset(disp, _gloffset_PrioritizeTextures, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexSubImage1D)(GLenum, GLint, GLint, GLsizei, GLenum, GLenum, const GLvoid *);
#define CALL_TexSubImage1D(disp, parameters) (* GET_TexSubImage1D(disp)) parameters
#define GET_TexSubImage1D(disp) ((_glptr_TexSubImage1D)(GET_by_offset((disp), _gloffset_TexSubImage1D)))
#define SET_TexSubImage1D(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLint, GLint, GLsizei, GLenum, GLenum, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_TexSubImage1D, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexSubImage2D)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const GLvoid *);
#define CALL_TexSubImage2D(disp, parameters) (* GET_TexSubImage2D(disp)) parameters
#define GET_TexSubImage2D(disp) ((_glptr_TexSubImage2D)(GET_by_offset((disp), _gloffset_TexSubImage2D)))
#define SET_TexSubImage2D(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_TexSubImage2D, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_PopClientAttrib)(void);
#define CALL_PopClientAttrib(disp, parameters) (* GET_PopClientAttrib(disp)) parameters
#define GET_PopClientAttrib(disp) ((_glptr_PopClientAttrib)(GET_by_offset((disp), _gloffset_PopClientAttrib)))
#define SET_PopClientAttrib(disp, func) do { \
   void (GLAPIENTRYP fn)(void) = func; \
   SET_by_offset(disp, _gloffset_PopClientAttrib, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_PushClientAttrib)(GLbitfield);
#define CALL_PushClientAttrib(disp, parameters) (* GET_PushClientAttrib(disp)) parameters
#define GET_PushClientAttrib(disp) ((_glptr_PushClientAttrib)(GET_by_offset((disp), _gloffset_PushClientAttrib)))
#define SET_PushClientAttrib(disp, func) do { \
   void (GLAPIENTRYP fn)(GLbitfield) = func; \
   SET_by_offset(disp, _gloffset_PushClientAttrib, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_BlendColor)(GLclampf, GLclampf, GLclampf, GLclampf);
#define CALL_BlendColor(disp, parameters) (* GET_BlendColor(disp)) parameters
#define GET_BlendColor(disp) ((_glptr_BlendColor)(GET_by_offset((disp), _gloffset_BlendColor)))
#define SET_BlendColor(disp, func) do { \
   void (GLAPIENTRYP fn)(GLclampf, GLclampf, GLclampf, GLclampf) = func; \
   SET_by_offset(disp, _gloffset_BlendColor, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_BlendEquation)(GLenum);
#define CALL_BlendEquation(disp, parameters) (* GET_BlendEquation(disp)) parameters
#define GET_BlendEquation(disp) ((_glptr_BlendEquation)(GET_by_offset((disp), _gloffset_BlendEquation)))
#define SET_BlendEquation(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum) = func; \
   SET_by_offset(disp, _gloffset_BlendEquation, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DrawRangeElements)(GLenum, GLuint, GLuint, GLsizei, GLenum, const GLvoid *);
#define CALL_DrawRangeElements(disp, parameters) (* GET_DrawRangeElements(disp)) parameters
#define GET_DrawRangeElements(disp) ((_glptr_DrawRangeElements)(GET_by_offset((disp), _gloffset_DrawRangeElements)))
#define SET_DrawRangeElements(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint, GLuint, GLsizei, GLenum, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_DrawRangeElements, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ColorTable)(GLenum, GLenum, GLsizei, GLenum, GLenum, const GLvoid *);
#define CALL_ColorTable(disp, parameters) (* GET_ColorTable(disp)) parameters
#define GET_ColorTable(disp) ((_glptr_ColorTable)(GET_by_offset((disp), _gloffset_ColorTable)))
#define SET_ColorTable(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLsizei, GLenum, GLenum, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_ColorTable, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ColorTableParameterfv)(GLenum, GLenum, const GLfloat *);
#define CALL_ColorTableParameterfv(disp, parameters) (* GET_ColorTableParameterfv(disp)) parameters
#define GET_ColorTableParameterfv(disp) ((_glptr_ColorTableParameterfv)(GET_by_offset((disp), _gloffset_ColorTableParameterfv)))
#define SET_ColorTableParameterfv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_ColorTableParameterfv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ColorTableParameteriv)(GLenum, GLenum, const GLint *);
#define CALL_ColorTableParameteriv(disp, parameters) (* GET_ColorTableParameteriv(disp)) parameters
#define GET_ColorTableParameteriv(disp) ((_glptr_ColorTableParameteriv)(GET_by_offset((disp), _gloffset_ColorTableParameteriv)))
#define SET_ColorTableParameteriv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, const GLint *) = func; \
   SET_by_offset(disp, _gloffset_ColorTableParameteriv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CopyColorTable)(GLenum, GLenum, GLint, GLint, GLsizei);
#define CALL_CopyColorTable(disp, parameters) (* GET_CopyColorTable(disp)) parameters
#define GET_CopyColorTable(disp) ((_glptr_CopyColorTable)(GET_by_offset((disp), _gloffset_CopyColorTable)))
#define SET_CopyColorTable(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLint, GLint, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_CopyColorTable, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetColorTable)(GLenum, GLenum, GLenum, GLvoid *);
#define CALL_GetColorTable(disp, parameters) (* GET_GetColorTable(disp)) parameters
#define GET_GetColorTable(disp) ((_glptr_GetColorTable)(GET_by_offset((disp), _gloffset_GetColorTable)))
#define SET_GetColorTable(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLenum, GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_GetColorTable, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetColorTableParameterfv)(GLenum, GLenum, GLfloat *);
#define CALL_GetColorTableParameterfv(disp, parameters) (* GET_GetColorTableParameterfv(disp)) parameters
#define GET_GetColorTableParameterfv(disp) ((_glptr_GetColorTableParameterfv)(GET_by_offset((disp), _gloffset_GetColorTableParameterfv)))
#define SET_GetColorTableParameterfv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_GetColorTableParameterfv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetColorTableParameteriv)(GLenum, GLenum, GLint *);
#define CALL_GetColorTableParameteriv(disp, parameters) (* GET_GetColorTableParameteriv(disp)) parameters
#define GET_GetColorTableParameteriv(disp) ((_glptr_GetColorTableParameteriv)(GET_by_offset((disp), _gloffset_GetColorTableParameteriv)))
#define SET_GetColorTableParameteriv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetColorTableParameteriv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ColorSubTable)(GLenum, GLsizei, GLsizei, GLenum, GLenum, const GLvoid *);
#define CALL_ColorSubTable(disp, parameters) (* GET_ColorSubTable(disp)) parameters
#define GET_ColorSubTable(disp) ((_glptr_ColorSubTable)(GET_by_offset((disp), _gloffset_ColorSubTable)))
#define SET_ColorSubTable(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLsizei, GLsizei, GLenum, GLenum, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_ColorSubTable, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CopyColorSubTable)(GLenum, GLsizei, GLint, GLint, GLsizei);
#define CALL_CopyColorSubTable(disp, parameters) (* GET_CopyColorSubTable(disp)) parameters
#define GET_CopyColorSubTable(disp) ((_glptr_CopyColorSubTable)(GET_by_offset((disp), _gloffset_CopyColorSubTable)))
#define SET_CopyColorSubTable(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLsizei, GLint, GLint, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_CopyColorSubTable, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ConvolutionFilter1D)(GLenum, GLenum, GLsizei, GLenum, GLenum, const GLvoid *);
#define CALL_ConvolutionFilter1D(disp, parameters) (* GET_ConvolutionFilter1D(disp)) parameters
#define GET_ConvolutionFilter1D(disp) ((_glptr_ConvolutionFilter1D)(GET_by_offset((disp), _gloffset_ConvolutionFilter1D)))
#define SET_ConvolutionFilter1D(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLsizei, GLenum, GLenum, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_ConvolutionFilter1D, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ConvolutionFilter2D)(GLenum, GLenum, GLsizei, GLsizei, GLenum, GLenum, const GLvoid *);
#define CALL_ConvolutionFilter2D(disp, parameters) (* GET_ConvolutionFilter2D(disp)) parameters
#define GET_ConvolutionFilter2D(disp) ((_glptr_ConvolutionFilter2D)(GET_by_offset((disp), _gloffset_ConvolutionFilter2D)))
#define SET_ConvolutionFilter2D(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLsizei, GLsizei, GLenum, GLenum, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_ConvolutionFilter2D, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ConvolutionParameterf)(GLenum, GLenum, GLfloat);
#define CALL_ConvolutionParameterf(disp, parameters) (* GET_ConvolutionParameterf(disp)) parameters
#define GET_ConvolutionParameterf(disp) ((_glptr_ConvolutionParameterf)(GET_by_offset((disp), _gloffset_ConvolutionParameterf)))
#define SET_ConvolutionParameterf(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_ConvolutionParameterf, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ConvolutionParameterfv)(GLenum, GLenum, const GLfloat *);
#define CALL_ConvolutionParameterfv(disp, parameters) (* GET_ConvolutionParameterfv(disp)) parameters
#define GET_ConvolutionParameterfv(disp) ((_glptr_ConvolutionParameterfv)(GET_by_offset((disp), _gloffset_ConvolutionParameterfv)))
#define SET_ConvolutionParameterfv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_ConvolutionParameterfv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ConvolutionParameteri)(GLenum, GLenum, GLint);
#define CALL_ConvolutionParameteri(disp, parameters) (* GET_ConvolutionParameteri(disp)) parameters
#define GET_ConvolutionParameteri(disp) ((_glptr_ConvolutionParameteri)(GET_by_offset((disp), _gloffset_ConvolutionParameteri)))
#define SET_ConvolutionParameteri(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLint) = func; \
   SET_by_offset(disp, _gloffset_ConvolutionParameteri, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ConvolutionParameteriv)(GLenum, GLenum, const GLint *);
#define CALL_ConvolutionParameteriv(disp, parameters) (* GET_ConvolutionParameteriv(disp)) parameters
#define GET_ConvolutionParameteriv(disp) ((_glptr_ConvolutionParameteriv)(GET_by_offset((disp), _gloffset_ConvolutionParameteriv)))
#define SET_ConvolutionParameteriv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, const GLint *) = func; \
   SET_by_offset(disp, _gloffset_ConvolutionParameteriv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CopyConvolutionFilter1D)(GLenum, GLenum, GLint, GLint, GLsizei);
#define CALL_CopyConvolutionFilter1D(disp, parameters) (* GET_CopyConvolutionFilter1D(disp)) parameters
#define GET_CopyConvolutionFilter1D(disp) ((_glptr_CopyConvolutionFilter1D)(GET_by_offset((disp), _gloffset_CopyConvolutionFilter1D)))
#define SET_CopyConvolutionFilter1D(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLint, GLint, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_CopyConvolutionFilter1D, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CopyConvolutionFilter2D)(GLenum, GLenum, GLint, GLint, GLsizei, GLsizei);
#define CALL_CopyConvolutionFilter2D(disp, parameters) (* GET_CopyConvolutionFilter2D(disp)) parameters
#define GET_CopyConvolutionFilter2D(disp) ((_glptr_CopyConvolutionFilter2D)(GET_by_offset((disp), _gloffset_CopyConvolutionFilter2D)))
#define SET_CopyConvolutionFilter2D(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLint, GLint, GLsizei, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_CopyConvolutionFilter2D, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetConvolutionFilter)(GLenum, GLenum, GLenum, GLvoid *);
#define CALL_GetConvolutionFilter(disp, parameters) (* GET_GetConvolutionFilter(disp)) parameters
#define GET_GetConvolutionFilter(disp) ((_glptr_GetConvolutionFilter)(GET_by_offset((disp), _gloffset_GetConvolutionFilter)))
#define SET_GetConvolutionFilter(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLenum, GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_GetConvolutionFilter, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetConvolutionParameterfv)(GLenum, GLenum, GLfloat *);
#define CALL_GetConvolutionParameterfv(disp, parameters) (* GET_GetConvolutionParameterfv(disp)) parameters
#define GET_GetConvolutionParameterfv(disp) ((_glptr_GetConvolutionParameterfv)(GET_by_offset((disp), _gloffset_GetConvolutionParameterfv)))
#define SET_GetConvolutionParameterfv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_GetConvolutionParameterfv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetConvolutionParameteriv)(GLenum, GLenum, GLint *);
#define CALL_GetConvolutionParameteriv(disp, parameters) (* GET_GetConvolutionParameteriv(disp)) parameters
#define GET_GetConvolutionParameteriv(disp) ((_glptr_GetConvolutionParameteriv)(GET_by_offset((disp), _gloffset_GetConvolutionParameteriv)))
#define SET_GetConvolutionParameteriv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetConvolutionParameteriv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetSeparableFilter)(GLenum, GLenum, GLenum, GLvoid *, GLvoid *, GLvoid *);
#define CALL_GetSeparableFilter(disp, parameters) (* GET_GetSeparableFilter(disp)) parameters
#define GET_GetSeparableFilter(disp) ((_glptr_GetSeparableFilter)(GET_by_offset((disp), _gloffset_GetSeparableFilter)))
#define SET_GetSeparableFilter(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLenum, GLvoid *, GLvoid *, GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_GetSeparableFilter, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_SeparableFilter2D)(GLenum, GLenum, GLsizei, GLsizei, GLenum, GLenum, const GLvoid *, const GLvoid *);
#define CALL_SeparableFilter2D(disp, parameters) (* GET_SeparableFilter2D(disp)) parameters
#define GET_SeparableFilter2D(disp) ((_glptr_SeparableFilter2D)(GET_by_offset((disp), _gloffset_SeparableFilter2D)))
#define SET_SeparableFilter2D(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLsizei, GLsizei, GLenum, GLenum, const GLvoid *, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_SeparableFilter2D, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetHistogram)(GLenum, GLboolean, GLenum, GLenum, GLvoid *);
#define CALL_GetHistogram(disp, parameters) (* GET_GetHistogram(disp)) parameters
#define GET_GetHistogram(disp) ((_glptr_GetHistogram)(GET_by_offset((disp), _gloffset_GetHistogram)))
#define SET_GetHistogram(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLboolean, GLenum, GLenum, GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_GetHistogram, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetHistogramParameterfv)(GLenum, GLenum, GLfloat *);
#define CALL_GetHistogramParameterfv(disp, parameters) (* GET_GetHistogramParameterfv(disp)) parameters
#define GET_GetHistogramParameterfv(disp) ((_glptr_GetHistogramParameterfv)(GET_by_offset((disp), _gloffset_GetHistogramParameterfv)))
#define SET_GetHistogramParameterfv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_GetHistogramParameterfv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetHistogramParameteriv)(GLenum, GLenum, GLint *);
#define CALL_GetHistogramParameteriv(disp, parameters) (* GET_GetHistogramParameteriv(disp)) parameters
#define GET_GetHistogramParameteriv(disp) ((_glptr_GetHistogramParameteriv)(GET_by_offset((disp), _gloffset_GetHistogramParameteriv)))
#define SET_GetHistogramParameteriv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetHistogramParameteriv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetMinmax)(GLenum, GLboolean, GLenum, GLenum, GLvoid *);
#define CALL_GetMinmax(disp, parameters) (* GET_GetMinmax(disp)) parameters
#define GET_GetMinmax(disp) ((_glptr_GetMinmax)(GET_by_offset((disp), _gloffset_GetMinmax)))
#define SET_GetMinmax(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLboolean, GLenum, GLenum, GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_GetMinmax, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetMinmaxParameterfv)(GLenum, GLenum, GLfloat *);
#define CALL_GetMinmaxParameterfv(disp, parameters) (* GET_GetMinmaxParameterfv(disp)) parameters
#define GET_GetMinmaxParameterfv(disp) ((_glptr_GetMinmaxParameterfv)(GET_by_offset((disp), _gloffset_GetMinmaxParameterfv)))
#define SET_GetMinmaxParameterfv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_GetMinmaxParameterfv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetMinmaxParameteriv)(GLenum, GLenum, GLint *);
#define CALL_GetMinmaxParameteriv(disp, parameters) (* GET_GetMinmaxParameteriv(disp)) parameters
#define GET_GetMinmaxParameteriv(disp) ((_glptr_GetMinmaxParameteriv)(GET_by_offset((disp), _gloffset_GetMinmaxParameteriv)))
#define SET_GetMinmaxParameteriv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetMinmaxParameteriv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Histogram)(GLenum, GLsizei, GLenum, GLboolean);
#define CALL_Histogram(disp, parameters) (* GET_Histogram(disp)) parameters
#define GET_Histogram(disp) ((_glptr_Histogram)(GET_by_offset((disp), _gloffset_Histogram)))
#define SET_Histogram(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLsizei, GLenum, GLboolean) = func; \
   SET_by_offset(disp, _gloffset_Histogram, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Minmax)(GLenum, GLenum, GLboolean);
#define CALL_Minmax(disp, parameters) (* GET_Minmax(disp)) parameters
#define GET_Minmax(disp) ((_glptr_Minmax)(GET_by_offset((disp), _gloffset_Minmax)))
#define SET_Minmax(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLboolean) = func; \
   SET_by_offset(disp, _gloffset_Minmax, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ResetHistogram)(GLenum);
#define CALL_ResetHistogram(disp, parameters) (* GET_ResetHistogram(disp)) parameters
#define GET_ResetHistogram(disp) ((_glptr_ResetHistogram)(GET_by_offset((disp), _gloffset_ResetHistogram)))
#define SET_ResetHistogram(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum) = func; \
   SET_by_offset(disp, _gloffset_ResetHistogram, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ResetMinmax)(GLenum);
#define CALL_ResetMinmax(disp, parameters) (* GET_ResetMinmax(disp)) parameters
#define GET_ResetMinmax(disp) ((_glptr_ResetMinmax)(GET_by_offset((disp), _gloffset_ResetMinmax)))
#define SET_ResetMinmax(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum) = func; \
   SET_by_offset(disp, _gloffset_ResetMinmax, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexImage3D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum, const GLvoid *);
#define CALL_TexImage3D(disp, parameters) (* GET_TexImage3D(disp)) parameters
#define GET_TexImage3D(disp) ((_glptr_TexImage3D)(GET_by_offset((disp), _gloffset_TexImage3D)))
#define SET_TexImage3D(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_TexImage3D, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexSubImage3D)(GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLenum, const GLvoid *);
#define CALL_TexSubImage3D(disp, parameters) (* GET_TexSubImage3D(disp)) parameters
#define GET_TexSubImage3D(disp) ((_glptr_TexSubImage3D)(GET_by_offset((disp), _gloffset_TexSubImage3D)))
#define SET_TexSubImage3D(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLenum, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_TexSubImage3D, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CopyTexSubImage3D)(GLenum, GLint, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei);
#define CALL_CopyTexSubImage3D(disp, parameters) (* GET_CopyTexSubImage3D(disp)) parameters
#define GET_CopyTexSubImage3D(disp) ((_glptr_CopyTexSubImage3D)(GET_by_offset((disp), _gloffset_CopyTexSubImage3D)))
#define SET_CopyTexSubImage3D(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLint, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_CopyTexSubImage3D, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ActiveTexture)(GLenum);
#define CALL_ActiveTexture(disp, parameters) (* GET_ActiveTexture(disp)) parameters
#define GET_ActiveTexture(disp) ((_glptr_ActiveTexture)(GET_by_offset((disp), _gloffset_ActiveTexture)))
#define SET_ActiveTexture(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum) = func; \
   SET_by_offset(disp, _gloffset_ActiveTexture, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ClientActiveTexture)(GLenum);
#define CALL_ClientActiveTexture(disp, parameters) (* GET_ClientActiveTexture(disp)) parameters
#define GET_ClientActiveTexture(disp) ((_glptr_ClientActiveTexture)(GET_by_offset((disp), _gloffset_ClientActiveTexture)))
#define SET_ClientActiveTexture(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum) = func; \
   SET_by_offset(disp, _gloffset_ClientActiveTexture, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexCoord1d)(GLenum, GLdouble);
#define CALL_MultiTexCoord1d(disp, parameters) (* GET_MultiTexCoord1d(disp)) parameters
#define GET_MultiTexCoord1d(disp) ((_glptr_MultiTexCoord1d)(GET_by_offset((disp), _gloffset_MultiTexCoord1d)))
#define SET_MultiTexCoord1d(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLdouble) = func; \
   SET_by_offset(disp, _gloffset_MultiTexCoord1d, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexCoord1dv)(GLenum, const GLdouble *);
#define CALL_MultiTexCoord1dv(disp, parameters) (* GET_MultiTexCoord1dv(disp)) parameters
#define GET_MultiTexCoord1dv(disp) ((_glptr_MultiTexCoord1dv)(GET_by_offset((disp), _gloffset_MultiTexCoord1dv)))
#define SET_MultiTexCoord1dv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_MultiTexCoord1dv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexCoord1fARB)(GLenum, GLfloat);
#define CALL_MultiTexCoord1fARB(disp, parameters) (* GET_MultiTexCoord1fARB(disp)) parameters
#define GET_MultiTexCoord1fARB(disp) ((_glptr_MultiTexCoord1fARB)(GET_by_offset((disp), _gloffset_MultiTexCoord1fARB)))
#define SET_MultiTexCoord1fARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_MultiTexCoord1fARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexCoord1fvARB)(GLenum, const GLfloat *);
#define CALL_MultiTexCoord1fvARB(disp, parameters) (* GET_MultiTexCoord1fvARB(disp)) parameters
#define GET_MultiTexCoord1fvARB(disp) ((_glptr_MultiTexCoord1fvARB)(GET_by_offset((disp), _gloffset_MultiTexCoord1fvARB)))
#define SET_MultiTexCoord1fvARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_MultiTexCoord1fvARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexCoord1i)(GLenum, GLint);
#define CALL_MultiTexCoord1i(disp, parameters) (* GET_MultiTexCoord1i(disp)) parameters
#define GET_MultiTexCoord1i(disp) ((_glptr_MultiTexCoord1i)(GET_by_offset((disp), _gloffset_MultiTexCoord1i)))
#define SET_MultiTexCoord1i(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLint) = func; \
   SET_by_offset(disp, _gloffset_MultiTexCoord1i, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexCoord1iv)(GLenum, const GLint *);
#define CALL_MultiTexCoord1iv(disp, parameters) (* GET_MultiTexCoord1iv(disp)) parameters
#define GET_MultiTexCoord1iv(disp) ((_glptr_MultiTexCoord1iv)(GET_by_offset((disp), _gloffset_MultiTexCoord1iv)))
#define SET_MultiTexCoord1iv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, const GLint *) = func; \
   SET_by_offset(disp, _gloffset_MultiTexCoord1iv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexCoord1s)(GLenum, GLshort);
#define CALL_MultiTexCoord1s(disp, parameters) (* GET_MultiTexCoord1s(disp)) parameters
#define GET_MultiTexCoord1s(disp) ((_glptr_MultiTexCoord1s)(GET_by_offset((disp), _gloffset_MultiTexCoord1s)))
#define SET_MultiTexCoord1s(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLshort) = func; \
   SET_by_offset(disp, _gloffset_MultiTexCoord1s, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexCoord1sv)(GLenum, const GLshort *);
#define CALL_MultiTexCoord1sv(disp, parameters) (* GET_MultiTexCoord1sv(disp)) parameters
#define GET_MultiTexCoord1sv(disp) ((_glptr_MultiTexCoord1sv)(GET_by_offset((disp), _gloffset_MultiTexCoord1sv)))
#define SET_MultiTexCoord1sv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, const GLshort *) = func; \
   SET_by_offset(disp, _gloffset_MultiTexCoord1sv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexCoord2d)(GLenum, GLdouble, GLdouble);
#define CALL_MultiTexCoord2d(disp, parameters) (* GET_MultiTexCoord2d(disp)) parameters
#define GET_MultiTexCoord2d(disp) ((_glptr_MultiTexCoord2d)(GET_by_offset((disp), _gloffset_MultiTexCoord2d)))
#define SET_MultiTexCoord2d(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLdouble, GLdouble) = func; \
   SET_by_offset(disp, _gloffset_MultiTexCoord2d, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexCoord2dv)(GLenum, const GLdouble *);
#define CALL_MultiTexCoord2dv(disp, parameters) (* GET_MultiTexCoord2dv(disp)) parameters
#define GET_MultiTexCoord2dv(disp) ((_glptr_MultiTexCoord2dv)(GET_by_offset((disp), _gloffset_MultiTexCoord2dv)))
#define SET_MultiTexCoord2dv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_MultiTexCoord2dv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexCoord2fARB)(GLenum, GLfloat, GLfloat);
#define CALL_MultiTexCoord2fARB(disp, parameters) (* GET_MultiTexCoord2fARB(disp)) parameters
#define GET_MultiTexCoord2fARB(disp) ((_glptr_MultiTexCoord2fARB)(GET_by_offset((disp), _gloffset_MultiTexCoord2fARB)))
#define SET_MultiTexCoord2fARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLfloat, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_MultiTexCoord2fARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexCoord2fvARB)(GLenum, const GLfloat *);
#define CALL_MultiTexCoord2fvARB(disp, parameters) (* GET_MultiTexCoord2fvARB(disp)) parameters
#define GET_MultiTexCoord2fvARB(disp) ((_glptr_MultiTexCoord2fvARB)(GET_by_offset((disp), _gloffset_MultiTexCoord2fvARB)))
#define SET_MultiTexCoord2fvARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_MultiTexCoord2fvARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexCoord2i)(GLenum, GLint, GLint);
#define CALL_MultiTexCoord2i(disp, parameters) (* GET_MultiTexCoord2i(disp)) parameters
#define GET_MultiTexCoord2i(disp) ((_glptr_MultiTexCoord2i)(GET_by_offset((disp), _gloffset_MultiTexCoord2i)))
#define SET_MultiTexCoord2i(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLint, GLint) = func; \
   SET_by_offset(disp, _gloffset_MultiTexCoord2i, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexCoord2iv)(GLenum, const GLint *);
#define CALL_MultiTexCoord2iv(disp, parameters) (* GET_MultiTexCoord2iv(disp)) parameters
#define GET_MultiTexCoord2iv(disp) ((_glptr_MultiTexCoord2iv)(GET_by_offset((disp), _gloffset_MultiTexCoord2iv)))
#define SET_MultiTexCoord2iv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, const GLint *) = func; \
   SET_by_offset(disp, _gloffset_MultiTexCoord2iv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexCoord2s)(GLenum, GLshort, GLshort);
#define CALL_MultiTexCoord2s(disp, parameters) (* GET_MultiTexCoord2s(disp)) parameters
#define GET_MultiTexCoord2s(disp) ((_glptr_MultiTexCoord2s)(GET_by_offset((disp), _gloffset_MultiTexCoord2s)))
#define SET_MultiTexCoord2s(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLshort, GLshort) = func; \
   SET_by_offset(disp, _gloffset_MultiTexCoord2s, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexCoord2sv)(GLenum, const GLshort *);
#define CALL_MultiTexCoord2sv(disp, parameters) (* GET_MultiTexCoord2sv(disp)) parameters
#define GET_MultiTexCoord2sv(disp) ((_glptr_MultiTexCoord2sv)(GET_by_offset((disp), _gloffset_MultiTexCoord2sv)))
#define SET_MultiTexCoord2sv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, const GLshort *) = func; \
   SET_by_offset(disp, _gloffset_MultiTexCoord2sv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexCoord3d)(GLenum, GLdouble, GLdouble, GLdouble);
#define CALL_MultiTexCoord3d(disp, parameters) (* GET_MultiTexCoord3d(disp)) parameters
#define GET_MultiTexCoord3d(disp) ((_glptr_MultiTexCoord3d)(GET_by_offset((disp), _gloffset_MultiTexCoord3d)))
#define SET_MultiTexCoord3d(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLdouble, GLdouble, GLdouble) = func; \
   SET_by_offset(disp, _gloffset_MultiTexCoord3d, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexCoord3dv)(GLenum, const GLdouble *);
#define CALL_MultiTexCoord3dv(disp, parameters) (* GET_MultiTexCoord3dv(disp)) parameters
#define GET_MultiTexCoord3dv(disp) ((_glptr_MultiTexCoord3dv)(GET_by_offset((disp), _gloffset_MultiTexCoord3dv)))
#define SET_MultiTexCoord3dv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_MultiTexCoord3dv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexCoord3fARB)(GLenum, GLfloat, GLfloat, GLfloat);
#define CALL_MultiTexCoord3fARB(disp, parameters) (* GET_MultiTexCoord3fARB(disp)) parameters
#define GET_MultiTexCoord3fARB(disp) ((_glptr_MultiTexCoord3fARB)(GET_by_offset((disp), _gloffset_MultiTexCoord3fARB)))
#define SET_MultiTexCoord3fARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLfloat, GLfloat, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_MultiTexCoord3fARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexCoord3fvARB)(GLenum, const GLfloat *);
#define CALL_MultiTexCoord3fvARB(disp, parameters) (* GET_MultiTexCoord3fvARB(disp)) parameters
#define GET_MultiTexCoord3fvARB(disp) ((_glptr_MultiTexCoord3fvARB)(GET_by_offset((disp), _gloffset_MultiTexCoord3fvARB)))
#define SET_MultiTexCoord3fvARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_MultiTexCoord3fvARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexCoord3i)(GLenum, GLint, GLint, GLint);
#define CALL_MultiTexCoord3i(disp, parameters) (* GET_MultiTexCoord3i(disp)) parameters
#define GET_MultiTexCoord3i(disp) ((_glptr_MultiTexCoord3i)(GET_by_offset((disp), _gloffset_MultiTexCoord3i)))
#define SET_MultiTexCoord3i(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLint, GLint, GLint) = func; \
   SET_by_offset(disp, _gloffset_MultiTexCoord3i, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexCoord3iv)(GLenum, const GLint *);
#define CALL_MultiTexCoord3iv(disp, parameters) (* GET_MultiTexCoord3iv(disp)) parameters
#define GET_MultiTexCoord3iv(disp) ((_glptr_MultiTexCoord3iv)(GET_by_offset((disp), _gloffset_MultiTexCoord3iv)))
#define SET_MultiTexCoord3iv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, const GLint *) = func; \
   SET_by_offset(disp, _gloffset_MultiTexCoord3iv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexCoord3s)(GLenum, GLshort, GLshort, GLshort);
#define CALL_MultiTexCoord3s(disp, parameters) (* GET_MultiTexCoord3s(disp)) parameters
#define GET_MultiTexCoord3s(disp) ((_glptr_MultiTexCoord3s)(GET_by_offset((disp), _gloffset_MultiTexCoord3s)))
#define SET_MultiTexCoord3s(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLshort, GLshort, GLshort) = func; \
   SET_by_offset(disp, _gloffset_MultiTexCoord3s, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexCoord3sv)(GLenum, const GLshort *);
#define CALL_MultiTexCoord3sv(disp, parameters) (* GET_MultiTexCoord3sv(disp)) parameters
#define GET_MultiTexCoord3sv(disp) ((_glptr_MultiTexCoord3sv)(GET_by_offset((disp), _gloffset_MultiTexCoord3sv)))
#define SET_MultiTexCoord3sv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, const GLshort *) = func; \
   SET_by_offset(disp, _gloffset_MultiTexCoord3sv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexCoord4d)(GLenum, GLdouble, GLdouble, GLdouble, GLdouble);
#define CALL_MultiTexCoord4d(disp, parameters) (* GET_MultiTexCoord4d(disp)) parameters
#define GET_MultiTexCoord4d(disp) ((_glptr_MultiTexCoord4d)(GET_by_offset((disp), _gloffset_MultiTexCoord4d)))
#define SET_MultiTexCoord4d(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLdouble, GLdouble, GLdouble, GLdouble) = func; \
   SET_by_offset(disp, _gloffset_MultiTexCoord4d, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexCoord4dv)(GLenum, const GLdouble *);
#define CALL_MultiTexCoord4dv(disp, parameters) (* GET_MultiTexCoord4dv(disp)) parameters
#define GET_MultiTexCoord4dv(disp) ((_glptr_MultiTexCoord4dv)(GET_by_offset((disp), _gloffset_MultiTexCoord4dv)))
#define SET_MultiTexCoord4dv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_MultiTexCoord4dv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexCoord4fARB)(GLenum, GLfloat, GLfloat, GLfloat, GLfloat);
#define CALL_MultiTexCoord4fARB(disp, parameters) (* GET_MultiTexCoord4fARB(disp)) parameters
#define GET_MultiTexCoord4fARB(disp) ((_glptr_MultiTexCoord4fARB)(GET_by_offset((disp), _gloffset_MultiTexCoord4fARB)))
#define SET_MultiTexCoord4fARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLfloat, GLfloat, GLfloat, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_MultiTexCoord4fARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexCoord4fvARB)(GLenum, const GLfloat *);
#define CALL_MultiTexCoord4fvARB(disp, parameters) (* GET_MultiTexCoord4fvARB(disp)) parameters
#define GET_MultiTexCoord4fvARB(disp) ((_glptr_MultiTexCoord4fvARB)(GET_by_offset((disp), _gloffset_MultiTexCoord4fvARB)))
#define SET_MultiTexCoord4fvARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_MultiTexCoord4fvARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexCoord4i)(GLenum, GLint, GLint, GLint, GLint);
#define CALL_MultiTexCoord4i(disp, parameters) (* GET_MultiTexCoord4i(disp)) parameters
#define GET_MultiTexCoord4i(disp) ((_glptr_MultiTexCoord4i)(GET_by_offset((disp), _gloffset_MultiTexCoord4i)))
#define SET_MultiTexCoord4i(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLint, GLint, GLint, GLint) = func; \
   SET_by_offset(disp, _gloffset_MultiTexCoord4i, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexCoord4iv)(GLenum, const GLint *);
#define CALL_MultiTexCoord4iv(disp, parameters) (* GET_MultiTexCoord4iv(disp)) parameters
#define GET_MultiTexCoord4iv(disp) ((_glptr_MultiTexCoord4iv)(GET_by_offset((disp), _gloffset_MultiTexCoord4iv)))
#define SET_MultiTexCoord4iv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, const GLint *) = func; \
   SET_by_offset(disp, _gloffset_MultiTexCoord4iv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexCoord4s)(GLenum, GLshort, GLshort, GLshort, GLshort);
#define CALL_MultiTexCoord4s(disp, parameters) (* GET_MultiTexCoord4s(disp)) parameters
#define GET_MultiTexCoord4s(disp) ((_glptr_MultiTexCoord4s)(GET_by_offset((disp), _gloffset_MultiTexCoord4s)))
#define SET_MultiTexCoord4s(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLshort, GLshort, GLshort, GLshort) = func; \
   SET_by_offset(disp, _gloffset_MultiTexCoord4s, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexCoord4sv)(GLenum, const GLshort *);
#define CALL_MultiTexCoord4sv(disp, parameters) (* GET_MultiTexCoord4sv(disp)) parameters
#define GET_MultiTexCoord4sv(disp) ((_glptr_MultiTexCoord4sv)(GET_by_offset((disp), _gloffset_MultiTexCoord4sv)))
#define SET_MultiTexCoord4sv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, const GLshort *) = func; \
   SET_by_offset(disp, _gloffset_MultiTexCoord4sv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CompressedTexImage1D)(GLenum, GLint, GLenum, GLsizei, GLint, GLsizei, const GLvoid *);
#define CALL_CompressedTexImage1D(disp, parameters) (* GET_CompressedTexImage1D(disp)) parameters
#define GET_CompressedTexImage1D(disp) ((_glptr_CompressedTexImage1D)(GET_by_offset((disp), _gloffset_CompressedTexImage1D)))
#define SET_CompressedTexImage1D(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLint, GLenum, GLsizei, GLint, GLsizei, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_CompressedTexImage1D, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CompressedTexImage2D)(GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const GLvoid *);
#define CALL_CompressedTexImage2D(disp, parameters) (* GET_CompressedTexImage2D(disp)) parameters
#define GET_CompressedTexImage2D(disp) ((_glptr_CompressedTexImage2D)(GET_by_offset((disp), _gloffset_CompressedTexImage2D)))
#define SET_CompressedTexImage2D(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_CompressedTexImage2D, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CompressedTexImage3D)(GLenum, GLint, GLenum, GLsizei, GLsizei, GLsizei, GLint, GLsizei, const GLvoid *);
#define CALL_CompressedTexImage3D(disp, parameters) (* GET_CompressedTexImage3D(disp)) parameters
#define GET_CompressedTexImage3D(disp) ((_glptr_CompressedTexImage3D)(GET_by_offset((disp), _gloffset_CompressedTexImage3D)))
#define SET_CompressedTexImage3D(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLint, GLenum, GLsizei, GLsizei, GLsizei, GLint, GLsizei, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_CompressedTexImage3D, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CompressedTexSubImage1D)(GLenum, GLint, GLint, GLsizei, GLenum, GLsizei, const GLvoid *);
#define CALL_CompressedTexSubImage1D(disp, parameters) (* GET_CompressedTexSubImage1D(disp)) parameters
#define GET_CompressedTexSubImage1D(disp) ((_glptr_CompressedTexSubImage1D)(GET_by_offset((disp), _gloffset_CompressedTexSubImage1D)))
#define SET_CompressedTexSubImage1D(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLint, GLint, GLsizei, GLenum, GLsizei, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_CompressedTexSubImage1D, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CompressedTexSubImage2D)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLsizei, const GLvoid *);
#define CALL_CompressedTexSubImage2D(disp, parameters) (* GET_CompressedTexSubImage2D(disp)) parameters
#define GET_CompressedTexSubImage2D(disp) ((_glptr_CompressedTexSubImage2D)(GET_by_offset((disp), _gloffset_CompressedTexSubImage2D)))
#define SET_CompressedTexSubImage2D(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLsizei, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_CompressedTexSubImage2D, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CompressedTexSubImage3D)(GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLsizei, const GLvoid *);
#define CALL_CompressedTexSubImage3D(disp, parameters) (* GET_CompressedTexSubImage3D(disp)) parameters
#define GET_CompressedTexSubImage3D(disp) ((_glptr_CompressedTexSubImage3D)(GET_by_offset((disp), _gloffset_CompressedTexSubImage3D)))
#define SET_CompressedTexSubImage3D(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLsizei, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_CompressedTexSubImage3D, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetCompressedTexImage)(GLenum, GLint, GLvoid *);
#define CALL_GetCompressedTexImage(disp, parameters) (* GET_GetCompressedTexImage(disp)) parameters
#define GET_GetCompressedTexImage(disp) ((_glptr_GetCompressedTexImage)(GET_by_offset((disp), _gloffset_GetCompressedTexImage)))
#define SET_GetCompressedTexImage(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLint, GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_GetCompressedTexImage, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_LoadTransposeMatrixd)(const GLdouble *);
#define CALL_LoadTransposeMatrixd(disp, parameters) (* GET_LoadTransposeMatrixd(disp)) parameters
#define GET_LoadTransposeMatrixd(disp) ((_glptr_LoadTransposeMatrixd)(GET_by_offset((disp), _gloffset_LoadTransposeMatrixd)))
#define SET_LoadTransposeMatrixd(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_LoadTransposeMatrixd, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_LoadTransposeMatrixf)(const GLfloat *);
#define CALL_LoadTransposeMatrixf(disp, parameters) (* GET_LoadTransposeMatrixf(disp)) parameters
#define GET_LoadTransposeMatrixf(disp) ((_glptr_LoadTransposeMatrixf)(GET_by_offset((disp), _gloffset_LoadTransposeMatrixf)))
#define SET_LoadTransposeMatrixf(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_LoadTransposeMatrixf, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultTransposeMatrixd)(const GLdouble *);
#define CALL_MultTransposeMatrixd(disp, parameters) (* GET_MultTransposeMatrixd(disp)) parameters
#define GET_MultTransposeMatrixd(disp) ((_glptr_MultTransposeMatrixd)(GET_by_offset((disp), _gloffset_MultTransposeMatrixd)))
#define SET_MultTransposeMatrixd(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_MultTransposeMatrixd, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultTransposeMatrixf)(const GLfloat *);
#define CALL_MultTransposeMatrixf(disp, parameters) (* GET_MultTransposeMatrixf(disp)) parameters
#define GET_MultTransposeMatrixf(disp) ((_glptr_MultTransposeMatrixf)(GET_by_offset((disp), _gloffset_MultTransposeMatrixf)))
#define SET_MultTransposeMatrixf(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_MultTransposeMatrixf, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_SampleCoverage)(GLclampf, GLboolean);
#define CALL_SampleCoverage(disp, parameters) (* GET_SampleCoverage(disp)) parameters
#define GET_SampleCoverage(disp) ((_glptr_SampleCoverage)(GET_by_offset((disp), _gloffset_SampleCoverage)))
#define SET_SampleCoverage(disp, func) do { \
   void (GLAPIENTRYP fn)(GLclampf, GLboolean) = func; \
   SET_by_offset(disp, _gloffset_SampleCoverage, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_BlendFuncSeparate)(GLenum, GLenum, GLenum, GLenum);
#define CALL_BlendFuncSeparate(disp, parameters) (* GET_BlendFuncSeparate(disp)) parameters
#define GET_BlendFuncSeparate(disp) ((_glptr_BlendFuncSeparate)(GET_by_offset((disp), _gloffset_BlendFuncSeparate)))
#define SET_BlendFuncSeparate(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLenum, GLenum) = func; \
   SET_by_offset(disp, _gloffset_BlendFuncSeparate, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_FogCoordPointer)(GLenum, GLsizei, const GLvoid *);
#define CALL_FogCoordPointer(disp, parameters) (* GET_FogCoordPointer(disp)) parameters
#define GET_FogCoordPointer(disp) ((_glptr_FogCoordPointer)(GET_by_offset((disp), _gloffset_FogCoordPointer)))
#define SET_FogCoordPointer(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLsizei, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_FogCoordPointer, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_FogCoordd)(GLdouble);
#define CALL_FogCoordd(disp, parameters) (* GET_FogCoordd(disp)) parameters
#define GET_FogCoordd(disp) ((_glptr_FogCoordd)(GET_by_offset((disp), _gloffset_FogCoordd)))
#define SET_FogCoordd(disp, func) do { \
   void (GLAPIENTRYP fn)(GLdouble) = func; \
   SET_by_offset(disp, _gloffset_FogCoordd, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_FogCoorddv)(const GLdouble *);
#define CALL_FogCoorddv(disp, parameters) (* GET_FogCoorddv(disp)) parameters
#define GET_FogCoorddv(disp) ((_glptr_FogCoorddv)(GET_by_offset((disp), _gloffset_FogCoorddv)))
#define SET_FogCoorddv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_FogCoorddv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiDrawArrays)(GLenum, const GLint *, const GLsizei *, GLsizei);
#define CALL_MultiDrawArrays(disp, parameters) (* GET_MultiDrawArrays(disp)) parameters
#define GET_MultiDrawArrays(disp) ((_glptr_MultiDrawArrays)(GET_by_offset((disp), _gloffset_MultiDrawArrays)))
#define SET_MultiDrawArrays(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, const GLint *, const GLsizei *, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_MultiDrawArrays, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_PointParameterf)(GLenum, GLfloat);
#define CALL_PointParameterf(disp, parameters) (* GET_PointParameterf(disp)) parameters
#define GET_PointParameterf(disp) ((_glptr_PointParameterf)(GET_by_offset((disp), _gloffset_PointParameterf)))
#define SET_PointParameterf(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_PointParameterf, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_PointParameterfv)(GLenum, const GLfloat *);
#define CALL_PointParameterfv(disp, parameters) (* GET_PointParameterfv(disp)) parameters
#define GET_PointParameterfv(disp) ((_glptr_PointParameterfv)(GET_by_offset((disp), _gloffset_PointParameterfv)))
#define SET_PointParameterfv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_PointParameterfv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_PointParameteri)(GLenum, GLint);
#define CALL_PointParameteri(disp, parameters) (* GET_PointParameteri(disp)) parameters
#define GET_PointParameteri(disp) ((_glptr_PointParameteri)(GET_by_offset((disp), _gloffset_PointParameteri)))
#define SET_PointParameteri(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLint) = func; \
   SET_by_offset(disp, _gloffset_PointParameteri, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_PointParameteriv)(GLenum, const GLint *);
#define CALL_PointParameteriv(disp, parameters) (* GET_PointParameteriv(disp)) parameters
#define GET_PointParameteriv(disp) ((_glptr_PointParameteriv)(GET_by_offset((disp), _gloffset_PointParameteriv)))
#define SET_PointParameteriv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, const GLint *) = func; \
   SET_by_offset(disp, _gloffset_PointParameteriv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_SecondaryColor3b)(GLbyte, GLbyte, GLbyte);
#define CALL_SecondaryColor3b(disp, parameters) (* GET_SecondaryColor3b(disp)) parameters
#define GET_SecondaryColor3b(disp) ((_glptr_SecondaryColor3b)(GET_by_offset((disp), _gloffset_SecondaryColor3b)))
#define SET_SecondaryColor3b(disp, func) do { \
   void (GLAPIENTRYP fn)(GLbyte, GLbyte, GLbyte) = func; \
   SET_by_offset(disp, _gloffset_SecondaryColor3b, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_SecondaryColor3bv)(const GLbyte *);
#define CALL_SecondaryColor3bv(disp, parameters) (* GET_SecondaryColor3bv(disp)) parameters
#define GET_SecondaryColor3bv(disp) ((_glptr_SecondaryColor3bv)(GET_by_offset((disp), _gloffset_SecondaryColor3bv)))
#define SET_SecondaryColor3bv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLbyte *) = func; \
   SET_by_offset(disp, _gloffset_SecondaryColor3bv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_SecondaryColor3d)(GLdouble, GLdouble, GLdouble);
#define CALL_SecondaryColor3d(disp, parameters) (* GET_SecondaryColor3d(disp)) parameters
#define GET_SecondaryColor3d(disp) ((_glptr_SecondaryColor3d)(GET_by_offset((disp), _gloffset_SecondaryColor3d)))
#define SET_SecondaryColor3d(disp, func) do { \
   void (GLAPIENTRYP fn)(GLdouble, GLdouble, GLdouble) = func; \
   SET_by_offset(disp, _gloffset_SecondaryColor3d, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_SecondaryColor3dv)(const GLdouble *);
#define CALL_SecondaryColor3dv(disp, parameters) (* GET_SecondaryColor3dv(disp)) parameters
#define GET_SecondaryColor3dv(disp) ((_glptr_SecondaryColor3dv)(GET_by_offset((disp), _gloffset_SecondaryColor3dv)))
#define SET_SecondaryColor3dv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_SecondaryColor3dv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_SecondaryColor3i)(GLint, GLint, GLint);
#define CALL_SecondaryColor3i(disp, parameters) (* GET_SecondaryColor3i(disp)) parameters
#define GET_SecondaryColor3i(disp) ((_glptr_SecondaryColor3i)(GET_by_offset((disp), _gloffset_SecondaryColor3i)))
#define SET_SecondaryColor3i(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLint, GLint) = func; \
   SET_by_offset(disp, _gloffset_SecondaryColor3i, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_SecondaryColor3iv)(const GLint *);
#define CALL_SecondaryColor3iv(disp, parameters) (* GET_SecondaryColor3iv(disp)) parameters
#define GET_SecondaryColor3iv(disp) ((_glptr_SecondaryColor3iv)(GET_by_offset((disp), _gloffset_SecondaryColor3iv)))
#define SET_SecondaryColor3iv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLint *) = func; \
   SET_by_offset(disp, _gloffset_SecondaryColor3iv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_SecondaryColor3s)(GLshort, GLshort, GLshort);
#define CALL_SecondaryColor3s(disp, parameters) (* GET_SecondaryColor3s(disp)) parameters
#define GET_SecondaryColor3s(disp) ((_glptr_SecondaryColor3s)(GET_by_offset((disp), _gloffset_SecondaryColor3s)))
#define SET_SecondaryColor3s(disp, func) do { \
   void (GLAPIENTRYP fn)(GLshort, GLshort, GLshort) = func; \
   SET_by_offset(disp, _gloffset_SecondaryColor3s, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_SecondaryColor3sv)(const GLshort *);
#define CALL_SecondaryColor3sv(disp, parameters) (* GET_SecondaryColor3sv(disp)) parameters
#define GET_SecondaryColor3sv(disp) ((_glptr_SecondaryColor3sv)(GET_by_offset((disp), _gloffset_SecondaryColor3sv)))
#define SET_SecondaryColor3sv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLshort *) = func; \
   SET_by_offset(disp, _gloffset_SecondaryColor3sv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_SecondaryColor3ub)(GLubyte, GLubyte, GLubyte);
#define CALL_SecondaryColor3ub(disp, parameters) (* GET_SecondaryColor3ub(disp)) parameters
#define GET_SecondaryColor3ub(disp) ((_glptr_SecondaryColor3ub)(GET_by_offset((disp), _gloffset_SecondaryColor3ub)))
#define SET_SecondaryColor3ub(disp, func) do { \
   void (GLAPIENTRYP fn)(GLubyte, GLubyte, GLubyte) = func; \
   SET_by_offset(disp, _gloffset_SecondaryColor3ub, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_SecondaryColor3ubv)(const GLubyte *);
#define CALL_SecondaryColor3ubv(disp, parameters) (* GET_SecondaryColor3ubv(disp)) parameters
#define GET_SecondaryColor3ubv(disp) ((_glptr_SecondaryColor3ubv)(GET_by_offset((disp), _gloffset_SecondaryColor3ubv)))
#define SET_SecondaryColor3ubv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLubyte *) = func; \
   SET_by_offset(disp, _gloffset_SecondaryColor3ubv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_SecondaryColor3ui)(GLuint, GLuint, GLuint);
#define CALL_SecondaryColor3ui(disp, parameters) (* GET_SecondaryColor3ui(disp)) parameters
#define GET_SecondaryColor3ui(disp) ((_glptr_SecondaryColor3ui)(GET_by_offset((disp), _gloffset_SecondaryColor3ui)))
#define SET_SecondaryColor3ui(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLuint) = func; \
   SET_by_offset(disp, _gloffset_SecondaryColor3ui, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_SecondaryColor3uiv)(const GLuint *);
#define CALL_SecondaryColor3uiv(disp, parameters) (* GET_SecondaryColor3uiv(disp)) parameters
#define GET_SecondaryColor3uiv(disp) ((_glptr_SecondaryColor3uiv)(GET_by_offset((disp), _gloffset_SecondaryColor3uiv)))
#define SET_SecondaryColor3uiv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_SecondaryColor3uiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_SecondaryColor3us)(GLushort, GLushort, GLushort);
#define CALL_SecondaryColor3us(disp, parameters) (* GET_SecondaryColor3us(disp)) parameters
#define GET_SecondaryColor3us(disp) ((_glptr_SecondaryColor3us)(GET_by_offset((disp), _gloffset_SecondaryColor3us)))
#define SET_SecondaryColor3us(disp, func) do { \
   void (GLAPIENTRYP fn)(GLushort, GLushort, GLushort) = func; \
   SET_by_offset(disp, _gloffset_SecondaryColor3us, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_SecondaryColor3usv)(const GLushort *);
#define CALL_SecondaryColor3usv(disp, parameters) (* GET_SecondaryColor3usv(disp)) parameters
#define GET_SecondaryColor3usv(disp) ((_glptr_SecondaryColor3usv)(GET_by_offset((disp), _gloffset_SecondaryColor3usv)))
#define SET_SecondaryColor3usv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLushort *) = func; \
   SET_by_offset(disp, _gloffset_SecondaryColor3usv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_SecondaryColorPointer)(GLint, GLenum, GLsizei, const GLvoid *);
#define CALL_SecondaryColorPointer(disp, parameters) (* GET_SecondaryColorPointer(disp)) parameters
#define GET_SecondaryColorPointer(disp) ((_glptr_SecondaryColorPointer)(GET_by_offset((disp), _gloffset_SecondaryColorPointer)))
#define SET_SecondaryColorPointer(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLenum, GLsizei, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_SecondaryColorPointer, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_WindowPos2d)(GLdouble, GLdouble);
#define CALL_WindowPos2d(disp, parameters) (* GET_WindowPos2d(disp)) parameters
#define GET_WindowPos2d(disp) ((_glptr_WindowPos2d)(GET_by_offset((disp), _gloffset_WindowPos2d)))
#define SET_WindowPos2d(disp, func) do { \
   void (GLAPIENTRYP fn)(GLdouble, GLdouble) = func; \
   SET_by_offset(disp, _gloffset_WindowPos2d, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_WindowPos2dv)(const GLdouble *);
#define CALL_WindowPos2dv(disp, parameters) (* GET_WindowPos2dv(disp)) parameters
#define GET_WindowPos2dv(disp) ((_glptr_WindowPos2dv)(GET_by_offset((disp), _gloffset_WindowPos2dv)))
#define SET_WindowPos2dv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_WindowPos2dv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_WindowPos2f)(GLfloat, GLfloat);
#define CALL_WindowPos2f(disp, parameters) (* GET_WindowPos2f(disp)) parameters
#define GET_WindowPos2f(disp) ((_glptr_WindowPos2f)(GET_by_offset((disp), _gloffset_WindowPos2f)))
#define SET_WindowPos2f(disp, func) do { \
   void (GLAPIENTRYP fn)(GLfloat, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_WindowPos2f, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_WindowPos2fv)(const GLfloat *);
#define CALL_WindowPos2fv(disp, parameters) (* GET_WindowPos2fv(disp)) parameters
#define GET_WindowPos2fv(disp) ((_glptr_WindowPos2fv)(GET_by_offset((disp), _gloffset_WindowPos2fv)))
#define SET_WindowPos2fv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_WindowPos2fv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_WindowPos2i)(GLint, GLint);
#define CALL_WindowPos2i(disp, parameters) (* GET_WindowPos2i(disp)) parameters
#define GET_WindowPos2i(disp) ((_glptr_WindowPos2i)(GET_by_offset((disp), _gloffset_WindowPos2i)))
#define SET_WindowPos2i(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLint) = func; \
   SET_by_offset(disp, _gloffset_WindowPos2i, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_WindowPos2iv)(const GLint *);
#define CALL_WindowPos2iv(disp, parameters) (* GET_WindowPos2iv(disp)) parameters
#define GET_WindowPos2iv(disp) ((_glptr_WindowPos2iv)(GET_by_offset((disp), _gloffset_WindowPos2iv)))
#define SET_WindowPos2iv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLint *) = func; \
   SET_by_offset(disp, _gloffset_WindowPos2iv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_WindowPos2s)(GLshort, GLshort);
#define CALL_WindowPos2s(disp, parameters) (* GET_WindowPos2s(disp)) parameters
#define GET_WindowPos2s(disp) ((_glptr_WindowPos2s)(GET_by_offset((disp), _gloffset_WindowPos2s)))
#define SET_WindowPos2s(disp, func) do { \
   void (GLAPIENTRYP fn)(GLshort, GLshort) = func; \
   SET_by_offset(disp, _gloffset_WindowPos2s, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_WindowPos2sv)(const GLshort *);
#define CALL_WindowPos2sv(disp, parameters) (* GET_WindowPos2sv(disp)) parameters
#define GET_WindowPos2sv(disp) ((_glptr_WindowPos2sv)(GET_by_offset((disp), _gloffset_WindowPos2sv)))
#define SET_WindowPos2sv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLshort *) = func; \
   SET_by_offset(disp, _gloffset_WindowPos2sv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_WindowPos3d)(GLdouble, GLdouble, GLdouble);
#define CALL_WindowPos3d(disp, parameters) (* GET_WindowPos3d(disp)) parameters
#define GET_WindowPos3d(disp) ((_glptr_WindowPos3d)(GET_by_offset((disp), _gloffset_WindowPos3d)))
#define SET_WindowPos3d(disp, func) do { \
   void (GLAPIENTRYP fn)(GLdouble, GLdouble, GLdouble) = func; \
   SET_by_offset(disp, _gloffset_WindowPos3d, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_WindowPos3dv)(const GLdouble *);
#define CALL_WindowPos3dv(disp, parameters) (* GET_WindowPos3dv(disp)) parameters
#define GET_WindowPos3dv(disp) ((_glptr_WindowPos3dv)(GET_by_offset((disp), _gloffset_WindowPos3dv)))
#define SET_WindowPos3dv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_WindowPos3dv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_WindowPos3f)(GLfloat, GLfloat, GLfloat);
#define CALL_WindowPos3f(disp, parameters) (* GET_WindowPos3f(disp)) parameters
#define GET_WindowPos3f(disp) ((_glptr_WindowPos3f)(GET_by_offset((disp), _gloffset_WindowPos3f)))
#define SET_WindowPos3f(disp, func) do { \
   void (GLAPIENTRYP fn)(GLfloat, GLfloat, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_WindowPos3f, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_WindowPos3fv)(const GLfloat *);
#define CALL_WindowPos3fv(disp, parameters) (* GET_WindowPos3fv(disp)) parameters
#define GET_WindowPos3fv(disp) ((_glptr_WindowPos3fv)(GET_by_offset((disp), _gloffset_WindowPos3fv)))
#define SET_WindowPos3fv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_WindowPos3fv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_WindowPos3i)(GLint, GLint, GLint);
#define CALL_WindowPos3i(disp, parameters) (* GET_WindowPos3i(disp)) parameters
#define GET_WindowPos3i(disp) ((_glptr_WindowPos3i)(GET_by_offset((disp), _gloffset_WindowPos3i)))
#define SET_WindowPos3i(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLint, GLint) = func; \
   SET_by_offset(disp, _gloffset_WindowPos3i, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_WindowPos3iv)(const GLint *);
#define CALL_WindowPos3iv(disp, parameters) (* GET_WindowPos3iv(disp)) parameters
#define GET_WindowPos3iv(disp) ((_glptr_WindowPos3iv)(GET_by_offset((disp), _gloffset_WindowPos3iv)))
#define SET_WindowPos3iv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLint *) = func; \
   SET_by_offset(disp, _gloffset_WindowPos3iv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_WindowPos3s)(GLshort, GLshort, GLshort);
#define CALL_WindowPos3s(disp, parameters) (* GET_WindowPos3s(disp)) parameters
#define GET_WindowPos3s(disp) ((_glptr_WindowPos3s)(GET_by_offset((disp), _gloffset_WindowPos3s)))
#define SET_WindowPos3s(disp, func) do { \
   void (GLAPIENTRYP fn)(GLshort, GLshort, GLshort) = func; \
   SET_by_offset(disp, _gloffset_WindowPos3s, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_WindowPos3sv)(const GLshort *);
#define CALL_WindowPos3sv(disp, parameters) (* GET_WindowPos3sv(disp)) parameters
#define GET_WindowPos3sv(disp) ((_glptr_WindowPos3sv)(GET_by_offset((disp), _gloffset_WindowPos3sv)))
#define SET_WindowPos3sv(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLshort *) = func; \
   SET_by_offset(disp, _gloffset_WindowPos3sv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_BeginQuery)(GLenum, GLuint);
#define CALL_BeginQuery(disp, parameters) (* GET_BeginQuery(disp)) parameters
#define GET_BeginQuery(disp) ((_glptr_BeginQuery)(GET_by_offset((disp), _gloffset_BeginQuery)))
#define SET_BeginQuery(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint) = func; \
   SET_by_offset(disp, _gloffset_BeginQuery, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_BindBuffer)(GLenum, GLuint);
#define CALL_BindBuffer(disp, parameters) (* GET_BindBuffer(disp)) parameters
#define GET_BindBuffer(disp) ((_glptr_BindBuffer)(GET_by_offset((disp), _gloffset_BindBuffer)))
#define SET_BindBuffer(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint) = func; \
   SET_by_offset(disp, _gloffset_BindBuffer, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_BufferData)(GLenum, GLsizeiptr, const GLvoid *, GLenum);
#define CALL_BufferData(disp, parameters) (* GET_BufferData(disp)) parameters
#define GET_BufferData(disp) ((_glptr_BufferData)(GET_by_offset((disp), _gloffset_BufferData)))
#define SET_BufferData(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLsizeiptr, const GLvoid *, GLenum) = func; \
   SET_by_offset(disp, _gloffset_BufferData, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_BufferSubData)(GLenum, GLintptr, GLsizeiptr, const GLvoid *);
#define CALL_BufferSubData(disp, parameters) (* GET_BufferSubData(disp)) parameters
#define GET_BufferSubData(disp) ((_glptr_BufferSubData)(GET_by_offset((disp), _gloffset_BufferSubData)))
#define SET_BufferSubData(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLintptr, GLsizeiptr, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_BufferSubData, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DeleteBuffers)(GLsizei, const GLuint *);
#define CALL_DeleteBuffers(disp, parameters) (* GET_DeleteBuffers(disp)) parameters
#define GET_DeleteBuffers(disp) ((_glptr_DeleteBuffers)(GET_by_offset((disp), _gloffset_DeleteBuffers)))
#define SET_DeleteBuffers(disp, func) do { \
   void (GLAPIENTRYP fn)(GLsizei, const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_DeleteBuffers, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DeleteQueries)(GLsizei, const GLuint *);
#define CALL_DeleteQueries(disp, parameters) (* GET_DeleteQueries(disp)) parameters
#define GET_DeleteQueries(disp) ((_glptr_DeleteQueries)(GET_by_offset((disp), _gloffset_DeleteQueries)))
#define SET_DeleteQueries(disp, func) do { \
   void (GLAPIENTRYP fn)(GLsizei, const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_DeleteQueries, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_EndQuery)(GLenum);
#define CALL_EndQuery(disp, parameters) (* GET_EndQuery(disp)) parameters
#define GET_EndQuery(disp) ((_glptr_EndQuery)(GET_by_offset((disp), _gloffset_EndQuery)))
#define SET_EndQuery(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum) = func; \
   SET_by_offset(disp, _gloffset_EndQuery, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GenBuffers)(GLsizei, GLuint *);
#define CALL_GenBuffers(disp, parameters) (* GET_GenBuffers(disp)) parameters
#define GET_GenBuffers(disp) ((_glptr_GenBuffers)(GET_by_offset((disp), _gloffset_GenBuffers)))
#define SET_GenBuffers(disp, func) do { \
   void (GLAPIENTRYP fn)(GLsizei, GLuint *) = func; \
   SET_by_offset(disp, _gloffset_GenBuffers, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GenQueries)(GLsizei, GLuint *);
#define CALL_GenQueries(disp, parameters) (* GET_GenQueries(disp)) parameters
#define GET_GenQueries(disp) ((_glptr_GenQueries)(GET_by_offset((disp), _gloffset_GenQueries)))
#define SET_GenQueries(disp, func) do { \
   void (GLAPIENTRYP fn)(GLsizei, GLuint *) = func; \
   SET_by_offset(disp, _gloffset_GenQueries, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetBufferParameteriv)(GLenum, GLenum, GLint *);
#define CALL_GetBufferParameteriv(disp, parameters) (* GET_GetBufferParameteriv(disp)) parameters
#define GET_GetBufferParameteriv(disp) ((_glptr_GetBufferParameteriv)(GET_by_offset((disp), _gloffset_GetBufferParameteriv)))
#define SET_GetBufferParameteriv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetBufferParameteriv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetBufferPointerv)(GLenum, GLenum, GLvoid **);
#define CALL_GetBufferPointerv(disp, parameters) (* GET_GetBufferPointerv(disp)) parameters
#define GET_GetBufferPointerv(disp) ((_glptr_GetBufferPointerv)(GET_by_offset((disp), _gloffset_GetBufferPointerv)))
#define SET_GetBufferPointerv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLvoid **) = func; \
   SET_by_offset(disp, _gloffset_GetBufferPointerv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetBufferSubData)(GLenum, GLintptr, GLsizeiptr, GLvoid *);
#define CALL_GetBufferSubData(disp, parameters) (* GET_GetBufferSubData(disp)) parameters
#define GET_GetBufferSubData(disp) ((_glptr_GetBufferSubData)(GET_by_offset((disp), _gloffset_GetBufferSubData)))
#define SET_GetBufferSubData(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLintptr, GLsizeiptr, GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_GetBufferSubData, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetQueryObjectiv)(GLuint, GLenum, GLint *);
#define CALL_GetQueryObjectiv(disp, parameters) (* GET_GetQueryObjectiv(disp)) parameters
#define GET_GetQueryObjectiv(disp) ((_glptr_GetQueryObjectiv)(GET_by_offset((disp), _gloffset_GetQueryObjectiv)))
#define SET_GetQueryObjectiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetQueryObjectiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetQueryObjectuiv)(GLuint, GLenum, GLuint *);
#define CALL_GetQueryObjectuiv(disp, parameters) (* GET_GetQueryObjectuiv(disp)) parameters
#define GET_GetQueryObjectuiv(disp) ((_glptr_GetQueryObjectuiv)(GET_by_offset((disp), _gloffset_GetQueryObjectuiv)))
#define SET_GetQueryObjectuiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLuint *) = func; \
   SET_by_offset(disp, _gloffset_GetQueryObjectuiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetQueryiv)(GLenum, GLenum, GLint *);
#define CALL_GetQueryiv(disp, parameters) (* GET_GetQueryiv(disp)) parameters
#define GET_GetQueryiv(disp) ((_glptr_GetQueryiv)(GET_by_offset((disp), _gloffset_GetQueryiv)))
#define SET_GetQueryiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetQueryiv, fn); \
} while (0)

typedef GLboolean (GLAPIENTRYP _glptr_IsBuffer)(GLuint);
#define CALL_IsBuffer(disp, parameters) (* GET_IsBuffer(disp)) parameters
#define GET_IsBuffer(disp) ((_glptr_IsBuffer)(GET_by_offset((disp), _gloffset_IsBuffer)))
#define SET_IsBuffer(disp, func) do { \
   GLboolean (GLAPIENTRYP fn)(GLuint) = func; \
   SET_by_offset(disp, _gloffset_IsBuffer, fn); \
} while (0)

typedef GLboolean (GLAPIENTRYP _glptr_IsQuery)(GLuint);
#define CALL_IsQuery(disp, parameters) (* GET_IsQuery(disp)) parameters
#define GET_IsQuery(disp) ((_glptr_IsQuery)(GET_by_offset((disp), _gloffset_IsQuery)))
#define SET_IsQuery(disp, func) do { \
   GLboolean (GLAPIENTRYP fn)(GLuint) = func; \
   SET_by_offset(disp, _gloffset_IsQuery, fn); \
} while (0)

typedef GLvoid * (GLAPIENTRYP _glptr_MapBuffer)(GLenum, GLenum);
#define CALL_MapBuffer(disp, parameters) (* GET_MapBuffer(disp)) parameters
#define GET_MapBuffer(disp) ((_glptr_MapBuffer)(GET_by_offset((disp), _gloffset_MapBuffer)))
#define SET_MapBuffer(disp, func) do { \
   GLvoid * (GLAPIENTRYP fn)(GLenum, GLenum) = func; \
   SET_by_offset(disp, _gloffset_MapBuffer, fn); \
} while (0)

typedef GLboolean (GLAPIENTRYP _glptr_UnmapBuffer)(GLenum);
#define CALL_UnmapBuffer(disp, parameters) (* GET_UnmapBuffer(disp)) parameters
#define GET_UnmapBuffer(disp) ((_glptr_UnmapBuffer)(GET_by_offset((disp), _gloffset_UnmapBuffer)))
#define SET_UnmapBuffer(disp, func) do { \
   GLboolean (GLAPIENTRYP fn)(GLenum) = func; \
   SET_by_offset(disp, _gloffset_UnmapBuffer, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_AttachShader)(GLuint, GLuint);
#define CALL_AttachShader(disp, parameters) (* GET_AttachShader(disp)) parameters
#define GET_AttachShader(disp) ((_glptr_AttachShader)(GET_by_offset((disp), _gloffset_AttachShader)))
#define SET_AttachShader(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint) = func; \
   SET_by_offset(disp, _gloffset_AttachShader, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_BindAttribLocation)(GLuint, GLuint, const GLchar *);
#define CALL_BindAttribLocation(disp, parameters) (* GET_BindAttribLocation(disp)) parameters
#define GET_BindAttribLocation(disp) ((_glptr_BindAttribLocation)(GET_by_offset((disp), _gloffset_BindAttribLocation)))
#define SET_BindAttribLocation(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, const GLchar *) = func; \
   SET_by_offset(disp, _gloffset_BindAttribLocation, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_BlendEquationSeparate)(GLenum, GLenum);
#define CALL_BlendEquationSeparate(disp, parameters) (* GET_BlendEquationSeparate(disp)) parameters
#define GET_BlendEquationSeparate(disp) ((_glptr_BlendEquationSeparate)(GET_by_offset((disp), _gloffset_BlendEquationSeparate)))
#define SET_BlendEquationSeparate(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum) = func; \
   SET_by_offset(disp, _gloffset_BlendEquationSeparate, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CompileShader)(GLuint);
#define CALL_CompileShader(disp, parameters) (* GET_CompileShader(disp)) parameters
#define GET_CompileShader(disp) ((_glptr_CompileShader)(GET_by_offset((disp), _gloffset_CompileShader)))
#define SET_CompileShader(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint) = func; \
   SET_by_offset(disp, _gloffset_CompileShader, fn); \
} while (0)

typedef GLuint (GLAPIENTRYP _glptr_CreateProgram)(void);
#define CALL_CreateProgram(disp, parameters) (* GET_CreateProgram(disp)) parameters
#define GET_CreateProgram(disp) ((_glptr_CreateProgram)(GET_by_offset((disp), _gloffset_CreateProgram)))
#define SET_CreateProgram(disp, func) do { \
   GLuint (GLAPIENTRYP fn)(void) = func; \
   SET_by_offset(disp, _gloffset_CreateProgram, fn); \
} while (0)

typedef GLuint (GLAPIENTRYP _glptr_CreateShader)(GLenum);
#define CALL_CreateShader(disp, parameters) (* GET_CreateShader(disp)) parameters
#define GET_CreateShader(disp) ((_glptr_CreateShader)(GET_by_offset((disp), _gloffset_CreateShader)))
#define SET_CreateShader(disp, func) do { \
   GLuint (GLAPIENTRYP fn)(GLenum) = func; \
   SET_by_offset(disp, _gloffset_CreateShader, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DeleteProgram)(GLuint);
#define CALL_DeleteProgram(disp, parameters) (* GET_DeleteProgram(disp)) parameters
#define GET_DeleteProgram(disp) ((_glptr_DeleteProgram)(GET_by_offset((disp), _gloffset_DeleteProgram)))
#define SET_DeleteProgram(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint) = func; \
   SET_by_offset(disp, _gloffset_DeleteProgram, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DeleteShader)(GLuint);
#define CALL_DeleteShader(disp, parameters) (* GET_DeleteShader(disp)) parameters
#define GET_DeleteShader(disp) ((_glptr_DeleteShader)(GET_by_offset((disp), _gloffset_DeleteShader)))
#define SET_DeleteShader(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint) = func; \
   SET_by_offset(disp, _gloffset_DeleteShader, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DetachShader)(GLuint, GLuint);
#define CALL_DetachShader(disp, parameters) (* GET_DetachShader(disp)) parameters
#define GET_DetachShader(disp) ((_glptr_DetachShader)(GET_by_offset((disp), _gloffset_DetachShader)))
#define SET_DetachShader(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint) = func; \
   SET_by_offset(disp, _gloffset_DetachShader, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DisableVertexAttribArray)(GLuint);
#define CALL_DisableVertexAttribArray(disp, parameters) (* GET_DisableVertexAttribArray(disp)) parameters
#define GET_DisableVertexAttribArray(disp) ((_glptr_DisableVertexAttribArray)(GET_by_offset((disp), _gloffset_DisableVertexAttribArray)))
#define SET_DisableVertexAttribArray(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint) = func; \
   SET_by_offset(disp, _gloffset_DisableVertexAttribArray, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DrawBuffers)(GLsizei, const GLenum *);
#define CALL_DrawBuffers(disp, parameters) (* GET_DrawBuffers(disp)) parameters
#define GET_DrawBuffers(disp) ((_glptr_DrawBuffers)(GET_by_offset((disp), _gloffset_DrawBuffers)))
#define SET_DrawBuffers(disp, func) do { \
   void (GLAPIENTRYP fn)(GLsizei, const GLenum *) = func; \
   SET_by_offset(disp, _gloffset_DrawBuffers, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_EnableVertexAttribArray)(GLuint);
#define CALL_EnableVertexAttribArray(disp, parameters) (* GET_EnableVertexAttribArray(disp)) parameters
#define GET_EnableVertexAttribArray(disp) ((_glptr_EnableVertexAttribArray)(GET_by_offset((disp), _gloffset_EnableVertexAttribArray)))
#define SET_EnableVertexAttribArray(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint) = func; \
   SET_by_offset(disp, _gloffset_EnableVertexAttribArray, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetActiveAttrib)(GLuint, GLuint, GLsizei, GLsizei *, GLint *, GLenum *, GLchar *);
#define CALL_GetActiveAttrib(disp, parameters) (* GET_GetActiveAttrib(disp)) parameters
#define GET_GetActiveAttrib(disp) ((_glptr_GetActiveAttrib)(GET_by_offset((disp), _gloffset_GetActiveAttrib)))
#define SET_GetActiveAttrib(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLsizei, GLsizei *, GLint *, GLenum *, GLchar *) = func; \
   SET_by_offset(disp, _gloffset_GetActiveAttrib, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetActiveUniform)(GLuint, GLuint, GLsizei, GLsizei *, GLint *, GLenum *, GLchar *);
#define CALL_GetActiveUniform(disp, parameters) (* GET_GetActiveUniform(disp)) parameters
#define GET_GetActiveUniform(disp) ((_glptr_GetActiveUniform)(GET_by_offset((disp), _gloffset_GetActiveUniform)))
#define SET_GetActiveUniform(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLsizei, GLsizei *, GLint *, GLenum *, GLchar *) = func; \
   SET_by_offset(disp, _gloffset_GetActiveUniform, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetAttachedShaders)(GLuint, GLsizei, GLsizei *, GLuint *);
#define CALL_GetAttachedShaders(disp, parameters) (* GET_GetAttachedShaders(disp)) parameters
#define GET_GetAttachedShaders(disp) ((_glptr_GetAttachedShaders)(GET_by_offset((disp), _gloffset_GetAttachedShaders)))
#define SET_GetAttachedShaders(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizei, GLsizei *, GLuint *) = func; \
   SET_by_offset(disp, _gloffset_GetAttachedShaders, fn); \
} while (0)

typedef GLint (GLAPIENTRYP _glptr_GetAttribLocation)(GLuint, const GLchar *);
#define CALL_GetAttribLocation(disp, parameters) (* GET_GetAttribLocation(disp)) parameters
#define GET_GetAttribLocation(disp) ((_glptr_GetAttribLocation)(GET_by_offset((disp), _gloffset_GetAttribLocation)))
#define SET_GetAttribLocation(disp, func) do { \
   GLint (GLAPIENTRYP fn)(GLuint, const GLchar *) = func; \
   SET_by_offset(disp, _gloffset_GetAttribLocation, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetProgramInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
#define CALL_GetProgramInfoLog(disp, parameters) (* GET_GetProgramInfoLog(disp)) parameters
#define GET_GetProgramInfoLog(disp) ((_glptr_GetProgramInfoLog)(GET_by_offset((disp), _gloffset_GetProgramInfoLog)))
#define SET_GetProgramInfoLog(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizei, GLsizei *, GLchar *) = func; \
   SET_by_offset(disp, _gloffset_GetProgramInfoLog, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetProgramiv)(GLuint, GLenum, GLint *);
#define CALL_GetProgramiv(disp, parameters) (* GET_GetProgramiv(disp)) parameters
#define GET_GetProgramiv(disp) ((_glptr_GetProgramiv)(GET_by_offset((disp), _gloffset_GetProgramiv)))
#define SET_GetProgramiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetProgramiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetShaderInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
#define CALL_GetShaderInfoLog(disp, parameters) (* GET_GetShaderInfoLog(disp)) parameters
#define GET_GetShaderInfoLog(disp) ((_glptr_GetShaderInfoLog)(GET_by_offset((disp), _gloffset_GetShaderInfoLog)))
#define SET_GetShaderInfoLog(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizei, GLsizei *, GLchar *) = func; \
   SET_by_offset(disp, _gloffset_GetShaderInfoLog, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetShaderSource)(GLuint, GLsizei, GLsizei *, GLchar *);
#define CALL_GetShaderSource(disp, parameters) (* GET_GetShaderSource(disp)) parameters
#define GET_GetShaderSource(disp) ((_glptr_GetShaderSource)(GET_by_offset((disp), _gloffset_GetShaderSource)))
#define SET_GetShaderSource(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizei, GLsizei *, GLchar *) = func; \
   SET_by_offset(disp, _gloffset_GetShaderSource, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetShaderiv)(GLuint, GLenum, GLint *);
#define CALL_GetShaderiv(disp, parameters) (* GET_GetShaderiv(disp)) parameters
#define GET_GetShaderiv(disp) ((_glptr_GetShaderiv)(GET_by_offset((disp), _gloffset_GetShaderiv)))
#define SET_GetShaderiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetShaderiv, fn); \
} while (0)

typedef GLint (GLAPIENTRYP _glptr_GetUniformLocation)(GLuint, const GLchar *);
#define CALL_GetUniformLocation(disp, parameters) (* GET_GetUniformLocation(disp)) parameters
#define GET_GetUniformLocation(disp) ((_glptr_GetUniformLocation)(GET_by_offset((disp), _gloffset_GetUniformLocation)))
#define SET_GetUniformLocation(disp, func) do { \
   GLint (GLAPIENTRYP fn)(GLuint, const GLchar *) = func; \
   SET_by_offset(disp, _gloffset_GetUniformLocation, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetUniformfv)(GLuint, GLint, GLfloat *);
#define CALL_GetUniformfv(disp, parameters) (* GET_GetUniformfv(disp)) parameters
#define GET_GetUniformfv(disp) ((_glptr_GetUniformfv)(GET_by_offset((disp), _gloffset_GetUniformfv)))
#define SET_GetUniformfv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_GetUniformfv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetUniformiv)(GLuint, GLint, GLint *);
#define CALL_GetUniformiv(disp, parameters) (* GET_GetUniformiv(disp)) parameters
#define GET_GetUniformiv(disp) ((_glptr_GetUniformiv)(GET_by_offset((disp), _gloffset_GetUniformiv)))
#define SET_GetUniformiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetUniformiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetVertexAttribPointerv)(GLuint, GLenum, GLvoid **);
#define CALL_GetVertexAttribPointerv(disp, parameters) (* GET_GetVertexAttribPointerv(disp)) parameters
#define GET_GetVertexAttribPointerv(disp) ((_glptr_GetVertexAttribPointerv)(GET_by_offset((disp), _gloffset_GetVertexAttribPointerv)))
#define SET_GetVertexAttribPointerv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLvoid **) = func; \
   SET_by_offset(disp, _gloffset_GetVertexAttribPointerv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetVertexAttribdv)(GLuint, GLenum, GLdouble *);
#define CALL_GetVertexAttribdv(disp, parameters) (* GET_GetVertexAttribdv(disp)) parameters
#define GET_GetVertexAttribdv(disp) ((_glptr_GetVertexAttribdv)(GET_by_offset((disp), _gloffset_GetVertexAttribdv)))
#define SET_GetVertexAttribdv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_GetVertexAttribdv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetVertexAttribfv)(GLuint, GLenum, GLfloat *);
#define CALL_GetVertexAttribfv(disp, parameters) (* GET_GetVertexAttribfv(disp)) parameters
#define GET_GetVertexAttribfv(disp) ((_glptr_GetVertexAttribfv)(GET_by_offset((disp), _gloffset_GetVertexAttribfv)))
#define SET_GetVertexAttribfv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_GetVertexAttribfv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetVertexAttribiv)(GLuint, GLenum, GLint *);
#define CALL_GetVertexAttribiv(disp, parameters) (* GET_GetVertexAttribiv(disp)) parameters
#define GET_GetVertexAttribiv(disp) ((_glptr_GetVertexAttribiv)(GET_by_offset((disp), _gloffset_GetVertexAttribiv)))
#define SET_GetVertexAttribiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetVertexAttribiv, fn); \
} while (0)

typedef GLboolean (GLAPIENTRYP _glptr_IsProgram)(GLuint);
#define CALL_IsProgram(disp, parameters) (* GET_IsProgram(disp)) parameters
#define GET_IsProgram(disp) ((_glptr_IsProgram)(GET_by_offset((disp), _gloffset_IsProgram)))
#define SET_IsProgram(disp, func) do { \
   GLboolean (GLAPIENTRYP fn)(GLuint) = func; \
   SET_by_offset(disp, _gloffset_IsProgram, fn); \
} while (0)

typedef GLboolean (GLAPIENTRYP _glptr_IsShader)(GLuint);
#define CALL_IsShader(disp, parameters) (* GET_IsShader(disp)) parameters
#define GET_IsShader(disp) ((_glptr_IsShader)(GET_by_offset((disp), _gloffset_IsShader)))
#define SET_IsShader(disp, func) do { \
   GLboolean (GLAPIENTRYP fn)(GLuint) = func; \
   SET_by_offset(disp, _gloffset_IsShader, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_LinkProgram)(GLuint);
#define CALL_LinkProgram(disp, parameters) (* GET_LinkProgram(disp)) parameters
#define GET_LinkProgram(disp) ((_glptr_LinkProgram)(GET_by_offset((disp), _gloffset_LinkProgram)))
#define SET_LinkProgram(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint) = func; \
   SET_by_offset(disp, _gloffset_LinkProgram, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ShaderSource)(GLuint, GLsizei, const GLchar * const *, const GLint *);
#define CALL_ShaderSource(disp, parameters) (* GET_ShaderSource(disp)) parameters
#define GET_ShaderSource(disp) ((_glptr_ShaderSource)(GET_by_offset((disp), _gloffset_ShaderSource)))
#define SET_ShaderSource(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizei, const GLchar * const *, const GLint *) = func; \
   SET_by_offset(disp, _gloffset_ShaderSource, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_StencilFuncSeparate)(GLenum, GLenum, GLint, GLuint);
#define CALL_StencilFuncSeparate(disp, parameters) (* GET_StencilFuncSeparate(disp)) parameters
#define GET_StencilFuncSeparate(disp) ((_glptr_StencilFuncSeparate)(GET_by_offset((disp), _gloffset_StencilFuncSeparate)))
#define SET_StencilFuncSeparate(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLint, GLuint) = func; \
   SET_by_offset(disp, _gloffset_StencilFuncSeparate, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_StencilMaskSeparate)(GLenum, GLuint);
#define CALL_StencilMaskSeparate(disp, parameters) (* GET_StencilMaskSeparate(disp)) parameters
#define GET_StencilMaskSeparate(disp) ((_glptr_StencilMaskSeparate)(GET_by_offset((disp), _gloffset_StencilMaskSeparate)))
#define SET_StencilMaskSeparate(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint) = func; \
   SET_by_offset(disp, _gloffset_StencilMaskSeparate, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_StencilOpSeparate)(GLenum, GLenum, GLenum, GLenum);
#define CALL_StencilOpSeparate(disp, parameters) (* GET_StencilOpSeparate(disp)) parameters
#define GET_StencilOpSeparate(disp) ((_glptr_StencilOpSeparate)(GET_by_offset((disp), _gloffset_StencilOpSeparate)))
#define SET_StencilOpSeparate(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLenum, GLenum) = func; \
   SET_by_offset(disp, _gloffset_StencilOpSeparate, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Uniform1f)(GLint, GLfloat);
#define CALL_Uniform1f(disp, parameters) (* GET_Uniform1f(disp)) parameters
#define GET_Uniform1f(disp) ((_glptr_Uniform1f)(GET_by_offset((disp), _gloffset_Uniform1f)))
#define SET_Uniform1f(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_Uniform1f, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Uniform1fv)(GLint, GLsizei, const GLfloat *);
#define CALL_Uniform1fv(disp, parameters) (* GET_Uniform1fv(disp)) parameters
#define GET_Uniform1fv(disp) ((_glptr_Uniform1fv)(GET_by_offset((disp), _gloffset_Uniform1fv)))
#define SET_Uniform1fv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLsizei, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_Uniform1fv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Uniform1i)(GLint, GLint);
#define CALL_Uniform1i(disp, parameters) (* GET_Uniform1i(disp)) parameters
#define GET_Uniform1i(disp) ((_glptr_Uniform1i)(GET_by_offset((disp), _gloffset_Uniform1i)))
#define SET_Uniform1i(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLint) = func; \
   SET_by_offset(disp, _gloffset_Uniform1i, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Uniform1iv)(GLint, GLsizei, const GLint *);
#define CALL_Uniform1iv(disp, parameters) (* GET_Uniform1iv(disp)) parameters
#define GET_Uniform1iv(disp) ((_glptr_Uniform1iv)(GET_by_offset((disp), _gloffset_Uniform1iv)))
#define SET_Uniform1iv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLsizei, const GLint *) = func; \
   SET_by_offset(disp, _gloffset_Uniform1iv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Uniform2f)(GLint, GLfloat, GLfloat);
#define CALL_Uniform2f(disp, parameters) (* GET_Uniform2f(disp)) parameters
#define GET_Uniform2f(disp) ((_glptr_Uniform2f)(GET_by_offset((disp), _gloffset_Uniform2f)))
#define SET_Uniform2f(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLfloat, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_Uniform2f, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Uniform2fv)(GLint, GLsizei, const GLfloat *);
#define CALL_Uniform2fv(disp, parameters) (* GET_Uniform2fv(disp)) parameters
#define GET_Uniform2fv(disp) ((_glptr_Uniform2fv)(GET_by_offset((disp), _gloffset_Uniform2fv)))
#define SET_Uniform2fv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLsizei, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_Uniform2fv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Uniform2i)(GLint, GLint, GLint);
#define CALL_Uniform2i(disp, parameters) (* GET_Uniform2i(disp)) parameters
#define GET_Uniform2i(disp) ((_glptr_Uniform2i)(GET_by_offset((disp), _gloffset_Uniform2i)))
#define SET_Uniform2i(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLint, GLint) = func; \
   SET_by_offset(disp, _gloffset_Uniform2i, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Uniform2iv)(GLint, GLsizei, const GLint *);
#define CALL_Uniform2iv(disp, parameters) (* GET_Uniform2iv(disp)) parameters
#define GET_Uniform2iv(disp) ((_glptr_Uniform2iv)(GET_by_offset((disp), _gloffset_Uniform2iv)))
#define SET_Uniform2iv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLsizei, const GLint *) = func; \
   SET_by_offset(disp, _gloffset_Uniform2iv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Uniform3f)(GLint, GLfloat, GLfloat, GLfloat);
#define CALL_Uniform3f(disp, parameters) (* GET_Uniform3f(disp)) parameters
#define GET_Uniform3f(disp) ((_glptr_Uniform3f)(GET_by_offset((disp), _gloffset_Uniform3f)))
#define SET_Uniform3f(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLfloat, GLfloat, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_Uniform3f, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Uniform3fv)(GLint, GLsizei, const GLfloat *);
#define CALL_Uniform3fv(disp, parameters) (* GET_Uniform3fv(disp)) parameters
#define GET_Uniform3fv(disp) ((_glptr_Uniform3fv)(GET_by_offset((disp), _gloffset_Uniform3fv)))
#define SET_Uniform3fv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLsizei, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_Uniform3fv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Uniform3i)(GLint, GLint, GLint, GLint);
#define CALL_Uniform3i(disp, parameters) (* GET_Uniform3i(disp)) parameters
#define GET_Uniform3i(disp) ((_glptr_Uniform3i)(GET_by_offset((disp), _gloffset_Uniform3i)))
#define SET_Uniform3i(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLint, GLint, GLint) = func; \
   SET_by_offset(disp, _gloffset_Uniform3i, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Uniform3iv)(GLint, GLsizei, const GLint *);
#define CALL_Uniform3iv(disp, parameters) (* GET_Uniform3iv(disp)) parameters
#define GET_Uniform3iv(disp) ((_glptr_Uniform3iv)(GET_by_offset((disp), _gloffset_Uniform3iv)))
#define SET_Uniform3iv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLsizei, const GLint *) = func; \
   SET_by_offset(disp, _gloffset_Uniform3iv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Uniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
#define CALL_Uniform4f(disp, parameters) (* GET_Uniform4f(disp)) parameters
#define GET_Uniform4f(disp) ((_glptr_Uniform4f)(GET_by_offset((disp), _gloffset_Uniform4f)))
#define SET_Uniform4f(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLfloat, GLfloat, GLfloat, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_Uniform4f, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Uniform4fv)(GLint, GLsizei, const GLfloat *);
#define CALL_Uniform4fv(disp, parameters) (* GET_Uniform4fv(disp)) parameters
#define GET_Uniform4fv(disp) ((_glptr_Uniform4fv)(GET_by_offset((disp), _gloffset_Uniform4fv)))
#define SET_Uniform4fv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLsizei, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_Uniform4fv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Uniform4i)(GLint, GLint, GLint, GLint, GLint);
#define CALL_Uniform4i(disp, parameters) (* GET_Uniform4i(disp)) parameters
#define GET_Uniform4i(disp) ((_glptr_Uniform4i)(GET_by_offset((disp), _gloffset_Uniform4i)))
#define SET_Uniform4i(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLint, GLint, GLint, GLint) = func; \
   SET_by_offset(disp, _gloffset_Uniform4i, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Uniform4iv)(GLint, GLsizei, const GLint *);
#define CALL_Uniform4iv(disp, parameters) (* GET_Uniform4iv(disp)) parameters
#define GET_Uniform4iv(disp) ((_glptr_Uniform4iv)(GET_by_offset((disp), _gloffset_Uniform4iv)))
#define SET_Uniform4iv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLsizei, const GLint *) = func; \
   SET_by_offset(disp, _gloffset_Uniform4iv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_UniformMatrix2fv)(GLint, GLsizei, GLboolean, const GLfloat *);
#define CALL_UniformMatrix2fv(disp, parameters) (* GET_UniformMatrix2fv(disp)) parameters
#define GET_UniformMatrix2fv(disp) ((_glptr_UniformMatrix2fv)(GET_by_offset((disp), _gloffset_UniformMatrix2fv)))
#define SET_UniformMatrix2fv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLsizei, GLboolean, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_UniformMatrix2fv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_UniformMatrix3fv)(GLint, GLsizei, GLboolean, const GLfloat *);
#define CALL_UniformMatrix3fv(disp, parameters) (* GET_UniformMatrix3fv(disp)) parameters
#define GET_UniformMatrix3fv(disp) ((_glptr_UniformMatrix3fv)(GET_by_offset((disp), _gloffset_UniformMatrix3fv)))
#define SET_UniformMatrix3fv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLsizei, GLboolean, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_UniformMatrix3fv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_UniformMatrix4fv)(GLint, GLsizei, GLboolean, const GLfloat *);
#define CALL_UniformMatrix4fv(disp, parameters) (* GET_UniformMatrix4fv(disp)) parameters
#define GET_UniformMatrix4fv(disp) ((_glptr_UniformMatrix4fv)(GET_by_offset((disp), _gloffset_UniformMatrix4fv)))
#define SET_UniformMatrix4fv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLsizei, GLboolean, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_UniformMatrix4fv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_UseProgram)(GLuint);
#define CALL_UseProgram(disp, parameters) (* GET_UseProgram(disp)) parameters
#define GET_UseProgram(disp) ((_glptr_UseProgram)(GET_by_offset((disp), _gloffset_UseProgram)))
#define SET_UseProgram(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint) = func; \
   SET_by_offset(disp, _gloffset_UseProgram, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ValidateProgram)(GLuint);
#define CALL_ValidateProgram(disp, parameters) (* GET_ValidateProgram(disp)) parameters
#define GET_ValidateProgram(disp) ((_glptr_ValidateProgram)(GET_by_offset((disp), _gloffset_ValidateProgram)))
#define SET_ValidateProgram(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint) = func; \
   SET_by_offset(disp, _gloffset_ValidateProgram, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib1d)(GLuint, GLdouble);
#define CALL_VertexAttrib1d(disp, parameters) (* GET_VertexAttrib1d(disp)) parameters
#define GET_VertexAttrib1d(disp) ((_glptr_VertexAttrib1d)(GET_by_offset((disp), _gloffset_VertexAttrib1d)))
#define SET_VertexAttrib1d(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLdouble) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib1d, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib1dv)(GLuint, const GLdouble *);
#define CALL_VertexAttrib1dv(disp, parameters) (* GET_VertexAttrib1dv(disp)) parameters
#define GET_VertexAttrib1dv(disp) ((_glptr_VertexAttrib1dv)(GET_by_offset((disp), _gloffset_VertexAttrib1dv)))
#define SET_VertexAttrib1dv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib1dv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib1s)(GLuint, GLshort);
#define CALL_VertexAttrib1s(disp, parameters) (* GET_VertexAttrib1s(disp)) parameters
#define GET_VertexAttrib1s(disp) ((_glptr_VertexAttrib1s)(GET_by_offset((disp), _gloffset_VertexAttrib1s)))
#define SET_VertexAttrib1s(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLshort) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib1s, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib1sv)(GLuint, const GLshort *);
#define CALL_VertexAttrib1sv(disp, parameters) (* GET_VertexAttrib1sv(disp)) parameters
#define GET_VertexAttrib1sv(disp) ((_glptr_VertexAttrib1sv)(GET_by_offset((disp), _gloffset_VertexAttrib1sv)))
#define SET_VertexAttrib1sv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLshort *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib1sv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib2d)(GLuint, GLdouble, GLdouble);
#define CALL_VertexAttrib2d(disp, parameters) (* GET_VertexAttrib2d(disp)) parameters
#define GET_VertexAttrib2d(disp) ((_glptr_VertexAttrib2d)(GET_by_offset((disp), _gloffset_VertexAttrib2d)))
#define SET_VertexAttrib2d(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLdouble, GLdouble) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib2d, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib2dv)(GLuint, const GLdouble *);
#define CALL_VertexAttrib2dv(disp, parameters) (* GET_VertexAttrib2dv(disp)) parameters
#define GET_VertexAttrib2dv(disp) ((_glptr_VertexAttrib2dv)(GET_by_offset((disp), _gloffset_VertexAttrib2dv)))
#define SET_VertexAttrib2dv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib2dv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib2s)(GLuint, GLshort, GLshort);
#define CALL_VertexAttrib2s(disp, parameters) (* GET_VertexAttrib2s(disp)) parameters
#define GET_VertexAttrib2s(disp) ((_glptr_VertexAttrib2s)(GET_by_offset((disp), _gloffset_VertexAttrib2s)))
#define SET_VertexAttrib2s(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLshort, GLshort) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib2s, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib2sv)(GLuint, const GLshort *);
#define CALL_VertexAttrib2sv(disp, parameters) (* GET_VertexAttrib2sv(disp)) parameters
#define GET_VertexAttrib2sv(disp) ((_glptr_VertexAttrib2sv)(GET_by_offset((disp), _gloffset_VertexAttrib2sv)))
#define SET_VertexAttrib2sv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLshort *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib2sv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib3d)(GLuint, GLdouble, GLdouble, GLdouble);
#define CALL_VertexAttrib3d(disp, parameters) (* GET_VertexAttrib3d(disp)) parameters
#define GET_VertexAttrib3d(disp) ((_glptr_VertexAttrib3d)(GET_by_offset((disp), _gloffset_VertexAttrib3d)))
#define SET_VertexAttrib3d(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLdouble, GLdouble, GLdouble) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib3d, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib3dv)(GLuint, const GLdouble *);
#define CALL_VertexAttrib3dv(disp, parameters) (* GET_VertexAttrib3dv(disp)) parameters
#define GET_VertexAttrib3dv(disp) ((_glptr_VertexAttrib3dv)(GET_by_offset((disp), _gloffset_VertexAttrib3dv)))
#define SET_VertexAttrib3dv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib3dv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib3s)(GLuint, GLshort, GLshort, GLshort);
#define CALL_VertexAttrib3s(disp, parameters) (* GET_VertexAttrib3s(disp)) parameters
#define GET_VertexAttrib3s(disp) ((_glptr_VertexAttrib3s)(GET_by_offset((disp), _gloffset_VertexAttrib3s)))
#define SET_VertexAttrib3s(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLshort, GLshort, GLshort) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib3s, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib3sv)(GLuint, const GLshort *);
#define CALL_VertexAttrib3sv(disp, parameters) (* GET_VertexAttrib3sv(disp)) parameters
#define GET_VertexAttrib3sv(disp) ((_glptr_VertexAttrib3sv)(GET_by_offset((disp), _gloffset_VertexAttrib3sv)))
#define SET_VertexAttrib3sv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLshort *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib3sv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib4Nbv)(GLuint, const GLbyte *);
#define CALL_VertexAttrib4Nbv(disp, parameters) (* GET_VertexAttrib4Nbv(disp)) parameters
#define GET_VertexAttrib4Nbv(disp) ((_glptr_VertexAttrib4Nbv)(GET_by_offset((disp), _gloffset_VertexAttrib4Nbv)))
#define SET_VertexAttrib4Nbv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLbyte *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib4Nbv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib4Niv)(GLuint, const GLint *);
#define CALL_VertexAttrib4Niv(disp, parameters) (* GET_VertexAttrib4Niv(disp)) parameters
#define GET_VertexAttrib4Niv(disp) ((_glptr_VertexAttrib4Niv)(GET_by_offset((disp), _gloffset_VertexAttrib4Niv)))
#define SET_VertexAttrib4Niv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLint *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib4Niv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib4Nsv)(GLuint, const GLshort *);
#define CALL_VertexAttrib4Nsv(disp, parameters) (* GET_VertexAttrib4Nsv(disp)) parameters
#define GET_VertexAttrib4Nsv(disp) ((_glptr_VertexAttrib4Nsv)(GET_by_offset((disp), _gloffset_VertexAttrib4Nsv)))
#define SET_VertexAttrib4Nsv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLshort *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib4Nsv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib4Nub)(GLuint, GLubyte, GLubyte, GLubyte, GLubyte);
#define CALL_VertexAttrib4Nub(disp, parameters) (* GET_VertexAttrib4Nub(disp)) parameters
#define GET_VertexAttrib4Nub(disp) ((_glptr_VertexAttrib4Nub)(GET_by_offset((disp), _gloffset_VertexAttrib4Nub)))
#define SET_VertexAttrib4Nub(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLubyte, GLubyte, GLubyte, GLubyte) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib4Nub, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib4Nubv)(GLuint, const GLubyte *);
#define CALL_VertexAttrib4Nubv(disp, parameters) (* GET_VertexAttrib4Nubv(disp)) parameters
#define GET_VertexAttrib4Nubv(disp) ((_glptr_VertexAttrib4Nubv)(GET_by_offset((disp), _gloffset_VertexAttrib4Nubv)))
#define SET_VertexAttrib4Nubv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLubyte *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib4Nubv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib4Nuiv)(GLuint, const GLuint *);
#define CALL_VertexAttrib4Nuiv(disp, parameters) (* GET_VertexAttrib4Nuiv(disp)) parameters
#define GET_VertexAttrib4Nuiv(disp) ((_glptr_VertexAttrib4Nuiv)(GET_by_offset((disp), _gloffset_VertexAttrib4Nuiv)))
#define SET_VertexAttrib4Nuiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib4Nuiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib4Nusv)(GLuint, const GLushort *);
#define CALL_VertexAttrib4Nusv(disp, parameters) (* GET_VertexAttrib4Nusv(disp)) parameters
#define GET_VertexAttrib4Nusv(disp) ((_glptr_VertexAttrib4Nusv)(GET_by_offset((disp), _gloffset_VertexAttrib4Nusv)))
#define SET_VertexAttrib4Nusv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLushort *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib4Nusv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib4bv)(GLuint, const GLbyte *);
#define CALL_VertexAttrib4bv(disp, parameters) (* GET_VertexAttrib4bv(disp)) parameters
#define GET_VertexAttrib4bv(disp) ((_glptr_VertexAttrib4bv)(GET_by_offset((disp), _gloffset_VertexAttrib4bv)))
#define SET_VertexAttrib4bv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLbyte *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib4bv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib4d)(GLuint, GLdouble, GLdouble, GLdouble, GLdouble);
#define CALL_VertexAttrib4d(disp, parameters) (* GET_VertexAttrib4d(disp)) parameters
#define GET_VertexAttrib4d(disp) ((_glptr_VertexAttrib4d)(GET_by_offset((disp), _gloffset_VertexAttrib4d)))
#define SET_VertexAttrib4d(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLdouble, GLdouble, GLdouble, GLdouble) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib4d, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib4dv)(GLuint, const GLdouble *);
#define CALL_VertexAttrib4dv(disp, parameters) (* GET_VertexAttrib4dv(disp)) parameters
#define GET_VertexAttrib4dv(disp) ((_glptr_VertexAttrib4dv)(GET_by_offset((disp), _gloffset_VertexAttrib4dv)))
#define SET_VertexAttrib4dv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib4dv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib4iv)(GLuint, const GLint *);
#define CALL_VertexAttrib4iv(disp, parameters) (* GET_VertexAttrib4iv(disp)) parameters
#define GET_VertexAttrib4iv(disp) ((_glptr_VertexAttrib4iv)(GET_by_offset((disp), _gloffset_VertexAttrib4iv)))
#define SET_VertexAttrib4iv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLint *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib4iv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib4s)(GLuint, GLshort, GLshort, GLshort, GLshort);
#define CALL_VertexAttrib4s(disp, parameters) (* GET_VertexAttrib4s(disp)) parameters
#define GET_VertexAttrib4s(disp) ((_glptr_VertexAttrib4s)(GET_by_offset((disp), _gloffset_VertexAttrib4s)))
#define SET_VertexAttrib4s(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLshort, GLshort, GLshort, GLshort) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib4s, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib4sv)(GLuint, const GLshort *);
#define CALL_VertexAttrib4sv(disp, parameters) (* GET_VertexAttrib4sv(disp)) parameters
#define GET_VertexAttrib4sv(disp) ((_glptr_VertexAttrib4sv)(GET_by_offset((disp), _gloffset_VertexAttrib4sv)))
#define SET_VertexAttrib4sv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLshort *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib4sv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib4ubv)(GLuint, const GLubyte *);
#define CALL_VertexAttrib4ubv(disp, parameters) (* GET_VertexAttrib4ubv(disp)) parameters
#define GET_VertexAttrib4ubv(disp) ((_glptr_VertexAttrib4ubv)(GET_by_offset((disp), _gloffset_VertexAttrib4ubv)))
#define SET_VertexAttrib4ubv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLubyte *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib4ubv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib4uiv)(GLuint, const GLuint *);
#define CALL_VertexAttrib4uiv(disp, parameters) (* GET_VertexAttrib4uiv(disp)) parameters
#define GET_VertexAttrib4uiv(disp) ((_glptr_VertexAttrib4uiv)(GET_by_offset((disp), _gloffset_VertexAttrib4uiv)))
#define SET_VertexAttrib4uiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib4uiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib4usv)(GLuint, const GLushort *);
#define CALL_VertexAttrib4usv(disp, parameters) (* GET_VertexAttrib4usv(disp)) parameters
#define GET_VertexAttrib4usv(disp) ((_glptr_VertexAttrib4usv)(GET_by_offset((disp), _gloffset_VertexAttrib4usv)))
#define SET_VertexAttrib4usv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLushort *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib4usv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const GLvoid *);
#define CALL_VertexAttribPointer(disp, parameters) (* GET_VertexAttribPointer(disp)) parameters
#define GET_VertexAttribPointer(disp) ((_glptr_VertexAttribPointer)(GET_by_offset((disp), _gloffset_VertexAttribPointer)))
#define SET_VertexAttribPointer(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLenum, GLboolean, GLsizei, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribPointer, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_UniformMatrix2x3fv)(GLint, GLsizei, GLboolean, const GLfloat *);
#define CALL_UniformMatrix2x3fv(disp, parameters) (* GET_UniformMatrix2x3fv(disp)) parameters
#define GET_UniformMatrix2x3fv(disp) ((_glptr_UniformMatrix2x3fv)(GET_by_offset((disp), _gloffset_UniformMatrix2x3fv)))
#define SET_UniformMatrix2x3fv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLsizei, GLboolean, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_UniformMatrix2x3fv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_UniformMatrix2x4fv)(GLint, GLsizei, GLboolean, const GLfloat *);
#define CALL_UniformMatrix2x4fv(disp, parameters) (* GET_UniformMatrix2x4fv(disp)) parameters
#define GET_UniformMatrix2x4fv(disp) ((_glptr_UniformMatrix2x4fv)(GET_by_offset((disp), _gloffset_UniformMatrix2x4fv)))
#define SET_UniformMatrix2x4fv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLsizei, GLboolean, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_UniformMatrix2x4fv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_UniformMatrix3x2fv)(GLint, GLsizei, GLboolean, const GLfloat *);
#define CALL_UniformMatrix3x2fv(disp, parameters) (* GET_UniformMatrix3x2fv(disp)) parameters
#define GET_UniformMatrix3x2fv(disp) ((_glptr_UniformMatrix3x2fv)(GET_by_offset((disp), _gloffset_UniformMatrix3x2fv)))
#define SET_UniformMatrix3x2fv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLsizei, GLboolean, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_UniformMatrix3x2fv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_UniformMatrix3x4fv)(GLint, GLsizei, GLboolean, const GLfloat *);
#define CALL_UniformMatrix3x4fv(disp, parameters) (* GET_UniformMatrix3x4fv(disp)) parameters
#define GET_UniformMatrix3x4fv(disp) ((_glptr_UniformMatrix3x4fv)(GET_by_offset((disp), _gloffset_UniformMatrix3x4fv)))
#define SET_UniformMatrix3x4fv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLsizei, GLboolean, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_UniformMatrix3x4fv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_UniformMatrix4x2fv)(GLint, GLsizei, GLboolean, const GLfloat *);
#define CALL_UniformMatrix4x2fv(disp, parameters) (* GET_UniformMatrix4x2fv(disp)) parameters
#define GET_UniformMatrix4x2fv(disp) ((_glptr_UniformMatrix4x2fv)(GET_by_offset((disp), _gloffset_UniformMatrix4x2fv)))
#define SET_UniformMatrix4x2fv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLsizei, GLboolean, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_UniformMatrix4x2fv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_UniformMatrix4x3fv)(GLint, GLsizei, GLboolean, const GLfloat *);
#define CALL_UniformMatrix4x3fv(disp, parameters) (* GET_UniformMatrix4x3fv(disp)) parameters
#define GET_UniformMatrix4x3fv(disp) ((_glptr_UniformMatrix4x3fv)(GET_by_offset((disp), _gloffset_UniformMatrix4x3fv)))
#define SET_UniformMatrix4x3fv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLsizei, GLboolean, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_UniformMatrix4x3fv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_BeginConditionalRender)(GLuint, GLenum);
#define CALL_BeginConditionalRender(disp, parameters) (* GET_BeginConditionalRender(disp)) parameters
#define GET_BeginConditionalRender(disp) ((_glptr_BeginConditionalRender)(GET_by_offset((disp), _gloffset_BeginConditionalRender)))
#define SET_BeginConditionalRender(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum) = func; \
   SET_by_offset(disp, _gloffset_BeginConditionalRender, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_BeginTransformFeedback)(GLenum);
#define CALL_BeginTransformFeedback(disp, parameters) (* GET_BeginTransformFeedback(disp)) parameters
#define GET_BeginTransformFeedback(disp) ((_glptr_BeginTransformFeedback)(GET_by_offset((disp), _gloffset_BeginTransformFeedback)))
#define SET_BeginTransformFeedback(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum) = func; \
   SET_by_offset(disp, _gloffset_BeginTransformFeedback, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_BindBufferBase)(GLenum, GLuint, GLuint);
#define CALL_BindBufferBase(disp, parameters) (* GET_BindBufferBase(disp)) parameters
#define GET_BindBufferBase(disp) ((_glptr_BindBufferBase)(GET_by_offset((disp), _gloffset_BindBufferBase)))
#define SET_BindBufferBase(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint, GLuint) = func; \
   SET_by_offset(disp, _gloffset_BindBufferBase, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_BindBufferRange)(GLenum, GLuint, GLuint, GLintptr, GLsizeiptr);
#define CALL_BindBufferRange(disp, parameters) (* GET_BindBufferRange(disp)) parameters
#define GET_BindBufferRange(disp) ((_glptr_BindBufferRange)(GET_by_offset((disp), _gloffset_BindBufferRange)))
#define SET_BindBufferRange(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint, GLuint, GLintptr, GLsizeiptr) = func; \
   SET_by_offset(disp, _gloffset_BindBufferRange, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_BindFragDataLocation)(GLuint, GLuint, const GLchar *);
#define CALL_BindFragDataLocation(disp, parameters) (* GET_BindFragDataLocation(disp)) parameters
#define GET_BindFragDataLocation(disp) ((_glptr_BindFragDataLocation)(GET_by_offset((disp), _gloffset_BindFragDataLocation)))
#define SET_BindFragDataLocation(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, const GLchar *) = func; \
   SET_by_offset(disp, _gloffset_BindFragDataLocation, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ClampColor)(GLenum, GLenum);
#define CALL_ClampColor(disp, parameters) (* GET_ClampColor(disp)) parameters
#define GET_ClampColor(disp) ((_glptr_ClampColor)(GET_by_offset((disp), _gloffset_ClampColor)))
#define SET_ClampColor(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum) = func; \
   SET_by_offset(disp, _gloffset_ClampColor, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ClearBufferfi)(GLenum, GLint, GLfloat, GLint);
#define CALL_ClearBufferfi(disp, parameters) (* GET_ClearBufferfi(disp)) parameters
#define GET_ClearBufferfi(disp) ((_glptr_ClearBufferfi)(GET_by_offset((disp), _gloffset_ClearBufferfi)))
#define SET_ClearBufferfi(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLint, GLfloat, GLint) = func; \
   SET_by_offset(disp, _gloffset_ClearBufferfi, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ClearBufferfv)(GLenum, GLint, const GLfloat *);
#define CALL_ClearBufferfv(disp, parameters) (* GET_ClearBufferfv(disp)) parameters
#define GET_ClearBufferfv(disp) ((_glptr_ClearBufferfv)(GET_by_offset((disp), _gloffset_ClearBufferfv)))
#define SET_ClearBufferfv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLint, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_ClearBufferfv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ClearBufferiv)(GLenum, GLint, const GLint *);
#define CALL_ClearBufferiv(disp, parameters) (* GET_ClearBufferiv(disp)) parameters
#define GET_ClearBufferiv(disp) ((_glptr_ClearBufferiv)(GET_by_offset((disp), _gloffset_ClearBufferiv)))
#define SET_ClearBufferiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLint, const GLint *) = func; \
   SET_by_offset(disp, _gloffset_ClearBufferiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ClearBufferuiv)(GLenum, GLint, const GLuint *);
#define CALL_ClearBufferuiv(disp, parameters) (* GET_ClearBufferuiv(disp)) parameters
#define GET_ClearBufferuiv(disp) ((_glptr_ClearBufferuiv)(GET_by_offset((disp), _gloffset_ClearBufferuiv)))
#define SET_ClearBufferuiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLint, const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_ClearBufferuiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ColorMaski)(GLuint, GLboolean, GLboolean, GLboolean, GLboolean);
#define CALL_ColorMaski(disp, parameters) (* GET_ColorMaski(disp)) parameters
#define GET_ColorMaski(disp) ((_glptr_ColorMaski)(GET_by_offset((disp), _gloffset_ColorMaski)))
#define SET_ColorMaski(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLboolean, GLboolean, GLboolean, GLboolean) = func; \
   SET_by_offset(disp, _gloffset_ColorMaski, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Disablei)(GLenum, GLuint);
#define CALL_Disablei(disp, parameters) (* GET_Disablei(disp)) parameters
#define GET_Disablei(disp) ((_glptr_Disablei)(GET_by_offset((disp), _gloffset_Disablei)))
#define SET_Disablei(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint) = func; \
   SET_by_offset(disp, _gloffset_Disablei, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Enablei)(GLenum, GLuint);
#define CALL_Enablei(disp, parameters) (* GET_Enablei(disp)) parameters
#define GET_Enablei(disp) ((_glptr_Enablei)(GET_by_offset((disp), _gloffset_Enablei)))
#define SET_Enablei(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint) = func; \
   SET_by_offset(disp, _gloffset_Enablei, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_EndConditionalRender)(void);
#define CALL_EndConditionalRender(disp, parameters) (* GET_EndConditionalRender(disp)) parameters
#define GET_EndConditionalRender(disp) ((_glptr_EndConditionalRender)(GET_by_offset((disp), _gloffset_EndConditionalRender)))
#define SET_EndConditionalRender(disp, func) do { \
   void (GLAPIENTRYP fn)(void) = func; \
   SET_by_offset(disp, _gloffset_EndConditionalRender, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_EndTransformFeedback)(void);
#define CALL_EndTransformFeedback(disp, parameters) (* GET_EndTransformFeedback(disp)) parameters
#define GET_EndTransformFeedback(disp) ((_glptr_EndTransformFeedback)(GET_by_offset((disp), _gloffset_EndTransformFeedback)))
#define SET_EndTransformFeedback(disp, func) do { \
   void (GLAPIENTRYP fn)(void) = func; \
   SET_by_offset(disp, _gloffset_EndTransformFeedback, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetBooleani_v)(GLenum, GLuint, GLboolean *);
#define CALL_GetBooleani_v(disp, parameters) (* GET_GetBooleani_v(disp)) parameters
#define GET_GetBooleani_v(disp) ((_glptr_GetBooleani_v)(GET_by_offset((disp), _gloffset_GetBooleani_v)))
#define SET_GetBooleani_v(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint, GLboolean *) = func; \
   SET_by_offset(disp, _gloffset_GetBooleani_v, fn); \
} while (0)

typedef GLint (GLAPIENTRYP _glptr_GetFragDataLocation)(GLuint, const GLchar *);
#define CALL_GetFragDataLocation(disp, parameters) (* GET_GetFragDataLocation(disp)) parameters
#define GET_GetFragDataLocation(disp) ((_glptr_GetFragDataLocation)(GET_by_offset((disp), _gloffset_GetFragDataLocation)))
#define SET_GetFragDataLocation(disp, func) do { \
   GLint (GLAPIENTRYP fn)(GLuint, const GLchar *) = func; \
   SET_by_offset(disp, _gloffset_GetFragDataLocation, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetIntegeri_v)(GLenum, GLuint, GLint *);
#define CALL_GetIntegeri_v(disp, parameters) (* GET_GetIntegeri_v(disp)) parameters
#define GET_GetIntegeri_v(disp) ((_glptr_GetIntegeri_v)(GET_by_offset((disp), _gloffset_GetIntegeri_v)))
#define SET_GetIntegeri_v(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetIntegeri_v, fn); \
} while (0)

typedef const GLubyte * (GLAPIENTRYP _glptr_GetStringi)(GLenum, GLuint);
#define CALL_GetStringi(disp, parameters) (* GET_GetStringi(disp)) parameters
#define GET_GetStringi(disp) ((_glptr_GetStringi)(GET_by_offset((disp), _gloffset_GetStringi)))
#define SET_GetStringi(disp, func) do { \
   const GLubyte * (GLAPIENTRYP fn)(GLenum, GLuint) = func; \
   SET_by_offset(disp, _gloffset_GetStringi, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetTexParameterIiv)(GLenum, GLenum, GLint *);
#define CALL_GetTexParameterIiv(disp, parameters) (* GET_GetTexParameterIiv(disp)) parameters
#define GET_GetTexParameterIiv(disp) ((_glptr_GetTexParameterIiv)(GET_by_offset((disp), _gloffset_GetTexParameterIiv)))
#define SET_GetTexParameterIiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetTexParameterIiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetTexParameterIuiv)(GLenum, GLenum, GLuint *);
#define CALL_GetTexParameterIuiv(disp, parameters) (* GET_GetTexParameterIuiv(disp)) parameters
#define GET_GetTexParameterIuiv(disp) ((_glptr_GetTexParameterIuiv)(GET_by_offset((disp), _gloffset_GetTexParameterIuiv)))
#define SET_GetTexParameterIuiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLuint *) = func; \
   SET_by_offset(disp, _gloffset_GetTexParameterIuiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetTransformFeedbackVarying)(GLuint, GLuint, GLsizei, GLsizei *, GLsizei *, GLenum *, GLchar *);
#define CALL_GetTransformFeedbackVarying(disp, parameters) (* GET_GetTransformFeedbackVarying(disp)) parameters
#define GET_GetTransformFeedbackVarying(disp) ((_glptr_GetTransformFeedbackVarying)(GET_by_offset((disp), _gloffset_GetTransformFeedbackVarying)))
#define SET_GetTransformFeedbackVarying(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLsizei, GLsizei *, GLsizei *, GLenum *, GLchar *) = func; \
   SET_by_offset(disp, _gloffset_GetTransformFeedbackVarying, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetUniformuiv)(GLuint, GLint, GLuint *);
#define CALL_GetUniformuiv(disp, parameters) (* GET_GetUniformuiv(disp)) parameters
#define GET_GetUniformuiv(disp) ((_glptr_GetUniformuiv)(GET_by_offset((disp), _gloffset_GetUniformuiv)))
#define SET_GetUniformuiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLuint *) = func; \
   SET_by_offset(disp, _gloffset_GetUniformuiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetVertexAttribIiv)(GLuint, GLenum, GLint *);
#define CALL_GetVertexAttribIiv(disp, parameters) (* GET_GetVertexAttribIiv(disp)) parameters
#define GET_GetVertexAttribIiv(disp) ((_glptr_GetVertexAttribIiv)(GET_by_offset((disp), _gloffset_GetVertexAttribIiv)))
#define SET_GetVertexAttribIiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetVertexAttribIiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetVertexAttribIuiv)(GLuint, GLenum, GLuint *);
#define CALL_GetVertexAttribIuiv(disp, parameters) (* GET_GetVertexAttribIuiv(disp)) parameters
#define GET_GetVertexAttribIuiv(disp) ((_glptr_GetVertexAttribIuiv)(GET_by_offset((disp), _gloffset_GetVertexAttribIuiv)))
#define SET_GetVertexAttribIuiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLuint *) = func; \
   SET_by_offset(disp, _gloffset_GetVertexAttribIuiv, fn); \
} while (0)

typedef GLboolean (GLAPIENTRYP _glptr_IsEnabledi)(GLenum, GLuint);
#define CALL_IsEnabledi(disp, parameters) (* GET_IsEnabledi(disp)) parameters
#define GET_IsEnabledi(disp) ((_glptr_IsEnabledi)(GET_by_offset((disp), _gloffset_IsEnabledi)))
#define SET_IsEnabledi(disp, func) do { \
   GLboolean (GLAPIENTRYP fn)(GLenum, GLuint) = func; \
   SET_by_offset(disp, _gloffset_IsEnabledi, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexParameterIiv)(GLenum, GLenum, const GLint *);
#define CALL_TexParameterIiv(disp, parameters) (* GET_TexParameterIiv(disp)) parameters
#define GET_TexParameterIiv(disp) ((_glptr_TexParameterIiv)(GET_by_offset((disp), _gloffset_TexParameterIiv)))
#define SET_TexParameterIiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, const GLint *) = func; \
   SET_by_offset(disp, _gloffset_TexParameterIiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexParameterIuiv)(GLenum, GLenum, const GLuint *);
#define CALL_TexParameterIuiv(disp, parameters) (* GET_TexParameterIuiv(disp)) parameters
#define GET_TexParameterIuiv(disp) ((_glptr_TexParameterIuiv)(GET_by_offset((disp), _gloffset_TexParameterIuiv)))
#define SET_TexParameterIuiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_TexParameterIuiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TransformFeedbackVaryings)(GLuint, GLsizei, const GLchar * const *, GLenum);
#define CALL_TransformFeedbackVaryings(disp, parameters) (* GET_TransformFeedbackVaryings(disp)) parameters
#define GET_TransformFeedbackVaryings(disp) ((_glptr_TransformFeedbackVaryings)(GET_by_offset((disp), _gloffset_TransformFeedbackVaryings)))
#define SET_TransformFeedbackVaryings(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizei, const GLchar * const *, GLenum) = func; \
   SET_by_offset(disp, _gloffset_TransformFeedbackVaryings, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Uniform1ui)(GLint, GLuint);
#define CALL_Uniform1ui(disp, parameters) (* GET_Uniform1ui(disp)) parameters
#define GET_Uniform1ui(disp) ((_glptr_Uniform1ui)(GET_by_offset((disp), _gloffset_Uniform1ui)))
#define SET_Uniform1ui(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLuint) = func; \
   SET_by_offset(disp, _gloffset_Uniform1ui, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Uniform1uiv)(GLint, GLsizei, const GLuint *);
#define CALL_Uniform1uiv(disp, parameters) (* GET_Uniform1uiv(disp)) parameters
#define GET_Uniform1uiv(disp) ((_glptr_Uniform1uiv)(GET_by_offset((disp), _gloffset_Uniform1uiv)))
#define SET_Uniform1uiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLsizei, const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_Uniform1uiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Uniform2ui)(GLint, GLuint, GLuint);
#define CALL_Uniform2ui(disp, parameters) (* GET_Uniform2ui(disp)) parameters
#define GET_Uniform2ui(disp) ((_glptr_Uniform2ui)(GET_by_offset((disp), _gloffset_Uniform2ui)))
#define SET_Uniform2ui(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLuint, GLuint) = func; \
   SET_by_offset(disp, _gloffset_Uniform2ui, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Uniform2uiv)(GLint, GLsizei, const GLuint *);
#define CALL_Uniform2uiv(disp, parameters) (* GET_Uniform2uiv(disp)) parameters
#define GET_Uniform2uiv(disp) ((_glptr_Uniform2uiv)(GET_by_offset((disp), _gloffset_Uniform2uiv)))
#define SET_Uniform2uiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLsizei, const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_Uniform2uiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Uniform3ui)(GLint, GLuint, GLuint, GLuint);
#define CALL_Uniform3ui(disp, parameters) (* GET_Uniform3ui(disp)) parameters
#define GET_Uniform3ui(disp) ((_glptr_Uniform3ui)(GET_by_offset((disp), _gloffset_Uniform3ui)))
#define SET_Uniform3ui(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLuint, GLuint, GLuint) = func; \
   SET_by_offset(disp, _gloffset_Uniform3ui, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Uniform3uiv)(GLint, GLsizei, const GLuint *);
#define CALL_Uniform3uiv(disp, parameters) (* GET_Uniform3uiv(disp)) parameters
#define GET_Uniform3uiv(disp) ((_glptr_Uniform3uiv)(GET_by_offset((disp), _gloffset_Uniform3uiv)))
#define SET_Uniform3uiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLsizei, const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_Uniform3uiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Uniform4ui)(GLint, GLuint, GLuint, GLuint, GLuint);
#define CALL_Uniform4ui(disp, parameters) (* GET_Uniform4ui(disp)) parameters
#define GET_Uniform4ui(disp) ((_glptr_Uniform4ui)(GET_by_offset((disp), _gloffset_Uniform4ui)))
#define SET_Uniform4ui(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLuint, GLuint, GLuint, GLuint) = func; \
   SET_by_offset(disp, _gloffset_Uniform4ui, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Uniform4uiv)(GLint, GLsizei, const GLuint *);
#define CALL_Uniform4uiv(disp, parameters) (* GET_Uniform4uiv(disp)) parameters
#define GET_Uniform4uiv(disp) ((_glptr_Uniform4uiv)(GET_by_offset((disp), _gloffset_Uniform4uiv)))
#define SET_Uniform4uiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLsizei, const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_Uniform4uiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribI1iv)(GLuint, const GLint *);
#define CALL_VertexAttribI1iv(disp, parameters) (* GET_VertexAttribI1iv(disp)) parameters
#define GET_VertexAttribI1iv(disp) ((_glptr_VertexAttribI1iv)(GET_by_offset((disp), _gloffset_VertexAttribI1iv)))
#define SET_VertexAttribI1iv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLint *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribI1iv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribI1uiv)(GLuint, const GLuint *);
#define CALL_VertexAttribI1uiv(disp, parameters) (* GET_VertexAttribI1uiv(disp)) parameters
#define GET_VertexAttribI1uiv(disp) ((_glptr_VertexAttribI1uiv)(GET_by_offset((disp), _gloffset_VertexAttribI1uiv)))
#define SET_VertexAttribI1uiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribI1uiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribI4bv)(GLuint, const GLbyte *);
#define CALL_VertexAttribI4bv(disp, parameters) (* GET_VertexAttribI4bv(disp)) parameters
#define GET_VertexAttribI4bv(disp) ((_glptr_VertexAttribI4bv)(GET_by_offset((disp), _gloffset_VertexAttribI4bv)))
#define SET_VertexAttribI4bv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLbyte *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribI4bv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribI4sv)(GLuint, const GLshort *);
#define CALL_VertexAttribI4sv(disp, parameters) (* GET_VertexAttribI4sv(disp)) parameters
#define GET_VertexAttribI4sv(disp) ((_glptr_VertexAttribI4sv)(GET_by_offset((disp), _gloffset_VertexAttribI4sv)))
#define SET_VertexAttribI4sv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLshort *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribI4sv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribI4ubv)(GLuint, const GLubyte *);
#define CALL_VertexAttribI4ubv(disp, parameters) (* GET_VertexAttribI4ubv(disp)) parameters
#define GET_VertexAttribI4ubv(disp) ((_glptr_VertexAttribI4ubv)(GET_by_offset((disp), _gloffset_VertexAttribI4ubv)))
#define SET_VertexAttribI4ubv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLubyte *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribI4ubv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribI4usv)(GLuint, const GLushort *);
#define CALL_VertexAttribI4usv(disp, parameters) (* GET_VertexAttribI4usv(disp)) parameters
#define GET_VertexAttribI4usv(disp) ((_glptr_VertexAttribI4usv)(GET_by_offset((disp), _gloffset_VertexAttribI4usv)))
#define SET_VertexAttribI4usv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLushort *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribI4usv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribIPointer)(GLuint, GLint, GLenum, GLsizei, const GLvoid *);
#define CALL_VertexAttribIPointer(disp, parameters) (* GET_VertexAttribIPointer(disp)) parameters
#define GET_VertexAttribIPointer(disp) ((_glptr_VertexAttribIPointer)(GET_by_offset((disp), _gloffset_VertexAttribIPointer)))
#define SET_VertexAttribIPointer(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLenum, GLsizei, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribIPointer, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_PrimitiveRestartIndex)(GLuint);
#define CALL_PrimitiveRestartIndex(disp, parameters) (* GET_PrimitiveRestartIndex(disp)) parameters
#define GET_PrimitiveRestartIndex(disp) ((_glptr_PrimitiveRestartIndex)(GET_by_offset((disp), _gloffset_PrimitiveRestartIndex)))
#define SET_PrimitiveRestartIndex(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint) = func; \
   SET_by_offset(disp, _gloffset_PrimitiveRestartIndex, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexBuffer)(GLenum, GLenum, GLuint);
#define CALL_TexBuffer(disp, parameters) (* GET_TexBuffer(disp)) parameters
#define GET_TexBuffer(disp) ((_glptr_TexBuffer)(GET_by_offset((disp), _gloffset_TexBuffer)))
#define SET_TexBuffer(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLuint) = func; \
   SET_by_offset(disp, _gloffset_TexBuffer, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_FramebufferTexture)(GLenum, GLenum, GLuint, GLint);
#define CALL_FramebufferTexture(disp, parameters) (* GET_FramebufferTexture(disp)) parameters
#define GET_FramebufferTexture(disp) ((_glptr_FramebufferTexture)(GET_by_offset((disp), _gloffset_FramebufferTexture)))
#define SET_FramebufferTexture(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLuint, GLint) = func; \
   SET_by_offset(disp, _gloffset_FramebufferTexture, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetBufferParameteri64v)(GLenum, GLenum, GLint64 *);
#define CALL_GetBufferParameteri64v(disp, parameters) (* GET_GetBufferParameteri64v(disp)) parameters
#define GET_GetBufferParameteri64v(disp) ((_glptr_GetBufferParameteri64v)(GET_by_offset((disp), _gloffset_GetBufferParameteri64v)))
#define SET_GetBufferParameteri64v(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLint64 *) = func; \
   SET_by_offset(disp, _gloffset_GetBufferParameteri64v, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetInteger64i_v)(GLenum, GLuint, GLint64 *);
#define CALL_GetInteger64i_v(disp, parameters) (* GET_GetInteger64i_v(disp)) parameters
#define GET_GetInteger64i_v(disp) ((_glptr_GetInteger64i_v)(GET_by_offset((disp), _gloffset_GetInteger64i_v)))
#define SET_GetInteger64i_v(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint, GLint64 *) = func; \
   SET_by_offset(disp, _gloffset_GetInteger64i_v, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribDivisor)(GLuint, GLuint);
#define CALL_VertexAttribDivisor(disp, parameters) (* GET_VertexAttribDivisor(disp)) parameters
#define GET_VertexAttribDivisor(disp) ((_glptr_VertexAttribDivisor)(GET_by_offset((disp), _gloffset_VertexAttribDivisor)))
#define SET_VertexAttribDivisor(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribDivisor, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MinSampleShading)(GLfloat);
#define CALL_MinSampleShading(disp, parameters) (* GET_MinSampleShading(disp)) parameters
#define GET_MinSampleShading(disp) ((_glptr_MinSampleShading)(GET_by_offset((disp), _gloffset_MinSampleShading)))
#define SET_MinSampleShading(disp, func) do { \
   void (GLAPIENTRYP fn)(GLfloat) = func; \
   SET_by_offset(disp, _gloffset_MinSampleShading, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MemoryBarrierByRegion)(GLbitfield);
#define CALL_MemoryBarrierByRegion(disp, parameters) (* GET_MemoryBarrierByRegion(disp)) parameters
#define GET_MemoryBarrierByRegion(disp) ((_glptr_MemoryBarrierByRegion)(GET_by_offset((disp), _gloffset_MemoryBarrierByRegion)))
#define SET_MemoryBarrierByRegion(disp, func) do { \
   void (GLAPIENTRYP fn)(GLbitfield) = func; \
   SET_by_offset(disp, _gloffset_MemoryBarrierByRegion, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_BindProgramARB)(GLenum, GLuint);
#define CALL_BindProgramARB(disp, parameters) (* GET_BindProgramARB(disp)) parameters
#define GET_BindProgramARB(disp) ((_glptr_BindProgramARB)(GET_by_offset((disp), _gloffset_BindProgramARB)))
#define SET_BindProgramARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint) = func; \
   SET_by_offset(disp, _gloffset_BindProgramARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DeleteProgramsARB)(GLsizei, const GLuint *);
#define CALL_DeleteProgramsARB(disp, parameters) (* GET_DeleteProgramsARB(disp)) parameters
#define GET_DeleteProgramsARB(disp) ((_glptr_DeleteProgramsARB)(GET_by_offset((disp), _gloffset_DeleteProgramsARB)))
#define SET_DeleteProgramsARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLsizei, const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_DeleteProgramsARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GenProgramsARB)(GLsizei, GLuint *);
#define CALL_GenProgramsARB(disp, parameters) (* GET_GenProgramsARB(disp)) parameters
#define GET_GenProgramsARB(disp) ((_glptr_GenProgramsARB)(GET_by_offset((disp), _gloffset_GenProgramsARB)))
#define SET_GenProgramsARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLsizei, GLuint *) = func; \
   SET_by_offset(disp, _gloffset_GenProgramsARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetProgramEnvParameterdvARB)(GLenum, GLuint, GLdouble *);
#define CALL_GetProgramEnvParameterdvARB(disp, parameters) (* GET_GetProgramEnvParameterdvARB(disp)) parameters
#define GET_GetProgramEnvParameterdvARB(disp) ((_glptr_GetProgramEnvParameterdvARB)(GET_by_offset((disp), _gloffset_GetProgramEnvParameterdvARB)))
#define SET_GetProgramEnvParameterdvARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint, GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_GetProgramEnvParameterdvARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetProgramEnvParameterfvARB)(GLenum, GLuint, GLfloat *);
#define CALL_GetProgramEnvParameterfvARB(disp, parameters) (* GET_GetProgramEnvParameterfvARB(disp)) parameters
#define GET_GetProgramEnvParameterfvARB(disp) ((_glptr_GetProgramEnvParameterfvARB)(GET_by_offset((disp), _gloffset_GetProgramEnvParameterfvARB)))
#define SET_GetProgramEnvParameterfvARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint, GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_GetProgramEnvParameterfvARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetProgramLocalParameterdvARB)(GLenum, GLuint, GLdouble *);
#define CALL_GetProgramLocalParameterdvARB(disp, parameters) (* GET_GetProgramLocalParameterdvARB(disp)) parameters
#define GET_GetProgramLocalParameterdvARB(disp) ((_glptr_GetProgramLocalParameterdvARB)(GET_by_offset((disp), _gloffset_GetProgramLocalParameterdvARB)))
#define SET_GetProgramLocalParameterdvARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint, GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_GetProgramLocalParameterdvARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetProgramLocalParameterfvARB)(GLenum, GLuint, GLfloat *);
#define CALL_GetProgramLocalParameterfvARB(disp, parameters) (* GET_GetProgramLocalParameterfvARB(disp)) parameters
#define GET_GetProgramLocalParameterfvARB(disp) ((_glptr_GetProgramLocalParameterfvARB)(GET_by_offset((disp), _gloffset_GetProgramLocalParameterfvARB)))
#define SET_GetProgramLocalParameterfvARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint, GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_GetProgramLocalParameterfvARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetProgramStringARB)(GLenum, GLenum, GLvoid *);
#define CALL_GetProgramStringARB(disp, parameters) (* GET_GetProgramStringARB(disp)) parameters
#define GET_GetProgramStringARB(disp) ((_glptr_GetProgramStringARB)(GET_by_offset((disp), _gloffset_GetProgramStringARB)))
#define SET_GetProgramStringARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_GetProgramStringARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetProgramivARB)(GLenum, GLenum, GLint *);
#define CALL_GetProgramivARB(disp, parameters) (* GET_GetProgramivARB(disp)) parameters
#define GET_GetProgramivARB(disp) ((_glptr_GetProgramivARB)(GET_by_offset((disp), _gloffset_GetProgramivARB)))
#define SET_GetProgramivARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetProgramivARB, fn); \
} while (0)

typedef GLboolean (GLAPIENTRYP _glptr_IsProgramARB)(GLuint);
#define CALL_IsProgramARB(disp, parameters) (* GET_IsProgramARB(disp)) parameters
#define GET_IsProgramARB(disp) ((_glptr_IsProgramARB)(GET_by_offset((disp), _gloffset_IsProgramARB)))
#define SET_IsProgramARB(disp, func) do { \
   GLboolean (GLAPIENTRYP fn)(GLuint) = func; \
   SET_by_offset(disp, _gloffset_IsProgramARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramEnvParameter4dARB)(GLenum, GLuint, GLdouble, GLdouble, GLdouble, GLdouble);
#define CALL_ProgramEnvParameter4dARB(disp, parameters) (* GET_ProgramEnvParameter4dARB(disp)) parameters
#define GET_ProgramEnvParameter4dARB(disp) ((_glptr_ProgramEnvParameter4dARB)(GET_by_offset((disp), _gloffset_ProgramEnvParameter4dARB)))
#define SET_ProgramEnvParameter4dARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint, GLdouble, GLdouble, GLdouble, GLdouble) = func; \
   SET_by_offset(disp, _gloffset_ProgramEnvParameter4dARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramEnvParameter4dvARB)(GLenum, GLuint, const GLdouble *);
#define CALL_ProgramEnvParameter4dvARB(disp, parameters) (* GET_ProgramEnvParameter4dvARB(disp)) parameters
#define GET_ProgramEnvParameter4dvARB(disp) ((_glptr_ProgramEnvParameter4dvARB)(GET_by_offset((disp), _gloffset_ProgramEnvParameter4dvARB)))
#define SET_ProgramEnvParameter4dvARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint, const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_ProgramEnvParameter4dvARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramEnvParameter4fARB)(GLenum, GLuint, GLfloat, GLfloat, GLfloat, GLfloat);
#define CALL_ProgramEnvParameter4fARB(disp, parameters) (* GET_ProgramEnvParameter4fARB(disp)) parameters
#define GET_ProgramEnvParameter4fARB(disp) ((_glptr_ProgramEnvParameter4fARB)(GET_by_offset((disp), _gloffset_ProgramEnvParameter4fARB)))
#define SET_ProgramEnvParameter4fARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint, GLfloat, GLfloat, GLfloat, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_ProgramEnvParameter4fARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramEnvParameter4fvARB)(GLenum, GLuint, const GLfloat *);
#define CALL_ProgramEnvParameter4fvARB(disp, parameters) (* GET_ProgramEnvParameter4fvARB(disp)) parameters
#define GET_ProgramEnvParameter4fvARB(disp) ((_glptr_ProgramEnvParameter4fvARB)(GET_by_offset((disp), _gloffset_ProgramEnvParameter4fvARB)))
#define SET_ProgramEnvParameter4fvARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_ProgramEnvParameter4fvARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramLocalParameter4dARB)(GLenum, GLuint, GLdouble, GLdouble, GLdouble, GLdouble);
#define CALL_ProgramLocalParameter4dARB(disp, parameters) (* GET_ProgramLocalParameter4dARB(disp)) parameters
#define GET_ProgramLocalParameter4dARB(disp) ((_glptr_ProgramLocalParameter4dARB)(GET_by_offset((disp), _gloffset_ProgramLocalParameter4dARB)))
#define SET_ProgramLocalParameter4dARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint, GLdouble, GLdouble, GLdouble, GLdouble) = func; \
   SET_by_offset(disp, _gloffset_ProgramLocalParameter4dARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramLocalParameter4dvARB)(GLenum, GLuint, const GLdouble *);
#define CALL_ProgramLocalParameter4dvARB(disp, parameters) (* GET_ProgramLocalParameter4dvARB(disp)) parameters
#define GET_ProgramLocalParameter4dvARB(disp) ((_glptr_ProgramLocalParameter4dvARB)(GET_by_offset((disp), _gloffset_ProgramLocalParameter4dvARB)))
#define SET_ProgramLocalParameter4dvARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint, const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_ProgramLocalParameter4dvARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramLocalParameter4fARB)(GLenum, GLuint, GLfloat, GLfloat, GLfloat, GLfloat);
#define CALL_ProgramLocalParameter4fARB(disp, parameters) (* GET_ProgramLocalParameter4fARB(disp)) parameters
#define GET_ProgramLocalParameter4fARB(disp) ((_glptr_ProgramLocalParameter4fARB)(GET_by_offset((disp), _gloffset_ProgramLocalParameter4fARB)))
#define SET_ProgramLocalParameter4fARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint, GLfloat, GLfloat, GLfloat, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_ProgramLocalParameter4fARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramLocalParameter4fvARB)(GLenum, GLuint, const GLfloat *);
#define CALL_ProgramLocalParameter4fvARB(disp, parameters) (* GET_ProgramLocalParameter4fvARB(disp)) parameters
#define GET_ProgramLocalParameter4fvARB(disp) ((_glptr_ProgramLocalParameter4fvARB)(GET_by_offset((disp), _gloffset_ProgramLocalParameter4fvARB)))
#define SET_ProgramLocalParameter4fvARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_ProgramLocalParameter4fvARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramStringARB)(GLenum, GLenum, GLsizei, const GLvoid *);
#define CALL_ProgramStringARB(disp, parameters) (* GET_ProgramStringARB(disp)) parameters
#define GET_ProgramStringARB(disp) ((_glptr_ProgramStringARB)(GET_by_offset((disp), _gloffset_ProgramStringARB)))
#define SET_ProgramStringARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLsizei, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_ProgramStringARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib1fARB)(GLuint, GLfloat);
#define CALL_VertexAttrib1fARB(disp, parameters) (* GET_VertexAttrib1fARB(disp)) parameters
#define GET_VertexAttrib1fARB(disp) ((_glptr_VertexAttrib1fARB)(GET_by_offset((disp), _gloffset_VertexAttrib1fARB)))
#define SET_VertexAttrib1fARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib1fARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib1fvARB)(GLuint, const GLfloat *);
#define CALL_VertexAttrib1fvARB(disp, parameters) (* GET_VertexAttrib1fvARB(disp)) parameters
#define GET_VertexAttrib1fvARB(disp) ((_glptr_VertexAttrib1fvARB)(GET_by_offset((disp), _gloffset_VertexAttrib1fvARB)))
#define SET_VertexAttrib1fvARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib1fvARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib2fARB)(GLuint, GLfloat, GLfloat);
#define CALL_VertexAttrib2fARB(disp, parameters) (* GET_VertexAttrib2fARB(disp)) parameters
#define GET_VertexAttrib2fARB(disp) ((_glptr_VertexAttrib2fARB)(GET_by_offset((disp), _gloffset_VertexAttrib2fARB)))
#define SET_VertexAttrib2fARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLfloat, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib2fARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib2fvARB)(GLuint, const GLfloat *);
#define CALL_VertexAttrib2fvARB(disp, parameters) (* GET_VertexAttrib2fvARB(disp)) parameters
#define GET_VertexAttrib2fvARB(disp) ((_glptr_VertexAttrib2fvARB)(GET_by_offset((disp), _gloffset_VertexAttrib2fvARB)))
#define SET_VertexAttrib2fvARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib2fvARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib3fARB)(GLuint, GLfloat, GLfloat, GLfloat);
#define CALL_VertexAttrib3fARB(disp, parameters) (* GET_VertexAttrib3fARB(disp)) parameters
#define GET_VertexAttrib3fARB(disp) ((_glptr_VertexAttrib3fARB)(GET_by_offset((disp), _gloffset_VertexAttrib3fARB)))
#define SET_VertexAttrib3fARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLfloat, GLfloat, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib3fARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib3fvARB)(GLuint, const GLfloat *);
#define CALL_VertexAttrib3fvARB(disp, parameters) (* GET_VertexAttrib3fvARB(disp)) parameters
#define GET_VertexAttrib3fvARB(disp) ((_glptr_VertexAttrib3fvARB)(GET_by_offset((disp), _gloffset_VertexAttrib3fvARB)))
#define SET_VertexAttrib3fvARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib3fvARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib4fARB)(GLuint, GLfloat, GLfloat, GLfloat, GLfloat);
#define CALL_VertexAttrib4fARB(disp, parameters) (* GET_VertexAttrib4fARB(disp)) parameters
#define GET_VertexAttrib4fARB(disp) ((_glptr_VertexAttrib4fARB)(GET_by_offset((disp), _gloffset_VertexAttrib4fARB)))
#define SET_VertexAttrib4fARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLfloat, GLfloat, GLfloat, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib4fARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib4fvARB)(GLuint, const GLfloat *);
#define CALL_VertexAttrib4fvARB(disp, parameters) (* GET_VertexAttrib4fvARB(disp)) parameters
#define GET_VertexAttrib4fvARB(disp) ((_glptr_VertexAttrib4fvARB)(GET_by_offset((disp), _gloffset_VertexAttrib4fvARB)))
#define SET_VertexAttrib4fvARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib4fvARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_AttachObjectARB)(GLhandleARB, GLhandleARB);
#define CALL_AttachObjectARB(disp, parameters) (* GET_AttachObjectARB(disp)) parameters
#define GET_AttachObjectARB(disp) ((_glptr_AttachObjectARB)(GET_by_offset((disp), _gloffset_AttachObjectARB)))
#define SET_AttachObjectARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLhandleARB, GLhandleARB) = func; \
   SET_by_offset(disp, _gloffset_AttachObjectARB, fn); \
} while (0)

typedef GLhandleARB (GLAPIENTRYP _glptr_CreateProgramObjectARB)(void);
#define CALL_CreateProgramObjectARB(disp, parameters) (* GET_CreateProgramObjectARB(disp)) parameters
#define GET_CreateProgramObjectARB(disp) ((_glptr_CreateProgramObjectARB)(GET_by_offset((disp), _gloffset_CreateProgramObjectARB)))
#define SET_CreateProgramObjectARB(disp, func) do { \
   GLhandleARB (GLAPIENTRYP fn)(void) = func; \
   SET_by_offset(disp, _gloffset_CreateProgramObjectARB, fn); \
} while (0)

typedef GLhandleARB (GLAPIENTRYP _glptr_CreateShaderObjectARB)(GLenum);
#define CALL_CreateShaderObjectARB(disp, parameters) (* GET_CreateShaderObjectARB(disp)) parameters
#define GET_CreateShaderObjectARB(disp) ((_glptr_CreateShaderObjectARB)(GET_by_offset((disp), _gloffset_CreateShaderObjectARB)))
#define SET_CreateShaderObjectARB(disp, func) do { \
   GLhandleARB (GLAPIENTRYP fn)(GLenum) = func; \
   SET_by_offset(disp, _gloffset_CreateShaderObjectARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DeleteObjectARB)(GLhandleARB);
#define CALL_DeleteObjectARB(disp, parameters) (* GET_DeleteObjectARB(disp)) parameters
#define GET_DeleteObjectARB(disp) ((_glptr_DeleteObjectARB)(GET_by_offset((disp), _gloffset_DeleteObjectARB)))
#define SET_DeleteObjectARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLhandleARB) = func; \
   SET_by_offset(disp, _gloffset_DeleteObjectARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DetachObjectARB)(GLhandleARB, GLhandleARB);
#define CALL_DetachObjectARB(disp, parameters) (* GET_DetachObjectARB(disp)) parameters
#define GET_DetachObjectARB(disp) ((_glptr_DetachObjectARB)(GET_by_offset((disp), _gloffset_DetachObjectARB)))
#define SET_DetachObjectARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLhandleARB, GLhandleARB) = func; \
   SET_by_offset(disp, _gloffset_DetachObjectARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetAttachedObjectsARB)(GLhandleARB, GLsizei, GLsizei *, GLhandleARB *);
#define CALL_GetAttachedObjectsARB(disp, parameters) (* GET_GetAttachedObjectsARB(disp)) parameters
#define GET_GetAttachedObjectsARB(disp) ((_glptr_GetAttachedObjectsARB)(GET_by_offset((disp), _gloffset_GetAttachedObjectsARB)))
#define SET_GetAttachedObjectsARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLhandleARB, GLsizei, GLsizei *, GLhandleARB *) = func; \
   SET_by_offset(disp, _gloffset_GetAttachedObjectsARB, fn); \
} while (0)

typedef GLhandleARB (GLAPIENTRYP _glptr_GetHandleARB)(GLenum);
#define CALL_GetHandleARB(disp, parameters) (* GET_GetHandleARB(disp)) parameters
#define GET_GetHandleARB(disp) ((_glptr_GetHandleARB)(GET_by_offset((disp), _gloffset_GetHandleARB)))
#define SET_GetHandleARB(disp, func) do { \
   GLhandleARB (GLAPIENTRYP fn)(GLenum) = func; \
   SET_by_offset(disp, _gloffset_GetHandleARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetInfoLogARB)(GLhandleARB, GLsizei, GLsizei *, GLcharARB *);
#define CALL_GetInfoLogARB(disp, parameters) (* GET_GetInfoLogARB(disp)) parameters
#define GET_GetInfoLogARB(disp) ((_glptr_GetInfoLogARB)(GET_by_offset((disp), _gloffset_GetInfoLogARB)))
#define SET_GetInfoLogARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLhandleARB, GLsizei, GLsizei *, GLcharARB *) = func; \
   SET_by_offset(disp, _gloffset_GetInfoLogARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetObjectParameterfvARB)(GLhandleARB, GLenum, GLfloat *);
#define CALL_GetObjectParameterfvARB(disp, parameters) (* GET_GetObjectParameterfvARB(disp)) parameters
#define GET_GetObjectParameterfvARB(disp) ((_glptr_GetObjectParameterfvARB)(GET_by_offset((disp), _gloffset_GetObjectParameterfvARB)))
#define SET_GetObjectParameterfvARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLhandleARB, GLenum, GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_GetObjectParameterfvARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetObjectParameterivARB)(GLhandleARB, GLenum, GLint *);
#define CALL_GetObjectParameterivARB(disp, parameters) (* GET_GetObjectParameterivARB(disp)) parameters
#define GET_GetObjectParameterivARB(disp) ((_glptr_GetObjectParameterivARB)(GET_by_offset((disp), _gloffset_GetObjectParameterivARB)))
#define SET_GetObjectParameterivARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLhandleARB, GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetObjectParameterivARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DrawArraysInstanced)(GLenum, GLint, GLsizei, GLsizei);
#define CALL_DrawArraysInstanced(disp, parameters) (* GET_DrawArraysInstanced(disp)) parameters
#define GET_DrawArraysInstanced(disp) ((_glptr_DrawArraysInstanced)(GET_by_offset((disp), _gloffset_DrawArraysInstanced)))
#define SET_DrawArraysInstanced(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLint, GLsizei, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_DrawArraysInstanced, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DrawElementsInstanced)(GLenum, GLsizei, GLenum, const GLvoid *, GLsizei);
#define CALL_DrawElementsInstanced(disp, parameters) (* GET_DrawElementsInstanced(disp)) parameters
#define GET_DrawElementsInstanced(disp) ((_glptr_DrawElementsInstanced)(GET_by_offset((disp), _gloffset_DrawElementsInstanced)))
#define SET_DrawElementsInstanced(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLsizei, GLenum, const GLvoid *, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_DrawElementsInstanced, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_BindFramebuffer)(GLenum, GLuint);
#define CALL_BindFramebuffer(disp, parameters) (* GET_BindFramebuffer(disp)) parameters
#define GET_BindFramebuffer(disp) ((_glptr_BindFramebuffer)(GET_by_offset((disp), _gloffset_BindFramebuffer)))
#define SET_BindFramebuffer(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint) = func; \
   SET_by_offset(disp, _gloffset_BindFramebuffer, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_BindRenderbuffer)(GLenum, GLuint);
#define CALL_BindRenderbuffer(disp, parameters) (* GET_BindRenderbuffer(disp)) parameters
#define GET_BindRenderbuffer(disp) ((_glptr_BindRenderbuffer)(GET_by_offset((disp), _gloffset_BindRenderbuffer)))
#define SET_BindRenderbuffer(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint) = func; \
   SET_by_offset(disp, _gloffset_BindRenderbuffer, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_BlitFramebuffer)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
#define CALL_BlitFramebuffer(disp, parameters) (* GET_BlitFramebuffer(disp)) parameters
#define GET_BlitFramebuffer(disp) ((_glptr_BlitFramebuffer)(GET_by_offset((disp), _gloffset_BlitFramebuffer)))
#define SET_BlitFramebuffer(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum) = func; \
   SET_by_offset(disp, _gloffset_BlitFramebuffer, fn); \
} while (0)

typedef GLenum (GLAPIENTRYP _glptr_CheckFramebufferStatus)(GLenum);
#define CALL_CheckFramebufferStatus(disp, parameters) (* GET_CheckFramebufferStatus(disp)) parameters
#define GET_CheckFramebufferStatus(disp) ((_glptr_CheckFramebufferStatus)(GET_by_offset((disp), _gloffset_CheckFramebufferStatus)))
#define SET_CheckFramebufferStatus(disp, func) do { \
   GLenum (GLAPIENTRYP fn)(GLenum) = func; \
   SET_by_offset(disp, _gloffset_CheckFramebufferStatus, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DeleteFramebuffers)(GLsizei, const GLuint *);
#define CALL_DeleteFramebuffers(disp, parameters) (* GET_DeleteFramebuffers(disp)) parameters
#define GET_DeleteFramebuffers(disp) ((_glptr_DeleteFramebuffers)(GET_by_offset((disp), _gloffset_DeleteFramebuffers)))
#define SET_DeleteFramebuffers(disp, func) do { \
   void (GLAPIENTRYP fn)(GLsizei, const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_DeleteFramebuffers, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DeleteRenderbuffers)(GLsizei, const GLuint *);
#define CALL_DeleteRenderbuffers(disp, parameters) (* GET_DeleteRenderbuffers(disp)) parameters
#define GET_DeleteRenderbuffers(disp) ((_glptr_DeleteRenderbuffers)(GET_by_offset((disp), _gloffset_DeleteRenderbuffers)))
#define SET_DeleteRenderbuffers(disp, func) do { \
   void (GLAPIENTRYP fn)(GLsizei, const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_DeleteRenderbuffers, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_FramebufferRenderbuffer)(GLenum, GLenum, GLenum, GLuint);
#define CALL_FramebufferRenderbuffer(disp, parameters) (* GET_FramebufferRenderbuffer(disp)) parameters
#define GET_FramebufferRenderbuffer(disp) ((_glptr_FramebufferRenderbuffer)(GET_by_offset((disp), _gloffset_FramebufferRenderbuffer)))
#define SET_FramebufferRenderbuffer(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLenum, GLuint) = func; \
   SET_by_offset(disp, _gloffset_FramebufferRenderbuffer, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_FramebufferTexture1D)(GLenum, GLenum, GLenum, GLuint, GLint);
#define CALL_FramebufferTexture1D(disp, parameters) (* GET_FramebufferTexture1D(disp)) parameters
#define GET_FramebufferTexture1D(disp) ((_glptr_FramebufferTexture1D)(GET_by_offset((disp), _gloffset_FramebufferTexture1D)))
#define SET_FramebufferTexture1D(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLenum, GLuint, GLint) = func; \
   SET_by_offset(disp, _gloffset_FramebufferTexture1D, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_FramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
#define CALL_FramebufferTexture2D(disp, parameters) (* GET_FramebufferTexture2D(disp)) parameters
#define GET_FramebufferTexture2D(disp) ((_glptr_FramebufferTexture2D)(GET_by_offset((disp), _gloffset_FramebufferTexture2D)))
#define SET_FramebufferTexture2D(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLenum, GLuint, GLint) = func; \
   SET_by_offset(disp, _gloffset_FramebufferTexture2D, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_FramebufferTexture3D)(GLenum, GLenum, GLenum, GLuint, GLint, GLint);
#define CALL_FramebufferTexture3D(disp, parameters) (* GET_FramebufferTexture3D(disp)) parameters
#define GET_FramebufferTexture3D(disp) ((_glptr_FramebufferTexture3D)(GET_by_offset((disp), _gloffset_FramebufferTexture3D)))
#define SET_FramebufferTexture3D(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLenum, GLuint, GLint, GLint) = func; \
   SET_by_offset(disp, _gloffset_FramebufferTexture3D, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_FramebufferTextureLayer)(GLenum, GLenum, GLuint, GLint, GLint);
#define CALL_FramebufferTextureLayer(disp, parameters) (* GET_FramebufferTextureLayer(disp)) parameters
#define GET_FramebufferTextureLayer(disp) ((_glptr_FramebufferTextureLayer)(GET_by_offset((disp), _gloffset_FramebufferTextureLayer)))
#define SET_FramebufferTextureLayer(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLuint, GLint, GLint) = func; \
   SET_by_offset(disp, _gloffset_FramebufferTextureLayer, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GenFramebuffers)(GLsizei, GLuint *);
#define CALL_GenFramebuffers(disp, parameters) (* GET_GenFramebuffers(disp)) parameters
#define GET_GenFramebuffers(disp) ((_glptr_GenFramebuffers)(GET_by_offset((disp), _gloffset_GenFramebuffers)))
#define SET_GenFramebuffers(disp, func) do { \
   void (GLAPIENTRYP fn)(GLsizei, GLuint *) = func; \
   SET_by_offset(disp, _gloffset_GenFramebuffers, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GenRenderbuffers)(GLsizei, GLuint *);
#define CALL_GenRenderbuffers(disp, parameters) (* GET_GenRenderbuffers(disp)) parameters
#define GET_GenRenderbuffers(disp) ((_glptr_GenRenderbuffers)(GET_by_offset((disp), _gloffset_GenRenderbuffers)))
#define SET_GenRenderbuffers(disp, func) do { \
   void (GLAPIENTRYP fn)(GLsizei, GLuint *) = func; \
   SET_by_offset(disp, _gloffset_GenRenderbuffers, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GenerateMipmap)(GLenum);
#define CALL_GenerateMipmap(disp, parameters) (* GET_GenerateMipmap(disp)) parameters
#define GET_GenerateMipmap(disp) ((_glptr_GenerateMipmap)(GET_by_offset((disp), _gloffset_GenerateMipmap)))
#define SET_GenerateMipmap(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum) = func; \
   SET_by_offset(disp, _gloffset_GenerateMipmap, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetFramebufferAttachmentParameteriv)(GLenum, GLenum, GLenum, GLint *);
#define CALL_GetFramebufferAttachmentParameteriv(disp, parameters) (* GET_GetFramebufferAttachmentParameteriv(disp)) parameters
#define GET_GetFramebufferAttachmentParameteriv(disp) ((_glptr_GetFramebufferAttachmentParameteriv)(GET_by_offset((disp), _gloffset_GetFramebufferAttachmentParameteriv)))
#define SET_GetFramebufferAttachmentParameteriv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetFramebufferAttachmentParameteriv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetRenderbufferParameteriv)(GLenum, GLenum, GLint *);
#define CALL_GetRenderbufferParameteriv(disp, parameters) (* GET_GetRenderbufferParameteriv(disp)) parameters
#define GET_GetRenderbufferParameteriv(disp) ((_glptr_GetRenderbufferParameteriv)(GET_by_offset((disp), _gloffset_GetRenderbufferParameteriv)))
#define SET_GetRenderbufferParameteriv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetRenderbufferParameteriv, fn); \
} while (0)

typedef GLboolean (GLAPIENTRYP _glptr_IsFramebuffer)(GLuint);
#define CALL_IsFramebuffer(disp, parameters) (* GET_IsFramebuffer(disp)) parameters
#define GET_IsFramebuffer(disp) ((_glptr_IsFramebuffer)(GET_by_offset((disp), _gloffset_IsFramebuffer)))
#define SET_IsFramebuffer(disp, func) do { \
   GLboolean (GLAPIENTRYP fn)(GLuint) = func; \
   SET_by_offset(disp, _gloffset_IsFramebuffer, fn); \
} while (0)

typedef GLboolean (GLAPIENTRYP _glptr_IsRenderbuffer)(GLuint);
#define CALL_IsRenderbuffer(disp, parameters) (* GET_IsRenderbuffer(disp)) parameters
#define GET_IsRenderbuffer(disp) ((_glptr_IsRenderbuffer)(GET_by_offset((disp), _gloffset_IsRenderbuffer)))
#define SET_IsRenderbuffer(disp, func) do { \
   GLboolean (GLAPIENTRYP fn)(GLuint) = func; \
   SET_by_offset(disp, _gloffset_IsRenderbuffer, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_RenderbufferStorage)(GLenum, GLenum, GLsizei, GLsizei);
#define CALL_RenderbufferStorage(disp, parameters) (* GET_RenderbufferStorage(disp)) parameters
#define GET_RenderbufferStorage(disp) ((_glptr_RenderbufferStorage)(GET_by_offset((disp), _gloffset_RenderbufferStorage)))
#define SET_RenderbufferStorage(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLsizei, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_RenderbufferStorage, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_RenderbufferStorageMultisample)(GLenum, GLsizei, GLenum, GLsizei, GLsizei);
#define CALL_RenderbufferStorageMultisample(disp, parameters) (* GET_RenderbufferStorageMultisample(disp)) parameters
#define GET_RenderbufferStorageMultisample(disp) ((_glptr_RenderbufferStorageMultisample)(GET_by_offset((disp), _gloffset_RenderbufferStorageMultisample)))
#define SET_RenderbufferStorageMultisample(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLsizei, GLenum, GLsizei, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_RenderbufferStorageMultisample, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_FlushMappedBufferRange)(GLenum, GLintptr, GLsizeiptr);
#define CALL_FlushMappedBufferRange(disp, parameters) (* GET_FlushMappedBufferRange(disp)) parameters
#define GET_FlushMappedBufferRange(disp) ((_glptr_FlushMappedBufferRange)(GET_by_offset((disp), _gloffset_FlushMappedBufferRange)))
#define SET_FlushMappedBufferRange(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLintptr, GLsizeiptr) = func; \
   SET_by_offset(disp, _gloffset_FlushMappedBufferRange, fn); \
} while (0)

typedef GLvoid * (GLAPIENTRYP _glptr_MapBufferRange)(GLenum, GLintptr, GLsizeiptr, GLbitfield);
#define CALL_MapBufferRange(disp, parameters) (* GET_MapBufferRange(disp)) parameters
#define GET_MapBufferRange(disp) ((_glptr_MapBufferRange)(GET_by_offset((disp), _gloffset_MapBufferRange)))
#define SET_MapBufferRange(disp, func) do { \
   GLvoid * (GLAPIENTRYP fn)(GLenum, GLintptr, GLsizeiptr, GLbitfield) = func; \
   SET_by_offset(disp, _gloffset_MapBufferRange, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_BindVertexArray)(GLuint);
#define CALL_BindVertexArray(disp, parameters) (* GET_BindVertexArray(disp)) parameters
#define GET_BindVertexArray(disp) ((_glptr_BindVertexArray)(GET_by_offset((disp), _gloffset_BindVertexArray)))
#define SET_BindVertexArray(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint) = func; \
   SET_by_offset(disp, _gloffset_BindVertexArray, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DeleteVertexArrays)(GLsizei, const GLuint *);
#define CALL_DeleteVertexArrays(disp, parameters) (* GET_DeleteVertexArrays(disp)) parameters
#define GET_DeleteVertexArrays(disp) ((_glptr_DeleteVertexArrays)(GET_by_offset((disp), _gloffset_DeleteVertexArrays)))
#define SET_DeleteVertexArrays(disp, func) do { \
   void (GLAPIENTRYP fn)(GLsizei, const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_DeleteVertexArrays, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GenVertexArrays)(GLsizei, GLuint *);
#define CALL_GenVertexArrays(disp, parameters) (* GET_GenVertexArrays(disp)) parameters
#define GET_GenVertexArrays(disp) ((_glptr_GenVertexArrays)(GET_by_offset((disp), _gloffset_GenVertexArrays)))
#define SET_GenVertexArrays(disp, func) do { \
   void (GLAPIENTRYP fn)(GLsizei, GLuint *) = func; \
   SET_by_offset(disp, _gloffset_GenVertexArrays, fn); \
} while (0)

typedef GLboolean (GLAPIENTRYP _glptr_IsVertexArray)(GLuint);
#define CALL_IsVertexArray(disp, parameters) (* GET_IsVertexArray(disp)) parameters
#define GET_IsVertexArray(disp) ((_glptr_IsVertexArray)(GET_by_offset((disp), _gloffset_IsVertexArray)))
#define SET_IsVertexArray(disp, func) do { \
   GLboolean (GLAPIENTRYP fn)(GLuint) = func; \
   SET_by_offset(disp, _gloffset_IsVertexArray, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetActiveUniformBlockName)(GLuint, GLuint, GLsizei, GLsizei *, GLchar *);
#define CALL_GetActiveUniformBlockName(disp, parameters) (* GET_GetActiveUniformBlockName(disp)) parameters
#define GET_GetActiveUniformBlockName(disp) ((_glptr_GetActiveUniformBlockName)(GET_by_offset((disp), _gloffset_GetActiveUniformBlockName)))
#define SET_GetActiveUniformBlockName(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLsizei, GLsizei *, GLchar *) = func; \
   SET_by_offset(disp, _gloffset_GetActiveUniformBlockName, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetActiveUniformBlockiv)(GLuint, GLuint, GLenum, GLint *);
#define CALL_GetActiveUniformBlockiv(disp, parameters) (* GET_GetActiveUniformBlockiv(disp)) parameters
#define GET_GetActiveUniformBlockiv(disp) ((_glptr_GetActiveUniformBlockiv)(GET_by_offset((disp), _gloffset_GetActiveUniformBlockiv)))
#define SET_GetActiveUniformBlockiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetActiveUniformBlockiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetActiveUniformName)(GLuint, GLuint, GLsizei, GLsizei *, GLchar *);
#define CALL_GetActiveUniformName(disp, parameters) (* GET_GetActiveUniformName(disp)) parameters
#define GET_GetActiveUniformName(disp) ((_glptr_GetActiveUniformName)(GET_by_offset((disp), _gloffset_GetActiveUniformName)))
#define SET_GetActiveUniformName(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLsizei, GLsizei *, GLchar *) = func; \
   SET_by_offset(disp, _gloffset_GetActiveUniformName, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetActiveUniformsiv)(GLuint, GLsizei, const GLuint *, GLenum, GLint *);
#define CALL_GetActiveUniformsiv(disp, parameters) (* GET_GetActiveUniformsiv(disp)) parameters
#define GET_GetActiveUniformsiv(disp) ((_glptr_GetActiveUniformsiv)(GET_by_offset((disp), _gloffset_GetActiveUniformsiv)))
#define SET_GetActiveUniformsiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizei, const GLuint *, GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetActiveUniformsiv, fn); \
} while (0)

typedef GLuint (GLAPIENTRYP _glptr_GetUniformBlockIndex)(GLuint, const GLchar *);
#define CALL_GetUniformBlockIndex(disp, parameters) (* GET_GetUniformBlockIndex(disp)) parameters
#define GET_GetUniformBlockIndex(disp) ((_glptr_GetUniformBlockIndex)(GET_by_offset((disp), _gloffset_GetUniformBlockIndex)))
#define SET_GetUniformBlockIndex(disp, func) do { \
   GLuint (GLAPIENTRYP fn)(GLuint, const GLchar *) = func; \
   SET_by_offset(disp, _gloffset_GetUniformBlockIndex, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetUniformIndices)(GLuint, GLsizei, const GLchar * const *, GLuint *);
#define CALL_GetUniformIndices(disp, parameters) (* GET_GetUniformIndices(disp)) parameters
#define GET_GetUniformIndices(disp) ((_glptr_GetUniformIndices)(GET_by_offset((disp), _gloffset_GetUniformIndices)))
#define SET_GetUniformIndices(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizei, const GLchar * const *, GLuint *) = func; \
   SET_by_offset(disp, _gloffset_GetUniformIndices, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_UniformBlockBinding)(GLuint, GLuint, GLuint);
#define CALL_UniformBlockBinding(disp, parameters) (* GET_UniformBlockBinding(disp)) parameters
#define GET_UniformBlockBinding(disp) ((_glptr_UniformBlockBinding)(GET_by_offset((disp), _gloffset_UniformBlockBinding)))
#define SET_UniformBlockBinding(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLuint) = func; \
   SET_by_offset(disp, _gloffset_UniformBlockBinding, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CopyBufferSubData)(GLenum, GLenum, GLintptr, GLintptr, GLsizeiptr);
#define CALL_CopyBufferSubData(disp, parameters) (* GET_CopyBufferSubData(disp)) parameters
#define GET_CopyBufferSubData(disp) ((_glptr_CopyBufferSubData)(GET_by_offset((disp), _gloffset_CopyBufferSubData)))
#define SET_CopyBufferSubData(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLintptr, GLintptr, GLsizeiptr) = func; \
   SET_by_offset(disp, _gloffset_CopyBufferSubData, fn); \
} while (0)

typedef GLenum (GLAPIENTRYP _glptr_ClientWaitSync)(GLsync, GLbitfield, GLuint64);
#define CALL_ClientWaitSync(disp, parameters) (* GET_ClientWaitSync(disp)) parameters
#define GET_ClientWaitSync(disp) ((_glptr_ClientWaitSync)(GET_by_offset((disp), _gloffset_ClientWaitSync)))
#define SET_ClientWaitSync(disp, func) do { \
   GLenum (GLAPIENTRYP fn)(GLsync, GLbitfield, GLuint64) = func; \
   SET_by_offset(disp, _gloffset_ClientWaitSync, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DeleteSync)(GLsync);
#define CALL_DeleteSync(disp, parameters) (* GET_DeleteSync(disp)) parameters
#define GET_DeleteSync(disp) ((_glptr_DeleteSync)(GET_by_offset((disp), _gloffset_DeleteSync)))
#define SET_DeleteSync(disp, func) do { \
   void (GLAPIENTRYP fn)(GLsync) = func; \
   SET_by_offset(disp, _gloffset_DeleteSync, fn); \
} while (0)

typedef GLsync (GLAPIENTRYP _glptr_FenceSync)(GLenum, GLbitfield);
#define CALL_FenceSync(disp, parameters) (* GET_FenceSync(disp)) parameters
#define GET_FenceSync(disp) ((_glptr_FenceSync)(GET_by_offset((disp), _gloffset_FenceSync)))
#define SET_FenceSync(disp, func) do { \
   GLsync (GLAPIENTRYP fn)(GLenum, GLbitfield) = func; \
   SET_by_offset(disp, _gloffset_FenceSync, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetInteger64v)(GLenum, GLint64 *);
#define CALL_GetInteger64v(disp, parameters) (* GET_GetInteger64v(disp)) parameters
#define GET_GetInteger64v(disp) ((_glptr_GetInteger64v)(GET_by_offset((disp), _gloffset_GetInteger64v)))
#define SET_GetInteger64v(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLint64 *) = func; \
   SET_by_offset(disp, _gloffset_GetInteger64v, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetSynciv)(GLsync, GLenum, GLsizei, GLsizei *, GLint *);
#define CALL_GetSynciv(disp, parameters) (* GET_GetSynciv(disp)) parameters
#define GET_GetSynciv(disp) ((_glptr_GetSynciv)(GET_by_offset((disp), _gloffset_GetSynciv)))
#define SET_GetSynciv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLsync, GLenum, GLsizei, GLsizei *, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetSynciv, fn); \
} while (0)

typedef GLboolean (GLAPIENTRYP _glptr_IsSync)(GLsync);
#define CALL_IsSync(disp, parameters) (* GET_IsSync(disp)) parameters
#define GET_IsSync(disp) ((_glptr_IsSync)(GET_by_offset((disp), _gloffset_IsSync)))
#define SET_IsSync(disp, func) do { \
   GLboolean (GLAPIENTRYP fn)(GLsync) = func; \
   SET_by_offset(disp, _gloffset_IsSync, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_WaitSync)(GLsync, GLbitfield, GLuint64);
#define CALL_WaitSync(disp, parameters) (* GET_WaitSync(disp)) parameters
#define GET_WaitSync(disp) ((_glptr_WaitSync)(GET_by_offset((disp), _gloffset_WaitSync)))
#define SET_WaitSync(disp, func) do { \
   void (GLAPIENTRYP fn)(GLsync, GLbitfield, GLuint64) = func; \
   SET_by_offset(disp, _gloffset_WaitSync, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DrawElementsBaseVertex)(GLenum, GLsizei, GLenum, const GLvoid *, GLint);
#define CALL_DrawElementsBaseVertex(disp, parameters) (* GET_DrawElementsBaseVertex(disp)) parameters
#define GET_DrawElementsBaseVertex(disp) ((_glptr_DrawElementsBaseVertex)(GET_by_offset((disp), _gloffset_DrawElementsBaseVertex)))
#define SET_DrawElementsBaseVertex(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLsizei, GLenum, const GLvoid *, GLint) = func; \
   SET_by_offset(disp, _gloffset_DrawElementsBaseVertex, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DrawElementsInstancedBaseVertex)(GLenum, GLsizei, GLenum, const GLvoid *, GLsizei, GLint);
#define CALL_DrawElementsInstancedBaseVertex(disp, parameters) (* GET_DrawElementsInstancedBaseVertex(disp)) parameters
#define GET_DrawElementsInstancedBaseVertex(disp) ((_glptr_DrawElementsInstancedBaseVertex)(GET_by_offset((disp), _gloffset_DrawElementsInstancedBaseVertex)))
#define SET_DrawElementsInstancedBaseVertex(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLsizei, GLenum, const GLvoid *, GLsizei, GLint) = func; \
   SET_by_offset(disp, _gloffset_DrawElementsInstancedBaseVertex, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DrawRangeElementsBaseVertex)(GLenum, GLuint, GLuint, GLsizei, GLenum, const GLvoid *, GLint);
#define CALL_DrawRangeElementsBaseVertex(disp, parameters) (* GET_DrawRangeElementsBaseVertex(disp)) parameters
#define GET_DrawRangeElementsBaseVertex(disp) ((_glptr_DrawRangeElementsBaseVertex)(GET_by_offset((disp), _gloffset_DrawRangeElementsBaseVertex)))
#define SET_DrawRangeElementsBaseVertex(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint, GLuint, GLsizei, GLenum, const GLvoid *, GLint) = func; \
   SET_by_offset(disp, _gloffset_DrawRangeElementsBaseVertex, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiDrawElementsBaseVertex)(GLenum, const GLsizei *, GLenum, const GLvoid * const *, GLsizei, const GLint *);
#define CALL_MultiDrawElementsBaseVertex(disp, parameters) (* GET_MultiDrawElementsBaseVertex(disp)) parameters
#define GET_MultiDrawElementsBaseVertex(disp) ((_glptr_MultiDrawElementsBaseVertex)(GET_by_offset((disp), _gloffset_MultiDrawElementsBaseVertex)))
#define SET_MultiDrawElementsBaseVertex(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, const GLsizei *, GLenum, const GLvoid * const *, GLsizei, const GLint *) = func; \
   SET_by_offset(disp, _gloffset_MultiDrawElementsBaseVertex, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProvokingVertex)(GLenum);
#define CALL_ProvokingVertex(disp, parameters) (* GET_ProvokingVertex(disp)) parameters
#define GET_ProvokingVertex(disp) ((_glptr_ProvokingVertex)(GET_by_offset((disp), _gloffset_ProvokingVertex)))
#define SET_ProvokingVertex(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum) = func; \
   SET_by_offset(disp, _gloffset_ProvokingVertex, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetMultisamplefv)(GLenum, GLuint, GLfloat *);
#define CALL_GetMultisamplefv(disp, parameters) (* GET_GetMultisamplefv(disp)) parameters
#define GET_GetMultisamplefv(disp) ((_glptr_GetMultisamplefv)(GET_by_offset((disp), _gloffset_GetMultisamplefv)))
#define SET_GetMultisamplefv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint, GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_GetMultisamplefv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_SampleMaski)(GLuint, GLbitfield);
#define CALL_SampleMaski(disp, parameters) (* GET_SampleMaski(disp)) parameters
#define GET_SampleMaski(disp) ((_glptr_SampleMaski)(GET_by_offset((disp), _gloffset_SampleMaski)))
#define SET_SampleMaski(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLbitfield) = func; \
   SET_by_offset(disp, _gloffset_SampleMaski, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexImage2DMultisample)(GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLboolean);
#define CALL_TexImage2DMultisample(disp, parameters) (* GET_TexImage2DMultisample(disp)) parameters
#define GET_TexImage2DMultisample(disp) ((_glptr_TexImage2DMultisample)(GET_by_offset((disp), _gloffset_TexImage2DMultisample)))
#define SET_TexImage2DMultisample(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLboolean) = func; \
   SET_by_offset(disp, _gloffset_TexImage2DMultisample, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexImage3DMultisample)(GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLsizei, GLboolean);
#define CALL_TexImage3DMultisample(disp, parameters) (* GET_TexImage3DMultisample(disp)) parameters
#define GET_TexImage3DMultisample(disp) ((_glptr_TexImage3DMultisample)(GET_by_offset((disp), _gloffset_TexImage3DMultisample)))
#define SET_TexImage3DMultisample(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLsizei, GLboolean) = func; \
   SET_by_offset(disp, _gloffset_TexImage3DMultisample, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_BlendEquationSeparateiARB)(GLuint, GLenum, GLenum);
#define CALL_BlendEquationSeparateiARB(disp, parameters) (* GET_BlendEquationSeparateiARB(disp)) parameters
#define GET_BlendEquationSeparateiARB(disp) ((_glptr_BlendEquationSeparateiARB)(GET_by_offset((disp), _gloffset_BlendEquationSeparateiARB)))
#define SET_BlendEquationSeparateiARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLenum) = func; \
   SET_by_offset(disp, _gloffset_BlendEquationSeparateiARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_BlendEquationiARB)(GLuint, GLenum);
#define CALL_BlendEquationiARB(disp, parameters) (* GET_BlendEquationiARB(disp)) parameters
#define GET_BlendEquationiARB(disp) ((_glptr_BlendEquationiARB)(GET_by_offset((disp), _gloffset_BlendEquationiARB)))
#define SET_BlendEquationiARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum) = func; \
   SET_by_offset(disp, _gloffset_BlendEquationiARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_BlendFuncSeparateiARB)(GLuint, GLenum, GLenum, GLenum, GLenum);
#define CALL_BlendFuncSeparateiARB(disp, parameters) (* GET_BlendFuncSeparateiARB(disp)) parameters
#define GET_BlendFuncSeparateiARB(disp) ((_glptr_BlendFuncSeparateiARB)(GET_by_offset((disp), _gloffset_BlendFuncSeparateiARB)))
#define SET_BlendFuncSeparateiARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLenum, GLenum, GLenum) = func; \
   SET_by_offset(disp, _gloffset_BlendFuncSeparateiARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_BlendFunciARB)(GLuint, GLenum, GLenum);
#define CALL_BlendFunciARB(disp, parameters) (* GET_BlendFunciARB(disp)) parameters
#define GET_BlendFunciARB(disp) ((_glptr_BlendFunciARB)(GET_by_offset((disp), _gloffset_BlendFunciARB)))
#define SET_BlendFunciARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLenum) = func; \
   SET_by_offset(disp, _gloffset_BlendFunciARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_BindFragDataLocationIndexed)(GLuint, GLuint, GLuint, const GLchar *);
#define CALL_BindFragDataLocationIndexed(disp, parameters) (* GET_BindFragDataLocationIndexed(disp)) parameters
#define GET_BindFragDataLocationIndexed(disp) ((_glptr_BindFragDataLocationIndexed)(GET_by_offset((disp), _gloffset_BindFragDataLocationIndexed)))
#define SET_BindFragDataLocationIndexed(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLuint, const GLchar *) = func; \
   SET_by_offset(disp, _gloffset_BindFragDataLocationIndexed, fn); \
} while (0)

typedef GLint (GLAPIENTRYP _glptr_GetFragDataIndex)(GLuint, const GLchar *);
#define CALL_GetFragDataIndex(disp, parameters) (* GET_GetFragDataIndex(disp)) parameters
#define GET_GetFragDataIndex(disp) ((_glptr_GetFragDataIndex)(GET_by_offset((disp), _gloffset_GetFragDataIndex)))
#define SET_GetFragDataIndex(disp, func) do { \
   GLint (GLAPIENTRYP fn)(GLuint, const GLchar *) = func; \
   SET_by_offset(disp, _gloffset_GetFragDataIndex, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_BindSampler)(GLuint, GLuint);
#define CALL_BindSampler(disp, parameters) (* GET_BindSampler(disp)) parameters
#define GET_BindSampler(disp) ((_glptr_BindSampler)(GET_by_offset((disp), _gloffset_BindSampler)))
#define SET_BindSampler(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint) = func; \
   SET_by_offset(disp, _gloffset_BindSampler, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DeleteSamplers)(GLsizei, const GLuint *);
#define CALL_DeleteSamplers(disp, parameters) (* GET_DeleteSamplers(disp)) parameters
#define GET_DeleteSamplers(disp) ((_glptr_DeleteSamplers)(GET_by_offset((disp), _gloffset_DeleteSamplers)))
#define SET_DeleteSamplers(disp, func) do { \
   void (GLAPIENTRYP fn)(GLsizei, const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_DeleteSamplers, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GenSamplers)(GLsizei, GLuint *);
#define CALL_GenSamplers(disp, parameters) (* GET_GenSamplers(disp)) parameters
#define GET_GenSamplers(disp) ((_glptr_GenSamplers)(GET_by_offset((disp), _gloffset_GenSamplers)))
#define SET_GenSamplers(disp, func) do { \
   void (GLAPIENTRYP fn)(GLsizei, GLuint *) = func; \
   SET_by_offset(disp, _gloffset_GenSamplers, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetSamplerParameterIiv)(GLuint, GLenum, GLint *);
#define CALL_GetSamplerParameterIiv(disp, parameters) (* GET_GetSamplerParameterIiv(disp)) parameters
#define GET_GetSamplerParameterIiv(disp) ((_glptr_GetSamplerParameterIiv)(GET_by_offset((disp), _gloffset_GetSamplerParameterIiv)))
#define SET_GetSamplerParameterIiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetSamplerParameterIiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetSamplerParameterIuiv)(GLuint, GLenum, GLuint *);
#define CALL_GetSamplerParameterIuiv(disp, parameters) (* GET_GetSamplerParameterIuiv(disp)) parameters
#define GET_GetSamplerParameterIuiv(disp) ((_glptr_GetSamplerParameterIuiv)(GET_by_offset((disp), _gloffset_GetSamplerParameterIuiv)))
#define SET_GetSamplerParameterIuiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLuint *) = func; \
   SET_by_offset(disp, _gloffset_GetSamplerParameterIuiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetSamplerParameterfv)(GLuint, GLenum, GLfloat *);
#define CALL_GetSamplerParameterfv(disp, parameters) (* GET_GetSamplerParameterfv(disp)) parameters
#define GET_GetSamplerParameterfv(disp) ((_glptr_GetSamplerParameterfv)(GET_by_offset((disp), _gloffset_GetSamplerParameterfv)))
#define SET_GetSamplerParameterfv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_GetSamplerParameterfv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetSamplerParameteriv)(GLuint, GLenum, GLint *);
#define CALL_GetSamplerParameteriv(disp, parameters) (* GET_GetSamplerParameteriv(disp)) parameters
#define GET_GetSamplerParameteriv(disp) ((_glptr_GetSamplerParameteriv)(GET_by_offset((disp), _gloffset_GetSamplerParameteriv)))
#define SET_GetSamplerParameteriv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetSamplerParameteriv, fn); \
} while (0)

typedef GLboolean (GLAPIENTRYP _glptr_IsSampler)(GLuint);
#define CALL_IsSampler(disp, parameters) (* GET_IsSampler(disp)) parameters
#define GET_IsSampler(disp) ((_glptr_IsSampler)(GET_by_offset((disp), _gloffset_IsSampler)))
#define SET_IsSampler(disp, func) do { \
   GLboolean (GLAPIENTRYP fn)(GLuint) = func; \
   SET_by_offset(disp, _gloffset_IsSampler, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_SamplerParameterIiv)(GLuint, GLenum, const GLint *);
#define CALL_SamplerParameterIiv(disp, parameters) (* GET_SamplerParameterIiv(disp)) parameters
#define GET_SamplerParameterIiv(disp) ((_glptr_SamplerParameterIiv)(GET_by_offset((disp), _gloffset_SamplerParameterIiv)))
#define SET_SamplerParameterIiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, const GLint *) = func; \
   SET_by_offset(disp, _gloffset_SamplerParameterIiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_SamplerParameterIuiv)(GLuint, GLenum, const GLuint *);
#define CALL_SamplerParameterIuiv(disp, parameters) (* GET_SamplerParameterIuiv(disp)) parameters
#define GET_SamplerParameterIuiv(disp) ((_glptr_SamplerParameterIuiv)(GET_by_offset((disp), _gloffset_SamplerParameterIuiv)))
#define SET_SamplerParameterIuiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_SamplerParameterIuiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_SamplerParameterf)(GLuint, GLenum, GLfloat);
#define CALL_SamplerParameterf(disp, parameters) (* GET_SamplerParameterf(disp)) parameters
#define GET_SamplerParameterf(disp) ((_glptr_SamplerParameterf)(GET_by_offset((disp), _gloffset_SamplerParameterf)))
#define SET_SamplerParameterf(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_SamplerParameterf, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_SamplerParameterfv)(GLuint, GLenum, const GLfloat *);
#define CALL_SamplerParameterfv(disp, parameters) (* GET_SamplerParameterfv(disp)) parameters
#define GET_SamplerParameterfv(disp) ((_glptr_SamplerParameterfv)(GET_by_offset((disp), _gloffset_SamplerParameterfv)))
#define SET_SamplerParameterfv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_SamplerParameterfv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_SamplerParameteri)(GLuint, GLenum, GLint);
#define CALL_SamplerParameteri(disp, parameters) (* GET_SamplerParameteri(disp)) parameters
#define GET_SamplerParameteri(disp) ((_glptr_SamplerParameteri)(GET_by_offset((disp), _gloffset_SamplerParameteri)))
#define SET_SamplerParameteri(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLint) = func; \
   SET_by_offset(disp, _gloffset_SamplerParameteri, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_SamplerParameteriv)(GLuint, GLenum, const GLint *);
#define CALL_SamplerParameteriv(disp, parameters) (* GET_SamplerParameteriv(disp)) parameters
#define GET_SamplerParameteriv(disp) ((_glptr_SamplerParameteriv)(GET_by_offset((disp), _gloffset_SamplerParameteriv)))
#define SET_SamplerParameteriv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, const GLint *) = func; \
   SET_by_offset(disp, _gloffset_SamplerParameteriv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetQueryObjecti64v)(GLuint, GLenum, GLint64 *);
#define CALL_GetQueryObjecti64v(disp, parameters) (* GET_GetQueryObjecti64v(disp)) parameters
#define GET_GetQueryObjecti64v(disp) ((_glptr_GetQueryObjecti64v)(GET_by_offset((disp), _gloffset_GetQueryObjecti64v)))
#define SET_GetQueryObjecti64v(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLint64 *) = func; \
   SET_by_offset(disp, _gloffset_GetQueryObjecti64v, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetQueryObjectui64v)(GLuint, GLenum, GLuint64 *);
#define CALL_GetQueryObjectui64v(disp, parameters) (* GET_GetQueryObjectui64v(disp)) parameters
#define GET_GetQueryObjectui64v(disp) ((_glptr_GetQueryObjectui64v)(GET_by_offset((disp), _gloffset_GetQueryObjectui64v)))
#define SET_GetQueryObjectui64v(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLuint64 *) = func; \
   SET_by_offset(disp, _gloffset_GetQueryObjectui64v, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_QueryCounter)(GLuint, GLenum);
#define CALL_QueryCounter(disp, parameters) (* GET_QueryCounter(disp)) parameters
#define GET_QueryCounter(disp) ((_glptr_QueryCounter)(GET_by_offset((disp), _gloffset_QueryCounter)))
#define SET_QueryCounter(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum) = func; \
   SET_by_offset(disp, _gloffset_QueryCounter, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ColorP3ui)(GLenum, GLuint);
#define CALL_ColorP3ui(disp, parameters) (* GET_ColorP3ui(disp)) parameters
#define GET_ColorP3ui(disp) ((_glptr_ColorP3ui)(GET_by_offset((disp), _gloffset_ColorP3ui)))
#define SET_ColorP3ui(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint) = func; \
   SET_by_offset(disp, _gloffset_ColorP3ui, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ColorP3uiv)(GLenum, const GLuint *);
#define CALL_ColorP3uiv(disp, parameters) (* GET_ColorP3uiv(disp)) parameters
#define GET_ColorP3uiv(disp) ((_glptr_ColorP3uiv)(GET_by_offset((disp), _gloffset_ColorP3uiv)))
#define SET_ColorP3uiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_ColorP3uiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ColorP4ui)(GLenum, GLuint);
#define CALL_ColorP4ui(disp, parameters) (* GET_ColorP4ui(disp)) parameters
#define GET_ColorP4ui(disp) ((_glptr_ColorP4ui)(GET_by_offset((disp), _gloffset_ColorP4ui)))
#define SET_ColorP4ui(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint) = func; \
   SET_by_offset(disp, _gloffset_ColorP4ui, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ColorP4uiv)(GLenum, const GLuint *);
#define CALL_ColorP4uiv(disp, parameters) (* GET_ColorP4uiv(disp)) parameters
#define GET_ColorP4uiv(disp) ((_glptr_ColorP4uiv)(GET_by_offset((disp), _gloffset_ColorP4uiv)))
#define SET_ColorP4uiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_ColorP4uiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexCoordP1ui)(GLenum, GLenum, GLuint);
#define CALL_MultiTexCoordP1ui(disp, parameters) (* GET_MultiTexCoordP1ui(disp)) parameters
#define GET_MultiTexCoordP1ui(disp) ((_glptr_MultiTexCoordP1ui)(GET_by_offset((disp), _gloffset_MultiTexCoordP1ui)))
#define SET_MultiTexCoordP1ui(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLuint) = func; \
   SET_by_offset(disp, _gloffset_MultiTexCoordP1ui, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexCoordP1uiv)(GLenum, GLenum, const GLuint *);
#define CALL_MultiTexCoordP1uiv(disp, parameters) (* GET_MultiTexCoordP1uiv(disp)) parameters
#define GET_MultiTexCoordP1uiv(disp) ((_glptr_MultiTexCoordP1uiv)(GET_by_offset((disp), _gloffset_MultiTexCoordP1uiv)))
#define SET_MultiTexCoordP1uiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_MultiTexCoordP1uiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexCoordP2ui)(GLenum, GLenum, GLuint);
#define CALL_MultiTexCoordP2ui(disp, parameters) (* GET_MultiTexCoordP2ui(disp)) parameters
#define GET_MultiTexCoordP2ui(disp) ((_glptr_MultiTexCoordP2ui)(GET_by_offset((disp), _gloffset_MultiTexCoordP2ui)))
#define SET_MultiTexCoordP2ui(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLuint) = func; \
   SET_by_offset(disp, _gloffset_MultiTexCoordP2ui, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexCoordP2uiv)(GLenum, GLenum, const GLuint *);
#define CALL_MultiTexCoordP2uiv(disp, parameters) (* GET_MultiTexCoordP2uiv(disp)) parameters
#define GET_MultiTexCoordP2uiv(disp) ((_glptr_MultiTexCoordP2uiv)(GET_by_offset((disp), _gloffset_MultiTexCoordP2uiv)))
#define SET_MultiTexCoordP2uiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_MultiTexCoordP2uiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexCoordP3ui)(GLenum, GLenum, GLuint);
#define CALL_MultiTexCoordP3ui(disp, parameters) (* GET_MultiTexCoordP3ui(disp)) parameters
#define GET_MultiTexCoordP3ui(disp) ((_glptr_MultiTexCoordP3ui)(GET_by_offset((disp), _gloffset_MultiTexCoordP3ui)))
#define SET_MultiTexCoordP3ui(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLuint) = func; \
   SET_by_offset(disp, _gloffset_MultiTexCoordP3ui, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexCoordP3uiv)(GLenum, GLenum, const GLuint *);
#define CALL_MultiTexCoordP3uiv(disp, parameters) (* GET_MultiTexCoordP3uiv(disp)) parameters
#define GET_MultiTexCoordP3uiv(disp) ((_glptr_MultiTexCoordP3uiv)(GET_by_offset((disp), _gloffset_MultiTexCoordP3uiv)))
#define SET_MultiTexCoordP3uiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_MultiTexCoordP3uiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexCoordP4ui)(GLenum, GLenum, GLuint);
#define CALL_MultiTexCoordP4ui(disp, parameters) (* GET_MultiTexCoordP4ui(disp)) parameters
#define GET_MultiTexCoordP4ui(disp) ((_glptr_MultiTexCoordP4ui)(GET_by_offset((disp), _gloffset_MultiTexCoordP4ui)))
#define SET_MultiTexCoordP4ui(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLuint) = func; \
   SET_by_offset(disp, _gloffset_MultiTexCoordP4ui, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexCoordP4uiv)(GLenum, GLenum, const GLuint *);
#define CALL_MultiTexCoordP4uiv(disp, parameters) (* GET_MultiTexCoordP4uiv(disp)) parameters
#define GET_MultiTexCoordP4uiv(disp) ((_glptr_MultiTexCoordP4uiv)(GET_by_offset((disp), _gloffset_MultiTexCoordP4uiv)))
#define SET_MultiTexCoordP4uiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_MultiTexCoordP4uiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_NormalP3ui)(GLenum, GLuint);
#define CALL_NormalP3ui(disp, parameters) (* GET_NormalP3ui(disp)) parameters
#define GET_NormalP3ui(disp) ((_glptr_NormalP3ui)(GET_by_offset((disp), _gloffset_NormalP3ui)))
#define SET_NormalP3ui(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint) = func; \
   SET_by_offset(disp, _gloffset_NormalP3ui, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_NormalP3uiv)(GLenum, const GLuint *);
#define CALL_NormalP3uiv(disp, parameters) (* GET_NormalP3uiv(disp)) parameters
#define GET_NormalP3uiv(disp) ((_glptr_NormalP3uiv)(GET_by_offset((disp), _gloffset_NormalP3uiv)))
#define SET_NormalP3uiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_NormalP3uiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_SecondaryColorP3ui)(GLenum, GLuint);
#define CALL_SecondaryColorP3ui(disp, parameters) (* GET_SecondaryColorP3ui(disp)) parameters
#define GET_SecondaryColorP3ui(disp) ((_glptr_SecondaryColorP3ui)(GET_by_offset((disp), _gloffset_SecondaryColorP3ui)))
#define SET_SecondaryColorP3ui(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint) = func; \
   SET_by_offset(disp, _gloffset_SecondaryColorP3ui, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_SecondaryColorP3uiv)(GLenum, const GLuint *);
#define CALL_SecondaryColorP3uiv(disp, parameters) (* GET_SecondaryColorP3uiv(disp)) parameters
#define GET_SecondaryColorP3uiv(disp) ((_glptr_SecondaryColorP3uiv)(GET_by_offset((disp), _gloffset_SecondaryColorP3uiv)))
#define SET_SecondaryColorP3uiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_SecondaryColorP3uiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexCoordP1ui)(GLenum, GLuint);
#define CALL_TexCoordP1ui(disp, parameters) (* GET_TexCoordP1ui(disp)) parameters
#define GET_TexCoordP1ui(disp) ((_glptr_TexCoordP1ui)(GET_by_offset((disp), _gloffset_TexCoordP1ui)))
#define SET_TexCoordP1ui(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint) = func; \
   SET_by_offset(disp, _gloffset_TexCoordP1ui, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexCoordP1uiv)(GLenum, const GLuint *);
#define CALL_TexCoordP1uiv(disp, parameters) (* GET_TexCoordP1uiv(disp)) parameters
#define GET_TexCoordP1uiv(disp) ((_glptr_TexCoordP1uiv)(GET_by_offset((disp), _gloffset_TexCoordP1uiv)))
#define SET_TexCoordP1uiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_TexCoordP1uiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexCoordP2ui)(GLenum, GLuint);
#define CALL_TexCoordP2ui(disp, parameters) (* GET_TexCoordP2ui(disp)) parameters
#define GET_TexCoordP2ui(disp) ((_glptr_TexCoordP2ui)(GET_by_offset((disp), _gloffset_TexCoordP2ui)))
#define SET_TexCoordP2ui(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint) = func; \
   SET_by_offset(disp, _gloffset_TexCoordP2ui, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexCoordP2uiv)(GLenum, const GLuint *);
#define CALL_TexCoordP2uiv(disp, parameters) (* GET_TexCoordP2uiv(disp)) parameters
#define GET_TexCoordP2uiv(disp) ((_glptr_TexCoordP2uiv)(GET_by_offset((disp), _gloffset_TexCoordP2uiv)))
#define SET_TexCoordP2uiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_TexCoordP2uiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexCoordP3ui)(GLenum, GLuint);
#define CALL_TexCoordP3ui(disp, parameters) (* GET_TexCoordP3ui(disp)) parameters
#define GET_TexCoordP3ui(disp) ((_glptr_TexCoordP3ui)(GET_by_offset((disp), _gloffset_TexCoordP3ui)))
#define SET_TexCoordP3ui(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint) = func; \
   SET_by_offset(disp, _gloffset_TexCoordP3ui, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexCoordP3uiv)(GLenum, const GLuint *);
#define CALL_TexCoordP3uiv(disp, parameters) (* GET_TexCoordP3uiv(disp)) parameters
#define GET_TexCoordP3uiv(disp) ((_glptr_TexCoordP3uiv)(GET_by_offset((disp), _gloffset_TexCoordP3uiv)))
#define SET_TexCoordP3uiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_TexCoordP3uiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexCoordP4ui)(GLenum, GLuint);
#define CALL_TexCoordP4ui(disp, parameters) (* GET_TexCoordP4ui(disp)) parameters
#define GET_TexCoordP4ui(disp) ((_glptr_TexCoordP4ui)(GET_by_offset((disp), _gloffset_TexCoordP4ui)))
#define SET_TexCoordP4ui(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint) = func; \
   SET_by_offset(disp, _gloffset_TexCoordP4ui, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexCoordP4uiv)(GLenum, const GLuint *);
#define CALL_TexCoordP4uiv(disp, parameters) (* GET_TexCoordP4uiv(disp)) parameters
#define GET_TexCoordP4uiv(disp) ((_glptr_TexCoordP4uiv)(GET_by_offset((disp), _gloffset_TexCoordP4uiv)))
#define SET_TexCoordP4uiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_TexCoordP4uiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribP1ui)(GLuint, GLenum, GLboolean, GLuint);
#define CALL_VertexAttribP1ui(disp, parameters) (* GET_VertexAttribP1ui(disp)) parameters
#define GET_VertexAttribP1ui(disp) ((_glptr_VertexAttribP1ui)(GET_by_offset((disp), _gloffset_VertexAttribP1ui)))
#define SET_VertexAttribP1ui(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLboolean, GLuint) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribP1ui, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribP1uiv)(GLuint, GLenum, GLboolean, const GLuint *);
#define CALL_VertexAttribP1uiv(disp, parameters) (* GET_VertexAttribP1uiv(disp)) parameters
#define GET_VertexAttribP1uiv(disp) ((_glptr_VertexAttribP1uiv)(GET_by_offset((disp), _gloffset_VertexAttribP1uiv)))
#define SET_VertexAttribP1uiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLboolean, const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribP1uiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribP2ui)(GLuint, GLenum, GLboolean, GLuint);
#define CALL_VertexAttribP2ui(disp, parameters) (* GET_VertexAttribP2ui(disp)) parameters
#define GET_VertexAttribP2ui(disp) ((_glptr_VertexAttribP2ui)(GET_by_offset((disp), _gloffset_VertexAttribP2ui)))
#define SET_VertexAttribP2ui(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLboolean, GLuint) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribP2ui, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribP2uiv)(GLuint, GLenum, GLboolean, const GLuint *);
#define CALL_VertexAttribP2uiv(disp, parameters) (* GET_VertexAttribP2uiv(disp)) parameters
#define GET_VertexAttribP2uiv(disp) ((_glptr_VertexAttribP2uiv)(GET_by_offset((disp), _gloffset_VertexAttribP2uiv)))
#define SET_VertexAttribP2uiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLboolean, const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribP2uiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribP3ui)(GLuint, GLenum, GLboolean, GLuint);
#define CALL_VertexAttribP3ui(disp, parameters) (* GET_VertexAttribP3ui(disp)) parameters
#define GET_VertexAttribP3ui(disp) ((_glptr_VertexAttribP3ui)(GET_by_offset((disp), _gloffset_VertexAttribP3ui)))
#define SET_VertexAttribP3ui(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLboolean, GLuint) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribP3ui, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribP3uiv)(GLuint, GLenum, GLboolean, const GLuint *);
#define CALL_VertexAttribP3uiv(disp, parameters) (* GET_VertexAttribP3uiv(disp)) parameters
#define GET_VertexAttribP3uiv(disp) ((_glptr_VertexAttribP3uiv)(GET_by_offset((disp), _gloffset_VertexAttribP3uiv)))
#define SET_VertexAttribP3uiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLboolean, const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribP3uiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribP4ui)(GLuint, GLenum, GLboolean, GLuint);
#define CALL_VertexAttribP4ui(disp, parameters) (* GET_VertexAttribP4ui(disp)) parameters
#define GET_VertexAttribP4ui(disp) ((_glptr_VertexAttribP4ui)(GET_by_offset((disp), _gloffset_VertexAttribP4ui)))
#define SET_VertexAttribP4ui(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLboolean, GLuint) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribP4ui, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribP4uiv)(GLuint, GLenum, GLboolean, const GLuint *);
#define CALL_VertexAttribP4uiv(disp, parameters) (* GET_VertexAttribP4uiv(disp)) parameters
#define GET_VertexAttribP4uiv(disp) ((_glptr_VertexAttribP4uiv)(GET_by_offset((disp), _gloffset_VertexAttribP4uiv)))
#define SET_VertexAttribP4uiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLboolean, const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribP4uiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexP2ui)(GLenum, GLuint);
#define CALL_VertexP2ui(disp, parameters) (* GET_VertexP2ui(disp)) parameters
#define GET_VertexP2ui(disp) ((_glptr_VertexP2ui)(GET_by_offset((disp), _gloffset_VertexP2ui)))
#define SET_VertexP2ui(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint) = func; \
   SET_by_offset(disp, _gloffset_VertexP2ui, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexP2uiv)(GLenum, const GLuint *);
#define CALL_VertexP2uiv(disp, parameters) (* GET_VertexP2uiv(disp)) parameters
#define GET_VertexP2uiv(disp) ((_glptr_VertexP2uiv)(GET_by_offset((disp), _gloffset_VertexP2uiv)))
#define SET_VertexP2uiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_VertexP2uiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexP3ui)(GLenum, GLuint);
#define CALL_VertexP3ui(disp, parameters) (* GET_VertexP3ui(disp)) parameters
#define GET_VertexP3ui(disp) ((_glptr_VertexP3ui)(GET_by_offset((disp), _gloffset_VertexP3ui)))
#define SET_VertexP3ui(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint) = func; \
   SET_by_offset(disp, _gloffset_VertexP3ui, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexP3uiv)(GLenum, const GLuint *);
#define CALL_VertexP3uiv(disp, parameters) (* GET_VertexP3uiv(disp)) parameters
#define GET_VertexP3uiv(disp) ((_glptr_VertexP3uiv)(GET_by_offset((disp), _gloffset_VertexP3uiv)))
#define SET_VertexP3uiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_VertexP3uiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexP4ui)(GLenum, GLuint);
#define CALL_VertexP4ui(disp, parameters) (* GET_VertexP4ui(disp)) parameters
#define GET_VertexP4ui(disp) ((_glptr_VertexP4ui)(GET_by_offset((disp), _gloffset_VertexP4ui)))
#define SET_VertexP4ui(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint) = func; \
   SET_by_offset(disp, _gloffset_VertexP4ui, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexP4uiv)(GLenum, const GLuint *);
#define CALL_VertexP4uiv(disp, parameters) (* GET_VertexP4uiv(disp)) parameters
#define GET_VertexP4uiv(disp) ((_glptr_VertexP4uiv)(GET_by_offset((disp), _gloffset_VertexP4uiv)))
#define SET_VertexP4uiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_VertexP4uiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DrawArraysIndirect)(GLenum, const GLvoid *);
#define CALL_DrawArraysIndirect(disp, parameters) (* GET_DrawArraysIndirect(disp)) parameters
#define GET_DrawArraysIndirect(disp) ((_glptr_DrawArraysIndirect)(GET_by_offset((disp), _gloffset_DrawArraysIndirect)))
#define SET_DrawArraysIndirect(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_DrawArraysIndirect, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DrawElementsIndirect)(GLenum, GLenum, const GLvoid *);
#define CALL_DrawElementsIndirect(disp, parameters) (* GET_DrawElementsIndirect(disp)) parameters
#define GET_DrawElementsIndirect(disp) ((_glptr_DrawElementsIndirect)(GET_by_offset((disp), _gloffset_DrawElementsIndirect)))
#define SET_DrawElementsIndirect(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_DrawElementsIndirect, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetUniformdv)(GLuint, GLint, GLdouble *);
#define CALL_GetUniformdv(disp, parameters) (* GET_GetUniformdv(disp)) parameters
#define GET_GetUniformdv(disp) ((_glptr_GetUniformdv)(GET_by_offset((disp), _gloffset_GetUniformdv)))
#define SET_GetUniformdv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_GetUniformdv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Uniform1d)(GLint, GLdouble);
#define CALL_Uniform1d(disp, parameters) (* GET_Uniform1d(disp)) parameters
#define GET_Uniform1d(disp) ((_glptr_Uniform1d)(GET_by_offset((disp), _gloffset_Uniform1d)))
#define SET_Uniform1d(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLdouble) = func; \
   SET_by_offset(disp, _gloffset_Uniform1d, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Uniform1dv)(GLint, GLsizei, const GLdouble *);
#define CALL_Uniform1dv(disp, parameters) (* GET_Uniform1dv(disp)) parameters
#define GET_Uniform1dv(disp) ((_glptr_Uniform1dv)(GET_by_offset((disp), _gloffset_Uniform1dv)))
#define SET_Uniform1dv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLsizei, const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_Uniform1dv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Uniform2d)(GLint, GLdouble, GLdouble);
#define CALL_Uniform2d(disp, parameters) (* GET_Uniform2d(disp)) parameters
#define GET_Uniform2d(disp) ((_glptr_Uniform2d)(GET_by_offset((disp), _gloffset_Uniform2d)))
#define SET_Uniform2d(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLdouble, GLdouble) = func; \
   SET_by_offset(disp, _gloffset_Uniform2d, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Uniform2dv)(GLint, GLsizei, const GLdouble *);
#define CALL_Uniform2dv(disp, parameters) (* GET_Uniform2dv(disp)) parameters
#define GET_Uniform2dv(disp) ((_glptr_Uniform2dv)(GET_by_offset((disp), _gloffset_Uniform2dv)))
#define SET_Uniform2dv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLsizei, const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_Uniform2dv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Uniform3d)(GLint, GLdouble, GLdouble, GLdouble);
#define CALL_Uniform3d(disp, parameters) (* GET_Uniform3d(disp)) parameters
#define GET_Uniform3d(disp) ((_glptr_Uniform3d)(GET_by_offset((disp), _gloffset_Uniform3d)))
#define SET_Uniform3d(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLdouble, GLdouble, GLdouble) = func; \
   SET_by_offset(disp, _gloffset_Uniform3d, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Uniform3dv)(GLint, GLsizei, const GLdouble *);
#define CALL_Uniform3dv(disp, parameters) (* GET_Uniform3dv(disp)) parameters
#define GET_Uniform3dv(disp) ((_glptr_Uniform3dv)(GET_by_offset((disp), _gloffset_Uniform3dv)))
#define SET_Uniform3dv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLsizei, const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_Uniform3dv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Uniform4d)(GLint, GLdouble, GLdouble, GLdouble, GLdouble);
#define CALL_Uniform4d(disp, parameters) (* GET_Uniform4d(disp)) parameters
#define GET_Uniform4d(disp) ((_glptr_Uniform4d)(GET_by_offset((disp), _gloffset_Uniform4d)))
#define SET_Uniform4d(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLdouble, GLdouble, GLdouble, GLdouble) = func; \
   SET_by_offset(disp, _gloffset_Uniform4d, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Uniform4dv)(GLint, GLsizei, const GLdouble *);
#define CALL_Uniform4dv(disp, parameters) (* GET_Uniform4dv(disp)) parameters
#define GET_Uniform4dv(disp) ((_glptr_Uniform4dv)(GET_by_offset((disp), _gloffset_Uniform4dv)))
#define SET_Uniform4dv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLsizei, const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_Uniform4dv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_UniformMatrix2dv)(GLint, GLsizei, GLboolean, const GLdouble *);
#define CALL_UniformMatrix2dv(disp, parameters) (* GET_UniformMatrix2dv(disp)) parameters
#define GET_UniformMatrix2dv(disp) ((_glptr_UniformMatrix2dv)(GET_by_offset((disp), _gloffset_UniformMatrix2dv)))
#define SET_UniformMatrix2dv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLsizei, GLboolean, const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_UniformMatrix2dv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_UniformMatrix2x3dv)(GLint, GLsizei, GLboolean, const GLdouble *);
#define CALL_UniformMatrix2x3dv(disp, parameters) (* GET_UniformMatrix2x3dv(disp)) parameters
#define GET_UniformMatrix2x3dv(disp) ((_glptr_UniformMatrix2x3dv)(GET_by_offset((disp), _gloffset_UniformMatrix2x3dv)))
#define SET_UniformMatrix2x3dv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLsizei, GLboolean, const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_UniformMatrix2x3dv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_UniformMatrix2x4dv)(GLint, GLsizei, GLboolean, const GLdouble *);
#define CALL_UniformMatrix2x4dv(disp, parameters) (* GET_UniformMatrix2x4dv(disp)) parameters
#define GET_UniformMatrix2x4dv(disp) ((_glptr_UniformMatrix2x4dv)(GET_by_offset((disp), _gloffset_UniformMatrix2x4dv)))
#define SET_UniformMatrix2x4dv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLsizei, GLboolean, const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_UniformMatrix2x4dv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_UniformMatrix3dv)(GLint, GLsizei, GLboolean, const GLdouble *);
#define CALL_UniformMatrix3dv(disp, parameters) (* GET_UniformMatrix3dv(disp)) parameters
#define GET_UniformMatrix3dv(disp) ((_glptr_UniformMatrix3dv)(GET_by_offset((disp), _gloffset_UniformMatrix3dv)))
#define SET_UniformMatrix3dv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLsizei, GLboolean, const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_UniformMatrix3dv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_UniformMatrix3x2dv)(GLint, GLsizei, GLboolean, const GLdouble *);
#define CALL_UniformMatrix3x2dv(disp, parameters) (* GET_UniformMatrix3x2dv(disp)) parameters
#define GET_UniformMatrix3x2dv(disp) ((_glptr_UniformMatrix3x2dv)(GET_by_offset((disp), _gloffset_UniformMatrix3x2dv)))
#define SET_UniformMatrix3x2dv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLsizei, GLboolean, const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_UniformMatrix3x2dv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_UniformMatrix3x4dv)(GLint, GLsizei, GLboolean, const GLdouble *);
#define CALL_UniformMatrix3x4dv(disp, parameters) (* GET_UniformMatrix3x4dv(disp)) parameters
#define GET_UniformMatrix3x4dv(disp) ((_glptr_UniformMatrix3x4dv)(GET_by_offset((disp), _gloffset_UniformMatrix3x4dv)))
#define SET_UniformMatrix3x4dv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLsizei, GLboolean, const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_UniformMatrix3x4dv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_UniformMatrix4dv)(GLint, GLsizei, GLboolean, const GLdouble *);
#define CALL_UniformMatrix4dv(disp, parameters) (* GET_UniformMatrix4dv(disp)) parameters
#define GET_UniformMatrix4dv(disp) ((_glptr_UniformMatrix4dv)(GET_by_offset((disp), _gloffset_UniformMatrix4dv)))
#define SET_UniformMatrix4dv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLsizei, GLboolean, const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_UniformMatrix4dv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_UniformMatrix4x2dv)(GLint, GLsizei, GLboolean, const GLdouble *);
#define CALL_UniformMatrix4x2dv(disp, parameters) (* GET_UniformMatrix4x2dv(disp)) parameters
#define GET_UniformMatrix4x2dv(disp) ((_glptr_UniformMatrix4x2dv)(GET_by_offset((disp), _gloffset_UniformMatrix4x2dv)))
#define SET_UniformMatrix4x2dv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLsizei, GLboolean, const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_UniformMatrix4x2dv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_UniformMatrix4x3dv)(GLint, GLsizei, GLboolean, const GLdouble *);
#define CALL_UniformMatrix4x3dv(disp, parameters) (* GET_UniformMatrix4x3dv(disp)) parameters
#define GET_UniformMatrix4x3dv(disp) ((_glptr_UniformMatrix4x3dv)(GET_by_offset((disp), _gloffset_UniformMatrix4x3dv)))
#define SET_UniformMatrix4x3dv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLsizei, GLboolean, const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_UniformMatrix4x3dv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetActiveSubroutineName)(GLuint, GLenum, GLuint, GLsizei, GLsizei *, GLchar *);
#define CALL_GetActiveSubroutineName(disp, parameters) (* GET_GetActiveSubroutineName(disp)) parameters
#define GET_GetActiveSubroutineName(disp) ((_glptr_GetActiveSubroutineName)(GET_by_offset((disp), _gloffset_GetActiveSubroutineName)))
#define SET_GetActiveSubroutineName(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLuint, GLsizei, GLsizei *, GLchar *) = func; \
   SET_by_offset(disp, _gloffset_GetActiveSubroutineName, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetActiveSubroutineUniformName)(GLuint, GLenum, GLuint, GLsizei, GLsizei *, GLchar *);
#define CALL_GetActiveSubroutineUniformName(disp, parameters) (* GET_GetActiveSubroutineUniformName(disp)) parameters
#define GET_GetActiveSubroutineUniformName(disp) ((_glptr_GetActiveSubroutineUniformName)(GET_by_offset((disp), _gloffset_GetActiveSubroutineUniformName)))
#define SET_GetActiveSubroutineUniformName(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLuint, GLsizei, GLsizei *, GLchar *) = func; \
   SET_by_offset(disp, _gloffset_GetActiveSubroutineUniformName, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetActiveSubroutineUniformiv)(GLuint, GLenum, GLuint, GLenum, GLint *);
#define CALL_GetActiveSubroutineUniformiv(disp, parameters) (* GET_GetActiveSubroutineUniformiv(disp)) parameters
#define GET_GetActiveSubroutineUniformiv(disp) ((_glptr_GetActiveSubroutineUniformiv)(GET_by_offset((disp), _gloffset_GetActiveSubroutineUniformiv)))
#define SET_GetActiveSubroutineUniformiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLuint, GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetActiveSubroutineUniformiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetProgramStageiv)(GLuint, GLenum, GLenum, GLint *);
#define CALL_GetProgramStageiv(disp, parameters) (* GET_GetProgramStageiv(disp)) parameters
#define GET_GetProgramStageiv(disp) ((_glptr_GetProgramStageiv)(GET_by_offset((disp), _gloffset_GetProgramStageiv)))
#define SET_GetProgramStageiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetProgramStageiv, fn); \
} while (0)

typedef GLuint (GLAPIENTRYP _glptr_GetSubroutineIndex)(GLuint, GLenum, const GLchar *);
#define CALL_GetSubroutineIndex(disp, parameters) (* GET_GetSubroutineIndex(disp)) parameters
#define GET_GetSubroutineIndex(disp) ((_glptr_GetSubroutineIndex)(GET_by_offset((disp), _gloffset_GetSubroutineIndex)))
#define SET_GetSubroutineIndex(disp, func) do { \
   GLuint (GLAPIENTRYP fn)(GLuint, GLenum, const GLchar *) = func; \
   SET_by_offset(disp, _gloffset_GetSubroutineIndex, fn); \
} while (0)

typedef GLint (GLAPIENTRYP _glptr_GetSubroutineUniformLocation)(GLuint, GLenum, const GLchar *);
#define CALL_GetSubroutineUniformLocation(disp, parameters) (* GET_GetSubroutineUniformLocation(disp)) parameters
#define GET_GetSubroutineUniformLocation(disp) ((_glptr_GetSubroutineUniformLocation)(GET_by_offset((disp), _gloffset_GetSubroutineUniformLocation)))
#define SET_GetSubroutineUniformLocation(disp, func) do { \
   GLint (GLAPIENTRYP fn)(GLuint, GLenum, const GLchar *) = func; \
   SET_by_offset(disp, _gloffset_GetSubroutineUniformLocation, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetUniformSubroutineuiv)(GLenum, GLint, GLuint *);
#define CALL_GetUniformSubroutineuiv(disp, parameters) (* GET_GetUniformSubroutineuiv(disp)) parameters
#define GET_GetUniformSubroutineuiv(disp) ((_glptr_GetUniformSubroutineuiv)(GET_by_offset((disp), _gloffset_GetUniformSubroutineuiv)))
#define SET_GetUniformSubroutineuiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLint, GLuint *) = func; \
   SET_by_offset(disp, _gloffset_GetUniformSubroutineuiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_UniformSubroutinesuiv)(GLenum, GLsizei, const GLuint *);
#define CALL_UniformSubroutinesuiv(disp, parameters) (* GET_UniformSubroutinesuiv(disp)) parameters
#define GET_UniformSubroutinesuiv(disp) ((_glptr_UniformSubroutinesuiv)(GET_by_offset((disp), _gloffset_UniformSubroutinesuiv)))
#define SET_UniformSubroutinesuiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLsizei, const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_UniformSubroutinesuiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_PatchParameterfv)(GLenum, const GLfloat *);
#define CALL_PatchParameterfv(disp, parameters) (* GET_PatchParameterfv(disp)) parameters
#define GET_PatchParameterfv(disp) ((_glptr_PatchParameterfv)(GET_by_offset((disp), _gloffset_PatchParameterfv)))
#define SET_PatchParameterfv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_PatchParameterfv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_PatchParameteri)(GLenum, GLint);
#define CALL_PatchParameteri(disp, parameters) (* GET_PatchParameteri(disp)) parameters
#define GET_PatchParameteri(disp) ((_glptr_PatchParameteri)(GET_by_offset((disp), _gloffset_PatchParameteri)))
#define SET_PatchParameteri(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLint) = func; \
   SET_by_offset(disp, _gloffset_PatchParameteri, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_BindTransformFeedback)(GLenum, GLuint);
#define CALL_BindTransformFeedback(disp, parameters) (* GET_BindTransformFeedback(disp)) parameters
#define GET_BindTransformFeedback(disp) ((_glptr_BindTransformFeedback)(GET_by_offset((disp), _gloffset_BindTransformFeedback)))
#define SET_BindTransformFeedback(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint) = func; \
   SET_by_offset(disp, _gloffset_BindTransformFeedback, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DeleteTransformFeedbacks)(GLsizei, const GLuint *);
#define CALL_DeleteTransformFeedbacks(disp, parameters) (* GET_DeleteTransformFeedbacks(disp)) parameters
#define GET_DeleteTransformFeedbacks(disp) ((_glptr_DeleteTransformFeedbacks)(GET_by_offset((disp), _gloffset_DeleteTransformFeedbacks)))
#define SET_DeleteTransformFeedbacks(disp, func) do { \
   void (GLAPIENTRYP fn)(GLsizei, const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_DeleteTransformFeedbacks, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DrawTransformFeedback)(GLenum, GLuint);
#define CALL_DrawTransformFeedback(disp, parameters) (* GET_DrawTransformFeedback(disp)) parameters
#define GET_DrawTransformFeedback(disp) ((_glptr_DrawTransformFeedback)(GET_by_offset((disp), _gloffset_DrawTransformFeedback)))
#define SET_DrawTransformFeedback(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint) = func; \
   SET_by_offset(disp, _gloffset_DrawTransformFeedback, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GenTransformFeedbacks)(GLsizei, GLuint *);
#define CALL_GenTransformFeedbacks(disp, parameters) (* GET_GenTransformFeedbacks(disp)) parameters
#define GET_GenTransformFeedbacks(disp) ((_glptr_GenTransformFeedbacks)(GET_by_offset((disp), _gloffset_GenTransformFeedbacks)))
#define SET_GenTransformFeedbacks(disp, func) do { \
   void (GLAPIENTRYP fn)(GLsizei, GLuint *) = func; \
   SET_by_offset(disp, _gloffset_GenTransformFeedbacks, fn); \
} while (0)

typedef GLboolean (GLAPIENTRYP _glptr_IsTransformFeedback)(GLuint);
#define CALL_IsTransformFeedback(disp, parameters) (* GET_IsTransformFeedback(disp)) parameters
#define GET_IsTransformFeedback(disp) ((_glptr_IsTransformFeedback)(GET_by_offset((disp), _gloffset_IsTransformFeedback)))
#define SET_IsTransformFeedback(disp, func) do { \
   GLboolean (GLAPIENTRYP fn)(GLuint) = func; \
   SET_by_offset(disp, _gloffset_IsTransformFeedback, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_PauseTransformFeedback)(void);
#define CALL_PauseTransformFeedback(disp, parameters) (* GET_PauseTransformFeedback(disp)) parameters
#define GET_PauseTransformFeedback(disp) ((_glptr_PauseTransformFeedback)(GET_by_offset((disp), _gloffset_PauseTransformFeedback)))
#define SET_PauseTransformFeedback(disp, func) do { \
   void (GLAPIENTRYP fn)(void) = func; \
   SET_by_offset(disp, _gloffset_PauseTransformFeedback, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ResumeTransformFeedback)(void);
#define CALL_ResumeTransformFeedback(disp, parameters) (* GET_ResumeTransformFeedback(disp)) parameters
#define GET_ResumeTransformFeedback(disp) ((_glptr_ResumeTransformFeedback)(GET_by_offset((disp), _gloffset_ResumeTransformFeedback)))
#define SET_ResumeTransformFeedback(disp, func) do { \
   void (GLAPIENTRYP fn)(void) = func; \
   SET_by_offset(disp, _gloffset_ResumeTransformFeedback, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_BeginQueryIndexed)(GLenum, GLuint, GLuint);
#define CALL_BeginQueryIndexed(disp, parameters) (* GET_BeginQueryIndexed(disp)) parameters
#define GET_BeginQueryIndexed(disp) ((_glptr_BeginQueryIndexed)(GET_by_offset((disp), _gloffset_BeginQueryIndexed)))
#define SET_BeginQueryIndexed(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint, GLuint) = func; \
   SET_by_offset(disp, _gloffset_BeginQueryIndexed, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DrawTransformFeedbackStream)(GLenum, GLuint, GLuint);
#define CALL_DrawTransformFeedbackStream(disp, parameters) (* GET_DrawTransformFeedbackStream(disp)) parameters
#define GET_DrawTransformFeedbackStream(disp) ((_glptr_DrawTransformFeedbackStream)(GET_by_offset((disp), _gloffset_DrawTransformFeedbackStream)))
#define SET_DrawTransformFeedbackStream(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint, GLuint) = func; \
   SET_by_offset(disp, _gloffset_DrawTransformFeedbackStream, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_EndQueryIndexed)(GLenum, GLuint);
#define CALL_EndQueryIndexed(disp, parameters) (* GET_EndQueryIndexed(disp)) parameters
#define GET_EndQueryIndexed(disp) ((_glptr_EndQueryIndexed)(GET_by_offset((disp), _gloffset_EndQueryIndexed)))
#define SET_EndQueryIndexed(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint) = func; \
   SET_by_offset(disp, _gloffset_EndQueryIndexed, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetQueryIndexediv)(GLenum, GLuint, GLenum, GLint *);
#define CALL_GetQueryIndexediv(disp, parameters) (* GET_GetQueryIndexediv(disp)) parameters
#define GET_GetQueryIndexediv(disp) ((_glptr_GetQueryIndexediv)(GET_by_offset((disp), _gloffset_GetQueryIndexediv)))
#define SET_GetQueryIndexediv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint, GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetQueryIndexediv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ClearDepthf)(GLclampf);
#define CALL_ClearDepthf(disp, parameters) (* GET_ClearDepthf(disp)) parameters
#define GET_ClearDepthf(disp) ((_glptr_ClearDepthf)(GET_by_offset((disp), _gloffset_ClearDepthf)))
#define SET_ClearDepthf(disp, func) do { \
   void (GLAPIENTRYP fn)(GLclampf) = func; \
   SET_by_offset(disp, _gloffset_ClearDepthf, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DepthRangef)(GLclampf, GLclampf);
#define CALL_DepthRangef(disp, parameters) (* GET_DepthRangef(disp)) parameters
#define GET_DepthRangef(disp) ((_glptr_DepthRangef)(GET_by_offset((disp), _gloffset_DepthRangef)))
#define SET_DepthRangef(disp, func) do { \
   void (GLAPIENTRYP fn)(GLclampf, GLclampf) = func; \
   SET_by_offset(disp, _gloffset_DepthRangef, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetShaderPrecisionFormat)(GLenum, GLenum, GLint *, GLint *);
#define CALL_GetShaderPrecisionFormat(disp, parameters) (* GET_GetShaderPrecisionFormat(disp)) parameters
#define GET_GetShaderPrecisionFormat(disp) ((_glptr_GetShaderPrecisionFormat)(GET_by_offset((disp), _gloffset_GetShaderPrecisionFormat)))
#define SET_GetShaderPrecisionFormat(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLint *, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetShaderPrecisionFormat, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ReleaseShaderCompiler)(void);
#define CALL_ReleaseShaderCompiler(disp, parameters) (* GET_ReleaseShaderCompiler(disp)) parameters
#define GET_ReleaseShaderCompiler(disp) ((_glptr_ReleaseShaderCompiler)(GET_by_offset((disp), _gloffset_ReleaseShaderCompiler)))
#define SET_ReleaseShaderCompiler(disp, func) do { \
   void (GLAPIENTRYP fn)(void) = func; \
   SET_by_offset(disp, _gloffset_ReleaseShaderCompiler, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ShaderBinary)(GLsizei, const GLuint *, GLenum, const GLvoid *, GLsizei);
#define CALL_ShaderBinary(disp, parameters) (* GET_ShaderBinary(disp)) parameters
#define GET_ShaderBinary(disp) ((_glptr_ShaderBinary)(GET_by_offset((disp), _gloffset_ShaderBinary)))
#define SET_ShaderBinary(disp, func) do { \
   void (GLAPIENTRYP fn)(GLsizei, const GLuint *, GLenum, const GLvoid *, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_ShaderBinary, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetProgramBinary)(GLuint, GLsizei, GLsizei *, GLenum *, GLvoid *);
#define CALL_GetProgramBinary(disp, parameters) (* GET_GetProgramBinary(disp)) parameters
#define GET_GetProgramBinary(disp) ((_glptr_GetProgramBinary)(GET_by_offset((disp), _gloffset_GetProgramBinary)))
#define SET_GetProgramBinary(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizei, GLsizei *, GLenum *, GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_GetProgramBinary, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramBinary)(GLuint, GLenum, const GLvoid *, GLsizei);
#define CALL_ProgramBinary(disp, parameters) (* GET_ProgramBinary(disp)) parameters
#define GET_ProgramBinary(disp) ((_glptr_ProgramBinary)(GET_by_offset((disp), _gloffset_ProgramBinary)))
#define SET_ProgramBinary(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, const GLvoid *, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_ProgramBinary, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramParameteri)(GLuint, GLenum, GLint);
#define CALL_ProgramParameteri(disp, parameters) (* GET_ProgramParameteri(disp)) parameters
#define GET_ProgramParameteri(disp) ((_glptr_ProgramParameteri)(GET_by_offset((disp), _gloffset_ProgramParameteri)))
#define SET_ProgramParameteri(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLint) = func; \
   SET_by_offset(disp, _gloffset_ProgramParameteri, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetVertexAttribLdv)(GLuint, GLenum, GLdouble *);
#define CALL_GetVertexAttribLdv(disp, parameters) (* GET_GetVertexAttribLdv(disp)) parameters
#define GET_GetVertexAttribLdv(disp) ((_glptr_GetVertexAttribLdv)(GET_by_offset((disp), _gloffset_GetVertexAttribLdv)))
#define SET_GetVertexAttribLdv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_GetVertexAttribLdv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribL1d)(GLuint, GLdouble);
#define CALL_VertexAttribL1d(disp, parameters) (* GET_VertexAttribL1d(disp)) parameters
#define GET_VertexAttribL1d(disp) ((_glptr_VertexAttribL1d)(GET_by_offset((disp), _gloffset_VertexAttribL1d)))
#define SET_VertexAttribL1d(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLdouble) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribL1d, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribL1dv)(GLuint, const GLdouble *);
#define CALL_VertexAttribL1dv(disp, parameters) (* GET_VertexAttribL1dv(disp)) parameters
#define GET_VertexAttribL1dv(disp) ((_glptr_VertexAttribL1dv)(GET_by_offset((disp), _gloffset_VertexAttribL1dv)))
#define SET_VertexAttribL1dv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribL1dv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribL2d)(GLuint, GLdouble, GLdouble);
#define CALL_VertexAttribL2d(disp, parameters) (* GET_VertexAttribL2d(disp)) parameters
#define GET_VertexAttribL2d(disp) ((_glptr_VertexAttribL2d)(GET_by_offset((disp), _gloffset_VertexAttribL2d)))
#define SET_VertexAttribL2d(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLdouble, GLdouble) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribL2d, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribL2dv)(GLuint, const GLdouble *);
#define CALL_VertexAttribL2dv(disp, parameters) (* GET_VertexAttribL2dv(disp)) parameters
#define GET_VertexAttribL2dv(disp) ((_glptr_VertexAttribL2dv)(GET_by_offset((disp), _gloffset_VertexAttribL2dv)))
#define SET_VertexAttribL2dv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribL2dv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribL3d)(GLuint, GLdouble, GLdouble, GLdouble);
#define CALL_VertexAttribL3d(disp, parameters) (* GET_VertexAttribL3d(disp)) parameters
#define GET_VertexAttribL3d(disp) ((_glptr_VertexAttribL3d)(GET_by_offset((disp), _gloffset_VertexAttribL3d)))
#define SET_VertexAttribL3d(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLdouble, GLdouble, GLdouble) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribL3d, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribL3dv)(GLuint, const GLdouble *);
#define CALL_VertexAttribL3dv(disp, parameters) (* GET_VertexAttribL3dv(disp)) parameters
#define GET_VertexAttribL3dv(disp) ((_glptr_VertexAttribL3dv)(GET_by_offset((disp), _gloffset_VertexAttribL3dv)))
#define SET_VertexAttribL3dv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribL3dv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribL4d)(GLuint, GLdouble, GLdouble, GLdouble, GLdouble);
#define CALL_VertexAttribL4d(disp, parameters) (* GET_VertexAttribL4d(disp)) parameters
#define GET_VertexAttribL4d(disp) ((_glptr_VertexAttribL4d)(GET_by_offset((disp), _gloffset_VertexAttribL4d)))
#define SET_VertexAttribL4d(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLdouble, GLdouble, GLdouble, GLdouble) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribL4d, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribL4dv)(GLuint, const GLdouble *);
#define CALL_VertexAttribL4dv(disp, parameters) (* GET_VertexAttribL4dv(disp)) parameters
#define GET_VertexAttribL4dv(disp) ((_glptr_VertexAttribL4dv)(GET_by_offset((disp), _gloffset_VertexAttribL4dv)))
#define SET_VertexAttribL4dv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribL4dv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribLPointer)(GLuint, GLint, GLenum, GLsizei, const GLvoid *);
#define CALL_VertexAttribLPointer(disp, parameters) (* GET_VertexAttribLPointer(disp)) parameters
#define GET_VertexAttribLPointer(disp) ((_glptr_VertexAttribLPointer)(GET_by_offset((disp), _gloffset_VertexAttribLPointer)))
#define SET_VertexAttribLPointer(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLenum, GLsizei, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribLPointer, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DepthRangeArrayv)(GLuint, GLsizei, const GLclampd *);
#define CALL_DepthRangeArrayv(disp, parameters) (* GET_DepthRangeArrayv(disp)) parameters
#define GET_DepthRangeArrayv(disp) ((_glptr_DepthRangeArrayv)(GET_by_offset((disp), _gloffset_DepthRangeArrayv)))
#define SET_DepthRangeArrayv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizei, const GLclampd *) = func; \
   SET_by_offset(disp, _gloffset_DepthRangeArrayv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DepthRangeIndexed)(GLuint, GLclampd, GLclampd);
#define CALL_DepthRangeIndexed(disp, parameters) (* GET_DepthRangeIndexed(disp)) parameters
#define GET_DepthRangeIndexed(disp) ((_glptr_DepthRangeIndexed)(GET_by_offset((disp), _gloffset_DepthRangeIndexed)))
#define SET_DepthRangeIndexed(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLclampd, GLclampd) = func; \
   SET_by_offset(disp, _gloffset_DepthRangeIndexed, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetDoublei_v)(GLenum, GLuint, GLdouble *);
#define CALL_GetDoublei_v(disp, parameters) (* GET_GetDoublei_v(disp)) parameters
#define GET_GetDoublei_v(disp) ((_glptr_GetDoublei_v)(GET_by_offset((disp), _gloffset_GetDoublei_v)))
#define SET_GetDoublei_v(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint, GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_GetDoublei_v, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetFloati_v)(GLenum, GLuint, GLfloat *);
#define CALL_GetFloati_v(disp, parameters) (* GET_GetFloati_v(disp)) parameters
#define GET_GetFloati_v(disp) ((_glptr_GetFloati_v)(GET_by_offset((disp), _gloffset_GetFloati_v)))
#define SET_GetFloati_v(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint, GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_GetFloati_v, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ScissorArrayv)(GLuint, GLsizei, const int *);
#define CALL_ScissorArrayv(disp, parameters) (* GET_ScissorArrayv(disp)) parameters
#define GET_ScissorArrayv(disp) ((_glptr_ScissorArrayv)(GET_by_offset((disp), _gloffset_ScissorArrayv)))
#define SET_ScissorArrayv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizei, const int *) = func; \
   SET_by_offset(disp, _gloffset_ScissorArrayv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ScissorIndexed)(GLuint, GLint, GLint, GLsizei, GLsizei);
#define CALL_ScissorIndexed(disp, parameters) (* GET_ScissorIndexed(disp)) parameters
#define GET_ScissorIndexed(disp) ((_glptr_ScissorIndexed)(GET_by_offset((disp), _gloffset_ScissorIndexed)))
#define SET_ScissorIndexed(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLint, GLsizei, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_ScissorIndexed, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ScissorIndexedv)(GLuint, const GLint *);
#define CALL_ScissorIndexedv(disp, parameters) (* GET_ScissorIndexedv(disp)) parameters
#define GET_ScissorIndexedv(disp) ((_glptr_ScissorIndexedv)(GET_by_offset((disp), _gloffset_ScissorIndexedv)))
#define SET_ScissorIndexedv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLint *) = func; \
   SET_by_offset(disp, _gloffset_ScissorIndexedv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ViewportArrayv)(GLuint, GLsizei, const GLfloat *);
#define CALL_ViewportArrayv(disp, parameters) (* GET_ViewportArrayv(disp)) parameters
#define GET_ViewportArrayv(disp) ((_glptr_ViewportArrayv)(GET_by_offset((disp), _gloffset_ViewportArrayv)))
#define SET_ViewportArrayv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizei, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_ViewportArrayv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ViewportIndexedf)(GLuint, GLfloat, GLfloat, GLfloat, GLfloat);
#define CALL_ViewportIndexedf(disp, parameters) (* GET_ViewportIndexedf(disp)) parameters
#define GET_ViewportIndexedf(disp) ((_glptr_ViewportIndexedf)(GET_by_offset((disp), _gloffset_ViewportIndexedf)))
#define SET_ViewportIndexedf(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLfloat, GLfloat, GLfloat, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_ViewportIndexedf, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ViewportIndexedfv)(GLuint, const GLfloat *);
#define CALL_ViewportIndexedfv(disp, parameters) (* GET_ViewportIndexedfv(disp)) parameters
#define GET_ViewportIndexedfv(disp) ((_glptr_ViewportIndexedfv)(GET_by_offset((disp), _gloffset_ViewportIndexedfv)))
#define SET_ViewportIndexedfv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_ViewportIndexedfv, fn); \
} while (0)

typedef GLenum (GLAPIENTRYP _glptr_GetGraphicsResetStatusARB)(void);
#define CALL_GetGraphicsResetStatusARB(disp, parameters) (* GET_GetGraphicsResetStatusARB(disp)) parameters
#define GET_GetGraphicsResetStatusARB(disp) ((_glptr_GetGraphicsResetStatusARB)(GET_by_offset((disp), _gloffset_GetGraphicsResetStatusARB)))
#define SET_GetGraphicsResetStatusARB(disp, func) do { \
   GLenum (GLAPIENTRYP fn)(void) = func; \
   SET_by_offset(disp, _gloffset_GetGraphicsResetStatusARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetnColorTableARB)(GLenum, GLenum, GLenum, GLsizei, GLvoid *);
#define CALL_GetnColorTableARB(disp, parameters) (* GET_GetnColorTableARB(disp)) parameters
#define GET_GetnColorTableARB(disp) ((_glptr_GetnColorTableARB)(GET_by_offset((disp), _gloffset_GetnColorTableARB)))
#define SET_GetnColorTableARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLenum, GLsizei, GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_GetnColorTableARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetnCompressedTexImageARB)(GLenum, GLint, GLsizei, GLvoid *);
#define CALL_GetnCompressedTexImageARB(disp, parameters) (* GET_GetnCompressedTexImageARB(disp)) parameters
#define GET_GetnCompressedTexImageARB(disp) ((_glptr_GetnCompressedTexImageARB)(GET_by_offset((disp), _gloffset_GetnCompressedTexImageARB)))
#define SET_GetnCompressedTexImageARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLint, GLsizei, GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_GetnCompressedTexImageARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetnConvolutionFilterARB)(GLenum, GLenum, GLenum, GLsizei, GLvoid *);
#define CALL_GetnConvolutionFilterARB(disp, parameters) (* GET_GetnConvolutionFilterARB(disp)) parameters
#define GET_GetnConvolutionFilterARB(disp) ((_glptr_GetnConvolutionFilterARB)(GET_by_offset((disp), _gloffset_GetnConvolutionFilterARB)))
#define SET_GetnConvolutionFilterARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLenum, GLsizei, GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_GetnConvolutionFilterARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetnHistogramARB)(GLenum, GLboolean, GLenum, GLenum, GLsizei, GLvoid *);
#define CALL_GetnHistogramARB(disp, parameters) (* GET_GetnHistogramARB(disp)) parameters
#define GET_GetnHistogramARB(disp) ((_glptr_GetnHistogramARB)(GET_by_offset((disp), _gloffset_GetnHistogramARB)))
#define SET_GetnHistogramARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLboolean, GLenum, GLenum, GLsizei, GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_GetnHistogramARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetnMapdvARB)(GLenum, GLenum, GLsizei, GLdouble *);
#define CALL_GetnMapdvARB(disp, parameters) (* GET_GetnMapdvARB(disp)) parameters
#define GET_GetnMapdvARB(disp) ((_glptr_GetnMapdvARB)(GET_by_offset((disp), _gloffset_GetnMapdvARB)))
#define SET_GetnMapdvARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLsizei, GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_GetnMapdvARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetnMapfvARB)(GLenum, GLenum, GLsizei, GLfloat *);
#define CALL_GetnMapfvARB(disp, parameters) (* GET_GetnMapfvARB(disp)) parameters
#define GET_GetnMapfvARB(disp) ((_glptr_GetnMapfvARB)(GET_by_offset((disp), _gloffset_GetnMapfvARB)))
#define SET_GetnMapfvARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLsizei, GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_GetnMapfvARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetnMapivARB)(GLenum, GLenum, GLsizei, GLint *);
#define CALL_GetnMapivARB(disp, parameters) (* GET_GetnMapivARB(disp)) parameters
#define GET_GetnMapivARB(disp) ((_glptr_GetnMapivARB)(GET_by_offset((disp), _gloffset_GetnMapivARB)))
#define SET_GetnMapivARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLsizei, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetnMapivARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetnMinmaxARB)(GLenum, GLboolean, GLenum, GLenum, GLsizei, GLvoid *);
#define CALL_GetnMinmaxARB(disp, parameters) (* GET_GetnMinmaxARB(disp)) parameters
#define GET_GetnMinmaxARB(disp) ((_glptr_GetnMinmaxARB)(GET_by_offset((disp), _gloffset_GetnMinmaxARB)))
#define SET_GetnMinmaxARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLboolean, GLenum, GLenum, GLsizei, GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_GetnMinmaxARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetnPixelMapfvARB)(GLenum, GLsizei, GLfloat *);
#define CALL_GetnPixelMapfvARB(disp, parameters) (* GET_GetnPixelMapfvARB(disp)) parameters
#define GET_GetnPixelMapfvARB(disp) ((_glptr_GetnPixelMapfvARB)(GET_by_offset((disp), _gloffset_GetnPixelMapfvARB)))
#define SET_GetnPixelMapfvARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLsizei, GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_GetnPixelMapfvARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetnPixelMapuivARB)(GLenum, GLsizei, GLuint *);
#define CALL_GetnPixelMapuivARB(disp, parameters) (* GET_GetnPixelMapuivARB(disp)) parameters
#define GET_GetnPixelMapuivARB(disp) ((_glptr_GetnPixelMapuivARB)(GET_by_offset((disp), _gloffset_GetnPixelMapuivARB)))
#define SET_GetnPixelMapuivARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLsizei, GLuint *) = func; \
   SET_by_offset(disp, _gloffset_GetnPixelMapuivARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetnPixelMapusvARB)(GLenum, GLsizei, GLushort *);
#define CALL_GetnPixelMapusvARB(disp, parameters) (* GET_GetnPixelMapusvARB(disp)) parameters
#define GET_GetnPixelMapusvARB(disp) ((_glptr_GetnPixelMapusvARB)(GET_by_offset((disp), _gloffset_GetnPixelMapusvARB)))
#define SET_GetnPixelMapusvARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLsizei, GLushort *) = func; \
   SET_by_offset(disp, _gloffset_GetnPixelMapusvARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetnPolygonStippleARB)(GLsizei, GLubyte *);
#define CALL_GetnPolygonStippleARB(disp, parameters) (* GET_GetnPolygonStippleARB(disp)) parameters
#define GET_GetnPolygonStippleARB(disp) ((_glptr_GetnPolygonStippleARB)(GET_by_offset((disp), _gloffset_GetnPolygonStippleARB)))
#define SET_GetnPolygonStippleARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLsizei, GLubyte *) = func; \
   SET_by_offset(disp, _gloffset_GetnPolygonStippleARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetnSeparableFilterARB)(GLenum, GLenum, GLenum, GLsizei, GLvoid *, GLsizei, GLvoid *, GLvoid *);
#define CALL_GetnSeparableFilterARB(disp, parameters) (* GET_GetnSeparableFilterARB(disp)) parameters
#define GET_GetnSeparableFilterARB(disp) ((_glptr_GetnSeparableFilterARB)(GET_by_offset((disp), _gloffset_GetnSeparableFilterARB)))
#define SET_GetnSeparableFilterARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLenum, GLsizei, GLvoid *, GLsizei, GLvoid *, GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_GetnSeparableFilterARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetnTexImageARB)(GLenum, GLint, GLenum, GLenum, GLsizei, GLvoid *);
#define CALL_GetnTexImageARB(disp, parameters) (* GET_GetnTexImageARB(disp)) parameters
#define GET_GetnTexImageARB(disp) ((_glptr_GetnTexImageARB)(GET_by_offset((disp), _gloffset_GetnTexImageARB)))
#define SET_GetnTexImageARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLint, GLenum, GLenum, GLsizei, GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_GetnTexImageARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetnUniformdvARB)(GLuint, GLint, GLsizei, GLdouble *);
#define CALL_GetnUniformdvARB(disp, parameters) (* GET_GetnUniformdvARB(disp)) parameters
#define GET_GetnUniformdvARB(disp) ((_glptr_GetnUniformdvARB)(GET_by_offset((disp), _gloffset_GetnUniformdvARB)))
#define SET_GetnUniformdvARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLsizei, GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_GetnUniformdvARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetnUniformfvARB)(GLuint, GLint, GLsizei, GLfloat *);
#define CALL_GetnUniformfvARB(disp, parameters) (* GET_GetnUniformfvARB(disp)) parameters
#define GET_GetnUniformfvARB(disp) ((_glptr_GetnUniformfvARB)(GET_by_offset((disp), _gloffset_GetnUniformfvARB)))
#define SET_GetnUniformfvARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLsizei, GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_GetnUniformfvARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetnUniformivARB)(GLuint, GLint, GLsizei, GLint *);
#define CALL_GetnUniformivARB(disp, parameters) (* GET_GetnUniformivARB(disp)) parameters
#define GET_GetnUniformivARB(disp) ((_glptr_GetnUniformivARB)(GET_by_offset((disp), _gloffset_GetnUniformivARB)))
#define SET_GetnUniformivARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLsizei, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetnUniformivARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetnUniformuivARB)(GLuint, GLint, GLsizei, GLuint *);
#define CALL_GetnUniformuivARB(disp, parameters) (* GET_GetnUniformuivARB(disp)) parameters
#define GET_GetnUniformuivARB(disp) ((_glptr_GetnUniformuivARB)(GET_by_offset((disp), _gloffset_GetnUniformuivARB)))
#define SET_GetnUniformuivARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLsizei, GLuint *) = func; \
   SET_by_offset(disp, _gloffset_GetnUniformuivARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ReadnPixelsARB)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, GLsizei, GLvoid *);
#define CALL_ReadnPixelsARB(disp, parameters) (* GET_ReadnPixelsARB(disp)) parameters
#define GET_ReadnPixelsARB(disp) ((_glptr_ReadnPixelsARB)(GET_by_offset((disp), _gloffset_ReadnPixelsARB)))
#define SET_ReadnPixelsARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, GLsizei, GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_ReadnPixelsARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DrawArraysInstancedBaseInstance)(GLenum, GLint, GLsizei, GLsizei, GLuint);
#define CALL_DrawArraysInstancedBaseInstance(disp, parameters) (* GET_DrawArraysInstancedBaseInstance(disp)) parameters
#define GET_DrawArraysInstancedBaseInstance(disp) ((_glptr_DrawArraysInstancedBaseInstance)(GET_by_offset((disp), _gloffset_DrawArraysInstancedBaseInstance)))
#define SET_DrawArraysInstancedBaseInstance(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLint, GLsizei, GLsizei, GLuint) = func; \
   SET_by_offset(disp, _gloffset_DrawArraysInstancedBaseInstance, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DrawElementsInstancedBaseInstance)(GLenum, GLsizei, GLenum, const GLvoid *, GLsizei, GLuint);
#define CALL_DrawElementsInstancedBaseInstance(disp, parameters) (* GET_DrawElementsInstancedBaseInstance(disp)) parameters
#define GET_DrawElementsInstancedBaseInstance(disp) ((_glptr_DrawElementsInstancedBaseInstance)(GET_by_offset((disp), _gloffset_DrawElementsInstancedBaseInstance)))
#define SET_DrawElementsInstancedBaseInstance(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLsizei, GLenum, const GLvoid *, GLsizei, GLuint) = func; \
   SET_by_offset(disp, _gloffset_DrawElementsInstancedBaseInstance, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DrawElementsInstancedBaseVertexBaseInstance)(GLenum, GLsizei, GLenum, const GLvoid *, GLsizei, GLint, GLuint);
#define CALL_DrawElementsInstancedBaseVertexBaseInstance(disp, parameters) (* GET_DrawElementsInstancedBaseVertexBaseInstance(disp)) parameters
#define GET_DrawElementsInstancedBaseVertexBaseInstance(disp) ((_glptr_DrawElementsInstancedBaseVertexBaseInstance)(GET_by_offset((disp), _gloffset_DrawElementsInstancedBaseVertexBaseInstance)))
#define SET_DrawElementsInstancedBaseVertexBaseInstance(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLsizei, GLenum, const GLvoid *, GLsizei, GLint, GLuint) = func; \
   SET_by_offset(disp, _gloffset_DrawElementsInstancedBaseVertexBaseInstance, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DrawTransformFeedbackInstanced)(GLenum, GLuint, GLsizei);
#define CALL_DrawTransformFeedbackInstanced(disp, parameters) (* GET_DrawTransformFeedbackInstanced(disp)) parameters
#define GET_DrawTransformFeedbackInstanced(disp) ((_glptr_DrawTransformFeedbackInstanced)(GET_by_offset((disp), _gloffset_DrawTransformFeedbackInstanced)))
#define SET_DrawTransformFeedbackInstanced(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_DrawTransformFeedbackInstanced, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DrawTransformFeedbackStreamInstanced)(GLenum, GLuint, GLuint, GLsizei);
#define CALL_DrawTransformFeedbackStreamInstanced(disp, parameters) (* GET_DrawTransformFeedbackStreamInstanced(disp)) parameters
#define GET_DrawTransformFeedbackStreamInstanced(disp) ((_glptr_DrawTransformFeedbackStreamInstanced)(GET_by_offset((disp), _gloffset_DrawTransformFeedbackStreamInstanced)))
#define SET_DrawTransformFeedbackStreamInstanced(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint, GLuint, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_DrawTransformFeedbackStreamInstanced, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetInternalformativ)(GLenum, GLenum, GLenum, GLsizei, GLint *);
#define CALL_GetInternalformativ(disp, parameters) (* GET_GetInternalformativ(disp)) parameters
#define GET_GetInternalformativ(disp) ((_glptr_GetInternalformativ)(GET_by_offset((disp), _gloffset_GetInternalformativ)))
#define SET_GetInternalformativ(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLenum, GLsizei, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetInternalformativ, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetActiveAtomicCounterBufferiv)(GLuint, GLuint, GLenum, GLint *);
#define CALL_GetActiveAtomicCounterBufferiv(disp, parameters) (* GET_GetActiveAtomicCounterBufferiv(disp)) parameters
#define GET_GetActiveAtomicCounterBufferiv(disp) ((_glptr_GetActiveAtomicCounterBufferiv)(GET_by_offset((disp), _gloffset_GetActiveAtomicCounterBufferiv)))
#define SET_GetActiveAtomicCounterBufferiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetActiveAtomicCounterBufferiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_BindImageTexture)(GLuint, GLuint, GLint, GLboolean, GLint, GLenum, GLenum);
#define CALL_BindImageTexture(disp, parameters) (* GET_BindImageTexture(disp)) parameters
#define GET_BindImageTexture(disp) ((_glptr_BindImageTexture)(GET_by_offset((disp), _gloffset_BindImageTexture)))
#define SET_BindImageTexture(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLint, GLboolean, GLint, GLenum, GLenum) = func; \
   SET_by_offset(disp, _gloffset_BindImageTexture, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MemoryBarrier)(GLbitfield);
#define CALL_MemoryBarrier(disp, parameters) (* GET_MemoryBarrier(disp)) parameters
#define GET_MemoryBarrier(disp) ((_glptr_MemoryBarrier)(GET_by_offset((disp), _gloffset_MemoryBarrier)))
#define SET_MemoryBarrier(disp, func) do { \
   void (GLAPIENTRYP fn)(GLbitfield) = func; \
   SET_by_offset(disp, _gloffset_MemoryBarrier, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexStorage1D)(GLenum, GLsizei, GLenum, GLsizei);
#define CALL_TexStorage1D(disp, parameters) (* GET_TexStorage1D(disp)) parameters
#define GET_TexStorage1D(disp) ((_glptr_TexStorage1D)(GET_by_offset((disp), _gloffset_TexStorage1D)))
#define SET_TexStorage1D(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLsizei, GLenum, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_TexStorage1D, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexStorage2D)(GLenum, GLsizei, GLenum, GLsizei, GLsizei);
#define CALL_TexStorage2D(disp, parameters) (* GET_TexStorage2D(disp)) parameters
#define GET_TexStorage2D(disp) ((_glptr_TexStorage2D)(GET_by_offset((disp), _gloffset_TexStorage2D)))
#define SET_TexStorage2D(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLsizei, GLenum, GLsizei, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_TexStorage2D, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexStorage3D)(GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLsizei);
#define CALL_TexStorage3D(disp, parameters) (* GET_TexStorage3D(disp)) parameters
#define GET_TexStorage3D(disp) ((_glptr_TexStorage3D)(GET_by_offset((disp), _gloffset_TexStorage3D)))
#define SET_TexStorage3D(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_TexStorage3D, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TextureStorage1DEXT)(GLuint, GLenum, GLsizei, GLenum, GLsizei);
#define CALL_TextureStorage1DEXT(disp, parameters) (* GET_TextureStorage1DEXT(disp)) parameters
#define GET_TextureStorage1DEXT(disp) ((_glptr_TextureStorage1DEXT)(GET_by_offset((disp), _gloffset_TextureStorage1DEXT)))
#define SET_TextureStorage1DEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLsizei, GLenum, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_TextureStorage1DEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TextureStorage2DEXT)(GLuint, GLenum, GLsizei, GLenum, GLsizei, GLsizei);
#define CALL_TextureStorage2DEXT(disp, parameters) (* GET_TextureStorage2DEXT(disp)) parameters
#define GET_TextureStorage2DEXT(disp) ((_glptr_TextureStorage2DEXT)(GET_by_offset((disp), _gloffset_TextureStorage2DEXT)))
#define SET_TextureStorage2DEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLsizei, GLenum, GLsizei, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_TextureStorage2DEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TextureStorage3DEXT)(GLuint, GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLsizei);
#define CALL_TextureStorage3DEXT(disp, parameters) (* GET_TextureStorage3DEXT(disp)) parameters
#define GET_TextureStorage3DEXT(disp) ((_glptr_TextureStorage3DEXT)(GET_by_offset((disp), _gloffset_TextureStorage3DEXT)))
#define SET_TextureStorage3DEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_TextureStorage3DEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ClearBufferData)(GLenum, GLenum, GLenum, GLenum, const GLvoid *);
#define CALL_ClearBufferData(disp, parameters) (* GET_ClearBufferData(disp)) parameters
#define GET_ClearBufferData(disp) ((_glptr_ClearBufferData)(GET_by_offset((disp), _gloffset_ClearBufferData)))
#define SET_ClearBufferData(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLenum, GLenum, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_ClearBufferData, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ClearBufferSubData)(GLenum, GLenum, GLintptr, GLsizeiptr, GLenum, GLenum, const GLvoid *);
#define CALL_ClearBufferSubData(disp, parameters) (* GET_ClearBufferSubData(disp)) parameters
#define GET_ClearBufferSubData(disp) ((_glptr_ClearBufferSubData)(GET_by_offset((disp), _gloffset_ClearBufferSubData)))
#define SET_ClearBufferSubData(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLintptr, GLsizeiptr, GLenum, GLenum, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_ClearBufferSubData, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DispatchCompute)(GLuint, GLuint, GLuint);
#define CALL_DispatchCompute(disp, parameters) (* GET_DispatchCompute(disp)) parameters
#define GET_DispatchCompute(disp) ((_glptr_DispatchCompute)(GET_by_offset((disp), _gloffset_DispatchCompute)))
#define SET_DispatchCompute(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLuint) = func; \
   SET_by_offset(disp, _gloffset_DispatchCompute, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DispatchComputeIndirect)(GLintptr);
#define CALL_DispatchComputeIndirect(disp, parameters) (* GET_DispatchComputeIndirect(disp)) parameters
#define GET_DispatchComputeIndirect(disp) ((_glptr_DispatchComputeIndirect)(GET_by_offset((disp), _gloffset_DispatchComputeIndirect)))
#define SET_DispatchComputeIndirect(disp, func) do { \
   void (GLAPIENTRYP fn)(GLintptr) = func; \
   SET_by_offset(disp, _gloffset_DispatchComputeIndirect, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CopyImageSubData)(GLuint, GLenum, GLint, GLint, GLint, GLint, GLuint, GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei);
#define CALL_CopyImageSubData(disp, parameters) (* GET_CopyImageSubData(disp)) parameters
#define GET_CopyImageSubData(disp) ((_glptr_CopyImageSubData)(GET_by_offset((disp), _gloffset_CopyImageSubData)))
#define SET_CopyImageSubData(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLint, GLint, GLint, GLint, GLuint, GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_CopyImageSubData, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TextureView)(GLuint, GLenum, GLuint, GLenum, GLuint, GLuint, GLuint, GLuint);
#define CALL_TextureView(disp, parameters) (* GET_TextureView(disp)) parameters
#define GET_TextureView(disp) ((_glptr_TextureView)(GET_by_offset((disp), _gloffset_TextureView)))
#define SET_TextureView(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLuint, GLenum, GLuint, GLuint, GLuint, GLuint) = func; \
   SET_by_offset(disp, _gloffset_TextureView, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_BindVertexBuffer)(GLuint, GLuint, GLintptr, GLsizei);
#define CALL_BindVertexBuffer(disp, parameters) (* GET_BindVertexBuffer(disp)) parameters
#define GET_BindVertexBuffer(disp) ((_glptr_BindVertexBuffer)(GET_by_offset((disp), _gloffset_BindVertexBuffer)))
#define SET_BindVertexBuffer(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLintptr, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_BindVertexBuffer, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribBinding)(GLuint, GLuint);
#define CALL_VertexAttribBinding(disp, parameters) (* GET_VertexAttribBinding(disp)) parameters
#define GET_VertexAttribBinding(disp) ((_glptr_VertexAttribBinding)(GET_by_offset((disp), _gloffset_VertexAttribBinding)))
#define SET_VertexAttribBinding(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribBinding, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribFormat)(GLuint, GLint, GLenum, GLboolean, GLuint);
#define CALL_VertexAttribFormat(disp, parameters) (* GET_VertexAttribFormat(disp)) parameters
#define GET_VertexAttribFormat(disp) ((_glptr_VertexAttribFormat)(GET_by_offset((disp), _gloffset_VertexAttribFormat)))
#define SET_VertexAttribFormat(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLenum, GLboolean, GLuint) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribFormat, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribIFormat)(GLuint, GLint, GLenum, GLuint);
#define CALL_VertexAttribIFormat(disp, parameters) (* GET_VertexAttribIFormat(disp)) parameters
#define GET_VertexAttribIFormat(disp) ((_glptr_VertexAttribIFormat)(GET_by_offset((disp), _gloffset_VertexAttribIFormat)))
#define SET_VertexAttribIFormat(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLenum, GLuint) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribIFormat, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribLFormat)(GLuint, GLint, GLenum, GLuint);
#define CALL_VertexAttribLFormat(disp, parameters) (* GET_VertexAttribLFormat(disp)) parameters
#define GET_VertexAttribLFormat(disp) ((_glptr_VertexAttribLFormat)(GET_by_offset((disp), _gloffset_VertexAttribLFormat)))
#define SET_VertexAttribLFormat(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLenum, GLuint) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribLFormat, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexBindingDivisor)(GLuint, GLuint);
#define CALL_VertexBindingDivisor(disp, parameters) (* GET_VertexBindingDivisor(disp)) parameters
#define GET_VertexBindingDivisor(disp) ((_glptr_VertexBindingDivisor)(GET_by_offset((disp), _gloffset_VertexBindingDivisor)))
#define SET_VertexBindingDivisor(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint) = func; \
   SET_by_offset(disp, _gloffset_VertexBindingDivisor, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_FramebufferParameteri)(GLenum, GLenum, GLint);
#define CALL_FramebufferParameteri(disp, parameters) (* GET_FramebufferParameteri(disp)) parameters
#define GET_FramebufferParameteri(disp) ((_glptr_FramebufferParameteri)(GET_by_offset((disp), _gloffset_FramebufferParameteri)))
#define SET_FramebufferParameteri(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLint) = func; \
   SET_by_offset(disp, _gloffset_FramebufferParameteri, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetFramebufferParameteriv)(GLenum, GLenum, GLint *);
#define CALL_GetFramebufferParameteriv(disp, parameters) (* GET_GetFramebufferParameteriv(disp)) parameters
#define GET_GetFramebufferParameteriv(disp) ((_glptr_GetFramebufferParameteriv)(GET_by_offset((disp), _gloffset_GetFramebufferParameteriv)))
#define SET_GetFramebufferParameteriv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetFramebufferParameteriv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetInternalformati64v)(GLenum, GLenum, GLenum, GLsizei, GLint64 *);
#define CALL_GetInternalformati64v(disp, parameters) (* GET_GetInternalformati64v(disp)) parameters
#define GET_GetInternalformati64v(disp) ((_glptr_GetInternalformati64v)(GET_by_offset((disp), _gloffset_GetInternalformati64v)))
#define SET_GetInternalformati64v(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLenum, GLsizei, GLint64 *) = func; \
   SET_by_offset(disp, _gloffset_GetInternalformati64v, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiDrawArraysIndirect)(GLenum, const GLvoid *, GLsizei, GLsizei);
#define CALL_MultiDrawArraysIndirect(disp, parameters) (* GET_MultiDrawArraysIndirect(disp)) parameters
#define GET_MultiDrawArraysIndirect(disp) ((_glptr_MultiDrawArraysIndirect)(GET_by_offset((disp), _gloffset_MultiDrawArraysIndirect)))
#define SET_MultiDrawArraysIndirect(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, const GLvoid *, GLsizei, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_MultiDrawArraysIndirect, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiDrawElementsIndirect)(GLenum, GLenum, const GLvoid *, GLsizei, GLsizei);
#define CALL_MultiDrawElementsIndirect(disp, parameters) (* GET_MultiDrawElementsIndirect(disp)) parameters
#define GET_MultiDrawElementsIndirect(disp) ((_glptr_MultiDrawElementsIndirect)(GET_by_offset((disp), _gloffset_MultiDrawElementsIndirect)))
#define SET_MultiDrawElementsIndirect(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, const GLvoid *, GLsizei, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_MultiDrawElementsIndirect, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetProgramInterfaceiv)(GLuint, GLenum, GLenum, GLint *);
#define CALL_GetProgramInterfaceiv(disp, parameters) (* GET_GetProgramInterfaceiv(disp)) parameters
#define GET_GetProgramInterfaceiv(disp) ((_glptr_GetProgramInterfaceiv)(GET_by_offset((disp), _gloffset_GetProgramInterfaceiv)))
#define SET_GetProgramInterfaceiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetProgramInterfaceiv, fn); \
} while (0)

typedef GLuint (GLAPIENTRYP _glptr_GetProgramResourceIndex)(GLuint, GLenum, const GLchar *);
#define CALL_GetProgramResourceIndex(disp, parameters) (* GET_GetProgramResourceIndex(disp)) parameters
#define GET_GetProgramResourceIndex(disp) ((_glptr_GetProgramResourceIndex)(GET_by_offset((disp), _gloffset_GetProgramResourceIndex)))
#define SET_GetProgramResourceIndex(disp, func) do { \
   GLuint (GLAPIENTRYP fn)(GLuint, GLenum, const GLchar *) = func; \
   SET_by_offset(disp, _gloffset_GetProgramResourceIndex, fn); \
} while (0)

typedef GLint (GLAPIENTRYP _glptr_GetProgramResourceLocation)(GLuint, GLenum, const GLchar *);
#define CALL_GetProgramResourceLocation(disp, parameters) (* GET_GetProgramResourceLocation(disp)) parameters
#define GET_GetProgramResourceLocation(disp) ((_glptr_GetProgramResourceLocation)(GET_by_offset((disp), _gloffset_GetProgramResourceLocation)))
#define SET_GetProgramResourceLocation(disp, func) do { \
   GLint (GLAPIENTRYP fn)(GLuint, GLenum, const GLchar *) = func; \
   SET_by_offset(disp, _gloffset_GetProgramResourceLocation, fn); \
} while (0)

typedef GLint (GLAPIENTRYP _glptr_GetProgramResourceLocationIndex)(GLuint, GLenum, const GLchar *);
#define CALL_GetProgramResourceLocationIndex(disp, parameters) (* GET_GetProgramResourceLocationIndex(disp)) parameters
#define GET_GetProgramResourceLocationIndex(disp) ((_glptr_GetProgramResourceLocationIndex)(GET_by_offset((disp), _gloffset_GetProgramResourceLocationIndex)))
#define SET_GetProgramResourceLocationIndex(disp, func) do { \
   GLint (GLAPIENTRYP fn)(GLuint, GLenum, const GLchar *) = func; \
   SET_by_offset(disp, _gloffset_GetProgramResourceLocationIndex, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetProgramResourceName)(GLuint, GLenum, GLuint, GLsizei, GLsizei *, GLchar *);
#define CALL_GetProgramResourceName(disp, parameters) (* GET_GetProgramResourceName(disp)) parameters
#define GET_GetProgramResourceName(disp) ((_glptr_GetProgramResourceName)(GET_by_offset((disp), _gloffset_GetProgramResourceName)))
#define SET_GetProgramResourceName(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLuint, GLsizei, GLsizei *, GLchar *) = func; \
   SET_by_offset(disp, _gloffset_GetProgramResourceName, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetProgramResourceiv)(GLuint, GLenum, GLuint, GLsizei, const GLenum *, GLsizei, GLsizei *, GLint *);
#define CALL_GetProgramResourceiv(disp, parameters) (* GET_GetProgramResourceiv(disp)) parameters
#define GET_GetProgramResourceiv(disp) ((_glptr_GetProgramResourceiv)(GET_by_offset((disp), _gloffset_GetProgramResourceiv)))
#define SET_GetProgramResourceiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLuint, GLsizei, const GLenum *, GLsizei, GLsizei *, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetProgramResourceiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ShaderStorageBlockBinding)(GLuint, GLuint, GLuint);
#define CALL_ShaderStorageBlockBinding(disp, parameters) (* GET_ShaderStorageBlockBinding(disp)) parameters
#define GET_ShaderStorageBlockBinding(disp) ((_glptr_ShaderStorageBlockBinding)(GET_by_offset((disp), _gloffset_ShaderStorageBlockBinding)))
#define SET_ShaderStorageBlockBinding(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLuint) = func; \
   SET_by_offset(disp, _gloffset_ShaderStorageBlockBinding, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexBufferRange)(GLenum, GLenum, GLuint, GLintptr, GLsizeiptr);
#define CALL_TexBufferRange(disp, parameters) (* GET_TexBufferRange(disp)) parameters
#define GET_TexBufferRange(disp) ((_glptr_TexBufferRange)(GET_by_offset((disp), _gloffset_TexBufferRange)))
#define SET_TexBufferRange(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLuint, GLintptr, GLsizeiptr) = func; \
   SET_by_offset(disp, _gloffset_TexBufferRange, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexStorage2DMultisample)(GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLboolean);
#define CALL_TexStorage2DMultisample(disp, parameters) (* GET_TexStorage2DMultisample(disp)) parameters
#define GET_TexStorage2DMultisample(disp) ((_glptr_TexStorage2DMultisample)(GET_by_offset((disp), _gloffset_TexStorage2DMultisample)))
#define SET_TexStorage2DMultisample(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLboolean) = func; \
   SET_by_offset(disp, _gloffset_TexStorage2DMultisample, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexStorage3DMultisample)(GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLsizei, GLboolean);
#define CALL_TexStorage3DMultisample(disp, parameters) (* GET_TexStorage3DMultisample(disp)) parameters
#define GET_TexStorage3DMultisample(disp) ((_glptr_TexStorage3DMultisample)(GET_by_offset((disp), _gloffset_TexStorage3DMultisample)))
#define SET_TexStorage3DMultisample(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLsizei, GLboolean) = func; \
   SET_by_offset(disp, _gloffset_TexStorage3DMultisample, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_BufferStorage)(GLenum, GLsizeiptr, const GLvoid *, GLbitfield);
#define CALL_BufferStorage(disp, parameters) (* GET_BufferStorage(disp)) parameters
#define GET_BufferStorage(disp) ((_glptr_BufferStorage)(GET_by_offset((disp), _gloffset_BufferStorage)))
#define SET_BufferStorage(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLsizeiptr, const GLvoid *, GLbitfield) = func; \
   SET_by_offset(disp, _gloffset_BufferStorage, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ClearTexImage)(GLuint, GLint, GLenum, GLenum, const GLvoid *);
#define CALL_ClearTexImage(disp, parameters) (* GET_ClearTexImage(disp)) parameters
#define GET_ClearTexImage(disp) ((_glptr_ClearTexImage)(GET_by_offset((disp), _gloffset_ClearTexImage)))
#define SET_ClearTexImage(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLenum, GLenum, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_ClearTexImage, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ClearTexSubImage)(GLuint, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLenum, const GLvoid *);
#define CALL_ClearTexSubImage(disp, parameters) (* GET_ClearTexSubImage(disp)) parameters
#define GET_ClearTexSubImage(disp) ((_glptr_ClearTexSubImage)(GET_by_offset((disp), _gloffset_ClearTexSubImage)))
#define SET_ClearTexSubImage(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLenum, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_ClearTexSubImage, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_BindBuffersBase)(GLenum, GLuint, GLsizei, const GLuint *);
#define CALL_BindBuffersBase(disp, parameters) (* GET_BindBuffersBase(disp)) parameters
#define GET_BindBuffersBase(disp) ((_glptr_BindBuffersBase)(GET_by_offset((disp), _gloffset_BindBuffersBase)))
#define SET_BindBuffersBase(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint, GLsizei, const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_BindBuffersBase, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_BindBuffersRange)(GLenum, GLuint, GLsizei, const GLuint *, const GLintptr *, const GLsizeiptr *);
#define CALL_BindBuffersRange(disp, parameters) (* GET_BindBuffersRange(disp)) parameters
#define GET_BindBuffersRange(disp) ((_glptr_BindBuffersRange)(GET_by_offset((disp), _gloffset_BindBuffersRange)))
#define SET_BindBuffersRange(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint, GLsizei, const GLuint *, const GLintptr *, const GLsizeiptr *) = func; \
   SET_by_offset(disp, _gloffset_BindBuffersRange, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_BindImageTextures)(GLuint, GLsizei, const GLuint *);
#define CALL_BindImageTextures(disp, parameters) (* GET_BindImageTextures(disp)) parameters
#define GET_BindImageTextures(disp) ((_glptr_BindImageTextures)(GET_by_offset((disp), _gloffset_BindImageTextures)))
#define SET_BindImageTextures(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizei, const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_BindImageTextures, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_BindSamplers)(GLuint, GLsizei, const GLuint *);
#define CALL_BindSamplers(disp, parameters) (* GET_BindSamplers(disp)) parameters
#define GET_BindSamplers(disp) ((_glptr_BindSamplers)(GET_by_offset((disp), _gloffset_BindSamplers)))
#define SET_BindSamplers(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizei, const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_BindSamplers, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_BindTextures)(GLuint, GLsizei, const GLuint *);
#define CALL_BindTextures(disp, parameters) (* GET_BindTextures(disp)) parameters
#define GET_BindTextures(disp) ((_glptr_BindTextures)(GET_by_offset((disp), _gloffset_BindTextures)))
#define SET_BindTextures(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizei, const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_BindTextures, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_BindVertexBuffers)(GLuint, GLsizei, const GLuint *, const GLintptr *, const GLsizei *);
#define CALL_BindVertexBuffers(disp, parameters) (* GET_BindVertexBuffers(disp)) parameters
#define GET_BindVertexBuffers(disp) ((_glptr_BindVertexBuffers)(GET_by_offset((disp), _gloffset_BindVertexBuffers)))
#define SET_BindVertexBuffers(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizei, const GLuint *, const GLintptr *, const GLsizei *) = func; \
   SET_by_offset(disp, _gloffset_BindVertexBuffers, fn); \
} while (0)

typedef GLuint64 (GLAPIENTRYP _glptr_GetImageHandleARB)(GLuint, GLint, GLboolean, GLint, GLenum);
#define CALL_GetImageHandleARB(disp, parameters) (* GET_GetImageHandleARB(disp)) parameters
#define GET_GetImageHandleARB(disp) ((_glptr_GetImageHandleARB)(GET_by_offset((disp), _gloffset_GetImageHandleARB)))
#define SET_GetImageHandleARB(disp, func) do { \
   GLuint64 (GLAPIENTRYP fn)(GLuint, GLint, GLboolean, GLint, GLenum) = func; \
   SET_by_offset(disp, _gloffset_GetImageHandleARB, fn); \
} while (0)

typedef GLuint64 (GLAPIENTRYP _glptr_GetTextureHandleARB)(GLuint);
#define CALL_GetTextureHandleARB(disp, parameters) (* GET_GetTextureHandleARB(disp)) parameters
#define GET_GetTextureHandleARB(disp) ((_glptr_GetTextureHandleARB)(GET_by_offset((disp), _gloffset_GetTextureHandleARB)))
#define SET_GetTextureHandleARB(disp, func) do { \
   GLuint64 (GLAPIENTRYP fn)(GLuint) = func; \
   SET_by_offset(disp, _gloffset_GetTextureHandleARB, fn); \
} while (0)

typedef GLuint64 (GLAPIENTRYP _glptr_GetTextureSamplerHandleARB)(GLuint, GLuint);
#define CALL_GetTextureSamplerHandleARB(disp, parameters) (* GET_GetTextureSamplerHandleARB(disp)) parameters
#define GET_GetTextureSamplerHandleARB(disp) ((_glptr_GetTextureSamplerHandleARB)(GET_by_offset((disp), _gloffset_GetTextureSamplerHandleARB)))
#define SET_GetTextureSamplerHandleARB(disp, func) do { \
   GLuint64 (GLAPIENTRYP fn)(GLuint, GLuint) = func; \
   SET_by_offset(disp, _gloffset_GetTextureSamplerHandleARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetVertexAttribLui64vARB)(GLuint, GLenum, GLuint64EXT *);
#define CALL_GetVertexAttribLui64vARB(disp, parameters) (* GET_GetVertexAttribLui64vARB(disp)) parameters
#define GET_GetVertexAttribLui64vARB(disp) ((_glptr_GetVertexAttribLui64vARB)(GET_by_offset((disp), _gloffset_GetVertexAttribLui64vARB)))
#define SET_GetVertexAttribLui64vARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLuint64EXT *) = func; \
   SET_by_offset(disp, _gloffset_GetVertexAttribLui64vARB, fn); \
} while (0)

typedef GLboolean (GLAPIENTRYP _glptr_IsImageHandleResidentARB)(GLuint64);
#define CALL_IsImageHandleResidentARB(disp, parameters) (* GET_IsImageHandleResidentARB(disp)) parameters
#define GET_IsImageHandleResidentARB(disp) ((_glptr_IsImageHandleResidentARB)(GET_by_offset((disp), _gloffset_IsImageHandleResidentARB)))
#define SET_IsImageHandleResidentARB(disp, func) do { \
   GLboolean (GLAPIENTRYP fn)(GLuint64) = func; \
   SET_by_offset(disp, _gloffset_IsImageHandleResidentARB, fn); \
} while (0)

typedef GLboolean (GLAPIENTRYP _glptr_IsTextureHandleResidentARB)(GLuint64);
#define CALL_IsTextureHandleResidentARB(disp, parameters) (* GET_IsTextureHandleResidentARB(disp)) parameters
#define GET_IsTextureHandleResidentARB(disp) ((_glptr_IsTextureHandleResidentARB)(GET_by_offset((disp), _gloffset_IsTextureHandleResidentARB)))
#define SET_IsTextureHandleResidentARB(disp, func) do { \
   GLboolean (GLAPIENTRYP fn)(GLuint64) = func; \
   SET_by_offset(disp, _gloffset_IsTextureHandleResidentARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MakeImageHandleNonResidentARB)(GLuint64);
#define CALL_MakeImageHandleNonResidentARB(disp, parameters) (* GET_MakeImageHandleNonResidentARB(disp)) parameters
#define GET_MakeImageHandleNonResidentARB(disp) ((_glptr_MakeImageHandleNonResidentARB)(GET_by_offset((disp), _gloffset_MakeImageHandleNonResidentARB)))
#define SET_MakeImageHandleNonResidentARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint64) = func; \
   SET_by_offset(disp, _gloffset_MakeImageHandleNonResidentARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MakeImageHandleResidentARB)(GLuint64, GLenum);
#define CALL_MakeImageHandleResidentARB(disp, parameters) (* GET_MakeImageHandleResidentARB(disp)) parameters
#define GET_MakeImageHandleResidentARB(disp) ((_glptr_MakeImageHandleResidentARB)(GET_by_offset((disp), _gloffset_MakeImageHandleResidentARB)))
#define SET_MakeImageHandleResidentARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint64, GLenum) = func; \
   SET_by_offset(disp, _gloffset_MakeImageHandleResidentARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MakeTextureHandleNonResidentARB)(GLuint64);
#define CALL_MakeTextureHandleNonResidentARB(disp, parameters) (* GET_MakeTextureHandleNonResidentARB(disp)) parameters
#define GET_MakeTextureHandleNonResidentARB(disp) ((_glptr_MakeTextureHandleNonResidentARB)(GET_by_offset((disp), _gloffset_MakeTextureHandleNonResidentARB)))
#define SET_MakeTextureHandleNonResidentARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint64) = func; \
   SET_by_offset(disp, _gloffset_MakeTextureHandleNonResidentARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MakeTextureHandleResidentARB)(GLuint64);
#define CALL_MakeTextureHandleResidentARB(disp, parameters) (* GET_MakeTextureHandleResidentARB(disp)) parameters
#define GET_MakeTextureHandleResidentARB(disp) ((_glptr_MakeTextureHandleResidentARB)(GET_by_offset((disp), _gloffset_MakeTextureHandleResidentARB)))
#define SET_MakeTextureHandleResidentARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint64) = func; \
   SET_by_offset(disp, _gloffset_MakeTextureHandleResidentARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniformHandleui64ARB)(GLuint, GLint, GLuint64);
#define CALL_ProgramUniformHandleui64ARB(disp, parameters) (* GET_ProgramUniformHandleui64ARB(disp)) parameters
#define GET_ProgramUniformHandleui64ARB(disp) ((_glptr_ProgramUniformHandleui64ARB)(GET_by_offset((disp), _gloffset_ProgramUniformHandleui64ARB)))
#define SET_ProgramUniformHandleui64ARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLuint64) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniformHandleui64ARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniformHandleui64vARB)(GLuint, GLint, GLsizei, const GLuint64 *);
#define CALL_ProgramUniformHandleui64vARB(disp, parameters) (* GET_ProgramUniformHandleui64vARB(disp)) parameters
#define GET_ProgramUniformHandleui64vARB(disp) ((_glptr_ProgramUniformHandleui64vARB)(GET_by_offset((disp), _gloffset_ProgramUniformHandleui64vARB)))
#define SET_ProgramUniformHandleui64vARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLsizei, const GLuint64 *) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniformHandleui64vARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_UniformHandleui64ARB)(GLint, GLuint64);
#define CALL_UniformHandleui64ARB(disp, parameters) (* GET_UniformHandleui64ARB(disp)) parameters
#define GET_UniformHandleui64ARB(disp) ((_glptr_UniformHandleui64ARB)(GET_by_offset((disp), _gloffset_UniformHandleui64ARB)))
#define SET_UniformHandleui64ARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLuint64) = func; \
   SET_by_offset(disp, _gloffset_UniformHandleui64ARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_UniformHandleui64vARB)(GLint, GLsizei, const GLuint64 *);
#define CALL_UniformHandleui64vARB(disp, parameters) (* GET_UniformHandleui64vARB(disp)) parameters
#define GET_UniformHandleui64vARB(disp) ((_glptr_UniformHandleui64vARB)(GET_by_offset((disp), _gloffset_UniformHandleui64vARB)))
#define SET_UniformHandleui64vARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLsizei, const GLuint64 *) = func; \
   SET_by_offset(disp, _gloffset_UniformHandleui64vARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribL1ui64ARB)(GLuint, GLuint64EXT);
#define CALL_VertexAttribL1ui64ARB(disp, parameters) (* GET_VertexAttribL1ui64ARB(disp)) parameters
#define GET_VertexAttribL1ui64ARB(disp) ((_glptr_VertexAttribL1ui64ARB)(GET_by_offset((disp), _gloffset_VertexAttribL1ui64ARB)))
#define SET_VertexAttribL1ui64ARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint64EXT) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribL1ui64ARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribL1ui64vARB)(GLuint, const GLuint64EXT *);
#define CALL_VertexAttribL1ui64vARB(disp, parameters) (* GET_VertexAttribL1ui64vARB(disp)) parameters
#define GET_VertexAttribL1ui64vARB(disp) ((_glptr_VertexAttribL1ui64vARB)(GET_by_offset((disp), _gloffset_VertexAttribL1ui64vARB)))
#define SET_VertexAttribL1ui64vARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLuint64EXT *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribL1ui64vARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DispatchComputeGroupSizeARB)(GLuint, GLuint, GLuint, GLuint, GLuint, GLuint);
#define CALL_DispatchComputeGroupSizeARB(disp, parameters) (* GET_DispatchComputeGroupSizeARB(disp)) parameters
#define GET_DispatchComputeGroupSizeARB(disp) ((_glptr_DispatchComputeGroupSizeARB)(GET_by_offset((disp), _gloffset_DispatchComputeGroupSizeARB)))
#define SET_DispatchComputeGroupSizeARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLuint, GLuint, GLuint, GLuint) = func; \
   SET_by_offset(disp, _gloffset_DispatchComputeGroupSizeARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiDrawArraysIndirectCountARB)(GLenum, GLintptr, GLintptr, GLsizei, GLsizei);
#define CALL_MultiDrawArraysIndirectCountARB(disp, parameters) (* GET_MultiDrawArraysIndirectCountARB(disp)) parameters
#define GET_MultiDrawArraysIndirectCountARB(disp) ((_glptr_MultiDrawArraysIndirectCountARB)(GET_by_offset((disp), _gloffset_MultiDrawArraysIndirectCountARB)))
#define SET_MultiDrawArraysIndirectCountARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLintptr, GLintptr, GLsizei, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_MultiDrawArraysIndirectCountARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiDrawElementsIndirectCountARB)(GLenum, GLenum, GLintptr, GLintptr, GLsizei, GLsizei);
#define CALL_MultiDrawElementsIndirectCountARB(disp, parameters) (* GET_MultiDrawElementsIndirectCountARB(disp)) parameters
#define GET_MultiDrawElementsIndirectCountARB(disp) ((_glptr_MultiDrawElementsIndirectCountARB)(GET_by_offset((disp), _gloffset_MultiDrawElementsIndirectCountARB)))
#define SET_MultiDrawElementsIndirectCountARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLintptr, GLintptr, GLsizei, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_MultiDrawElementsIndirectCountARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ClipControl)(GLenum, GLenum);
#define CALL_ClipControl(disp, parameters) (* GET_ClipControl(disp)) parameters
#define GET_ClipControl(disp) ((_glptr_ClipControl)(GET_by_offset((disp), _gloffset_ClipControl)))
#define SET_ClipControl(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum) = func; \
   SET_by_offset(disp, _gloffset_ClipControl, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_BindTextureUnit)(GLuint, GLuint);
#define CALL_BindTextureUnit(disp, parameters) (* GET_BindTextureUnit(disp)) parameters
#define GET_BindTextureUnit(disp) ((_glptr_BindTextureUnit)(GET_by_offset((disp), _gloffset_BindTextureUnit)))
#define SET_BindTextureUnit(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint) = func; \
   SET_by_offset(disp, _gloffset_BindTextureUnit, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_BlitNamedFramebuffer)(GLuint, GLuint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
#define CALL_BlitNamedFramebuffer(disp, parameters) (* GET_BlitNamedFramebuffer(disp)) parameters
#define GET_BlitNamedFramebuffer(disp) ((_glptr_BlitNamedFramebuffer)(GET_by_offset((disp), _gloffset_BlitNamedFramebuffer)))
#define SET_BlitNamedFramebuffer(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum) = func; \
   SET_by_offset(disp, _gloffset_BlitNamedFramebuffer, fn); \
} while (0)

typedef GLenum (GLAPIENTRYP _glptr_CheckNamedFramebufferStatus)(GLuint, GLenum);
#define CALL_CheckNamedFramebufferStatus(disp, parameters) (* GET_CheckNamedFramebufferStatus(disp)) parameters
#define GET_CheckNamedFramebufferStatus(disp) ((_glptr_CheckNamedFramebufferStatus)(GET_by_offset((disp), _gloffset_CheckNamedFramebufferStatus)))
#define SET_CheckNamedFramebufferStatus(disp, func) do { \
   GLenum (GLAPIENTRYP fn)(GLuint, GLenum) = func; \
   SET_by_offset(disp, _gloffset_CheckNamedFramebufferStatus, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ClearNamedBufferData)(GLuint, GLenum, GLenum, GLenum, const GLvoid *);
#define CALL_ClearNamedBufferData(disp, parameters) (* GET_ClearNamedBufferData(disp)) parameters
#define GET_ClearNamedBufferData(disp) ((_glptr_ClearNamedBufferData)(GET_by_offset((disp), _gloffset_ClearNamedBufferData)))
#define SET_ClearNamedBufferData(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLenum, GLenum, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_ClearNamedBufferData, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ClearNamedBufferSubData)(GLuint, GLenum, GLintptr, GLsizeiptr, GLenum, GLenum, const GLvoid *);
#define CALL_ClearNamedBufferSubData(disp, parameters) (* GET_ClearNamedBufferSubData(disp)) parameters
#define GET_ClearNamedBufferSubData(disp) ((_glptr_ClearNamedBufferSubData)(GET_by_offset((disp), _gloffset_ClearNamedBufferSubData)))
#define SET_ClearNamedBufferSubData(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLintptr, GLsizeiptr, GLenum, GLenum, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_ClearNamedBufferSubData, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ClearNamedFramebufferfi)(GLuint, GLenum, GLint, GLfloat, GLint);
#define CALL_ClearNamedFramebufferfi(disp, parameters) (* GET_ClearNamedFramebufferfi(disp)) parameters
#define GET_ClearNamedFramebufferfi(disp) ((_glptr_ClearNamedFramebufferfi)(GET_by_offset((disp), _gloffset_ClearNamedFramebufferfi)))
#define SET_ClearNamedFramebufferfi(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLint, GLfloat, GLint) = func; \
   SET_by_offset(disp, _gloffset_ClearNamedFramebufferfi, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ClearNamedFramebufferfv)(GLuint, GLenum, GLint, const GLfloat *);
#define CALL_ClearNamedFramebufferfv(disp, parameters) (* GET_ClearNamedFramebufferfv(disp)) parameters
#define GET_ClearNamedFramebufferfv(disp) ((_glptr_ClearNamedFramebufferfv)(GET_by_offset((disp), _gloffset_ClearNamedFramebufferfv)))
#define SET_ClearNamedFramebufferfv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLint, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_ClearNamedFramebufferfv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ClearNamedFramebufferiv)(GLuint, GLenum, GLint, const GLint *);
#define CALL_ClearNamedFramebufferiv(disp, parameters) (* GET_ClearNamedFramebufferiv(disp)) parameters
#define GET_ClearNamedFramebufferiv(disp) ((_glptr_ClearNamedFramebufferiv)(GET_by_offset((disp), _gloffset_ClearNamedFramebufferiv)))
#define SET_ClearNamedFramebufferiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLint, const GLint *) = func; \
   SET_by_offset(disp, _gloffset_ClearNamedFramebufferiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ClearNamedFramebufferuiv)(GLuint, GLenum, GLint, const GLuint *);
#define CALL_ClearNamedFramebufferuiv(disp, parameters) (* GET_ClearNamedFramebufferuiv(disp)) parameters
#define GET_ClearNamedFramebufferuiv(disp) ((_glptr_ClearNamedFramebufferuiv)(GET_by_offset((disp), _gloffset_ClearNamedFramebufferuiv)))
#define SET_ClearNamedFramebufferuiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLint, const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_ClearNamedFramebufferuiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CompressedTextureSubImage1D)(GLuint, GLint, GLint, GLsizei, GLenum, GLsizei, const GLvoid *);
#define CALL_CompressedTextureSubImage1D(disp, parameters) (* GET_CompressedTextureSubImage1D(disp)) parameters
#define GET_CompressedTextureSubImage1D(disp) ((_glptr_CompressedTextureSubImage1D)(GET_by_offset((disp), _gloffset_CompressedTextureSubImage1D)))
#define SET_CompressedTextureSubImage1D(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLint, GLsizei, GLenum, GLsizei, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_CompressedTextureSubImage1D, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CompressedTextureSubImage2D)(GLuint, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLsizei, const GLvoid *);
#define CALL_CompressedTextureSubImage2D(disp, parameters) (* GET_CompressedTextureSubImage2D(disp)) parameters
#define GET_CompressedTextureSubImage2D(disp) ((_glptr_CompressedTextureSubImage2D)(GET_by_offset((disp), _gloffset_CompressedTextureSubImage2D)))
#define SET_CompressedTextureSubImage2D(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLsizei, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_CompressedTextureSubImage2D, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CompressedTextureSubImage3D)(GLuint, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLsizei, const GLvoid *);
#define CALL_CompressedTextureSubImage3D(disp, parameters) (* GET_CompressedTextureSubImage3D(disp)) parameters
#define GET_CompressedTextureSubImage3D(disp) ((_glptr_CompressedTextureSubImage3D)(GET_by_offset((disp), _gloffset_CompressedTextureSubImage3D)))
#define SET_CompressedTextureSubImage3D(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLsizei, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_CompressedTextureSubImage3D, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CopyNamedBufferSubData)(GLuint, GLuint, GLintptr, GLintptr, GLsizeiptr);
#define CALL_CopyNamedBufferSubData(disp, parameters) (* GET_CopyNamedBufferSubData(disp)) parameters
#define GET_CopyNamedBufferSubData(disp) ((_glptr_CopyNamedBufferSubData)(GET_by_offset((disp), _gloffset_CopyNamedBufferSubData)))
#define SET_CopyNamedBufferSubData(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLintptr, GLintptr, GLsizeiptr) = func; \
   SET_by_offset(disp, _gloffset_CopyNamedBufferSubData, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CopyTextureSubImage1D)(GLuint, GLint, GLint, GLint, GLint, GLsizei);
#define CALL_CopyTextureSubImage1D(disp, parameters) (* GET_CopyTextureSubImage1D(disp)) parameters
#define GET_CopyTextureSubImage1D(disp) ((_glptr_CopyTextureSubImage1D)(GET_by_offset((disp), _gloffset_CopyTextureSubImage1D)))
#define SET_CopyTextureSubImage1D(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLint, GLint, GLint, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_CopyTextureSubImage1D, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CopyTextureSubImage2D)(GLuint, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei);
#define CALL_CopyTextureSubImage2D(disp, parameters) (* GET_CopyTextureSubImage2D(disp)) parameters
#define GET_CopyTextureSubImage2D(disp) ((_glptr_CopyTextureSubImage2D)(GET_by_offset((disp), _gloffset_CopyTextureSubImage2D)))
#define SET_CopyTextureSubImage2D(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_CopyTextureSubImage2D, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CopyTextureSubImage3D)(GLuint, GLint, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei);
#define CALL_CopyTextureSubImage3D(disp, parameters) (* GET_CopyTextureSubImage3D(disp)) parameters
#define GET_CopyTextureSubImage3D(disp) ((_glptr_CopyTextureSubImage3D)(GET_by_offset((disp), _gloffset_CopyTextureSubImage3D)))
#define SET_CopyTextureSubImage3D(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_CopyTextureSubImage3D, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CreateBuffers)(GLsizei, GLuint *);
#define CALL_CreateBuffers(disp, parameters) (* GET_CreateBuffers(disp)) parameters
#define GET_CreateBuffers(disp) ((_glptr_CreateBuffers)(GET_by_offset((disp), _gloffset_CreateBuffers)))
#define SET_CreateBuffers(disp, func) do { \
   void (GLAPIENTRYP fn)(GLsizei, GLuint *) = func; \
   SET_by_offset(disp, _gloffset_CreateBuffers, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CreateFramebuffers)(GLsizei, GLuint *);
#define CALL_CreateFramebuffers(disp, parameters) (* GET_CreateFramebuffers(disp)) parameters
#define GET_CreateFramebuffers(disp) ((_glptr_CreateFramebuffers)(GET_by_offset((disp), _gloffset_CreateFramebuffers)))
#define SET_CreateFramebuffers(disp, func) do { \
   void (GLAPIENTRYP fn)(GLsizei, GLuint *) = func; \
   SET_by_offset(disp, _gloffset_CreateFramebuffers, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CreateProgramPipelines)(GLsizei, GLuint *);
#define CALL_CreateProgramPipelines(disp, parameters) (* GET_CreateProgramPipelines(disp)) parameters
#define GET_CreateProgramPipelines(disp) ((_glptr_CreateProgramPipelines)(GET_by_offset((disp), _gloffset_CreateProgramPipelines)))
#define SET_CreateProgramPipelines(disp, func) do { \
   void (GLAPIENTRYP fn)(GLsizei, GLuint *) = func; \
   SET_by_offset(disp, _gloffset_CreateProgramPipelines, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CreateQueries)(GLenum, GLsizei, GLuint *);
#define CALL_CreateQueries(disp, parameters) (* GET_CreateQueries(disp)) parameters
#define GET_CreateQueries(disp) ((_glptr_CreateQueries)(GET_by_offset((disp), _gloffset_CreateQueries)))
#define SET_CreateQueries(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLsizei, GLuint *) = func; \
   SET_by_offset(disp, _gloffset_CreateQueries, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CreateRenderbuffers)(GLsizei, GLuint *);
#define CALL_CreateRenderbuffers(disp, parameters) (* GET_CreateRenderbuffers(disp)) parameters
#define GET_CreateRenderbuffers(disp) ((_glptr_CreateRenderbuffers)(GET_by_offset((disp), _gloffset_CreateRenderbuffers)))
#define SET_CreateRenderbuffers(disp, func) do { \
   void (GLAPIENTRYP fn)(GLsizei, GLuint *) = func; \
   SET_by_offset(disp, _gloffset_CreateRenderbuffers, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CreateSamplers)(GLsizei, GLuint *);
#define CALL_CreateSamplers(disp, parameters) (* GET_CreateSamplers(disp)) parameters
#define GET_CreateSamplers(disp) ((_glptr_CreateSamplers)(GET_by_offset((disp), _gloffset_CreateSamplers)))
#define SET_CreateSamplers(disp, func) do { \
   void (GLAPIENTRYP fn)(GLsizei, GLuint *) = func; \
   SET_by_offset(disp, _gloffset_CreateSamplers, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CreateTextures)(GLenum, GLsizei, GLuint *);
#define CALL_CreateTextures(disp, parameters) (* GET_CreateTextures(disp)) parameters
#define GET_CreateTextures(disp) ((_glptr_CreateTextures)(GET_by_offset((disp), _gloffset_CreateTextures)))
#define SET_CreateTextures(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLsizei, GLuint *) = func; \
   SET_by_offset(disp, _gloffset_CreateTextures, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CreateTransformFeedbacks)(GLsizei, GLuint *);
#define CALL_CreateTransformFeedbacks(disp, parameters) (* GET_CreateTransformFeedbacks(disp)) parameters
#define GET_CreateTransformFeedbacks(disp) ((_glptr_CreateTransformFeedbacks)(GET_by_offset((disp), _gloffset_CreateTransformFeedbacks)))
#define SET_CreateTransformFeedbacks(disp, func) do { \
   void (GLAPIENTRYP fn)(GLsizei, GLuint *) = func; \
   SET_by_offset(disp, _gloffset_CreateTransformFeedbacks, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CreateVertexArrays)(GLsizei, GLuint *);
#define CALL_CreateVertexArrays(disp, parameters) (* GET_CreateVertexArrays(disp)) parameters
#define GET_CreateVertexArrays(disp) ((_glptr_CreateVertexArrays)(GET_by_offset((disp), _gloffset_CreateVertexArrays)))
#define SET_CreateVertexArrays(disp, func) do { \
   void (GLAPIENTRYP fn)(GLsizei, GLuint *) = func; \
   SET_by_offset(disp, _gloffset_CreateVertexArrays, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DisableVertexArrayAttrib)(GLuint, GLuint);
#define CALL_DisableVertexArrayAttrib(disp, parameters) (* GET_DisableVertexArrayAttrib(disp)) parameters
#define GET_DisableVertexArrayAttrib(disp) ((_glptr_DisableVertexArrayAttrib)(GET_by_offset((disp), _gloffset_DisableVertexArrayAttrib)))
#define SET_DisableVertexArrayAttrib(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint) = func; \
   SET_by_offset(disp, _gloffset_DisableVertexArrayAttrib, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_EnableVertexArrayAttrib)(GLuint, GLuint);
#define CALL_EnableVertexArrayAttrib(disp, parameters) (* GET_EnableVertexArrayAttrib(disp)) parameters
#define GET_EnableVertexArrayAttrib(disp) ((_glptr_EnableVertexArrayAttrib)(GET_by_offset((disp), _gloffset_EnableVertexArrayAttrib)))
#define SET_EnableVertexArrayAttrib(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint) = func; \
   SET_by_offset(disp, _gloffset_EnableVertexArrayAttrib, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_FlushMappedNamedBufferRange)(GLuint, GLintptr, GLsizeiptr);
#define CALL_FlushMappedNamedBufferRange(disp, parameters) (* GET_FlushMappedNamedBufferRange(disp)) parameters
#define GET_FlushMappedNamedBufferRange(disp) ((_glptr_FlushMappedNamedBufferRange)(GET_by_offset((disp), _gloffset_FlushMappedNamedBufferRange)))
#define SET_FlushMappedNamedBufferRange(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLintptr, GLsizeiptr) = func; \
   SET_by_offset(disp, _gloffset_FlushMappedNamedBufferRange, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GenerateTextureMipmap)(GLuint);
#define CALL_GenerateTextureMipmap(disp, parameters) (* GET_GenerateTextureMipmap(disp)) parameters
#define GET_GenerateTextureMipmap(disp) ((_glptr_GenerateTextureMipmap)(GET_by_offset((disp), _gloffset_GenerateTextureMipmap)))
#define SET_GenerateTextureMipmap(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint) = func; \
   SET_by_offset(disp, _gloffset_GenerateTextureMipmap, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetCompressedTextureImage)(GLuint, GLint, GLsizei, GLvoid *);
#define CALL_GetCompressedTextureImage(disp, parameters) (* GET_GetCompressedTextureImage(disp)) parameters
#define GET_GetCompressedTextureImage(disp) ((_glptr_GetCompressedTextureImage)(GET_by_offset((disp), _gloffset_GetCompressedTextureImage)))
#define SET_GetCompressedTextureImage(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLsizei, GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_GetCompressedTextureImage, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetNamedBufferParameteri64v)(GLuint, GLenum, GLint64 *);
#define CALL_GetNamedBufferParameteri64v(disp, parameters) (* GET_GetNamedBufferParameteri64v(disp)) parameters
#define GET_GetNamedBufferParameteri64v(disp) ((_glptr_GetNamedBufferParameteri64v)(GET_by_offset((disp), _gloffset_GetNamedBufferParameteri64v)))
#define SET_GetNamedBufferParameteri64v(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLint64 *) = func; \
   SET_by_offset(disp, _gloffset_GetNamedBufferParameteri64v, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetNamedBufferParameteriv)(GLuint, GLenum, GLint *);
#define CALL_GetNamedBufferParameteriv(disp, parameters) (* GET_GetNamedBufferParameteriv(disp)) parameters
#define GET_GetNamedBufferParameteriv(disp) ((_glptr_GetNamedBufferParameteriv)(GET_by_offset((disp), _gloffset_GetNamedBufferParameteriv)))
#define SET_GetNamedBufferParameteriv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetNamedBufferParameteriv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetNamedBufferPointerv)(GLuint, GLenum, GLvoid **);
#define CALL_GetNamedBufferPointerv(disp, parameters) (* GET_GetNamedBufferPointerv(disp)) parameters
#define GET_GetNamedBufferPointerv(disp) ((_glptr_GetNamedBufferPointerv)(GET_by_offset((disp), _gloffset_GetNamedBufferPointerv)))
#define SET_GetNamedBufferPointerv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLvoid **) = func; \
   SET_by_offset(disp, _gloffset_GetNamedBufferPointerv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetNamedBufferSubData)(GLuint, GLintptr, GLsizeiptr, GLvoid *);
#define CALL_GetNamedBufferSubData(disp, parameters) (* GET_GetNamedBufferSubData(disp)) parameters
#define GET_GetNamedBufferSubData(disp) ((_glptr_GetNamedBufferSubData)(GET_by_offset((disp), _gloffset_GetNamedBufferSubData)))
#define SET_GetNamedBufferSubData(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLintptr, GLsizeiptr, GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_GetNamedBufferSubData, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetNamedFramebufferAttachmentParameteriv)(GLuint, GLenum, GLenum, GLint *);
#define CALL_GetNamedFramebufferAttachmentParameteriv(disp, parameters) (* GET_GetNamedFramebufferAttachmentParameteriv(disp)) parameters
#define GET_GetNamedFramebufferAttachmentParameteriv(disp) ((_glptr_GetNamedFramebufferAttachmentParameteriv)(GET_by_offset((disp), _gloffset_GetNamedFramebufferAttachmentParameteriv)))
#define SET_GetNamedFramebufferAttachmentParameteriv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetNamedFramebufferAttachmentParameteriv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetNamedFramebufferParameteriv)(GLuint, GLenum, GLint *);
#define CALL_GetNamedFramebufferParameteriv(disp, parameters) (* GET_GetNamedFramebufferParameteriv(disp)) parameters
#define GET_GetNamedFramebufferParameteriv(disp) ((_glptr_GetNamedFramebufferParameteriv)(GET_by_offset((disp), _gloffset_GetNamedFramebufferParameteriv)))
#define SET_GetNamedFramebufferParameteriv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetNamedFramebufferParameteriv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetNamedRenderbufferParameteriv)(GLuint, GLenum, GLint *);
#define CALL_GetNamedRenderbufferParameteriv(disp, parameters) (* GET_GetNamedRenderbufferParameteriv(disp)) parameters
#define GET_GetNamedRenderbufferParameteriv(disp) ((_glptr_GetNamedRenderbufferParameteriv)(GET_by_offset((disp), _gloffset_GetNamedRenderbufferParameteriv)))
#define SET_GetNamedRenderbufferParameteriv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetNamedRenderbufferParameteriv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetQueryBufferObjecti64v)(GLuint, GLuint, GLenum, GLintptr);
#define CALL_GetQueryBufferObjecti64v(disp, parameters) (* GET_GetQueryBufferObjecti64v(disp)) parameters
#define GET_GetQueryBufferObjecti64v(disp) ((_glptr_GetQueryBufferObjecti64v)(GET_by_offset((disp), _gloffset_GetQueryBufferObjecti64v)))
#define SET_GetQueryBufferObjecti64v(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLenum, GLintptr) = func; \
   SET_by_offset(disp, _gloffset_GetQueryBufferObjecti64v, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetQueryBufferObjectiv)(GLuint, GLuint, GLenum, GLintptr);
#define CALL_GetQueryBufferObjectiv(disp, parameters) (* GET_GetQueryBufferObjectiv(disp)) parameters
#define GET_GetQueryBufferObjectiv(disp) ((_glptr_GetQueryBufferObjectiv)(GET_by_offset((disp), _gloffset_GetQueryBufferObjectiv)))
#define SET_GetQueryBufferObjectiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLenum, GLintptr) = func; \
   SET_by_offset(disp, _gloffset_GetQueryBufferObjectiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetQueryBufferObjectui64v)(GLuint, GLuint, GLenum, GLintptr);
#define CALL_GetQueryBufferObjectui64v(disp, parameters) (* GET_GetQueryBufferObjectui64v(disp)) parameters
#define GET_GetQueryBufferObjectui64v(disp) ((_glptr_GetQueryBufferObjectui64v)(GET_by_offset((disp), _gloffset_GetQueryBufferObjectui64v)))
#define SET_GetQueryBufferObjectui64v(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLenum, GLintptr) = func; \
   SET_by_offset(disp, _gloffset_GetQueryBufferObjectui64v, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetQueryBufferObjectuiv)(GLuint, GLuint, GLenum, GLintptr);
#define CALL_GetQueryBufferObjectuiv(disp, parameters) (* GET_GetQueryBufferObjectuiv(disp)) parameters
#define GET_GetQueryBufferObjectuiv(disp) ((_glptr_GetQueryBufferObjectuiv)(GET_by_offset((disp), _gloffset_GetQueryBufferObjectuiv)))
#define SET_GetQueryBufferObjectuiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLenum, GLintptr) = func; \
   SET_by_offset(disp, _gloffset_GetQueryBufferObjectuiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetTextureImage)(GLuint, GLint, GLenum, GLenum, GLsizei, GLvoid *);
#define CALL_GetTextureImage(disp, parameters) (* GET_GetTextureImage(disp)) parameters
#define GET_GetTextureImage(disp) ((_glptr_GetTextureImage)(GET_by_offset((disp), _gloffset_GetTextureImage)))
#define SET_GetTextureImage(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLenum, GLenum, GLsizei, GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_GetTextureImage, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetTextureLevelParameterfv)(GLuint, GLint, GLenum, GLfloat *);
#define CALL_GetTextureLevelParameterfv(disp, parameters) (* GET_GetTextureLevelParameterfv(disp)) parameters
#define GET_GetTextureLevelParameterfv(disp) ((_glptr_GetTextureLevelParameterfv)(GET_by_offset((disp), _gloffset_GetTextureLevelParameterfv)))
#define SET_GetTextureLevelParameterfv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLenum, GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_GetTextureLevelParameterfv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetTextureLevelParameteriv)(GLuint, GLint, GLenum, GLint *);
#define CALL_GetTextureLevelParameteriv(disp, parameters) (* GET_GetTextureLevelParameteriv(disp)) parameters
#define GET_GetTextureLevelParameteriv(disp) ((_glptr_GetTextureLevelParameteriv)(GET_by_offset((disp), _gloffset_GetTextureLevelParameteriv)))
#define SET_GetTextureLevelParameteriv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetTextureLevelParameteriv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetTextureParameterIiv)(GLuint, GLenum, GLint *);
#define CALL_GetTextureParameterIiv(disp, parameters) (* GET_GetTextureParameterIiv(disp)) parameters
#define GET_GetTextureParameterIiv(disp) ((_glptr_GetTextureParameterIiv)(GET_by_offset((disp), _gloffset_GetTextureParameterIiv)))
#define SET_GetTextureParameterIiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetTextureParameterIiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetTextureParameterIuiv)(GLuint, GLenum, GLuint *);
#define CALL_GetTextureParameterIuiv(disp, parameters) (* GET_GetTextureParameterIuiv(disp)) parameters
#define GET_GetTextureParameterIuiv(disp) ((_glptr_GetTextureParameterIuiv)(GET_by_offset((disp), _gloffset_GetTextureParameterIuiv)))
#define SET_GetTextureParameterIuiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLuint *) = func; \
   SET_by_offset(disp, _gloffset_GetTextureParameterIuiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetTextureParameterfv)(GLuint, GLenum, GLfloat *);
#define CALL_GetTextureParameterfv(disp, parameters) (* GET_GetTextureParameterfv(disp)) parameters
#define GET_GetTextureParameterfv(disp) ((_glptr_GetTextureParameterfv)(GET_by_offset((disp), _gloffset_GetTextureParameterfv)))
#define SET_GetTextureParameterfv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_GetTextureParameterfv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetTextureParameteriv)(GLuint, GLenum, GLint *);
#define CALL_GetTextureParameteriv(disp, parameters) (* GET_GetTextureParameteriv(disp)) parameters
#define GET_GetTextureParameteriv(disp) ((_glptr_GetTextureParameteriv)(GET_by_offset((disp), _gloffset_GetTextureParameteriv)))
#define SET_GetTextureParameteriv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetTextureParameteriv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetTransformFeedbacki64_v)(GLuint, GLenum, GLuint, GLint64 *);
#define CALL_GetTransformFeedbacki64_v(disp, parameters) (* GET_GetTransformFeedbacki64_v(disp)) parameters
#define GET_GetTransformFeedbacki64_v(disp) ((_glptr_GetTransformFeedbacki64_v)(GET_by_offset((disp), _gloffset_GetTransformFeedbacki64_v)))
#define SET_GetTransformFeedbacki64_v(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLuint, GLint64 *) = func; \
   SET_by_offset(disp, _gloffset_GetTransformFeedbacki64_v, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetTransformFeedbacki_v)(GLuint, GLenum, GLuint, GLint *);
#define CALL_GetTransformFeedbacki_v(disp, parameters) (* GET_GetTransformFeedbacki_v(disp)) parameters
#define GET_GetTransformFeedbacki_v(disp) ((_glptr_GetTransformFeedbacki_v)(GET_by_offset((disp), _gloffset_GetTransformFeedbacki_v)))
#define SET_GetTransformFeedbacki_v(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLuint, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetTransformFeedbacki_v, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetTransformFeedbackiv)(GLuint, GLenum, GLint *);
#define CALL_GetTransformFeedbackiv(disp, parameters) (* GET_GetTransformFeedbackiv(disp)) parameters
#define GET_GetTransformFeedbackiv(disp) ((_glptr_GetTransformFeedbackiv)(GET_by_offset((disp), _gloffset_GetTransformFeedbackiv)))
#define SET_GetTransformFeedbackiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetTransformFeedbackiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetVertexArrayIndexed64iv)(GLuint, GLuint, GLenum, GLint64 *);
#define CALL_GetVertexArrayIndexed64iv(disp, parameters) (* GET_GetVertexArrayIndexed64iv(disp)) parameters
#define GET_GetVertexArrayIndexed64iv(disp) ((_glptr_GetVertexArrayIndexed64iv)(GET_by_offset((disp), _gloffset_GetVertexArrayIndexed64iv)))
#define SET_GetVertexArrayIndexed64iv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLenum, GLint64 *) = func; \
   SET_by_offset(disp, _gloffset_GetVertexArrayIndexed64iv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetVertexArrayIndexediv)(GLuint, GLuint, GLenum, GLint *);
#define CALL_GetVertexArrayIndexediv(disp, parameters) (* GET_GetVertexArrayIndexediv(disp)) parameters
#define GET_GetVertexArrayIndexediv(disp) ((_glptr_GetVertexArrayIndexediv)(GET_by_offset((disp), _gloffset_GetVertexArrayIndexediv)))
#define SET_GetVertexArrayIndexediv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetVertexArrayIndexediv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetVertexArrayiv)(GLuint, GLenum, GLint *);
#define CALL_GetVertexArrayiv(disp, parameters) (* GET_GetVertexArrayiv(disp)) parameters
#define GET_GetVertexArrayiv(disp) ((_glptr_GetVertexArrayiv)(GET_by_offset((disp), _gloffset_GetVertexArrayiv)))
#define SET_GetVertexArrayiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetVertexArrayiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_InvalidateNamedFramebufferData)(GLuint, GLsizei, const GLenum *);
#define CALL_InvalidateNamedFramebufferData(disp, parameters) (* GET_InvalidateNamedFramebufferData(disp)) parameters
#define GET_InvalidateNamedFramebufferData(disp) ((_glptr_InvalidateNamedFramebufferData)(GET_by_offset((disp), _gloffset_InvalidateNamedFramebufferData)))
#define SET_InvalidateNamedFramebufferData(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizei, const GLenum *) = func; \
   SET_by_offset(disp, _gloffset_InvalidateNamedFramebufferData, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_InvalidateNamedFramebufferSubData)(GLuint, GLsizei, const GLenum *, GLint, GLint, GLsizei, GLsizei);
#define CALL_InvalidateNamedFramebufferSubData(disp, parameters) (* GET_InvalidateNamedFramebufferSubData(disp)) parameters
#define GET_InvalidateNamedFramebufferSubData(disp) ((_glptr_InvalidateNamedFramebufferSubData)(GET_by_offset((disp), _gloffset_InvalidateNamedFramebufferSubData)))
#define SET_InvalidateNamedFramebufferSubData(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizei, const GLenum *, GLint, GLint, GLsizei, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_InvalidateNamedFramebufferSubData, fn); \
} while (0)

typedef GLvoid * (GLAPIENTRYP _glptr_MapNamedBuffer)(GLuint, GLenum);
#define CALL_MapNamedBuffer(disp, parameters) (* GET_MapNamedBuffer(disp)) parameters
#define GET_MapNamedBuffer(disp) ((_glptr_MapNamedBuffer)(GET_by_offset((disp), _gloffset_MapNamedBuffer)))
#define SET_MapNamedBuffer(disp, func) do { \
   GLvoid * (GLAPIENTRYP fn)(GLuint, GLenum) = func; \
   SET_by_offset(disp, _gloffset_MapNamedBuffer, fn); \
} while (0)

typedef GLvoid * (GLAPIENTRYP _glptr_MapNamedBufferRange)(GLuint, GLintptr, GLsizeiptr, GLbitfield);
#define CALL_MapNamedBufferRange(disp, parameters) (* GET_MapNamedBufferRange(disp)) parameters
#define GET_MapNamedBufferRange(disp) ((_glptr_MapNamedBufferRange)(GET_by_offset((disp), _gloffset_MapNamedBufferRange)))
#define SET_MapNamedBufferRange(disp, func) do { \
   GLvoid * (GLAPIENTRYP fn)(GLuint, GLintptr, GLsizeiptr, GLbitfield) = func; \
   SET_by_offset(disp, _gloffset_MapNamedBufferRange, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_NamedBufferData)(GLuint, GLsizeiptr, const GLvoid *, GLenum);
#define CALL_NamedBufferData(disp, parameters) (* GET_NamedBufferData(disp)) parameters
#define GET_NamedBufferData(disp) ((_glptr_NamedBufferData)(GET_by_offset((disp), _gloffset_NamedBufferData)))
#define SET_NamedBufferData(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizeiptr, const GLvoid *, GLenum) = func; \
   SET_by_offset(disp, _gloffset_NamedBufferData, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_NamedBufferStorage)(GLuint, GLsizeiptr, const GLvoid *, GLbitfield);
#define CALL_NamedBufferStorage(disp, parameters) (* GET_NamedBufferStorage(disp)) parameters
#define GET_NamedBufferStorage(disp) ((_glptr_NamedBufferStorage)(GET_by_offset((disp), _gloffset_NamedBufferStorage)))
#define SET_NamedBufferStorage(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizeiptr, const GLvoid *, GLbitfield) = func; \
   SET_by_offset(disp, _gloffset_NamedBufferStorage, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_NamedBufferSubData)(GLuint, GLintptr, GLsizeiptr, const GLvoid *);
#define CALL_NamedBufferSubData(disp, parameters) (* GET_NamedBufferSubData(disp)) parameters
#define GET_NamedBufferSubData(disp) ((_glptr_NamedBufferSubData)(GET_by_offset((disp), _gloffset_NamedBufferSubData)))
#define SET_NamedBufferSubData(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLintptr, GLsizeiptr, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_NamedBufferSubData, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_NamedFramebufferDrawBuffer)(GLuint, GLenum);
#define CALL_NamedFramebufferDrawBuffer(disp, parameters) (* GET_NamedFramebufferDrawBuffer(disp)) parameters
#define GET_NamedFramebufferDrawBuffer(disp) ((_glptr_NamedFramebufferDrawBuffer)(GET_by_offset((disp), _gloffset_NamedFramebufferDrawBuffer)))
#define SET_NamedFramebufferDrawBuffer(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum) = func; \
   SET_by_offset(disp, _gloffset_NamedFramebufferDrawBuffer, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_NamedFramebufferDrawBuffers)(GLuint, GLsizei, const GLenum *);
#define CALL_NamedFramebufferDrawBuffers(disp, parameters) (* GET_NamedFramebufferDrawBuffers(disp)) parameters
#define GET_NamedFramebufferDrawBuffers(disp) ((_glptr_NamedFramebufferDrawBuffers)(GET_by_offset((disp), _gloffset_NamedFramebufferDrawBuffers)))
#define SET_NamedFramebufferDrawBuffers(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizei, const GLenum *) = func; \
   SET_by_offset(disp, _gloffset_NamedFramebufferDrawBuffers, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_NamedFramebufferParameteri)(GLuint, GLenum, GLint);
#define CALL_NamedFramebufferParameteri(disp, parameters) (* GET_NamedFramebufferParameteri(disp)) parameters
#define GET_NamedFramebufferParameteri(disp) ((_glptr_NamedFramebufferParameteri)(GET_by_offset((disp), _gloffset_NamedFramebufferParameteri)))
#define SET_NamedFramebufferParameteri(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLint) = func; \
   SET_by_offset(disp, _gloffset_NamedFramebufferParameteri, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_NamedFramebufferReadBuffer)(GLuint, GLenum);
#define CALL_NamedFramebufferReadBuffer(disp, parameters) (* GET_NamedFramebufferReadBuffer(disp)) parameters
#define GET_NamedFramebufferReadBuffer(disp) ((_glptr_NamedFramebufferReadBuffer)(GET_by_offset((disp), _gloffset_NamedFramebufferReadBuffer)))
#define SET_NamedFramebufferReadBuffer(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum) = func; \
   SET_by_offset(disp, _gloffset_NamedFramebufferReadBuffer, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_NamedFramebufferRenderbuffer)(GLuint, GLenum, GLenum, GLuint);
#define CALL_NamedFramebufferRenderbuffer(disp, parameters) (* GET_NamedFramebufferRenderbuffer(disp)) parameters
#define GET_NamedFramebufferRenderbuffer(disp) ((_glptr_NamedFramebufferRenderbuffer)(GET_by_offset((disp), _gloffset_NamedFramebufferRenderbuffer)))
#define SET_NamedFramebufferRenderbuffer(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLenum, GLuint) = func; \
   SET_by_offset(disp, _gloffset_NamedFramebufferRenderbuffer, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_NamedFramebufferTexture)(GLuint, GLenum, GLuint, GLint);
#define CALL_NamedFramebufferTexture(disp, parameters) (* GET_NamedFramebufferTexture(disp)) parameters
#define GET_NamedFramebufferTexture(disp) ((_glptr_NamedFramebufferTexture)(GET_by_offset((disp), _gloffset_NamedFramebufferTexture)))
#define SET_NamedFramebufferTexture(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLuint, GLint) = func; \
   SET_by_offset(disp, _gloffset_NamedFramebufferTexture, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_NamedFramebufferTextureLayer)(GLuint, GLenum, GLuint, GLint, GLint);
#define CALL_NamedFramebufferTextureLayer(disp, parameters) (* GET_NamedFramebufferTextureLayer(disp)) parameters
#define GET_NamedFramebufferTextureLayer(disp) ((_glptr_NamedFramebufferTextureLayer)(GET_by_offset((disp), _gloffset_NamedFramebufferTextureLayer)))
#define SET_NamedFramebufferTextureLayer(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLuint, GLint, GLint) = func; \
   SET_by_offset(disp, _gloffset_NamedFramebufferTextureLayer, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_NamedRenderbufferStorage)(GLuint, GLenum, GLsizei, GLsizei);
#define CALL_NamedRenderbufferStorage(disp, parameters) (* GET_NamedRenderbufferStorage(disp)) parameters
#define GET_NamedRenderbufferStorage(disp) ((_glptr_NamedRenderbufferStorage)(GET_by_offset((disp), _gloffset_NamedRenderbufferStorage)))
#define SET_NamedRenderbufferStorage(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLsizei, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_NamedRenderbufferStorage, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_NamedRenderbufferStorageMultisample)(GLuint, GLsizei, GLenum, GLsizei, GLsizei);
#define CALL_NamedRenderbufferStorageMultisample(disp, parameters) (* GET_NamedRenderbufferStorageMultisample(disp)) parameters
#define GET_NamedRenderbufferStorageMultisample(disp) ((_glptr_NamedRenderbufferStorageMultisample)(GET_by_offset((disp), _gloffset_NamedRenderbufferStorageMultisample)))
#define SET_NamedRenderbufferStorageMultisample(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizei, GLenum, GLsizei, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_NamedRenderbufferStorageMultisample, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TextureBuffer)(GLuint, GLenum, GLuint);
#define CALL_TextureBuffer(disp, parameters) (* GET_TextureBuffer(disp)) parameters
#define GET_TextureBuffer(disp) ((_glptr_TextureBuffer)(GET_by_offset((disp), _gloffset_TextureBuffer)))
#define SET_TextureBuffer(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLuint) = func; \
   SET_by_offset(disp, _gloffset_TextureBuffer, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TextureBufferRange)(GLuint, GLenum, GLuint, GLintptr, GLsizeiptr);
#define CALL_TextureBufferRange(disp, parameters) (* GET_TextureBufferRange(disp)) parameters
#define GET_TextureBufferRange(disp) ((_glptr_TextureBufferRange)(GET_by_offset((disp), _gloffset_TextureBufferRange)))
#define SET_TextureBufferRange(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLuint, GLintptr, GLsizeiptr) = func; \
   SET_by_offset(disp, _gloffset_TextureBufferRange, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TextureParameterIiv)(GLuint, GLenum, const GLint *);
#define CALL_TextureParameterIiv(disp, parameters) (* GET_TextureParameterIiv(disp)) parameters
#define GET_TextureParameterIiv(disp) ((_glptr_TextureParameterIiv)(GET_by_offset((disp), _gloffset_TextureParameterIiv)))
#define SET_TextureParameterIiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, const GLint *) = func; \
   SET_by_offset(disp, _gloffset_TextureParameterIiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TextureParameterIuiv)(GLuint, GLenum, const GLuint *);
#define CALL_TextureParameterIuiv(disp, parameters) (* GET_TextureParameterIuiv(disp)) parameters
#define GET_TextureParameterIuiv(disp) ((_glptr_TextureParameterIuiv)(GET_by_offset((disp), _gloffset_TextureParameterIuiv)))
#define SET_TextureParameterIuiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_TextureParameterIuiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TextureParameterf)(GLuint, GLenum, GLfloat);
#define CALL_TextureParameterf(disp, parameters) (* GET_TextureParameterf(disp)) parameters
#define GET_TextureParameterf(disp) ((_glptr_TextureParameterf)(GET_by_offset((disp), _gloffset_TextureParameterf)))
#define SET_TextureParameterf(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_TextureParameterf, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TextureParameterfv)(GLuint, GLenum, const GLfloat *);
#define CALL_TextureParameterfv(disp, parameters) (* GET_TextureParameterfv(disp)) parameters
#define GET_TextureParameterfv(disp) ((_glptr_TextureParameterfv)(GET_by_offset((disp), _gloffset_TextureParameterfv)))
#define SET_TextureParameterfv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_TextureParameterfv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TextureParameteri)(GLuint, GLenum, GLint);
#define CALL_TextureParameteri(disp, parameters) (* GET_TextureParameteri(disp)) parameters
#define GET_TextureParameteri(disp) ((_glptr_TextureParameteri)(GET_by_offset((disp), _gloffset_TextureParameteri)))
#define SET_TextureParameteri(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLint) = func; \
   SET_by_offset(disp, _gloffset_TextureParameteri, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TextureParameteriv)(GLuint, GLenum, const GLint *);
#define CALL_TextureParameteriv(disp, parameters) (* GET_TextureParameteriv(disp)) parameters
#define GET_TextureParameteriv(disp) ((_glptr_TextureParameteriv)(GET_by_offset((disp), _gloffset_TextureParameteriv)))
#define SET_TextureParameteriv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, const GLint *) = func; \
   SET_by_offset(disp, _gloffset_TextureParameteriv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TextureStorage1D)(GLuint, GLsizei, GLenum, GLsizei);
#define CALL_TextureStorage1D(disp, parameters) (* GET_TextureStorage1D(disp)) parameters
#define GET_TextureStorage1D(disp) ((_glptr_TextureStorage1D)(GET_by_offset((disp), _gloffset_TextureStorage1D)))
#define SET_TextureStorage1D(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizei, GLenum, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_TextureStorage1D, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TextureStorage2D)(GLuint, GLsizei, GLenum, GLsizei, GLsizei);
#define CALL_TextureStorage2D(disp, parameters) (* GET_TextureStorage2D(disp)) parameters
#define GET_TextureStorage2D(disp) ((_glptr_TextureStorage2D)(GET_by_offset((disp), _gloffset_TextureStorage2D)))
#define SET_TextureStorage2D(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizei, GLenum, GLsizei, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_TextureStorage2D, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TextureStorage2DMultisample)(GLuint, GLsizei, GLenum, GLsizei, GLsizei, GLboolean);
#define CALL_TextureStorage2DMultisample(disp, parameters) (* GET_TextureStorage2DMultisample(disp)) parameters
#define GET_TextureStorage2DMultisample(disp) ((_glptr_TextureStorage2DMultisample)(GET_by_offset((disp), _gloffset_TextureStorage2DMultisample)))
#define SET_TextureStorage2DMultisample(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizei, GLenum, GLsizei, GLsizei, GLboolean) = func; \
   SET_by_offset(disp, _gloffset_TextureStorage2DMultisample, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TextureStorage3D)(GLuint, GLsizei, GLenum, GLsizei, GLsizei, GLsizei);
#define CALL_TextureStorage3D(disp, parameters) (* GET_TextureStorage3D(disp)) parameters
#define GET_TextureStorage3D(disp) ((_glptr_TextureStorage3D)(GET_by_offset((disp), _gloffset_TextureStorage3D)))
#define SET_TextureStorage3D(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizei, GLenum, GLsizei, GLsizei, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_TextureStorage3D, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TextureStorage3DMultisample)(GLuint, GLsizei, GLenum, GLsizei, GLsizei, GLsizei, GLboolean);
#define CALL_TextureStorage3DMultisample(disp, parameters) (* GET_TextureStorage3DMultisample(disp)) parameters
#define GET_TextureStorage3DMultisample(disp) ((_glptr_TextureStorage3DMultisample)(GET_by_offset((disp), _gloffset_TextureStorage3DMultisample)))
#define SET_TextureStorage3DMultisample(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizei, GLenum, GLsizei, GLsizei, GLsizei, GLboolean) = func; \
   SET_by_offset(disp, _gloffset_TextureStorage3DMultisample, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TextureSubImage1D)(GLuint, GLint, GLint, GLsizei, GLenum, GLenum, const GLvoid *);
#define CALL_TextureSubImage1D(disp, parameters) (* GET_TextureSubImage1D(disp)) parameters
#define GET_TextureSubImage1D(disp) ((_glptr_TextureSubImage1D)(GET_by_offset((disp), _gloffset_TextureSubImage1D)))
#define SET_TextureSubImage1D(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLint, GLsizei, GLenum, GLenum, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_TextureSubImage1D, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TextureSubImage2D)(GLuint, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const GLvoid *);
#define CALL_TextureSubImage2D(disp, parameters) (* GET_TextureSubImage2D(disp)) parameters
#define GET_TextureSubImage2D(disp) ((_glptr_TextureSubImage2D)(GET_by_offset((disp), _gloffset_TextureSubImage2D)))
#define SET_TextureSubImage2D(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_TextureSubImage2D, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TextureSubImage3D)(GLuint, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLenum, const GLvoid *);
#define CALL_TextureSubImage3D(disp, parameters) (* GET_TextureSubImage3D(disp)) parameters
#define GET_TextureSubImage3D(disp) ((_glptr_TextureSubImage3D)(GET_by_offset((disp), _gloffset_TextureSubImage3D)))
#define SET_TextureSubImage3D(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLenum, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_TextureSubImage3D, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TransformFeedbackBufferBase)(GLuint, GLuint, GLuint);
#define CALL_TransformFeedbackBufferBase(disp, parameters) (* GET_TransformFeedbackBufferBase(disp)) parameters
#define GET_TransformFeedbackBufferBase(disp) ((_glptr_TransformFeedbackBufferBase)(GET_by_offset((disp), _gloffset_TransformFeedbackBufferBase)))
#define SET_TransformFeedbackBufferBase(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLuint) = func; \
   SET_by_offset(disp, _gloffset_TransformFeedbackBufferBase, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TransformFeedbackBufferRange)(GLuint, GLuint, GLuint, GLintptr, GLsizeiptr);
#define CALL_TransformFeedbackBufferRange(disp, parameters) (* GET_TransformFeedbackBufferRange(disp)) parameters
#define GET_TransformFeedbackBufferRange(disp) ((_glptr_TransformFeedbackBufferRange)(GET_by_offset((disp), _gloffset_TransformFeedbackBufferRange)))
#define SET_TransformFeedbackBufferRange(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLuint, GLintptr, GLsizeiptr) = func; \
   SET_by_offset(disp, _gloffset_TransformFeedbackBufferRange, fn); \
} while (0)

typedef GLboolean (GLAPIENTRYP _glptr_UnmapNamedBufferEXT)(GLuint);
#define CALL_UnmapNamedBufferEXT(disp, parameters) (* GET_UnmapNamedBufferEXT(disp)) parameters
#define GET_UnmapNamedBufferEXT(disp) ((_glptr_UnmapNamedBufferEXT)(GET_by_offset((disp), _gloffset_UnmapNamedBufferEXT)))
#define SET_UnmapNamedBufferEXT(disp, func) do { \
   GLboolean (GLAPIENTRYP fn)(GLuint) = func; \
   SET_by_offset(disp, _gloffset_UnmapNamedBufferEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexArrayAttribBinding)(GLuint, GLuint, GLuint);
#define CALL_VertexArrayAttribBinding(disp, parameters) (* GET_VertexArrayAttribBinding(disp)) parameters
#define GET_VertexArrayAttribBinding(disp) ((_glptr_VertexArrayAttribBinding)(GET_by_offset((disp), _gloffset_VertexArrayAttribBinding)))
#define SET_VertexArrayAttribBinding(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLuint) = func; \
   SET_by_offset(disp, _gloffset_VertexArrayAttribBinding, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexArrayAttribFormat)(GLuint, GLuint, GLint, GLenum, GLboolean, GLuint);
#define CALL_VertexArrayAttribFormat(disp, parameters) (* GET_VertexArrayAttribFormat(disp)) parameters
#define GET_VertexArrayAttribFormat(disp) ((_glptr_VertexArrayAttribFormat)(GET_by_offset((disp), _gloffset_VertexArrayAttribFormat)))
#define SET_VertexArrayAttribFormat(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLint, GLenum, GLboolean, GLuint) = func; \
   SET_by_offset(disp, _gloffset_VertexArrayAttribFormat, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexArrayAttribIFormat)(GLuint, GLuint, GLint, GLenum, GLuint);
#define CALL_VertexArrayAttribIFormat(disp, parameters) (* GET_VertexArrayAttribIFormat(disp)) parameters
#define GET_VertexArrayAttribIFormat(disp) ((_glptr_VertexArrayAttribIFormat)(GET_by_offset((disp), _gloffset_VertexArrayAttribIFormat)))
#define SET_VertexArrayAttribIFormat(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLint, GLenum, GLuint) = func; \
   SET_by_offset(disp, _gloffset_VertexArrayAttribIFormat, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexArrayAttribLFormat)(GLuint, GLuint, GLint, GLenum, GLuint);
#define CALL_VertexArrayAttribLFormat(disp, parameters) (* GET_VertexArrayAttribLFormat(disp)) parameters
#define GET_VertexArrayAttribLFormat(disp) ((_glptr_VertexArrayAttribLFormat)(GET_by_offset((disp), _gloffset_VertexArrayAttribLFormat)))
#define SET_VertexArrayAttribLFormat(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLint, GLenum, GLuint) = func; \
   SET_by_offset(disp, _gloffset_VertexArrayAttribLFormat, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexArrayBindingDivisor)(GLuint, GLuint, GLuint);
#define CALL_VertexArrayBindingDivisor(disp, parameters) (* GET_VertexArrayBindingDivisor(disp)) parameters
#define GET_VertexArrayBindingDivisor(disp) ((_glptr_VertexArrayBindingDivisor)(GET_by_offset((disp), _gloffset_VertexArrayBindingDivisor)))
#define SET_VertexArrayBindingDivisor(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLuint) = func; \
   SET_by_offset(disp, _gloffset_VertexArrayBindingDivisor, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexArrayElementBuffer)(GLuint, GLuint);
#define CALL_VertexArrayElementBuffer(disp, parameters) (* GET_VertexArrayElementBuffer(disp)) parameters
#define GET_VertexArrayElementBuffer(disp) ((_glptr_VertexArrayElementBuffer)(GET_by_offset((disp), _gloffset_VertexArrayElementBuffer)))
#define SET_VertexArrayElementBuffer(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint) = func; \
   SET_by_offset(disp, _gloffset_VertexArrayElementBuffer, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexArrayVertexBuffer)(GLuint, GLuint, GLuint, GLintptr, GLsizei);
#define CALL_VertexArrayVertexBuffer(disp, parameters) (* GET_VertexArrayVertexBuffer(disp)) parameters
#define GET_VertexArrayVertexBuffer(disp) ((_glptr_VertexArrayVertexBuffer)(GET_by_offset((disp), _gloffset_VertexArrayVertexBuffer)))
#define SET_VertexArrayVertexBuffer(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLuint, GLintptr, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_VertexArrayVertexBuffer, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexArrayVertexBuffers)(GLuint, GLuint, GLsizei, const GLuint *, const GLintptr *, const GLsizei *);
#define CALL_VertexArrayVertexBuffers(disp, parameters) (* GET_VertexArrayVertexBuffers(disp)) parameters
#define GET_VertexArrayVertexBuffers(disp) ((_glptr_VertexArrayVertexBuffers)(GET_by_offset((disp), _gloffset_VertexArrayVertexBuffers)))
#define SET_VertexArrayVertexBuffers(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLsizei, const GLuint *, const GLintptr *, const GLsizei *) = func; \
   SET_by_offset(disp, _gloffset_VertexArrayVertexBuffers, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetCompressedTextureSubImage)(GLuint, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLsizei, GLvoid *);
#define CALL_GetCompressedTextureSubImage(disp, parameters) (* GET_GetCompressedTextureSubImage(disp)) parameters
#define GET_GetCompressedTextureSubImage(disp) ((_glptr_GetCompressedTextureSubImage)(GET_by_offset((disp), _gloffset_GetCompressedTextureSubImage)))
#define SET_GetCompressedTextureSubImage(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLsizei, GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_GetCompressedTextureSubImage, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetTextureSubImage)(GLuint, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLenum, GLsizei, GLvoid *);
#define CALL_GetTextureSubImage(disp, parameters) (* GET_GetTextureSubImage(disp)) parameters
#define GET_GetTextureSubImage(disp) ((_glptr_GetTextureSubImage)(GET_by_offset((disp), _gloffset_GetTextureSubImage)))
#define SET_GetTextureSubImage(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLenum, GLsizei, GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_GetTextureSubImage, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_BufferPageCommitmentARB)(GLenum, GLintptr, GLsizeiptr, GLboolean);
#define CALL_BufferPageCommitmentARB(disp, parameters) (* GET_BufferPageCommitmentARB(disp)) parameters
#define GET_BufferPageCommitmentARB(disp) ((_glptr_BufferPageCommitmentARB)(GET_by_offset((disp), _gloffset_BufferPageCommitmentARB)))
#define SET_BufferPageCommitmentARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLintptr, GLsizeiptr, GLboolean) = func; \
   SET_by_offset(disp, _gloffset_BufferPageCommitmentARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_NamedBufferPageCommitmentARB)(GLuint, GLintptr, GLsizeiptr, GLboolean);
#define CALL_NamedBufferPageCommitmentARB(disp, parameters) (* GET_NamedBufferPageCommitmentARB(disp)) parameters
#define GET_NamedBufferPageCommitmentARB(disp) ((_glptr_NamedBufferPageCommitmentARB)(GET_by_offset((disp), _gloffset_NamedBufferPageCommitmentARB)))
#define SET_NamedBufferPageCommitmentARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLintptr, GLsizeiptr, GLboolean) = func; \
   SET_by_offset(disp, _gloffset_NamedBufferPageCommitmentARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetUniformi64vARB)(GLuint, GLint, GLint64 *);
#define CALL_GetUniformi64vARB(disp, parameters) (* GET_GetUniformi64vARB(disp)) parameters
#define GET_GetUniformi64vARB(disp) ((_glptr_GetUniformi64vARB)(GET_by_offset((disp), _gloffset_GetUniformi64vARB)))
#define SET_GetUniformi64vARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLint64 *) = func; \
   SET_by_offset(disp, _gloffset_GetUniformi64vARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetUniformui64vARB)(GLuint, GLint, GLuint64 *);
#define CALL_GetUniformui64vARB(disp, parameters) (* GET_GetUniformui64vARB(disp)) parameters
#define GET_GetUniformui64vARB(disp) ((_glptr_GetUniformui64vARB)(GET_by_offset((disp), _gloffset_GetUniformui64vARB)))
#define SET_GetUniformui64vARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLuint64 *) = func; \
   SET_by_offset(disp, _gloffset_GetUniformui64vARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetnUniformi64vARB)(GLuint, GLint, GLsizei, GLint64 *);
#define CALL_GetnUniformi64vARB(disp, parameters) (* GET_GetnUniformi64vARB(disp)) parameters
#define GET_GetnUniformi64vARB(disp) ((_glptr_GetnUniformi64vARB)(GET_by_offset((disp), _gloffset_GetnUniformi64vARB)))
#define SET_GetnUniformi64vARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLsizei, GLint64 *) = func; \
   SET_by_offset(disp, _gloffset_GetnUniformi64vARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetnUniformui64vARB)(GLuint, GLint, GLsizei, GLuint64 *);
#define CALL_GetnUniformui64vARB(disp, parameters) (* GET_GetnUniformui64vARB(disp)) parameters
#define GET_GetnUniformui64vARB(disp) ((_glptr_GetnUniformui64vARB)(GET_by_offset((disp), _gloffset_GetnUniformui64vARB)))
#define SET_GetnUniformui64vARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLsizei, GLuint64 *) = func; \
   SET_by_offset(disp, _gloffset_GetnUniformui64vARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniform1i64ARB)(GLuint, GLint, GLint64);
#define CALL_ProgramUniform1i64ARB(disp, parameters) (* GET_ProgramUniform1i64ARB(disp)) parameters
#define GET_ProgramUniform1i64ARB(disp) ((_glptr_ProgramUniform1i64ARB)(GET_by_offset((disp), _gloffset_ProgramUniform1i64ARB)))
#define SET_ProgramUniform1i64ARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLint64) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniform1i64ARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniform1i64vARB)(GLuint, GLint, GLsizei, const GLint64 *);
#define CALL_ProgramUniform1i64vARB(disp, parameters) (* GET_ProgramUniform1i64vARB(disp)) parameters
#define GET_ProgramUniform1i64vARB(disp) ((_glptr_ProgramUniform1i64vARB)(GET_by_offset((disp), _gloffset_ProgramUniform1i64vARB)))
#define SET_ProgramUniform1i64vARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLsizei, const GLint64 *) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniform1i64vARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniform1ui64ARB)(GLuint, GLint, GLuint64);
#define CALL_ProgramUniform1ui64ARB(disp, parameters) (* GET_ProgramUniform1ui64ARB(disp)) parameters
#define GET_ProgramUniform1ui64ARB(disp) ((_glptr_ProgramUniform1ui64ARB)(GET_by_offset((disp), _gloffset_ProgramUniform1ui64ARB)))
#define SET_ProgramUniform1ui64ARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLuint64) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniform1ui64ARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniform1ui64vARB)(GLuint, GLint, GLsizei, const GLuint64 *);
#define CALL_ProgramUniform1ui64vARB(disp, parameters) (* GET_ProgramUniform1ui64vARB(disp)) parameters
#define GET_ProgramUniform1ui64vARB(disp) ((_glptr_ProgramUniform1ui64vARB)(GET_by_offset((disp), _gloffset_ProgramUniform1ui64vARB)))
#define SET_ProgramUniform1ui64vARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLsizei, const GLuint64 *) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniform1ui64vARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniform2i64ARB)(GLuint, GLint, GLint64, GLint64);
#define CALL_ProgramUniform2i64ARB(disp, parameters) (* GET_ProgramUniform2i64ARB(disp)) parameters
#define GET_ProgramUniform2i64ARB(disp) ((_glptr_ProgramUniform2i64ARB)(GET_by_offset((disp), _gloffset_ProgramUniform2i64ARB)))
#define SET_ProgramUniform2i64ARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLint64, GLint64) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniform2i64ARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniform2i64vARB)(GLuint, GLint, GLsizei, const GLint64 *);
#define CALL_ProgramUniform2i64vARB(disp, parameters) (* GET_ProgramUniform2i64vARB(disp)) parameters
#define GET_ProgramUniform2i64vARB(disp) ((_glptr_ProgramUniform2i64vARB)(GET_by_offset((disp), _gloffset_ProgramUniform2i64vARB)))
#define SET_ProgramUniform2i64vARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLsizei, const GLint64 *) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniform2i64vARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniform2ui64ARB)(GLuint, GLint, GLuint64, GLuint64);
#define CALL_ProgramUniform2ui64ARB(disp, parameters) (* GET_ProgramUniform2ui64ARB(disp)) parameters
#define GET_ProgramUniform2ui64ARB(disp) ((_glptr_ProgramUniform2ui64ARB)(GET_by_offset((disp), _gloffset_ProgramUniform2ui64ARB)))
#define SET_ProgramUniform2ui64ARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLuint64, GLuint64) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniform2ui64ARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniform2ui64vARB)(GLuint, GLint, GLsizei, const GLuint64 *);
#define CALL_ProgramUniform2ui64vARB(disp, parameters) (* GET_ProgramUniform2ui64vARB(disp)) parameters
#define GET_ProgramUniform2ui64vARB(disp) ((_glptr_ProgramUniform2ui64vARB)(GET_by_offset((disp), _gloffset_ProgramUniform2ui64vARB)))
#define SET_ProgramUniform2ui64vARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLsizei, const GLuint64 *) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniform2ui64vARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniform3i64ARB)(GLuint, GLint, GLint64, GLint64, GLint64);
#define CALL_ProgramUniform3i64ARB(disp, parameters) (* GET_ProgramUniform3i64ARB(disp)) parameters
#define GET_ProgramUniform3i64ARB(disp) ((_glptr_ProgramUniform3i64ARB)(GET_by_offset((disp), _gloffset_ProgramUniform3i64ARB)))
#define SET_ProgramUniform3i64ARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLint64, GLint64, GLint64) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniform3i64ARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniform3i64vARB)(GLuint, GLint, GLsizei, const GLint64 *);
#define CALL_ProgramUniform3i64vARB(disp, parameters) (* GET_ProgramUniform3i64vARB(disp)) parameters
#define GET_ProgramUniform3i64vARB(disp) ((_glptr_ProgramUniform3i64vARB)(GET_by_offset((disp), _gloffset_ProgramUniform3i64vARB)))
#define SET_ProgramUniform3i64vARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLsizei, const GLint64 *) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniform3i64vARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniform3ui64ARB)(GLuint, GLint, GLuint64, GLuint64, GLuint64);
#define CALL_ProgramUniform3ui64ARB(disp, parameters) (* GET_ProgramUniform3ui64ARB(disp)) parameters
#define GET_ProgramUniform3ui64ARB(disp) ((_glptr_ProgramUniform3ui64ARB)(GET_by_offset((disp), _gloffset_ProgramUniform3ui64ARB)))
#define SET_ProgramUniform3ui64ARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLuint64, GLuint64, GLuint64) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniform3ui64ARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniform3ui64vARB)(GLuint, GLint, GLsizei, const GLuint64 *);
#define CALL_ProgramUniform3ui64vARB(disp, parameters) (* GET_ProgramUniform3ui64vARB(disp)) parameters
#define GET_ProgramUniform3ui64vARB(disp) ((_glptr_ProgramUniform3ui64vARB)(GET_by_offset((disp), _gloffset_ProgramUniform3ui64vARB)))
#define SET_ProgramUniform3ui64vARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLsizei, const GLuint64 *) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniform3ui64vARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniform4i64ARB)(GLuint, GLint, GLint64, GLint64, GLint64, GLint64);
#define CALL_ProgramUniform4i64ARB(disp, parameters) (* GET_ProgramUniform4i64ARB(disp)) parameters
#define GET_ProgramUniform4i64ARB(disp) ((_glptr_ProgramUniform4i64ARB)(GET_by_offset((disp), _gloffset_ProgramUniform4i64ARB)))
#define SET_ProgramUniform4i64ARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLint64, GLint64, GLint64, GLint64) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniform4i64ARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniform4i64vARB)(GLuint, GLint, GLsizei, const GLint64 *);
#define CALL_ProgramUniform4i64vARB(disp, parameters) (* GET_ProgramUniform4i64vARB(disp)) parameters
#define GET_ProgramUniform4i64vARB(disp) ((_glptr_ProgramUniform4i64vARB)(GET_by_offset((disp), _gloffset_ProgramUniform4i64vARB)))
#define SET_ProgramUniform4i64vARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLsizei, const GLint64 *) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniform4i64vARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniform4ui64ARB)(GLuint, GLint, GLuint64, GLuint64, GLuint64, GLuint64);
#define CALL_ProgramUniform4ui64ARB(disp, parameters) (* GET_ProgramUniform4ui64ARB(disp)) parameters
#define GET_ProgramUniform4ui64ARB(disp) ((_glptr_ProgramUniform4ui64ARB)(GET_by_offset((disp), _gloffset_ProgramUniform4ui64ARB)))
#define SET_ProgramUniform4ui64ARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLuint64, GLuint64, GLuint64, GLuint64) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniform4ui64ARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniform4ui64vARB)(GLuint, GLint, GLsizei, const GLuint64 *);
#define CALL_ProgramUniform4ui64vARB(disp, parameters) (* GET_ProgramUniform4ui64vARB(disp)) parameters
#define GET_ProgramUniform4ui64vARB(disp) ((_glptr_ProgramUniform4ui64vARB)(GET_by_offset((disp), _gloffset_ProgramUniform4ui64vARB)))
#define SET_ProgramUniform4ui64vARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLsizei, const GLuint64 *) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniform4ui64vARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Uniform1i64ARB)(GLint, GLint64);
#define CALL_Uniform1i64ARB(disp, parameters) (* GET_Uniform1i64ARB(disp)) parameters
#define GET_Uniform1i64ARB(disp) ((_glptr_Uniform1i64ARB)(GET_by_offset((disp), _gloffset_Uniform1i64ARB)))
#define SET_Uniform1i64ARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLint64) = func; \
   SET_by_offset(disp, _gloffset_Uniform1i64ARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Uniform1i64vARB)(GLint, GLsizei, const GLint64 *);
#define CALL_Uniform1i64vARB(disp, parameters) (* GET_Uniform1i64vARB(disp)) parameters
#define GET_Uniform1i64vARB(disp) ((_glptr_Uniform1i64vARB)(GET_by_offset((disp), _gloffset_Uniform1i64vARB)))
#define SET_Uniform1i64vARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLsizei, const GLint64 *) = func; \
   SET_by_offset(disp, _gloffset_Uniform1i64vARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Uniform1ui64ARB)(GLint, GLuint64);
#define CALL_Uniform1ui64ARB(disp, parameters) (* GET_Uniform1ui64ARB(disp)) parameters
#define GET_Uniform1ui64ARB(disp) ((_glptr_Uniform1ui64ARB)(GET_by_offset((disp), _gloffset_Uniform1ui64ARB)))
#define SET_Uniform1ui64ARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLuint64) = func; \
   SET_by_offset(disp, _gloffset_Uniform1ui64ARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Uniform1ui64vARB)(GLint, GLsizei, const GLuint64 *);
#define CALL_Uniform1ui64vARB(disp, parameters) (* GET_Uniform1ui64vARB(disp)) parameters
#define GET_Uniform1ui64vARB(disp) ((_glptr_Uniform1ui64vARB)(GET_by_offset((disp), _gloffset_Uniform1ui64vARB)))
#define SET_Uniform1ui64vARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLsizei, const GLuint64 *) = func; \
   SET_by_offset(disp, _gloffset_Uniform1ui64vARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Uniform2i64ARB)(GLint, GLint64, GLint64);
#define CALL_Uniform2i64ARB(disp, parameters) (* GET_Uniform2i64ARB(disp)) parameters
#define GET_Uniform2i64ARB(disp) ((_glptr_Uniform2i64ARB)(GET_by_offset((disp), _gloffset_Uniform2i64ARB)))
#define SET_Uniform2i64ARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLint64, GLint64) = func; \
   SET_by_offset(disp, _gloffset_Uniform2i64ARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Uniform2i64vARB)(GLint, GLsizei, const GLint64 *);
#define CALL_Uniform2i64vARB(disp, parameters) (* GET_Uniform2i64vARB(disp)) parameters
#define GET_Uniform2i64vARB(disp) ((_glptr_Uniform2i64vARB)(GET_by_offset((disp), _gloffset_Uniform2i64vARB)))
#define SET_Uniform2i64vARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLsizei, const GLint64 *) = func; \
   SET_by_offset(disp, _gloffset_Uniform2i64vARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Uniform2ui64ARB)(GLint, GLuint64, GLuint64);
#define CALL_Uniform2ui64ARB(disp, parameters) (* GET_Uniform2ui64ARB(disp)) parameters
#define GET_Uniform2ui64ARB(disp) ((_glptr_Uniform2ui64ARB)(GET_by_offset((disp), _gloffset_Uniform2ui64ARB)))
#define SET_Uniform2ui64ARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLuint64, GLuint64) = func; \
   SET_by_offset(disp, _gloffset_Uniform2ui64ARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Uniform2ui64vARB)(GLint, GLsizei, const GLuint64 *);
#define CALL_Uniform2ui64vARB(disp, parameters) (* GET_Uniform2ui64vARB(disp)) parameters
#define GET_Uniform2ui64vARB(disp) ((_glptr_Uniform2ui64vARB)(GET_by_offset((disp), _gloffset_Uniform2ui64vARB)))
#define SET_Uniform2ui64vARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLsizei, const GLuint64 *) = func; \
   SET_by_offset(disp, _gloffset_Uniform2ui64vARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Uniform3i64ARB)(GLint, GLint64, GLint64, GLint64);
#define CALL_Uniform3i64ARB(disp, parameters) (* GET_Uniform3i64ARB(disp)) parameters
#define GET_Uniform3i64ARB(disp) ((_glptr_Uniform3i64ARB)(GET_by_offset((disp), _gloffset_Uniform3i64ARB)))
#define SET_Uniform3i64ARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLint64, GLint64, GLint64) = func; \
   SET_by_offset(disp, _gloffset_Uniform3i64ARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Uniform3i64vARB)(GLint, GLsizei, const GLint64 *);
#define CALL_Uniform3i64vARB(disp, parameters) (* GET_Uniform3i64vARB(disp)) parameters
#define GET_Uniform3i64vARB(disp) ((_glptr_Uniform3i64vARB)(GET_by_offset((disp), _gloffset_Uniform3i64vARB)))
#define SET_Uniform3i64vARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLsizei, const GLint64 *) = func; \
   SET_by_offset(disp, _gloffset_Uniform3i64vARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Uniform3ui64ARB)(GLint, GLuint64, GLuint64, GLuint64);
#define CALL_Uniform3ui64ARB(disp, parameters) (* GET_Uniform3ui64ARB(disp)) parameters
#define GET_Uniform3ui64ARB(disp) ((_glptr_Uniform3ui64ARB)(GET_by_offset((disp), _gloffset_Uniform3ui64ARB)))
#define SET_Uniform3ui64ARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLuint64, GLuint64, GLuint64) = func; \
   SET_by_offset(disp, _gloffset_Uniform3ui64ARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Uniform3ui64vARB)(GLint, GLsizei, const GLuint64 *);
#define CALL_Uniform3ui64vARB(disp, parameters) (* GET_Uniform3ui64vARB(disp)) parameters
#define GET_Uniform3ui64vARB(disp) ((_glptr_Uniform3ui64vARB)(GET_by_offset((disp), _gloffset_Uniform3ui64vARB)))
#define SET_Uniform3ui64vARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLsizei, const GLuint64 *) = func; \
   SET_by_offset(disp, _gloffset_Uniform3ui64vARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Uniform4i64ARB)(GLint, GLint64, GLint64, GLint64, GLint64);
#define CALL_Uniform4i64ARB(disp, parameters) (* GET_Uniform4i64ARB(disp)) parameters
#define GET_Uniform4i64ARB(disp) ((_glptr_Uniform4i64ARB)(GET_by_offset((disp), _gloffset_Uniform4i64ARB)))
#define SET_Uniform4i64ARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLint64, GLint64, GLint64, GLint64) = func; \
   SET_by_offset(disp, _gloffset_Uniform4i64ARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Uniform4i64vARB)(GLint, GLsizei, const GLint64 *);
#define CALL_Uniform4i64vARB(disp, parameters) (* GET_Uniform4i64vARB(disp)) parameters
#define GET_Uniform4i64vARB(disp) ((_glptr_Uniform4i64vARB)(GET_by_offset((disp), _gloffset_Uniform4i64vARB)))
#define SET_Uniform4i64vARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLsizei, const GLint64 *) = func; \
   SET_by_offset(disp, _gloffset_Uniform4i64vARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Uniform4ui64ARB)(GLint, GLuint64, GLuint64, GLuint64, GLuint64);
#define CALL_Uniform4ui64ARB(disp, parameters) (* GET_Uniform4ui64ARB(disp)) parameters
#define GET_Uniform4ui64ARB(disp) ((_glptr_Uniform4ui64ARB)(GET_by_offset((disp), _gloffset_Uniform4ui64ARB)))
#define SET_Uniform4ui64ARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLuint64, GLuint64, GLuint64, GLuint64) = func; \
   SET_by_offset(disp, _gloffset_Uniform4ui64ARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Uniform4ui64vARB)(GLint, GLsizei, const GLuint64 *);
#define CALL_Uniform4ui64vARB(disp, parameters) (* GET_Uniform4ui64vARB(disp)) parameters
#define GET_Uniform4ui64vARB(disp) ((_glptr_Uniform4ui64vARB)(GET_by_offset((disp), _gloffset_Uniform4ui64vARB)))
#define SET_Uniform4ui64vARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLsizei, const GLuint64 *) = func; \
   SET_by_offset(disp, _gloffset_Uniform4ui64vARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_EvaluateDepthValuesARB)(void);
#define CALL_EvaluateDepthValuesARB(disp, parameters) (* GET_EvaluateDepthValuesARB(disp)) parameters
#define GET_EvaluateDepthValuesARB(disp) ((_glptr_EvaluateDepthValuesARB)(GET_by_offset((disp), _gloffset_EvaluateDepthValuesARB)))
#define SET_EvaluateDepthValuesARB(disp, func) do { \
   void (GLAPIENTRYP fn)(void) = func; \
   SET_by_offset(disp, _gloffset_EvaluateDepthValuesARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_FramebufferSampleLocationsfvARB)(GLenum, GLuint, GLsizei, const GLfloat *);
#define CALL_FramebufferSampleLocationsfvARB(disp, parameters) (* GET_FramebufferSampleLocationsfvARB(disp)) parameters
#define GET_FramebufferSampleLocationsfvARB(disp) ((_glptr_FramebufferSampleLocationsfvARB)(GET_by_offset((disp), _gloffset_FramebufferSampleLocationsfvARB)))
#define SET_FramebufferSampleLocationsfvARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint, GLsizei, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_FramebufferSampleLocationsfvARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_NamedFramebufferSampleLocationsfvARB)(GLuint, GLuint, GLsizei, const GLfloat *);
#define CALL_NamedFramebufferSampleLocationsfvARB(disp, parameters) (* GET_NamedFramebufferSampleLocationsfvARB(disp)) parameters
#define GET_NamedFramebufferSampleLocationsfvARB(disp) ((_glptr_NamedFramebufferSampleLocationsfvARB)(GET_by_offset((disp), _gloffset_NamedFramebufferSampleLocationsfvARB)))
#define SET_NamedFramebufferSampleLocationsfvARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLsizei, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_NamedFramebufferSampleLocationsfvARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_SpecializeShaderARB)(GLuint, const GLchar *, GLuint, const GLuint *, const GLuint *);
#define CALL_SpecializeShaderARB(disp, parameters) (* GET_SpecializeShaderARB(disp)) parameters
#define GET_SpecializeShaderARB(disp) ((_glptr_SpecializeShaderARB)(GET_by_offset((disp), _gloffset_SpecializeShaderARB)))
#define SET_SpecializeShaderARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLchar *, GLuint, const GLuint *, const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_SpecializeShaderARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_InvalidateBufferData)(GLuint);
#define CALL_InvalidateBufferData(disp, parameters) (* GET_InvalidateBufferData(disp)) parameters
#define GET_InvalidateBufferData(disp) ((_glptr_InvalidateBufferData)(GET_by_offset((disp), _gloffset_InvalidateBufferData)))
#define SET_InvalidateBufferData(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint) = func; \
   SET_by_offset(disp, _gloffset_InvalidateBufferData, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_InvalidateBufferSubData)(GLuint, GLintptr, GLsizeiptr);
#define CALL_InvalidateBufferSubData(disp, parameters) (* GET_InvalidateBufferSubData(disp)) parameters
#define GET_InvalidateBufferSubData(disp) ((_glptr_InvalidateBufferSubData)(GET_by_offset((disp), _gloffset_InvalidateBufferSubData)))
#define SET_InvalidateBufferSubData(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLintptr, GLsizeiptr) = func; \
   SET_by_offset(disp, _gloffset_InvalidateBufferSubData, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_InvalidateFramebuffer)(GLenum, GLsizei, const GLenum *);
#define CALL_InvalidateFramebuffer(disp, parameters) (* GET_InvalidateFramebuffer(disp)) parameters
#define GET_InvalidateFramebuffer(disp) ((_glptr_InvalidateFramebuffer)(GET_by_offset((disp), _gloffset_InvalidateFramebuffer)))
#define SET_InvalidateFramebuffer(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLsizei, const GLenum *) = func; \
   SET_by_offset(disp, _gloffset_InvalidateFramebuffer, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_InvalidateSubFramebuffer)(GLenum, GLsizei, const GLenum *, GLint, GLint, GLsizei, GLsizei);
#define CALL_InvalidateSubFramebuffer(disp, parameters) (* GET_InvalidateSubFramebuffer(disp)) parameters
#define GET_InvalidateSubFramebuffer(disp) ((_glptr_InvalidateSubFramebuffer)(GET_by_offset((disp), _gloffset_InvalidateSubFramebuffer)))
#define SET_InvalidateSubFramebuffer(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLsizei, const GLenum *, GLint, GLint, GLsizei, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_InvalidateSubFramebuffer, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_InvalidateTexImage)(GLuint, GLint);
#define CALL_InvalidateTexImage(disp, parameters) (* GET_InvalidateTexImage(disp)) parameters
#define GET_InvalidateTexImage(disp) ((_glptr_InvalidateTexImage)(GET_by_offset((disp), _gloffset_InvalidateTexImage)))
#define SET_InvalidateTexImage(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint) = func; \
   SET_by_offset(disp, _gloffset_InvalidateTexImage, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_InvalidateTexSubImage)(GLuint, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei);
#define CALL_InvalidateTexSubImage(disp, parameters) (* GET_InvalidateTexSubImage(disp)) parameters
#define GET_InvalidateTexSubImage(disp) ((_glptr_InvalidateTexSubImage)(GET_by_offset((disp), _gloffset_InvalidateTexSubImage)))
#define SET_InvalidateTexSubImage(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_InvalidateTexSubImage, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DrawTexfOES)(GLfloat, GLfloat, GLfloat, GLfloat, GLfloat);
#define CALL_DrawTexfOES(disp, parameters) (* GET_DrawTexfOES(disp)) parameters
#define GET_DrawTexfOES(disp) ((_glptr_DrawTexfOES)(GET_by_offset((disp), _gloffset_DrawTexfOES)))
#define SET_DrawTexfOES(disp, func) do { \
   void (GLAPIENTRYP fn)(GLfloat, GLfloat, GLfloat, GLfloat, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_DrawTexfOES, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DrawTexfvOES)(const GLfloat *);
#define CALL_DrawTexfvOES(disp, parameters) (* GET_DrawTexfvOES(disp)) parameters
#define GET_DrawTexfvOES(disp) ((_glptr_DrawTexfvOES)(GET_by_offset((disp), _gloffset_DrawTexfvOES)))
#define SET_DrawTexfvOES(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_DrawTexfvOES, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DrawTexiOES)(GLint, GLint, GLint, GLint, GLint);
#define CALL_DrawTexiOES(disp, parameters) (* GET_DrawTexiOES(disp)) parameters
#define GET_DrawTexiOES(disp) ((_glptr_DrawTexiOES)(GET_by_offset((disp), _gloffset_DrawTexiOES)))
#define SET_DrawTexiOES(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLint, GLint, GLint, GLint) = func; \
   SET_by_offset(disp, _gloffset_DrawTexiOES, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DrawTexivOES)(const GLint *);
#define CALL_DrawTexivOES(disp, parameters) (* GET_DrawTexivOES(disp)) parameters
#define GET_DrawTexivOES(disp) ((_glptr_DrawTexivOES)(GET_by_offset((disp), _gloffset_DrawTexivOES)))
#define SET_DrawTexivOES(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLint *) = func; \
   SET_by_offset(disp, _gloffset_DrawTexivOES, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DrawTexsOES)(GLshort, GLshort, GLshort, GLshort, GLshort);
#define CALL_DrawTexsOES(disp, parameters) (* GET_DrawTexsOES(disp)) parameters
#define GET_DrawTexsOES(disp) ((_glptr_DrawTexsOES)(GET_by_offset((disp), _gloffset_DrawTexsOES)))
#define SET_DrawTexsOES(disp, func) do { \
   void (GLAPIENTRYP fn)(GLshort, GLshort, GLshort, GLshort, GLshort) = func; \
   SET_by_offset(disp, _gloffset_DrawTexsOES, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DrawTexsvOES)(const GLshort *);
#define CALL_DrawTexsvOES(disp, parameters) (* GET_DrawTexsvOES(disp)) parameters
#define GET_DrawTexsvOES(disp) ((_glptr_DrawTexsvOES)(GET_by_offset((disp), _gloffset_DrawTexsvOES)))
#define SET_DrawTexsvOES(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLshort *) = func; \
   SET_by_offset(disp, _gloffset_DrawTexsvOES, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DrawTexxOES)(GLfixed, GLfixed, GLfixed, GLfixed, GLfixed);
#define CALL_DrawTexxOES(disp, parameters) (* GET_DrawTexxOES(disp)) parameters
#define GET_DrawTexxOES(disp) ((_glptr_DrawTexxOES)(GET_by_offset((disp), _gloffset_DrawTexxOES)))
#define SET_DrawTexxOES(disp, func) do { \
   void (GLAPIENTRYP fn)(GLfixed, GLfixed, GLfixed, GLfixed, GLfixed) = func; \
   SET_by_offset(disp, _gloffset_DrawTexxOES, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DrawTexxvOES)(const GLfixed *);
#define CALL_DrawTexxvOES(disp, parameters) (* GET_DrawTexxvOES(disp)) parameters
#define GET_DrawTexxvOES(disp) ((_glptr_DrawTexxvOES)(GET_by_offset((disp), _gloffset_DrawTexxvOES)))
#define SET_DrawTexxvOES(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLfixed *) = func; \
   SET_by_offset(disp, _gloffset_DrawTexxvOES, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_PointSizePointerOES)(GLenum, GLsizei, const GLvoid *);
#define CALL_PointSizePointerOES(disp, parameters) (* GET_PointSizePointerOES(disp)) parameters
#define GET_PointSizePointerOES(disp) ((_glptr_PointSizePointerOES)(GET_by_offset((disp), _gloffset_PointSizePointerOES)))
#define SET_PointSizePointerOES(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLsizei, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_PointSizePointerOES, fn); \
} while (0)

typedef GLbitfield (GLAPIENTRYP _glptr_QueryMatrixxOES)(GLfixed *, GLint *);
#define CALL_QueryMatrixxOES(disp, parameters) (* GET_QueryMatrixxOES(disp)) parameters
#define GET_QueryMatrixxOES(disp) ((_glptr_QueryMatrixxOES)(GET_by_offset((disp), _gloffset_QueryMatrixxOES)))
#define SET_QueryMatrixxOES(disp, func) do { \
   GLbitfield (GLAPIENTRYP fn)(GLfixed *, GLint *) = func; \
   SET_by_offset(disp, _gloffset_QueryMatrixxOES, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_SampleMaskSGIS)(GLclampf, GLboolean);
#define CALL_SampleMaskSGIS(disp, parameters) (* GET_SampleMaskSGIS(disp)) parameters
#define GET_SampleMaskSGIS(disp) ((_glptr_SampleMaskSGIS)(GET_by_offset((disp), _gloffset_SampleMaskSGIS)))
#define SET_SampleMaskSGIS(disp, func) do { \
   void (GLAPIENTRYP fn)(GLclampf, GLboolean) = func; \
   SET_by_offset(disp, _gloffset_SampleMaskSGIS, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_SamplePatternSGIS)(GLenum);
#define CALL_SamplePatternSGIS(disp, parameters) (* GET_SamplePatternSGIS(disp)) parameters
#define GET_SamplePatternSGIS(disp) ((_glptr_SamplePatternSGIS)(GET_by_offset((disp), _gloffset_SamplePatternSGIS)))
#define SET_SamplePatternSGIS(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum) = func; \
   SET_by_offset(disp, _gloffset_SamplePatternSGIS, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ColorPointerEXT)(GLint, GLenum, GLsizei, GLsizei, const GLvoid *);
#define CALL_ColorPointerEXT(disp, parameters) (* GET_ColorPointerEXT(disp)) parameters
#define GET_ColorPointerEXT(disp) ((_glptr_ColorPointerEXT)(GET_by_offset((disp), _gloffset_ColorPointerEXT)))
#define SET_ColorPointerEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLenum, GLsizei, GLsizei, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_ColorPointerEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_EdgeFlagPointerEXT)(GLsizei, GLsizei, const GLboolean *);
#define CALL_EdgeFlagPointerEXT(disp, parameters) (* GET_EdgeFlagPointerEXT(disp)) parameters
#define GET_EdgeFlagPointerEXT(disp) ((_glptr_EdgeFlagPointerEXT)(GET_by_offset((disp), _gloffset_EdgeFlagPointerEXT)))
#define SET_EdgeFlagPointerEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLsizei, GLsizei, const GLboolean *) = func; \
   SET_by_offset(disp, _gloffset_EdgeFlagPointerEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_IndexPointerEXT)(GLenum, GLsizei, GLsizei, const GLvoid *);
#define CALL_IndexPointerEXT(disp, parameters) (* GET_IndexPointerEXT(disp)) parameters
#define GET_IndexPointerEXT(disp) ((_glptr_IndexPointerEXT)(GET_by_offset((disp), _gloffset_IndexPointerEXT)))
#define SET_IndexPointerEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLsizei, GLsizei, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_IndexPointerEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_NormalPointerEXT)(GLenum, GLsizei, GLsizei, const GLvoid *);
#define CALL_NormalPointerEXT(disp, parameters) (* GET_NormalPointerEXT(disp)) parameters
#define GET_NormalPointerEXT(disp) ((_glptr_NormalPointerEXT)(GET_by_offset((disp), _gloffset_NormalPointerEXT)))
#define SET_NormalPointerEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLsizei, GLsizei, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_NormalPointerEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexCoordPointerEXT)(GLint, GLenum, GLsizei, GLsizei, const GLvoid *);
#define CALL_TexCoordPointerEXT(disp, parameters) (* GET_TexCoordPointerEXT(disp)) parameters
#define GET_TexCoordPointerEXT(disp) ((_glptr_TexCoordPointerEXT)(GET_by_offset((disp), _gloffset_TexCoordPointerEXT)))
#define SET_TexCoordPointerEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLenum, GLsizei, GLsizei, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_TexCoordPointerEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexPointerEXT)(GLint, GLenum, GLsizei, GLsizei, const GLvoid *);
#define CALL_VertexPointerEXT(disp, parameters) (* GET_VertexPointerEXT(disp)) parameters
#define GET_VertexPointerEXT(disp) ((_glptr_VertexPointerEXT)(GET_by_offset((disp), _gloffset_VertexPointerEXT)))
#define SET_VertexPointerEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLenum, GLsizei, GLsizei, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_VertexPointerEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DiscardFramebufferEXT)(GLenum, GLsizei, const GLenum *);
#define CALL_DiscardFramebufferEXT(disp, parameters) (* GET_DiscardFramebufferEXT(disp)) parameters
#define GET_DiscardFramebufferEXT(disp) ((_glptr_DiscardFramebufferEXT)(GET_by_offset((disp), _gloffset_DiscardFramebufferEXT)))
#define SET_DiscardFramebufferEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLsizei, const GLenum *) = func; \
   SET_by_offset(disp, _gloffset_DiscardFramebufferEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ActiveShaderProgram)(GLuint, GLuint);
#define CALL_ActiveShaderProgram(disp, parameters) (* GET_ActiveShaderProgram(disp)) parameters
#define GET_ActiveShaderProgram(disp) ((_glptr_ActiveShaderProgram)(GET_by_offset((disp), _gloffset_ActiveShaderProgram)))
#define SET_ActiveShaderProgram(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint) = func; \
   SET_by_offset(disp, _gloffset_ActiveShaderProgram, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_BindProgramPipeline)(GLuint);
#define CALL_BindProgramPipeline(disp, parameters) (* GET_BindProgramPipeline(disp)) parameters
#define GET_BindProgramPipeline(disp) ((_glptr_BindProgramPipeline)(GET_by_offset((disp), _gloffset_BindProgramPipeline)))
#define SET_BindProgramPipeline(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint) = func; \
   SET_by_offset(disp, _gloffset_BindProgramPipeline, fn); \
} while (0)

typedef GLuint (GLAPIENTRYP _glptr_CreateShaderProgramv)(GLenum, GLsizei, const GLchar * const *);
#define CALL_CreateShaderProgramv(disp, parameters) (* GET_CreateShaderProgramv(disp)) parameters
#define GET_CreateShaderProgramv(disp) ((_glptr_CreateShaderProgramv)(GET_by_offset((disp), _gloffset_CreateShaderProgramv)))
#define SET_CreateShaderProgramv(disp, func) do { \
   GLuint (GLAPIENTRYP fn)(GLenum, GLsizei, const GLchar * const *) = func; \
   SET_by_offset(disp, _gloffset_CreateShaderProgramv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DeleteProgramPipelines)(GLsizei, const GLuint *);
#define CALL_DeleteProgramPipelines(disp, parameters) (* GET_DeleteProgramPipelines(disp)) parameters
#define GET_DeleteProgramPipelines(disp) ((_glptr_DeleteProgramPipelines)(GET_by_offset((disp), _gloffset_DeleteProgramPipelines)))
#define SET_DeleteProgramPipelines(disp, func) do { \
   void (GLAPIENTRYP fn)(GLsizei, const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_DeleteProgramPipelines, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GenProgramPipelines)(GLsizei, GLuint *);
#define CALL_GenProgramPipelines(disp, parameters) (* GET_GenProgramPipelines(disp)) parameters
#define GET_GenProgramPipelines(disp) ((_glptr_GenProgramPipelines)(GET_by_offset((disp), _gloffset_GenProgramPipelines)))
#define SET_GenProgramPipelines(disp, func) do { \
   void (GLAPIENTRYP fn)(GLsizei, GLuint *) = func; \
   SET_by_offset(disp, _gloffset_GenProgramPipelines, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetProgramPipelineInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
#define CALL_GetProgramPipelineInfoLog(disp, parameters) (* GET_GetProgramPipelineInfoLog(disp)) parameters
#define GET_GetProgramPipelineInfoLog(disp) ((_glptr_GetProgramPipelineInfoLog)(GET_by_offset((disp), _gloffset_GetProgramPipelineInfoLog)))
#define SET_GetProgramPipelineInfoLog(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizei, GLsizei *, GLchar *) = func; \
   SET_by_offset(disp, _gloffset_GetProgramPipelineInfoLog, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetProgramPipelineiv)(GLuint, GLenum, GLint *);
#define CALL_GetProgramPipelineiv(disp, parameters) (* GET_GetProgramPipelineiv(disp)) parameters
#define GET_GetProgramPipelineiv(disp) ((_glptr_GetProgramPipelineiv)(GET_by_offset((disp), _gloffset_GetProgramPipelineiv)))
#define SET_GetProgramPipelineiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetProgramPipelineiv, fn); \
} while (0)

typedef GLboolean (GLAPIENTRYP _glptr_IsProgramPipeline)(GLuint);
#define CALL_IsProgramPipeline(disp, parameters) (* GET_IsProgramPipeline(disp)) parameters
#define GET_IsProgramPipeline(disp) ((_glptr_IsProgramPipeline)(GET_by_offset((disp), _gloffset_IsProgramPipeline)))
#define SET_IsProgramPipeline(disp, func) do { \
   GLboolean (GLAPIENTRYP fn)(GLuint) = func; \
   SET_by_offset(disp, _gloffset_IsProgramPipeline, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_LockArraysEXT)(GLint, GLsizei);
#define CALL_LockArraysEXT(disp, parameters) (* GET_LockArraysEXT(disp)) parameters
#define GET_LockArraysEXT(disp) ((_glptr_LockArraysEXT)(GET_by_offset((disp), _gloffset_LockArraysEXT)))
#define SET_LockArraysEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_LockArraysEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniform1d)(GLuint, GLint, GLdouble);
#define CALL_ProgramUniform1d(disp, parameters) (* GET_ProgramUniform1d(disp)) parameters
#define GET_ProgramUniform1d(disp) ((_glptr_ProgramUniform1d)(GET_by_offset((disp), _gloffset_ProgramUniform1d)))
#define SET_ProgramUniform1d(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLdouble) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniform1d, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniform1dv)(GLuint, GLint, GLsizei, const GLdouble *);
#define CALL_ProgramUniform1dv(disp, parameters) (* GET_ProgramUniform1dv(disp)) parameters
#define GET_ProgramUniform1dv(disp) ((_glptr_ProgramUniform1dv)(GET_by_offset((disp), _gloffset_ProgramUniform1dv)))
#define SET_ProgramUniform1dv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLsizei, const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniform1dv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniform1f)(GLuint, GLint, GLfloat);
#define CALL_ProgramUniform1f(disp, parameters) (* GET_ProgramUniform1f(disp)) parameters
#define GET_ProgramUniform1f(disp) ((_glptr_ProgramUniform1f)(GET_by_offset((disp), _gloffset_ProgramUniform1f)))
#define SET_ProgramUniform1f(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniform1f, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniform1fv)(GLuint, GLint, GLsizei, const GLfloat *);
#define CALL_ProgramUniform1fv(disp, parameters) (* GET_ProgramUniform1fv(disp)) parameters
#define GET_ProgramUniform1fv(disp) ((_glptr_ProgramUniform1fv)(GET_by_offset((disp), _gloffset_ProgramUniform1fv)))
#define SET_ProgramUniform1fv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLsizei, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniform1fv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniform1i)(GLuint, GLint, GLint);
#define CALL_ProgramUniform1i(disp, parameters) (* GET_ProgramUniform1i(disp)) parameters
#define GET_ProgramUniform1i(disp) ((_glptr_ProgramUniform1i)(GET_by_offset((disp), _gloffset_ProgramUniform1i)))
#define SET_ProgramUniform1i(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLint) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniform1i, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniform1iv)(GLuint, GLint, GLsizei, const GLint *);
#define CALL_ProgramUniform1iv(disp, parameters) (* GET_ProgramUniform1iv(disp)) parameters
#define GET_ProgramUniform1iv(disp) ((_glptr_ProgramUniform1iv)(GET_by_offset((disp), _gloffset_ProgramUniform1iv)))
#define SET_ProgramUniform1iv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLsizei, const GLint *) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniform1iv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniform1ui)(GLuint, GLint, GLuint);
#define CALL_ProgramUniform1ui(disp, parameters) (* GET_ProgramUniform1ui(disp)) parameters
#define GET_ProgramUniform1ui(disp) ((_glptr_ProgramUniform1ui)(GET_by_offset((disp), _gloffset_ProgramUniform1ui)))
#define SET_ProgramUniform1ui(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLuint) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniform1ui, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniform1uiv)(GLuint, GLint, GLsizei, const GLuint *);
#define CALL_ProgramUniform1uiv(disp, parameters) (* GET_ProgramUniform1uiv(disp)) parameters
#define GET_ProgramUniform1uiv(disp) ((_glptr_ProgramUniform1uiv)(GET_by_offset((disp), _gloffset_ProgramUniform1uiv)))
#define SET_ProgramUniform1uiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLsizei, const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniform1uiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniform2d)(GLuint, GLint, GLdouble, GLdouble);
#define CALL_ProgramUniform2d(disp, parameters) (* GET_ProgramUniform2d(disp)) parameters
#define GET_ProgramUniform2d(disp) ((_glptr_ProgramUniform2d)(GET_by_offset((disp), _gloffset_ProgramUniform2d)))
#define SET_ProgramUniform2d(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLdouble, GLdouble) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniform2d, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniform2dv)(GLuint, GLint, GLsizei, const GLdouble *);
#define CALL_ProgramUniform2dv(disp, parameters) (* GET_ProgramUniform2dv(disp)) parameters
#define GET_ProgramUniform2dv(disp) ((_glptr_ProgramUniform2dv)(GET_by_offset((disp), _gloffset_ProgramUniform2dv)))
#define SET_ProgramUniform2dv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLsizei, const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniform2dv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniform2f)(GLuint, GLint, GLfloat, GLfloat);
#define CALL_ProgramUniform2f(disp, parameters) (* GET_ProgramUniform2f(disp)) parameters
#define GET_ProgramUniform2f(disp) ((_glptr_ProgramUniform2f)(GET_by_offset((disp), _gloffset_ProgramUniform2f)))
#define SET_ProgramUniform2f(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLfloat, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniform2f, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniform2fv)(GLuint, GLint, GLsizei, const GLfloat *);
#define CALL_ProgramUniform2fv(disp, parameters) (* GET_ProgramUniform2fv(disp)) parameters
#define GET_ProgramUniform2fv(disp) ((_glptr_ProgramUniform2fv)(GET_by_offset((disp), _gloffset_ProgramUniform2fv)))
#define SET_ProgramUniform2fv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLsizei, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniform2fv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniform2i)(GLuint, GLint, GLint, GLint);
#define CALL_ProgramUniform2i(disp, parameters) (* GET_ProgramUniform2i(disp)) parameters
#define GET_ProgramUniform2i(disp) ((_glptr_ProgramUniform2i)(GET_by_offset((disp), _gloffset_ProgramUniform2i)))
#define SET_ProgramUniform2i(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLint, GLint) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniform2i, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniform2iv)(GLuint, GLint, GLsizei, const GLint *);
#define CALL_ProgramUniform2iv(disp, parameters) (* GET_ProgramUniform2iv(disp)) parameters
#define GET_ProgramUniform2iv(disp) ((_glptr_ProgramUniform2iv)(GET_by_offset((disp), _gloffset_ProgramUniform2iv)))
#define SET_ProgramUniform2iv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLsizei, const GLint *) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniform2iv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniform2ui)(GLuint, GLint, GLuint, GLuint);
#define CALL_ProgramUniform2ui(disp, parameters) (* GET_ProgramUniform2ui(disp)) parameters
#define GET_ProgramUniform2ui(disp) ((_glptr_ProgramUniform2ui)(GET_by_offset((disp), _gloffset_ProgramUniform2ui)))
#define SET_ProgramUniform2ui(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLuint, GLuint) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniform2ui, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniform2uiv)(GLuint, GLint, GLsizei, const GLuint *);
#define CALL_ProgramUniform2uiv(disp, parameters) (* GET_ProgramUniform2uiv(disp)) parameters
#define GET_ProgramUniform2uiv(disp) ((_glptr_ProgramUniform2uiv)(GET_by_offset((disp), _gloffset_ProgramUniform2uiv)))
#define SET_ProgramUniform2uiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLsizei, const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniform2uiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniform3d)(GLuint, GLint, GLdouble, GLdouble, GLdouble);
#define CALL_ProgramUniform3d(disp, parameters) (* GET_ProgramUniform3d(disp)) parameters
#define GET_ProgramUniform3d(disp) ((_glptr_ProgramUniform3d)(GET_by_offset((disp), _gloffset_ProgramUniform3d)))
#define SET_ProgramUniform3d(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLdouble, GLdouble, GLdouble) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniform3d, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniform3dv)(GLuint, GLint, GLsizei, const GLdouble *);
#define CALL_ProgramUniform3dv(disp, parameters) (* GET_ProgramUniform3dv(disp)) parameters
#define GET_ProgramUniform3dv(disp) ((_glptr_ProgramUniform3dv)(GET_by_offset((disp), _gloffset_ProgramUniform3dv)))
#define SET_ProgramUniform3dv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLsizei, const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniform3dv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniform3f)(GLuint, GLint, GLfloat, GLfloat, GLfloat);
#define CALL_ProgramUniform3f(disp, parameters) (* GET_ProgramUniform3f(disp)) parameters
#define GET_ProgramUniform3f(disp) ((_glptr_ProgramUniform3f)(GET_by_offset((disp), _gloffset_ProgramUniform3f)))
#define SET_ProgramUniform3f(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLfloat, GLfloat, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniform3f, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniform3fv)(GLuint, GLint, GLsizei, const GLfloat *);
#define CALL_ProgramUniform3fv(disp, parameters) (* GET_ProgramUniform3fv(disp)) parameters
#define GET_ProgramUniform3fv(disp) ((_glptr_ProgramUniform3fv)(GET_by_offset((disp), _gloffset_ProgramUniform3fv)))
#define SET_ProgramUniform3fv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLsizei, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniform3fv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniform3i)(GLuint, GLint, GLint, GLint, GLint);
#define CALL_ProgramUniform3i(disp, parameters) (* GET_ProgramUniform3i(disp)) parameters
#define GET_ProgramUniform3i(disp) ((_glptr_ProgramUniform3i)(GET_by_offset((disp), _gloffset_ProgramUniform3i)))
#define SET_ProgramUniform3i(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLint, GLint, GLint) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniform3i, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniform3iv)(GLuint, GLint, GLsizei, const GLint *);
#define CALL_ProgramUniform3iv(disp, parameters) (* GET_ProgramUniform3iv(disp)) parameters
#define GET_ProgramUniform3iv(disp) ((_glptr_ProgramUniform3iv)(GET_by_offset((disp), _gloffset_ProgramUniform3iv)))
#define SET_ProgramUniform3iv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLsizei, const GLint *) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniform3iv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniform3ui)(GLuint, GLint, GLuint, GLuint, GLuint);
#define CALL_ProgramUniform3ui(disp, parameters) (* GET_ProgramUniform3ui(disp)) parameters
#define GET_ProgramUniform3ui(disp) ((_glptr_ProgramUniform3ui)(GET_by_offset((disp), _gloffset_ProgramUniform3ui)))
#define SET_ProgramUniform3ui(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLuint, GLuint, GLuint) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniform3ui, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniform3uiv)(GLuint, GLint, GLsizei, const GLuint *);
#define CALL_ProgramUniform3uiv(disp, parameters) (* GET_ProgramUniform3uiv(disp)) parameters
#define GET_ProgramUniform3uiv(disp) ((_glptr_ProgramUniform3uiv)(GET_by_offset((disp), _gloffset_ProgramUniform3uiv)))
#define SET_ProgramUniform3uiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLsizei, const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniform3uiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniform4d)(GLuint, GLint, GLdouble, GLdouble, GLdouble, GLdouble);
#define CALL_ProgramUniform4d(disp, parameters) (* GET_ProgramUniform4d(disp)) parameters
#define GET_ProgramUniform4d(disp) ((_glptr_ProgramUniform4d)(GET_by_offset((disp), _gloffset_ProgramUniform4d)))
#define SET_ProgramUniform4d(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLdouble, GLdouble, GLdouble, GLdouble) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniform4d, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniform4dv)(GLuint, GLint, GLsizei, const GLdouble *);
#define CALL_ProgramUniform4dv(disp, parameters) (* GET_ProgramUniform4dv(disp)) parameters
#define GET_ProgramUniform4dv(disp) ((_glptr_ProgramUniform4dv)(GET_by_offset((disp), _gloffset_ProgramUniform4dv)))
#define SET_ProgramUniform4dv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLsizei, const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniform4dv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniform4f)(GLuint, GLint, GLfloat, GLfloat, GLfloat, GLfloat);
#define CALL_ProgramUniform4f(disp, parameters) (* GET_ProgramUniform4f(disp)) parameters
#define GET_ProgramUniform4f(disp) ((_glptr_ProgramUniform4f)(GET_by_offset((disp), _gloffset_ProgramUniform4f)))
#define SET_ProgramUniform4f(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLfloat, GLfloat, GLfloat, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniform4f, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniform4fv)(GLuint, GLint, GLsizei, const GLfloat *);
#define CALL_ProgramUniform4fv(disp, parameters) (* GET_ProgramUniform4fv(disp)) parameters
#define GET_ProgramUniform4fv(disp) ((_glptr_ProgramUniform4fv)(GET_by_offset((disp), _gloffset_ProgramUniform4fv)))
#define SET_ProgramUniform4fv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLsizei, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniform4fv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniform4i)(GLuint, GLint, GLint, GLint, GLint, GLint);
#define CALL_ProgramUniform4i(disp, parameters) (* GET_ProgramUniform4i(disp)) parameters
#define GET_ProgramUniform4i(disp) ((_glptr_ProgramUniform4i)(GET_by_offset((disp), _gloffset_ProgramUniform4i)))
#define SET_ProgramUniform4i(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLint, GLint, GLint, GLint) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniform4i, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniform4iv)(GLuint, GLint, GLsizei, const GLint *);
#define CALL_ProgramUniform4iv(disp, parameters) (* GET_ProgramUniform4iv(disp)) parameters
#define GET_ProgramUniform4iv(disp) ((_glptr_ProgramUniform4iv)(GET_by_offset((disp), _gloffset_ProgramUniform4iv)))
#define SET_ProgramUniform4iv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLsizei, const GLint *) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniform4iv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniform4ui)(GLuint, GLint, GLuint, GLuint, GLuint, GLuint);
#define CALL_ProgramUniform4ui(disp, parameters) (* GET_ProgramUniform4ui(disp)) parameters
#define GET_ProgramUniform4ui(disp) ((_glptr_ProgramUniform4ui)(GET_by_offset((disp), _gloffset_ProgramUniform4ui)))
#define SET_ProgramUniform4ui(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLuint, GLuint, GLuint, GLuint) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniform4ui, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniform4uiv)(GLuint, GLint, GLsizei, const GLuint *);
#define CALL_ProgramUniform4uiv(disp, parameters) (* GET_ProgramUniform4uiv(disp)) parameters
#define GET_ProgramUniform4uiv(disp) ((_glptr_ProgramUniform4uiv)(GET_by_offset((disp), _gloffset_ProgramUniform4uiv)))
#define SET_ProgramUniform4uiv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLsizei, const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniform4uiv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniformMatrix2dv)(GLuint, GLint, GLsizei, GLboolean, const GLdouble *);
#define CALL_ProgramUniformMatrix2dv(disp, parameters) (* GET_ProgramUniformMatrix2dv(disp)) parameters
#define GET_ProgramUniformMatrix2dv(disp) ((_glptr_ProgramUniformMatrix2dv)(GET_by_offset((disp), _gloffset_ProgramUniformMatrix2dv)))
#define SET_ProgramUniformMatrix2dv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLsizei, GLboolean, const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniformMatrix2dv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniformMatrix2fv)(GLuint, GLint, GLsizei, GLboolean, const GLfloat *);
#define CALL_ProgramUniformMatrix2fv(disp, parameters) (* GET_ProgramUniformMatrix2fv(disp)) parameters
#define GET_ProgramUniformMatrix2fv(disp) ((_glptr_ProgramUniformMatrix2fv)(GET_by_offset((disp), _gloffset_ProgramUniformMatrix2fv)))
#define SET_ProgramUniformMatrix2fv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLsizei, GLboolean, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniformMatrix2fv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniformMatrix2x3dv)(GLuint, GLint, GLsizei, GLboolean, const GLdouble *);
#define CALL_ProgramUniformMatrix2x3dv(disp, parameters) (* GET_ProgramUniformMatrix2x3dv(disp)) parameters
#define GET_ProgramUniformMatrix2x3dv(disp) ((_glptr_ProgramUniformMatrix2x3dv)(GET_by_offset((disp), _gloffset_ProgramUniformMatrix2x3dv)))
#define SET_ProgramUniformMatrix2x3dv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLsizei, GLboolean, const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniformMatrix2x3dv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniformMatrix2x3fv)(GLuint, GLint, GLsizei, GLboolean, const GLfloat *);
#define CALL_ProgramUniformMatrix2x3fv(disp, parameters) (* GET_ProgramUniformMatrix2x3fv(disp)) parameters
#define GET_ProgramUniformMatrix2x3fv(disp) ((_glptr_ProgramUniformMatrix2x3fv)(GET_by_offset((disp), _gloffset_ProgramUniformMatrix2x3fv)))
#define SET_ProgramUniformMatrix2x3fv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLsizei, GLboolean, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniformMatrix2x3fv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniformMatrix2x4dv)(GLuint, GLint, GLsizei, GLboolean, const GLdouble *);
#define CALL_ProgramUniformMatrix2x4dv(disp, parameters) (* GET_ProgramUniformMatrix2x4dv(disp)) parameters
#define GET_ProgramUniformMatrix2x4dv(disp) ((_glptr_ProgramUniformMatrix2x4dv)(GET_by_offset((disp), _gloffset_ProgramUniformMatrix2x4dv)))
#define SET_ProgramUniformMatrix2x4dv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLsizei, GLboolean, const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniformMatrix2x4dv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniformMatrix2x4fv)(GLuint, GLint, GLsizei, GLboolean, const GLfloat *);
#define CALL_ProgramUniformMatrix2x4fv(disp, parameters) (* GET_ProgramUniformMatrix2x4fv(disp)) parameters
#define GET_ProgramUniformMatrix2x4fv(disp) ((_glptr_ProgramUniformMatrix2x4fv)(GET_by_offset((disp), _gloffset_ProgramUniformMatrix2x4fv)))
#define SET_ProgramUniformMatrix2x4fv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLsizei, GLboolean, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniformMatrix2x4fv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniformMatrix3dv)(GLuint, GLint, GLsizei, GLboolean, const GLdouble *);
#define CALL_ProgramUniformMatrix3dv(disp, parameters) (* GET_ProgramUniformMatrix3dv(disp)) parameters
#define GET_ProgramUniformMatrix3dv(disp) ((_glptr_ProgramUniformMatrix3dv)(GET_by_offset((disp), _gloffset_ProgramUniformMatrix3dv)))
#define SET_ProgramUniformMatrix3dv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLsizei, GLboolean, const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniformMatrix3dv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniformMatrix3fv)(GLuint, GLint, GLsizei, GLboolean, const GLfloat *);
#define CALL_ProgramUniformMatrix3fv(disp, parameters) (* GET_ProgramUniformMatrix3fv(disp)) parameters
#define GET_ProgramUniformMatrix3fv(disp) ((_glptr_ProgramUniformMatrix3fv)(GET_by_offset((disp), _gloffset_ProgramUniformMatrix3fv)))
#define SET_ProgramUniformMatrix3fv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLsizei, GLboolean, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniformMatrix3fv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniformMatrix3x2dv)(GLuint, GLint, GLsizei, GLboolean, const GLdouble *);
#define CALL_ProgramUniformMatrix3x2dv(disp, parameters) (* GET_ProgramUniformMatrix3x2dv(disp)) parameters
#define GET_ProgramUniformMatrix3x2dv(disp) ((_glptr_ProgramUniformMatrix3x2dv)(GET_by_offset((disp), _gloffset_ProgramUniformMatrix3x2dv)))
#define SET_ProgramUniformMatrix3x2dv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLsizei, GLboolean, const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniformMatrix3x2dv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniformMatrix3x2fv)(GLuint, GLint, GLsizei, GLboolean, const GLfloat *);
#define CALL_ProgramUniformMatrix3x2fv(disp, parameters) (* GET_ProgramUniformMatrix3x2fv(disp)) parameters
#define GET_ProgramUniformMatrix3x2fv(disp) ((_glptr_ProgramUniformMatrix3x2fv)(GET_by_offset((disp), _gloffset_ProgramUniformMatrix3x2fv)))
#define SET_ProgramUniformMatrix3x2fv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLsizei, GLboolean, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniformMatrix3x2fv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniformMatrix3x4dv)(GLuint, GLint, GLsizei, GLboolean, const GLdouble *);
#define CALL_ProgramUniformMatrix3x4dv(disp, parameters) (* GET_ProgramUniformMatrix3x4dv(disp)) parameters
#define GET_ProgramUniformMatrix3x4dv(disp) ((_glptr_ProgramUniformMatrix3x4dv)(GET_by_offset((disp), _gloffset_ProgramUniformMatrix3x4dv)))
#define SET_ProgramUniformMatrix3x4dv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLsizei, GLboolean, const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniformMatrix3x4dv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniformMatrix3x4fv)(GLuint, GLint, GLsizei, GLboolean, const GLfloat *);
#define CALL_ProgramUniformMatrix3x4fv(disp, parameters) (* GET_ProgramUniformMatrix3x4fv(disp)) parameters
#define GET_ProgramUniformMatrix3x4fv(disp) ((_glptr_ProgramUniformMatrix3x4fv)(GET_by_offset((disp), _gloffset_ProgramUniformMatrix3x4fv)))
#define SET_ProgramUniformMatrix3x4fv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLsizei, GLboolean, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniformMatrix3x4fv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniformMatrix4dv)(GLuint, GLint, GLsizei, GLboolean, const GLdouble *);
#define CALL_ProgramUniformMatrix4dv(disp, parameters) (* GET_ProgramUniformMatrix4dv(disp)) parameters
#define GET_ProgramUniformMatrix4dv(disp) ((_glptr_ProgramUniformMatrix4dv)(GET_by_offset((disp), _gloffset_ProgramUniformMatrix4dv)))
#define SET_ProgramUniformMatrix4dv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLsizei, GLboolean, const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniformMatrix4dv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniformMatrix4fv)(GLuint, GLint, GLsizei, GLboolean, const GLfloat *);
#define CALL_ProgramUniformMatrix4fv(disp, parameters) (* GET_ProgramUniformMatrix4fv(disp)) parameters
#define GET_ProgramUniformMatrix4fv(disp) ((_glptr_ProgramUniformMatrix4fv)(GET_by_offset((disp), _gloffset_ProgramUniformMatrix4fv)))
#define SET_ProgramUniformMatrix4fv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLsizei, GLboolean, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniformMatrix4fv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniformMatrix4x2dv)(GLuint, GLint, GLsizei, GLboolean, const GLdouble *);
#define CALL_ProgramUniformMatrix4x2dv(disp, parameters) (* GET_ProgramUniformMatrix4x2dv(disp)) parameters
#define GET_ProgramUniformMatrix4x2dv(disp) ((_glptr_ProgramUniformMatrix4x2dv)(GET_by_offset((disp), _gloffset_ProgramUniformMatrix4x2dv)))
#define SET_ProgramUniformMatrix4x2dv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLsizei, GLboolean, const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniformMatrix4x2dv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniformMatrix4x2fv)(GLuint, GLint, GLsizei, GLboolean, const GLfloat *);
#define CALL_ProgramUniformMatrix4x2fv(disp, parameters) (* GET_ProgramUniformMatrix4x2fv(disp)) parameters
#define GET_ProgramUniformMatrix4x2fv(disp) ((_glptr_ProgramUniformMatrix4x2fv)(GET_by_offset((disp), _gloffset_ProgramUniformMatrix4x2fv)))
#define SET_ProgramUniformMatrix4x2fv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLsizei, GLboolean, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniformMatrix4x2fv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniformMatrix4x3dv)(GLuint, GLint, GLsizei, GLboolean, const GLdouble *);
#define CALL_ProgramUniformMatrix4x3dv(disp, parameters) (* GET_ProgramUniformMatrix4x3dv(disp)) parameters
#define GET_ProgramUniformMatrix4x3dv(disp) ((_glptr_ProgramUniformMatrix4x3dv)(GET_by_offset((disp), _gloffset_ProgramUniformMatrix4x3dv)))
#define SET_ProgramUniformMatrix4x3dv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLsizei, GLboolean, const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniformMatrix4x3dv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramUniformMatrix4x3fv)(GLuint, GLint, GLsizei, GLboolean, const GLfloat *);
#define CALL_ProgramUniformMatrix4x3fv(disp, parameters) (* GET_ProgramUniformMatrix4x3fv(disp)) parameters
#define GET_ProgramUniformMatrix4x3fv(disp) ((_glptr_ProgramUniformMatrix4x3fv)(GET_by_offset((disp), _gloffset_ProgramUniformMatrix4x3fv)))
#define SET_ProgramUniformMatrix4x3fv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLsizei, GLboolean, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_ProgramUniformMatrix4x3fv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_UnlockArraysEXT)(void);
#define CALL_UnlockArraysEXT(disp, parameters) (* GET_UnlockArraysEXT(disp)) parameters
#define GET_UnlockArraysEXT(disp) ((_glptr_UnlockArraysEXT)(GET_by_offset((disp), _gloffset_UnlockArraysEXT)))
#define SET_UnlockArraysEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(void) = func; \
   SET_by_offset(disp, _gloffset_UnlockArraysEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_UseProgramStages)(GLuint, GLbitfield, GLuint);
#define CALL_UseProgramStages(disp, parameters) (* GET_UseProgramStages(disp)) parameters
#define GET_UseProgramStages(disp) ((_glptr_UseProgramStages)(GET_by_offset((disp), _gloffset_UseProgramStages)))
#define SET_UseProgramStages(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLbitfield, GLuint) = func; \
   SET_by_offset(disp, _gloffset_UseProgramStages, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ValidateProgramPipeline)(GLuint);
#define CALL_ValidateProgramPipeline(disp, parameters) (* GET_ValidateProgramPipeline(disp)) parameters
#define GET_ValidateProgramPipeline(disp) ((_glptr_ValidateProgramPipeline)(GET_by_offset((disp), _gloffset_ValidateProgramPipeline)))
#define SET_ValidateProgramPipeline(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint) = func; \
   SET_by_offset(disp, _gloffset_ValidateProgramPipeline, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_FramebufferTexture2DMultisampleEXT)(GLenum, GLenum, GLenum, GLuint, GLint, GLsizei);
#define CALL_FramebufferTexture2DMultisampleEXT(disp, parameters) (* GET_FramebufferTexture2DMultisampleEXT(disp)) parameters
#define GET_FramebufferTexture2DMultisampleEXT(disp) ((_glptr_FramebufferTexture2DMultisampleEXT)(GET_by_offset((disp), _gloffset_FramebufferTexture2DMultisampleEXT)))
#define SET_FramebufferTexture2DMultisampleEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLenum, GLuint, GLint, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_FramebufferTexture2DMultisampleEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DebugMessageCallback)(GLDEBUGPROC, const GLvoid *);
#define CALL_DebugMessageCallback(disp, parameters) (* GET_DebugMessageCallback(disp)) parameters
#define GET_DebugMessageCallback(disp) ((_glptr_DebugMessageCallback)(GET_by_offset((disp), _gloffset_DebugMessageCallback)))
#define SET_DebugMessageCallback(disp, func) do { \
   void (GLAPIENTRYP fn)(GLDEBUGPROC, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_DebugMessageCallback, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DebugMessageControl)(GLenum, GLenum, GLenum, GLsizei, const GLuint *, GLboolean);
#define CALL_DebugMessageControl(disp, parameters) (* GET_DebugMessageControl(disp)) parameters
#define GET_DebugMessageControl(disp) ((_glptr_DebugMessageControl)(GET_by_offset((disp), _gloffset_DebugMessageControl)))
#define SET_DebugMessageControl(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLenum, GLsizei, const GLuint *, GLboolean) = func; \
   SET_by_offset(disp, _gloffset_DebugMessageControl, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DebugMessageInsert)(GLenum, GLenum, GLuint, GLenum, GLsizei, const GLchar *);
#define CALL_DebugMessageInsert(disp, parameters) (* GET_DebugMessageInsert(disp)) parameters
#define GET_DebugMessageInsert(disp) ((_glptr_DebugMessageInsert)(GET_by_offset((disp), _gloffset_DebugMessageInsert)))
#define SET_DebugMessageInsert(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLuint, GLenum, GLsizei, const GLchar *) = func; \
   SET_by_offset(disp, _gloffset_DebugMessageInsert, fn); \
} while (0)

typedef GLuint (GLAPIENTRYP _glptr_GetDebugMessageLog)(GLuint, GLsizei, GLenum *, GLenum *, GLuint *, GLenum *, GLsizei *, GLchar *);
#define CALL_GetDebugMessageLog(disp, parameters) (* GET_GetDebugMessageLog(disp)) parameters
#define GET_GetDebugMessageLog(disp) ((_glptr_GetDebugMessageLog)(GET_by_offset((disp), _gloffset_GetDebugMessageLog)))
#define SET_GetDebugMessageLog(disp, func) do { \
   GLuint (GLAPIENTRYP fn)(GLuint, GLsizei, GLenum *, GLenum *, GLuint *, GLenum *, GLsizei *, GLchar *) = func; \
   SET_by_offset(disp, _gloffset_GetDebugMessageLog, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetObjectLabel)(GLenum, GLuint, GLsizei, GLsizei *, GLchar *);
#define CALL_GetObjectLabel(disp, parameters) (* GET_GetObjectLabel(disp)) parameters
#define GET_GetObjectLabel(disp) ((_glptr_GetObjectLabel)(GET_by_offset((disp), _gloffset_GetObjectLabel)))
#define SET_GetObjectLabel(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint, GLsizei, GLsizei *, GLchar *) = func; \
   SET_by_offset(disp, _gloffset_GetObjectLabel, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetObjectPtrLabel)(const GLvoid *, GLsizei, GLsizei *, GLchar *);
#define CALL_GetObjectPtrLabel(disp, parameters) (* GET_GetObjectPtrLabel(disp)) parameters
#define GET_GetObjectPtrLabel(disp) ((_glptr_GetObjectPtrLabel)(GET_by_offset((disp), _gloffset_GetObjectPtrLabel)))
#define SET_GetObjectPtrLabel(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLvoid *, GLsizei, GLsizei *, GLchar *) = func; \
   SET_by_offset(disp, _gloffset_GetObjectPtrLabel, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ObjectLabel)(GLenum, GLuint, GLsizei, const GLchar *);
#define CALL_ObjectLabel(disp, parameters) (* GET_ObjectLabel(disp)) parameters
#define GET_ObjectLabel(disp) ((_glptr_ObjectLabel)(GET_by_offset((disp), _gloffset_ObjectLabel)))
#define SET_ObjectLabel(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint, GLsizei, const GLchar *) = func; \
   SET_by_offset(disp, _gloffset_ObjectLabel, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ObjectPtrLabel)(const GLvoid *, GLsizei, const GLchar *);
#define CALL_ObjectPtrLabel(disp, parameters) (* GET_ObjectPtrLabel(disp)) parameters
#define GET_ObjectPtrLabel(disp) ((_glptr_ObjectPtrLabel)(GET_by_offset((disp), _gloffset_ObjectPtrLabel)))
#define SET_ObjectPtrLabel(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLvoid *, GLsizei, const GLchar *) = func; \
   SET_by_offset(disp, _gloffset_ObjectPtrLabel, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_PopDebugGroup)(void);
#define CALL_PopDebugGroup(disp, parameters) (* GET_PopDebugGroup(disp)) parameters
#define GET_PopDebugGroup(disp) ((_glptr_PopDebugGroup)(GET_by_offset((disp), _gloffset_PopDebugGroup)))
#define SET_PopDebugGroup(disp, func) do { \
   void (GLAPIENTRYP fn)(void) = func; \
   SET_by_offset(disp, _gloffset_PopDebugGroup, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_PushDebugGroup)(GLenum, GLuint, GLsizei, const GLchar *);
#define CALL_PushDebugGroup(disp, parameters) (* GET_PushDebugGroup(disp)) parameters
#define GET_PushDebugGroup(disp) ((_glptr_PushDebugGroup)(GET_by_offset((disp), _gloffset_PushDebugGroup)))
#define SET_PushDebugGroup(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint, GLsizei, const GLchar *) = func; \
   SET_by_offset(disp, _gloffset_PushDebugGroup, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_SecondaryColor3fEXT)(GLfloat, GLfloat, GLfloat);
#define CALL_SecondaryColor3fEXT(disp, parameters) (* GET_SecondaryColor3fEXT(disp)) parameters
#define GET_SecondaryColor3fEXT(disp) ((_glptr_SecondaryColor3fEXT)(GET_by_offset((disp), _gloffset_SecondaryColor3fEXT)))
#define SET_SecondaryColor3fEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLfloat, GLfloat, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_SecondaryColor3fEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_SecondaryColor3fvEXT)(const GLfloat *);
#define CALL_SecondaryColor3fvEXT(disp, parameters) (* GET_SecondaryColor3fvEXT(disp)) parameters
#define GET_SecondaryColor3fvEXT(disp) ((_glptr_SecondaryColor3fvEXT)(GET_by_offset((disp), _gloffset_SecondaryColor3fvEXT)))
#define SET_SecondaryColor3fvEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_SecondaryColor3fvEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiDrawElements)(GLenum, const GLsizei *, GLenum, const GLvoid * const *, GLsizei);
#define CALL_MultiDrawElements(disp, parameters) (* GET_MultiDrawElements(disp)) parameters
#define GET_MultiDrawElements(disp) ((_glptr_MultiDrawElements)(GET_by_offset((disp), _gloffset_MultiDrawElements)))
#define SET_MultiDrawElements(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, const GLsizei *, GLenum, const GLvoid * const *, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_MultiDrawElements, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_FogCoordfEXT)(GLfloat);
#define CALL_FogCoordfEXT(disp, parameters) (* GET_FogCoordfEXT(disp)) parameters
#define GET_FogCoordfEXT(disp) ((_glptr_FogCoordfEXT)(GET_by_offset((disp), _gloffset_FogCoordfEXT)))
#define SET_FogCoordfEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLfloat) = func; \
   SET_by_offset(disp, _gloffset_FogCoordfEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_FogCoordfvEXT)(const GLfloat *);
#define CALL_FogCoordfvEXT(disp, parameters) (* GET_FogCoordfvEXT(disp)) parameters
#define GET_FogCoordfvEXT(disp) ((_glptr_FogCoordfvEXT)(GET_by_offset((disp), _gloffset_FogCoordfvEXT)))
#define SET_FogCoordfvEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_FogCoordfvEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ResizeBuffersMESA)(void);
#define CALL_ResizeBuffersMESA(disp, parameters) (* GET_ResizeBuffersMESA(disp)) parameters
#define GET_ResizeBuffersMESA(disp) ((_glptr_ResizeBuffersMESA)(GET_by_offset((disp), _gloffset_ResizeBuffersMESA)))
#define SET_ResizeBuffersMESA(disp, func) do { \
   void (GLAPIENTRYP fn)(void) = func; \
   SET_by_offset(disp, _gloffset_ResizeBuffersMESA, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_WindowPos4dMESA)(GLdouble, GLdouble, GLdouble, GLdouble);
#define CALL_WindowPos4dMESA(disp, parameters) (* GET_WindowPos4dMESA(disp)) parameters
#define GET_WindowPos4dMESA(disp) ((_glptr_WindowPos4dMESA)(GET_by_offset((disp), _gloffset_WindowPos4dMESA)))
#define SET_WindowPos4dMESA(disp, func) do { \
   void (GLAPIENTRYP fn)(GLdouble, GLdouble, GLdouble, GLdouble) = func; \
   SET_by_offset(disp, _gloffset_WindowPos4dMESA, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_WindowPos4dvMESA)(const GLdouble *);
#define CALL_WindowPos4dvMESA(disp, parameters) (* GET_WindowPos4dvMESA(disp)) parameters
#define GET_WindowPos4dvMESA(disp) ((_glptr_WindowPos4dvMESA)(GET_by_offset((disp), _gloffset_WindowPos4dvMESA)))
#define SET_WindowPos4dvMESA(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_WindowPos4dvMESA, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_WindowPos4fMESA)(GLfloat, GLfloat, GLfloat, GLfloat);
#define CALL_WindowPos4fMESA(disp, parameters) (* GET_WindowPos4fMESA(disp)) parameters
#define GET_WindowPos4fMESA(disp) ((_glptr_WindowPos4fMESA)(GET_by_offset((disp), _gloffset_WindowPos4fMESA)))
#define SET_WindowPos4fMESA(disp, func) do { \
   void (GLAPIENTRYP fn)(GLfloat, GLfloat, GLfloat, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_WindowPos4fMESA, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_WindowPos4fvMESA)(const GLfloat *);
#define CALL_WindowPos4fvMESA(disp, parameters) (* GET_WindowPos4fvMESA(disp)) parameters
#define GET_WindowPos4fvMESA(disp) ((_glptr_WindowPos4fvMESA)(GET_by_offset((disp), _gloffset_WindowPos4fvMESA)))
#define SET_WindowPos4fvMESA(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_WindowPos4fvMESA, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_WindowPos4iMESA)(GLint, GLint, GLint, GLint);
#define CALL_WindowPos4iMESA(disp, parameters) (* GET_WindowPos4iMESA(disp)) parameters
#define GET_WindowPos4iMESA(disp) ((_glptr_WindowPos4iMESA)(GET_by_offset((disp), _gloffset_WindowPos4iMESA)))
#define SET_WindowPos4iMESA(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLint, GLint, GLint) = func; \
   SET_by_offset(disp, _gloffset_WindowPos4iMESA, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_WindowPos4ivMESA)(const GLint *);
#define CALL_WindowPos4ivMESA(disp, parameters) (* GET_WindowPos4ivMESA(disp)) parameters
#define GET_WindowPos4ivMESA(disp) ((_glptr_WindowPos4ivMESA)(GET_by_offset((disp), _gloffset_WindowPos4ivMESA)))
#define SET_WindowPos4ivMESA(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLint *) = func; \
   SET_by_offset(disp, _gloffset_WindowPos4ivMESA, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_WindowPos4sMESA)(GLshort, GLshort, GLshort, GLshort);
#define CALL_WindowPos4sMESA(disp, parameters) (* GET_WindowPos4sMESA(disp)) parameters
#define GET_WindowPos4sMESA(disp) ((_glptr_WindowPos4sMESA)(GET_by_offset((disp), _gloffset_WindowPos4sMESA)))
#define SET_WindowPos4sMESA(disp, func) do { \
   void (GLAPIENTRYP fn)(GLshort, GLshort, GLshort, GLshort) = func; \
   SET_by_offset(disp, _gloffset_WindowPos4sMESA, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_WindowPos4svMESA)(const GLshort *);
#define CALL_WindowPos4svMESA(disp, parameters) (* GET_WindowPos4svMESA(disp)) parameters
#define GET_WindowPos4svMESA(disp) ((_glptr_WindowPos4svMESA)(GET_by_offset((disp), _gloffset_WindowPos4svMESA)))
#define SET_WindowPos4svMESA(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLshort *) = func; \
   SET_by_offset(disp, _gloffset_WindowPos4svMESA, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiModeDrawArraysIBM)(const GLenum *, const GLint *, const GLsizei *, GLsizei, GLint);
#define CALL_MultiModeDrawArraysIBM(disp, parameters) (* GET_MultiModeDrawArraysIBM(disp)) parameters
#define GET_MultiModeDrawArraysIBM(disp) ((_glptr_MultiModeDrawArraysIBM)(GET_by_offset((disp), _gloffset_MultiModeDrawArraysIBM)))
#define SET_MultiModeDrawArraysIBM(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLenum *, const GLint *, const GLsizei *, GLsizei, GLint) = func; \
   SET_by_offset(disp, _gloffset_MultiModeDrawArraysIBM, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiModeDrawElementsIBM)(const GLenum *, const GLsizei *, GLenum, const GLvoid * const *, GLsizei, GLint);
#define CALL_MultiModeDrawElementsIBM(disp, parameters) (* GET_MultiModeDrawElementsIBM(disp)) parameters
#define GET_MultiModeDrawElementsIBM(disp) ((_glptr_MultiModeDrawElementsIBM)(GET_by_offset((disp), _gloffset_MultiModeDrawElementsIBM)))
#define SET_MultiModeDrawElementsIBM(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLenum *, const GLsizei *, GLenum, const GLvoid * const *, GLsizei, GLint) = func; \
   SET_by_offset(disp, _gloffset_MultiModeDrawElementsIBM, fn); \
} while (0)

typedef GLboolean (GLAPIENTRYP _glptr_AreProgramsResidentNV)(GLsizei, const GLuint *, GLboolean *);
#define CALL_AreProgramsResidentNV(disp, parameters) (* GET_AreProgramsResidentNV(disp)) parameters
#define GET_AreProgramsResidentNV(disp) ((_glptr_AreProgramsResidentNV)(GET_by_offset((disp), _gloffset_AreProgramsResidentNV)))
#define SET_AreProgramsResidentNV(disp, func) do { \
   GLboolean (GLAPIENTRYP fn)(GLsizei, const GLuint *, GLboolean *) = func; \
   SET_by_offset(disp, _gloffset_AreProgramsResidentNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ExecuteProgramNV)(GLenum, GLuint, const GLfloat *);
#define CALL_ExecuteProgramNV(disp, parameters) (* GET_ExecuteProgramNV(disp)) parameters
#define GET_ExecuteProgramNV(disp) ((_glptr_ExecuteProgramNV)(GET_by_offset((disp), _gloffset_ExecuteProgramNV)))
#define SET_ExecuteProgramNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_ExecuteProgramNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetProgramParameterdvNV)(GLenum, GLuint, GLenum, GLdouble *);
#define CALL_GetProgramParameterdvNV(disp, parameters) (* GET_GetProgramParameterdvNV(disp)) parameters
#define GET_GetProgramParameterdvNV(disp) ((_glptr_GetProgramParameterdvNV)(GET_by_offset((disp), _gloffset_GetProgramParameterdvNV)))
#define SET_GetProgramParameterdvNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint, GLenum, GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_GetProgramParameterdvNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetProgramParameterfvNV)(GLenum, GLuint, GLenum, GLfloat *);
#define CALL_GetProgramParameterfvNV(disp, parameters) (* GET_GetProgramParameterfvNV(disp)) parameters
#define GET_GetProgramParameterfvNV(disp) ((_glptr_GetProgramParameterfvNV)(GET_by_offset((disp), _gloffset_GetProgramParameterfvNV)))
#define SET_GetProgramParameterfvNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint, GLenum, GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_GetProgramParameterfvNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetProgramStringNV)(GLuint, GLenum, GLubyte *);
#define CALL_GetProgramStringNV(disp, parameters) (* GET_GetProgramStringNV(disp)) parameters
#define GET_GetProgramStringNV(disp) ((_glptr_GetProgramStringNV)(GET_by_offset((disp), _gloffset_GetProgramStringNV)))
#define SET_GetProgramStringNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLubyte *) = func; \
   SET_by_offset(disp, _gloffset_GetProgramStringNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetProgramivNV)(GLuint, GLenum, GLint *);
#define CALL_GetProgramivNV(disp, parameters) (* GET_GetProgramivNV(disp)) parameters
#define GET_GetProgramivNV(disp) ((_glptr_GetProgramivNV)(GET_by_offset((disp), _gloffset_GetProgramivNV)))
#define SET_GetProgramivNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetProgramivNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetTrackMatrixivNV)(GLenum, GLuint, GLenum, GLint *);
#define CALL_GetTrackMatrixivNV(disp, parameters) (* GET_GetTrackMatrixivNV(disp)) parameters
#define GET_GetTrackMatrixivNV(disp) ((_glptr_GetTrackMatrixivNV)(GET_by_offset((disp), _gloffset_GetTrackMatrixivNV)))
#define SET_GetTrackMatrixivNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint, GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetTrackMatrixivNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetVertexAttribdvNV)(GLuint, GLenum, GLdouble *);
#define CALL_GetVertexAttribdvNV(disp, parameters) (* GET_GetVertexAttribdvNV(disp)) parameters
#define GET_GetVertexAttribdvNV(disp) ((_glptr_GetVertexAttribdvNV)(GET_by_offset((disp), _gloffset_GetVertexAttribdvNV)))
#define SET_GetVertexAttribdvNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_GetVertexAttribdvNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetVertexAttribfvNV)(GLuint, GLenum, GLfloat *);
#define CALL_GetVertexAttribfvNV(disp, parameters) (* GET_GetVertexAttribfvNV(disp)) parameters
#define GET_GetVertexAttribfvNV(disp) ((_glptr_GetVertexAttribfvNV)(GET_by_offset((disp), _gloffset_GetVertexAttribfvNV)))
#define SET_GetVertexAttribfvNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_GetVertexAttribfvNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetVertexAttribivNV)(GLuint, GLenum, GLint *);
#define CALL_GetVertexAttribivNV(disp, parameters) (* GET_GetVertexAttribivNV(disp)) parameters
#define GET_GetVertexAttribivNV(disp) ((_glptr_GetVertexAttribivNV)(GET_by_offset((disp), _gloffset_GetVertexAttribivNV)))
#define SET_GetVertexAttribivNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetVertexAttribivNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_LoadProgramNV)(GLenum, GLuint, GLsizei, const GLubyte *);
#define CALL_LoadProgramNV(disp, parameters) (* GET_LoadProgramNV(disp)) parameters
#define GET_LoadProgramNV(disp) ((_glptr_LoadProgramNV)(GET_by_offset((disp), _gloffset_LoadProgramNV)))
#define SET_LoadProgramNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint, GLsizei, const GLubyte *) = func; \
   SET_by_offset(disp, _gloffset_LoadProgramNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramParameters4dvNV)(GLenum, GLuint, GLsizei, const GLdouble *);
#define CALL_ProgramParameters4dvNV(disp, parameters) (* GET_ProgramParameters4dvNV(disp)) parameters
#define GET_ProgramParameters4dvNV(disp) ((_glptr_ProgramParameters4dvNV)(GET_by_offset((disp), _gloffset_ProgramParameters4dvNV)))
#define SET_ProgramParameters4dvNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint, GLsizei, const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_ProgramParameters4dvNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramParameters4fvNV)(GLenum, GLuint, GLsizei, const GLfloat *);
#define CALL_ProgramParameters4fvNV(disp, parameters) (* GET_ProgramParameters4fvNV(disp)) parameters
#define GET_ProgramParameters4fvNV(disp) ((_glptr_ProgramParameters4fvNV)(GET_by_offset((disp), _gloffset_ProgramParameters4fvNV)))
#define SET_ProgramParameters4fvNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint, GLsizei, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_ProgramParameters4fvNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_RequestResidentProgramsNV)(GLsizei, const GLuint *);
#define CALL_RequestResidentProgramsNV(disp, parameters) (* GET_RequestResidentProgramsNV(disp)) parameters
#define GET_RequestResidentProgramsNV(disp) ((_glptr_RequestResidentProgramsNV)(GET_by_offset((disp), _gloffset_RequestResidentProgramsNV)))
#define SET_RequestResidentProgramsNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLsizei, const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_RequestResidentProgramsNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TrackMatrixNV)(GLenum, GLuint, GLenum, GLenum);
#define CALL_TrackMatrixNV(disp, parameters) (* GET_TrackMatrixNV(disp)) parameters
#define GET_TrackMatrixNV(disp) ((_glptr_TrackMatrixNV)(GET_by_offset((disp), _gloffset_TrackMatrixNV)))
#define SET_TrackMatrixNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint, GLenum, GLenum) = func; \
   SET_by_offset(disp, _gloffset_TrackMatrixNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib1dNV)(GLuint, GLdouble);
#define CALL_VertexAttrib1dNV(disp, parameters) (* GET_VertexAttrib1dNV(disp)) parameters
#define GET_VertexAttrib1dNV(disp) ((_glptr_VertexAttrib1dNV)(GET_by_offset((disp), _gloffset_VertexAttrib1dNV)))
#define SET_VertexAttrib1dNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLdouble) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib1dNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib1dvNV)(GLuint, const GLdouble *);
#define CALL_VertexAttrib1dvNV(disp, parameters) (* GET_VertexAttrib1dvNV(disp)) parameters
#define GET_VertexAttrib1dvNV(disp) ((_glptr_VertexAttrib1dvNV)(GET_by_offset((disp), _gloffset_VertexAttrib1dvNV)))
#define SET_VertexAttrib1dvNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib1dvNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib1fNV)(GLuint, GLfloat);
#define CALL_VertexAttrib1fNV(disp, parameters) (* GET_VertexAttrib1fNV(disp)) parameters
#define GET_VertexAttrib1fNV(disp) ((_glptr_VertexAttrib1fNV)(GET_by_offset((disp), _gloffset_VertexAttrib1fNV)))
#define SET_VertexAttrib1fNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib1fNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib1fvNV)(GLuint, const GLfloat *);
#define CALL_VertexAttrib1fvNV(disp, parameters) (* GET_VertexAttrib1fvNV(disp)) parameters
#define GET_VertexAttrib1fvNV(disp) ((_glptr_VertexAttrib1fvNV)(GET_by_offset((disp), _gloffset_VertexAttrib1fvNV)))
#define SET_VertexAttrib1fvNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib1fvNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib1sNV)(GLuint, GLshort);
#define CALL_VertexAttrib1sNV(disp, parameters) (* GET_VertexAttrib1sNV(disp)) parameters
#define GET_VertexAttrib1sNV(disp) ((_glptr_VertexAttrib1sNV)(GET_by_offset((disp), _gloffset_VertexAttrib1sNV)))
#define SET_VertexAttrib1sNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLshort) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib1sNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib1svNV)(GLuint, const GLshort *);
#define CALL_VertexAttrib1svNV(disp, parameters) (* GET_VertexAttrib1svNV(disp)) parameters
#define GET_VertexAttrib1svNV(disp) ((_glptr_VertexAttrib1svNV)(GET_by_offset((disp), _gloffset_VertexAttrib1svNV)))
#define SET_VertexAttrib1svNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLshort *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib1svNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib2dNV)(GLuint, GLdouble, GLdouble);
#define CALL_VertexAttrib2dNV(disp, parameters) (* GET_VertexAttrib2dNV(disp)) parameters
#define GET_VertexAttrib2dNV(disp) ((_glptr_VertexAttrib2dNV)(GET_by_offset((disp), _gloffset_VertexAttrib2dNV)))
#define SET_VertexAttrib2dNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLdouble, GLdouble) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib2dNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib2dvNV)(GLuint, const GLdouble *);
#define CALL_VertexAttrib2dvNV(disp, parameters) (* GET_VertexAttrib2dvNV(disp)) parameters
#define GET_VertexAttrib2dvNV(disp) ((_glptr_VertexAttrib2dvNV)(GET_by_offset((disp), _gloffset_VertexAttrib2dvNV)))
#define SET_VertexAttrib2dvNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib2dvNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib2fNV)(GLuint, GLfloat, GLfloat);
#define CALL_VertexAttrib2fNV(disp, parameters) (* GET_VertexAttrib2fNV(disp)) parameters
#define GET_VertexAttrib2fNV(disp) ((_glptr_VertexAttrib2fNV)(GET_by_offset((disp), _gloffset_VertexAttrib2fNV)))
#define SET_VertexAttrib2fNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLfloat, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib2fNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib2fvNV)(GLuint, const GLfloat *);
#define CALL_VertexAttrib2fvNV(disp, parameters) (* GET_VertexAttrib2fvNV(disp)) parameters
#define GET_VertexAttrib2fvNV(disp) ((_glptr_VertexAttrib2fvNV)(GET_by_offset((disp), _gloffset_VertexAttrib2fvNV)))
#define SET_VertexAttrib2fvNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib2fvNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib2sNV)(GLuint, GLshort, GLshort);
#define CALL_VertexAttrib2sNV(disp, parameters) (* GET_VertexAttrib2sNV(disp)) parameters
#define GET_VertexAttrib2sNV(disp) ((_glptr_VertexAttrib2sNV)(GET_by_offset((disp), _gloffset_VertexAttrib2sNV)))
#define SET_VertexAttrib2sNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLshort, GLshort) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib2sNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib2svNV)(GLuint, const GLshort *);
#define CALL_VertexAttrib2svNV(disp, parameters) (* GET_VertexAttrib2svNV(disp)) parameters
#define GET_VertexAttrib2svNV(disp) ((_glptr_VertexAttrib2svNV)(GET_by_offset((disp), _gloffset_VertexAttrib2svNV)))
#define SET_VertexAttrib2svNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLshort *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib2svNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib3dNV)(GLuint, GLdouble, GLdouble, GLdouble);
#define CALL_VertexAttrib3dNV(disp, parameters) (* GET_VertexAttrib3dNV(disp)) parameters
#define GET_VertexAttrib3dNV(disp) ((_glptr_VertexAttrib3dNV)(GET_by_offset((disp), _gloffset_VertexAttrib3dNV)))
#define SET_VertexAttrib3dNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLdouble, GLdouble, GLdouble) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib3dNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib3dvNV)(GLuint, const GLdouble *);
#define CALL_VertexAttrib3dvNV(disp, parameters) (* GET_VertexAttrib3dvNV(disp)) parameters
#define GET_VertexAttrib3dvNV(disp) ((_glptr_VertexAttrib3dvNV)(GET_by_offset((disp), _gloffset_VertexAttrib3dvNV)))
#define SET_VertexAttrib3dvNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib3dvNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib3fNV)(GLuint, GLfloat, GLfloat, GLfloat);
#define CALL_VertexAttrib3fNV(disp, parameters) (* GET_VertexAttrib3fNV(disp)) parameters
#define GET_VertexAttrib3fNV(disp) ((_glptr_VertexAttrib3fNV)(GET_by_offset((disp), _gloffset_VertexAttrib3fNV)))
#define SET_VertexAttrib3fNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLfloat, GLfloat, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib3fNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib3fvNV)(GLuint, const GLfloat *);
#define CALL_VertexAttrib3fvNV(disp, parameters) (* GET_VertexAttrib3fvNV(disp)) parameters
#define GET_VertexAttrib3fvNV(disp) ((_glptr_VertexAttrib3fvNV)(GET_by_offset((disp), _gloffset_VertexAttrib3fvNV)))
#define SET_VertexAttrib3fvNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib3fvNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib3sNV)(GLuint, GLshort, GLshort, GLshort);
#define CALL_VertexAttrib3sNV(disp, parameters) (* GET_VertexAttrib3sNV(disp)) parameters
#define GET_VertexAttrib3sNV(disp) ((_glptr_VertexAttrib3sNV)(GET_by_offset((disp), _gloffset_VertexAttrib3sNV)))
#define SET_VertexAttrib3sNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLshort, GLshort, GLshort) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib3sNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib3svNV)(GLuint, const GLshort *);
#define CALL_VertexAttrib3svNV(disp, parameters) (* GET_VertexAttrib3svNV(disp)) parameters
#define GET_VertexAttrib3svNV(disp) ((_glptr_VertexAttrib3svNV)(GET_by_offset((disp), _gloffset_VertexAttrib3svNV)))
#define SET_VertexAttrib3svNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLshort *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib3svNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib4dNV)(GLuint, GLdouble, GLdouble, GLdouble, GLdouble);
#define CALL_VertexAttrib4dNV(disp, parameters) (* GET_VertexAttrib4dNV(disp)) parameters
#define GET_VertexAttrib4dNV(disp) ((_glptr_VertexAttrib4dNV)(GET_by_offset((disp), _gloffset_VertexAttrib4dNV)))
#define SET_VertexAttrib4dNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLdouble, GLdouble, GLdouble, GLdouble) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib4dNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib4dvNV)(GLuint, const GLdouble *);
#define CALL_VertexAttrib4dvNV(disp, parameters) (* GET_VertexAttrib4dvNV(disp)) parameters
#define GET_VertexAttrib4dvNV(disp) ((_glptr_VertexAttrib4dvNV)(GET_by_offset((disp), _gloffset_VertexAttrib4dvNV)))
#define SET_VertexAttrib4dvNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib4dvNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib4fNV)(GLuint, GLfloat, GLfloat, GLfloat, GLfloat);
#define CALL_VertexAttrib4fNV(disp, parameters) (* GET_VertexAttrib4fNV(disp)) parameters
#define GET_VertexAttrib4fNV(disp) ((_glptr_VertexAttrib4fNV)(GET_by_offset((disp), _gloffset_VertexAttrib4fNV)))
#define SET_VertexAttrib4fNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLfloat, GLfloat, GLfloat, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib4fNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib4fvNV)(GLuint, const GLfloat *);
#define CALL_VertexAttrib4fvNV(disp, parameters) (* GET_VertexAttrib4fvNV(disp)) parameters
#define GET_VertexAttrib4fvNV(disp) ((_glptr_VertexAttrib4fvNV)(GET_by_offset((disp), _gloffset_VertexAttrib4fvNV)))
#define SET_VertexAttrib4fvNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib4fvNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib4sNV)(GLuint, GLshort, GLshort, GLshort, GLshort);
#define CALL_VertexAttrib4sNV(disp, parameters) (* GET_VertexAttrib4sNV(disp)) parameters
#define GET_VertexAttrib4sNV(disp) ((_glptr_VertexAttrib4sNV)(GET_by_offset((disp), _gloffset_VertexAttrib4sNV)))
#define SET_VertexAttrib4sNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLshort, GLshort, GLshort, GLshort) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib4sNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib4svNV)(GLuint, const GLshort *);
#define CALL_VertexAttrib4svNV(disp, parameters) (* GET_VertexAttrib4svNV(disp)) parameters
#define GET_VertexAttrib4svNV(disp) ((_glptr_VertexAttrib4svNV)(GET_by_offset((disp), _gloffset_VertexAttrib4svNV)))
#define SET_VertexAttrib4svNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLshort *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib4svNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib4ubNV)(GLuint, GLubyte, GLubyte, GLubyte, GLubyte);
#define CALL_VertexAttrib4ubNV(disp, parameters) (* GET_VertexAttrib4ubNV(disp)) parameters
#define GET_VertexAttrib4ubNV(disp) ((_glptr_VertexAttrib4ubNV)(GET_by_offset((disp), _gloffset_VertexAttrib4ubNV)))
#define SET_VertexAttrib4ubNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLubyte, GLubyte, GLubyte, GLubyte) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib4ubNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib4ubvNV)(GLuint, const GLubyte *);
#define CALL_VertexAttrib4ubvNV(disp, parameters) (* GET_VertexAttrib4ubvNV(disp)) parameters
#define GET_VertexAttrib4ubvNV(disp) ((_glptr_VertexAttrib4ubvNV)(GET_by_offset((disp), _gloffset_VertexAttrib4ubvNV)))
#define SET_VertexAttrib4ubvNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLubyte *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib4ubvNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribPointerNV)(GLuint, GLint, GLenum, GLsizei, const GLvoid *);
#define CALL_VertexAttribPointerNV(disp, parameters) (* GET_VertexAttribPointerNV(disp)) parameters
#define GET_VertexAttribPointerNV(disp) ((_glptr_VertexAttribPointerNV)(GET_by_offset((disp), _gloffset_VertexAttribPointerNV)))
#define SET_VertexAttribPointerNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLenum, GLsizei, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribPointerNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribs1dvNV)(GLuint, GLsizei, const GLdouble *);
#define CALL_VertexAttribs1dvNV(disp, parameters) (* GET_VertexAttribs1dvNV(disp)) parameters
#define GET_VertexAttribs1dvNV(disp) ((_glptr_VertexAttribs1dvNV)(GET_by_offset((disp), _gloffset_VertexAttribs1dvNV)))
#define SET_VertexAttribs1dvNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizei, const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribs1dvNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribs1fvNV)(GLuint, GLsizei, const GLfloat *);
#define CALL_VertexAttribs1fvNV(disp, parameters) (* GET_VertexAttribs1fvNV(disp)) parameters
#define GET_VertexAttribs1fvNV(disp) ((_glptr_VertexAttribs1fvNV)(GET_by_offset((disp), _gloffset_VertexAttribs1fvNV)))
#define SET_VertexAttribs1fvNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizei, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribs1fvNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribs1svNV)(GLuint, GLsizei, const GLshort *);
#define CALL_VertexAttribs1svNV(disp, parameters) (* GET_VertexAttribs1svNV(disp)) parameters
#define GET_VertexAttribs1svNV(disp) ((_glptr_VertexAttribs1svNV)(GET_by_offset((disp), _gloffset_VertexAttribs1svNV)))
#define SET_VertexAttribs1svNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizei, const GLshort *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribs1svNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribs2dvNV)(GLuint, GLsizei, const GLdouble *);
#define CALL_VertexAttribs2dvNV(disp, parameters) (* GET_VertexAttribs2dvNV(disp)) parameters
#define GET_VertexAttribs2dvNV(disp) ((_glptr_VertexAttribs2dvNV)(GET_by_offset((disp), _gloffset_VertexAttribs2dvNV)))
#define SET_VertexAttribs2dvNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizei, const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribs2dvNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribs2fvNV)(GLuint, GLsizei, const GLfloat *);
#define CALL_VertexAttribs2fvNV(disp, parameters) (* GET_VertexAttribs2fvNV(disp)) parameters
#define GET_VertexAttribs2fvNV(disp) ((_glptr_VertexAttribs2fvNV)(GET_by_offset((disp), _gloffset_VertexAttribs2fvNV)))
#define SET_VertexAttribs2fvNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizei, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribs2fvNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribs2svNV)(GLuint, GLsizei, const GLshort *);
#define CALL_VertexAttribs2svNV(disp, parameters) (* GET_VertexAttribs2svNV(disp)) parameters
#define GET_VertexAttribs2svNV(disp) ((_glptr_VertexAttribs2svNV)(GET_by_offset((disp), _gloffset_VertexAttribs2svNV)))
#define SET_VertexAttribs2svNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizei, const GLshort *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribs2svNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribs3dvNV)(GLuint, GLsizei, const GLdouble *);
#define CALL_VertexAttribs3dvNV(disp, parameters) (* GET_VertexAttribs3dvNV(disp)) parameters
#define GET_VertexAttribs3dvNV(disp) ((_glptr_VertexAttribs3dvNV)(GET_by_offset((disp), _gloffset_VertexAttribs3dvNV)))
#define SET_VertexAttribs3dvNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizei, const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribs3dvNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribs3fvNV)(GLuint, GLsizei, const GLfloat *);
#define CALL_VertexAttribs3fvNV(disp, parameters) (* GET_VertexAttribs3fvNV(disp)) parameters
#define GET_VertexAttribs3fvNV(disp) ((_glptr_VertexAttribs3fvNV)(GET_by_offset((disp), _gloffset_VertexAttribs3fvNV)))
#define SET_VertexAttribs3fvNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizei, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribs3fvNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribs3svNV)(GLuint, GLsizei, const GLshort *);
#define CALL_VertexAttribs3svNV(disp, parameters) (* GET_VertexAttribs3svNV(disp)) parameters
#define GET_VertexAttribs3svNV(disp) ((_glptr_VertexAttribs3svNV)(GET_by_offset((disp), _gloffset_VertexAttribs3svNV)))
#define SET_VertexAttribs3svNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizei, const GLshort *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribs3svNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribs4dvNV)(GLuint, GLsizei, const GLdouble *);
#define CALL_VertexAttribs4dvNV(disp, parameters) (* GET_VertexAttribs4dvNV(disp)) parameters
#define GET_VertexAttribs4dvNV(disp) ((_glptr_VertexAttribs4dvNV)(GET_by_offset((disp), _gloffset_VertexAttribs4dvNV)))
#define SET_VertexAttribs4dvNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizei, const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribs4dvNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribs4fvNV)(GLuint, GLsizei, const GLfloat *);
#define CALL_VertexAttribs4fvNV(disp, parameters) (* GET_VertexAttribs4fvNV(disp)) parameters
#define GET_VertexAttribs4fvNV(disp) ((_glptr_VertexAttribs4fvNV)(GET_by_offset((disp), _gloffset_VertexAttribs4fvNV)))
#define SET_VertexAttribs4fvNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizei, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribs4fvNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribs4svNV)(GLuint, GLsizei, const GLshort *);
#define CALL_VertexAttribs4svNV(disp, parameters) (* GET_VertexAttribs4svNV(disp)) parameters
#define GET_VertexAttribs4svNV(disp) ((_glptr_VertexAttribs4svNV)(GET_by_offset((disp), _gloffset_VertexAttribs4svNV)))
#define SET_VertexAttribs4svNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizei, const GLshort *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribs4svNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribs4ubvNV)(GLuint, GLsizei, const GLubyte *);
#define CALL_VertexAttribs4ubvNV(disp, parameters) (* GET_VertexAttribs4ubvNV(disp)) parameters
#define GET_VertexAttribs4ubvNV(disp) ((_glptr_VertexAttribs4ubvNV)(GET_by_offset((disp), _gloffset_VertexAttribs4ubvNV)))
#define SET_VertexAttribs4ubvNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizei, const GLubyte *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribs4ubvNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetTexBumpParameterfvATI)(GLenum, GLfloat *);
#define CALL_GetTexBumpParameterfvATI(disp, parameters) (* GET_GetTexBumpParameterfvATI(disp)) parameters
#define GET_GetTexBumpParameterfvATI(disp) ((_glptr_GetTexBumpParameterfvATI)(GET_by_offset((disp), _gloffset_GetTexBumpParameterfvATI)))
#define SET_GetTexBumpParameterfvATI(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_GetTexBumpParameterfvATI, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetTexBumpParameterivATI)(GLenum, GLint *);
#define CALL_GetTexBumpParameterivATI(disp, parameters) (* GET_GetTexBumpParameterivATI(disp)) parameters
#define GET_GetTexBumpParameterivATI(disp) ((_glptr_GetTexBumpParameterivATI)(GET_by_offset((disp), _gloffset_GetTexBumpParameterivATI)))
#define SET_GetTexBumpParameterivATI(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetTexBumpParameterivATI, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexBumpParameterfvATI)(GLenum, const GLfloat *);
#define CALL_TexBumpParameterfvATI(disp, parameters) (* GET_TexBumpParameterfvATI(disp)) parameters
#define GET_TexBumpParameterfvATI(disp) ((_glptr_TexBumpParameterfvATI)(GET_by_offset((disp), _gloffset_TexBumpParameterfvATI)))
#define SET_TexBumpParameterfvATI(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_TexBumpParameterfvATI, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexBumpParameterivATI)(GLenum, const GLint *);
#define CALL_TexBumpParameterivATI(disp, parameters) (* GET_TexBumpParameterivATI(disp)) parameters
#define GET_TexBumpParameterivATI(disp) ((_glptr_TexBumpParameterivATI)(GET_by_offset((disp), _gloffset_TexBumpParameterivATI)))
#define SET_TexBumpParameterivATI(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, const GLint *) = func; \
   SET_by_offset(disp, _gloffset_TexBumpParameterivATI, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_AlphaFragmentOp1ATI)(GLenum, GLuint, GLuint, GLuint, GLuint, GLuint);
#define CALL_AlphaFragmentOp1ATI(disp, parameters) (* GET_AlphaFragmentOp1ATI(disp)) parameters
#define GET_AlphaFragmentOp1ATI(disp) ((_glptr_AlphaFragmentOp1ATI)(GET_by_offset((disp), _gloffset_AlphaFragmentOp1ATI)))
#define SET_AlphaFragmentOp1ATI(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint, GLuint, GLuint, GLuint, GLuint) = func; \
   SET_by_offset(disp, _gloffset_AlphaFragmentOp1ATI, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_AlphaFragmentOp2ATI)(GLenum, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint);
#define CALL_AlphaFragmentOp2ATI(disp, parameters) (* GET_AlphaFragmentOp2ATI(disp)) parameters
#define GET_AlphaFragmentOp2ATI(disp) ((_glptr_AlphaFragmentOp2ATI)(GET_by_offset((disp), _gloffset_AlphaFragmentOp2ATI)))
#define SET_AlphaFragmentOp2ATI(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint) = func; \
   SET_by_offset(disp, _gloffset_AlphaFragmentOp2ATI, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_AlphaFragmentOp3ATI)(GLenum, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint);
#define CALL_AlphaFragmentOp3ATI(disp, parameters) (* GET_AlphaFragmentOp3ATI(disp)) parameters
#define GET_AlphaFragmentOp3ATI(disp) ((_glptr_AlphaFragmentOp3ATI)(GET_by_offset((disp), _gloffset_AlphaFragmentOp3ATI)))
#define SET_AlphaFragmentOp3ATI(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint) = func; \
   SET_by_offset(disp, _gloffset_AlphaFragmentOp3ATI, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_BeginFragmentShaderATI)(void);
#define CALL_BeginFragmentShaderATI(disp, parameters) (* GET_BeginFragmentShaderATI(disp)) parameters
#define GET_BeginFragmentShaderATI(disp) ((_glptr_BeginFragmentShaderATI)(GET_by_offset((disp), _gloffset_BeginFragmentShaderATI)))
#define SET_BeginFragmentShaderATI(disp, func) do { \
   void (GLAPIENTRYP fn)(void) = func; \
   SET_by_offset(disp, _gloffset_BeginFragmentShaderATI, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_BindFragmentShaderATI)(GLuint);
#define CALL_BindFragmentShaderATI(disp, parameters) (* GET_BindFragmentShaderATI(disp)) parameters
#define GET_BindFragmentShaderATI(disp) ((_glptr_BindFragmentShaderATI)(GET_by_offset((disp), _gloffset_BindFragmentShaderATI)))
#define SET_BindFragmentShaderATI(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint) = func; \
   SET_by_offset(disp, _gloffset_BindFragmentShaderATI, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ColorFragmentOp1ATI)(GLenum, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint);
#define CALL_ColorFragmentOp1ATI(disp, parameters) (* GET_ColorFragmentOp1ATI(disp)) parameters
#define GET_ColorFragmentOp1ATI(disp) ((_glptr_ColorFragmentOp1ATI)(GET_by_offset((disp), _gloffset_ColorFragmentOp1ATI)))
#define SET_ColorFragmentOp1ATI(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint) = func; \
   SET_by_offset(disp, _gloffset_ColorFragmentOp1ATI, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ColorFragmentOp2ATI)(GLenum, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint);
#define CALL_ColorFragmentOp2ATI(disp, parameters) (* GET_ColorFragmentOp2ATI(disp)) parameters
#define GET_ColorFragmentOp2ATI(disp) ((_glptr_ColorFragmentOp2ATI)(GET_by_offset((disp), _gloffset_ColorFragmentOp2ATI)))
#define SET_ColorFragmentOp2ATI(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint) = func; \
   SET_by_offset(disp, _gloffset_ColorFragmentOp2ATI, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ColorFragmentOp3ATI)(GLenum, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint);
#define CALL_ColorFragmentOp3ATI(disp, parameters) (* GET_ColorFragmentOp3ATI(disp)) parameters
#define GET_ColorFragmentOp3ATI(disp) ((_glptr_ColorFragmentOp3ATI)(GET_by_offset((disp), _gloffset_ColorFragmentOp3ATI)))
#define SET_ColorFragmentOp3ATI(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint) = func; \
   SET_by_offset(disp, _gloffset_ColorFragmentOp3ATI, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DeleteFragmentShaderATI)(GLuint);
#define CALL_DeleteFragmentShaderATI(disp, parameters) (* GET_DeleteFragmentShaderATI(disp)) parameters
#define GET_DeleteFragmentShaderATI(disp) ((_glptr_DeleteFragmentShaderATI)(GET_by_offset((disp), _gloffset_DeleteFragmentShaderATI)))
#define SET_DeleteFragmentShaderATI(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint) = func; \
   SET_by_offset(disp, _gloffset_DeleteFragmentShaderATI, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_EndFragmentShaderATI)(void);
#define CALL_EndFragmentShaderATI(disp, parameters) (* GET_EndFragmentShaderATI(disp)) parameters
#define GET_EndFragmentShaderATI(disp) ((_glptr_EndFragmentShaderATI)(GET_by_offset((disp), _gloffset_EndFragmentShaderATI)))
#define SET_EndFragmentShaderATI(disp, func) do { \
   void (GLAPIENTRYP fn)(void) = func; \
   SET_by_offset(disp, _gloffset_EndFragmentShaderATI, fn); \
} while (0)

typedef GLuint (GLAPIENTRYP _glptr_GenFragmentShadersATI)(GLuint);
#define CALL_GenFragmentShadersATI(disp, parameters) (* GET_GenFragmentShadersATI(disp)) parameters
#define GET_GenFragmentShadersATI(disp) ((_glptr_GenFragmentShadersATI)(GET_by_offset((disp), _gloffset_GenFragmentShadersATI)))
#define SET_GenFragmentShadersATI(disp, func) do { \
   GLuint (GLAPIENTRYP fn)(GLuint) = func; \
   SET_by_offset(disp, _gloffset_GenFragmentShadersATI, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_PassTexCoordATI)(GLuint, GLuint, GLenum);
#define CALL_PassTexCoordATI(disp, parameters) (* GET_PassTexCoordATI(disp)) parameters
#define GET_PassTexCoordATI(disp) ((_glptr_PassTexCoordATI)(GET_by_offset((disp), _gloffset_PassTexCoordATI)))
#define SET_PassTexCoordATI(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLenum) = func; \
   SET_by_offset(disp, _gloffset_PassTexCoordATI, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_SampleMapATI)(GLuint, GLuint, GLenum);
#define CALL_SampleMapATI(disp, parameters) (* GET_SampleMapATI(disp)) parameters
#define GET_SampleMapATI(disp) ((_glptr_SampleMapATI)(GET_by_offset((disp), _gloffset_SampleMapATI)))
#define SET_SampleMapATI(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLenum) = func; \
   SET_by_offset(disp, _gloffset_SampleMapATI, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_SetFragmentShaderConstantATI)(GLuint, const GLfloat *);
#define CALL_SetFragmentShaderConstantATI(disp, parameters) (* GET_SetFragmentShaderConstantATI(disp)) parameters
#define GET_SetFragmentShaderConstantATI(disp) ((_glptr_SetFragmentShaderConstantATI)(GET_by_offset((disp), _gloffset_SetFragmentShaderConstantATI)))
#define SET_SetFragmentShaderConstantATI(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_SetFragmentShaderConstantATI, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DepthRangeArrayfvOES)(GLuint, GLsizei, const GLfloat *);
#define CALL_DepthRangeArrayfvOES(disp, parameters) (* GET_DepthRangeArrayfvOES(disp)) parameters
#define GET_DepthRangeArrayfvOES(disp) ((_glptr_DepthRangeArrayfvOES)(GET_by_offset((disp), _gloffset_DepthRangeArrayfvOES)))
#define SET_DepthRangeArrayfvOES(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizei, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_DepthRangeArrayfvOES, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DepthRangeIndexedfOES)(GLuint, GLfloat, GLfloat);
#define CALL_DepthRangeIndexedfOES(disp, parameters) (* GET_DepthRangeIndexedfOES(disp)) parameters
#define GET_DepthRangeIndexedfOES(disp) ((_glptr_DepthRangeIndexedfOES)(GET_by_offset((disp), _gloffset_DepthRangeIndexedfOES)))
#define SET_DepthRangeIndexedfOES(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLfloat, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_DepthRangeIndexedfOES, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ActiveStencilFaceEXT)(GLenum);
#define CALL_ActiveStencilFaceEXT(disp, parameters) (* GET_ActiveStencilFaceEXT(disp)) parameters
#define GET_ActiveStencilFaceEXT(disp) ((_glptr_ActiveStencilFaceEXT)(GET_by_offset((disp), _gloffset_ActiveStencilFaceEXT)))
#define SET_ActiveStencilFaceEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum) = func; \
   SET_by_offset(disp, _gloffset_ActiveStencilFaceEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetProgramNamedParameterdvNV)(GLuint, GLsizei, const GLubyte *, GLdouble *);
#define CALL_GetProgramNamedParameterdvNV(disp, parameters) (* GET_GetProgramNamedParameterdvNV(disp)) parameters
#define GET_GetProgramNamedParameterdvNV(disp) ((_glptr_GetProgramNamedParameterdvNV)(GET_by_offset((disp), _gloffset_GetProgramNamedParameterdvNV)))
#define SET_GetProgramNamedParameterdvNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizei, const GLubyte *, GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_GetProgramNamedParameterdvNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetProgramNamedParameterfvNV)(GLuint, GLsizei, const GLubyte *, GLfloat *);
#define CALL_GetProgramNamedParameterfvNV(disp, parameters) (* GET_GetProgramNamedParameterfvNV(disp)) parameters
#define GET_GetProgramNamedParameterfvNV(disp) ((_glptr_GetProgramNamedParameterfvNV)(GET_by_offset((disp), _gloffset_GetProgramNamedParameterfvNV)))
#define SET_GetProgramNamedParameterfvNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizei, const GLubyte *, GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_GetProgramNamedParameterfvNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramNamedParameter4dNV)(GLuint, GLsizei, const GLubyte *, GLdouble, GLdouble, GLdouble, GLdouble);
#define CALL_ProgramNamedParameter4dNV(disp, parameters) (* GET_ProgramNamedParameter4dNV(disp)) parameters
#define GET_ProgramNamedParameter4dNV(disp) ((_glptr_ProgramNamedParameter4dNV)(GET_by_offset((disp), _gloffset_ProgramNamedParameter4dNV)))
#define SET_ProgramNamedParameter4dNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizei, const GLubyte *, GLdouble, GLdouble, GLdouble, GLdouble) = func; \
   SET_by_offset(disp, _gloffset_ProgramNamedParameter4dNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramNamedParameter4dvNV)(GLuint, GLsizei, const GLubyte *, const GLdouble *);
#define CALL_ProgramNamedParameter4dvNV(disp, parameters) (* GET_ProgramNamedParameter4dvNV(disp)) parameters
#define GET_ProgramNamedParameter4dvNV(disp) ((_glptr_ProgramNamedParameter4dvNV)(GET_by_offset((disp), _gloffset_ProgramNamedParameter4dvNV)))
#define SET_ProgramNamedParameter4dvNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizei, const GLubyte *, const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_ProgramNamedParameter4dvNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramNamedParameter4fNV)(GLuint, GLsizei, const GLubyte *, GLfloat, GLfloat, GLfloat, GLfloat);
#define CALL_ProgramNamedParameter4fNV(disp, parameters) (* GET_ProgramNamedParameter4fNV(disp)) parameters
#define GET_ProgramNamedParameter4fNV(disp) ((_glptr_ProgramNamedParameter4fNV)(GET_by_offset((disp), _gloffset_ProgramNamedParameter4fNV)))
#define SET_ProgramNamedParameter4fNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizei, const GLubyte *, GLfloat, GLfloat, GLfloat, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_ProgramNamedParameter4fNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramNamedParameter4fvNV)(GLuint, GLsizei, const GLubyte *, const GLfloat *);
#define CALL_ProgramNamedParameter4fvNV(disp, parameters) (* GET_ProgramNamedParameter4fvNV(disp)) parameters
#define GET_ProgramNamedParameter4fvNV(disp) ((_glptr_ProgramNamedParameter4fvNV)(GET_by_offset((disp), _gloffset_ProgramNamedParameter4fvNV)))
#define SET_ProgramNamedParameter4fvNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizei, const GLubyte *, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_ProgramNamedParameter4fvNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_PrimitiveRestartNV)(void);
#define CALL_PrimitiveRestartNV(disp, parameters) (* GET_PrimitiveRestartNV(disp)) parameters
#define GET_PrimitiveRestartNV(disp) ((_glptr_PrimitiveRestartNV)(GET_by_offset((disp), _gloffset_PrimitiveRestartNV)))
#define SET_PrimitiveRestartNV(disp, func) do { \
   void (GLAPIENTRYP fn)(void) = func; \
   SET_by_offset(disp, _gloffset_PrimitiveRestartNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetTexGenxvOES)(GLenum, GLenum, GLfixed *);
#define CALL_GetTexGenxvOES(disp, parameters) (* GET_GetTexGenxvOES(disp)) parameters
#define GET_GetTexGenxvOES(disp) ((_glptr_GetTexGenxvOES)(GET_by_offset((disp), _gloffset_GetTexGenxvOES)))
#define SET_GetTexGenxvOES(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLfixed *) = func; \
   SET_by_offset(disp, _gloffset_GetTexGenxvOES, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexGenxOES)(GLenum, GLenum, GLfixed);
#define CALL_TexGenxOES(disp, parameters) (* GET_TexGenxOES(disp)) parameters
#define GET_TexGenxOES(disp) ((_glptr_TexGenxOES)(GET_by_offset((disp), _gloffset_TexGenxOES)))
#define SET_TexGenxOES(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLfixed) = func; \
   SET_by_offset(disp, _gloffset_TexGenxOES, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexGenxvOES)(GLenum, GLenum, const GLfixed *);
#define CALL_TexGenxvOES(disp, parameters) (* GET_TexGenxvOES(disp)) parameters
#define GET_TexGenxvOES(disp) ((_glptr_TexGenxvOES)(GET_by_offset((disp), _gloffset_TexGenxvOES)))
#define SET_TexGenxvOES(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, const GLfixed *) = func; \
   SET_by_offset(disp, _gloffset_TexGenxvOES, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DepthBoundsEXT)(GLclampd, GLclampd);
#define CALL_DepthBoundsEXT(disp, parameters) (* GET_DepthBoundsEXT(disp)) parameters
#define GET_DepthBoundsEXT(disp) ((_glptr_DepthBoundsEXT)(GET_by_offset((disp), _gloffset_DepthBoundsEXT)))
#define SET_DepthBoundsEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLclampd, GLclampd) = func; \
   SET_by_offset(disp, _gloffset_DepthBoundsEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_BindFramebufferEXT)(GLenum, GLuint);
#define CALL_BindFramebufferEXT(disp, parameters) (* GET_BindFramebufferEXT(disp)) parameters
#define GET_BindFramebufferEXT(disp) ((_glptr_BindFramebufferEXT)(GET_by_offset((disp), _gloffset_BindFramebufferEXT)))
#define SET_BindFramebufferEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint) = func; \
   SET_by_offset(disp, _gloffset_BindFramebufferEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_BindRenderbufferEXT)(GLenum, GLuint);
#define CALL_BindRenderbufferEXT(disp, parameters) (* GET_BindRenderbufferEXT(disp)) parameters
#define GET_BindRenderbufferEXT(disp) ((_glptr_BindRenderbufferEXT)(GET_by_offset((disp), _gloffset_BindRenderbufferEXT)))
#define SET_BindRenderbufferEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint) = func; \
   SET_by_offset(disp, _gloffset_BindRenderbufferEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_StringMarkerGREMEDY)(GLsizei, const GLvoid *);
#define CALL_StringMarkerGREMEDY(disp, parameters) (* GET_StringMarkerGREMEDY(disp)) parameters
#define GET_StringMarkerGREMEDY(disp) ((_glptr_StringMarkerGREMEDY)(GET_by_offset((disp), _gloffset_StringMarkerGREMEDY)))
#define SET_StringMarkerGREMEDY(disp, func) do { \
   void (GLAPIENTRYP fn)(GLsizei, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_StringMarkerGREMEDY, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_BufferParameteriAPPLE)(GLenum, GLenum, GLint);
#define CALL_BufferParameteriAPPLE(disp, parameters) (* GET_BufferParameteriAPPLE(disp)) parameters
#define GET_BufferParameteriAPPLE(disp) ((_glptr_BufferParameteriAPPLE)(GET_by_offset((disp), _gloffset_BufferParameteriAPPLE)))
#define SET_BufferParameteriAPPLE(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLint) = func; \
   SET_by_offset(disp, _gloffset_BufferParameteriAPPLE, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_FlushMappedBufferRangeAPPLE)(GLenum, GLintptr, GLsizeiptr);
#define CALL_FlushMappedBufferRangeAPPLE(disp, parameters) (* GET_FlushMappedBufferRangeAPPLE(disp)) parameters
#define GET_FlushMappedBufferRangeAPPLE(disp) ((_glptr_FlushMappedBufferRangeAPPLE)(GET_by_offset((disp), _gloffset_FlushMappedBufferRangeAPPLE)))
#define SET_FlushMappedBufferRangeAPPLE(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLintptr, GLsizeiptr) = func; \
   SET_by_offset(disp, _gloffset_FlushMappedBufferRangeAPPLE, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribI1iEXT)(GLuint, GLint);
#define CALL_VertexAttribI1iEXT(disp, parameters) (* GET_VertexAttribI1iEXT(disp)) parameters
#define GET_VertexAttribI1iEXT(disp) ((_glptr_VertexAttribI1iEXT)(GET_by_offset((disp), _gloffset_VertexAttribI1iEXT)))
#define SET_VertexAttribI1iEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribI1iEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribI1uiEXT)(GLuint, GLuint);
#define CALL_VertexAttribI1uiEXT(disp, parameters) (* GET_VertexAttribI1uiEXT(disp)) parameters
#define GET_VertexAttribI1uiEXT(disp) ((_glptr_VertexAttribI1uiEXT)(GET_by_offset((disp), _gloffset_VertexAttribI1uiEXT)))
#define SET_VertexAttribI1uiEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribI1uiEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribI2iEXT)(GLuint, GLint, GLint);
#define CALL_VertexAttribI2iEXT(disp, parameters) (* GET_VertexAttribI2iEXT(disp)) parameters
#define GET_VertexAttribI2iEXT(disp) ((_glptr_VertexAttribI2iEXT)(GET_by_offset((disp), _gloffset_VertexAttribI2iEXT)))
#define SET_VertexAttribI2iEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLint) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribI2iEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribI2ivEXT)(GLuint, const GLint *);
#define CALL_VertexAttribI2ivEXT(disp, parameters) (* GET_VertexAttribI2ivEXT(disp)) parameters
#define GET_VertexAttribI2ivEXT(disp) ((_glptr_VertexAttribI2ivEXT)(GET_by_offset((disp), _gloffset_VertexAttribI2ivEXT)))
#define SET_VertexAttribI2ivEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLint *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribI2ivEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribI2uiEXT)(GLuint, GLuint, GLuint);
#define CALL_VertexAttribI2uiEXT(disp, parameters) (* GET_VertexAttribI2uiEXT(disp)) parameters
#define GET_VertexAttribI2uiEXT(disp) ((_glptr_VertexAttribI2uiEXT)(GET_by_offset((disp), _gloffset_VertexAttribI2uiEXT)))
#define SET_VertexAttribI2uiEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLuint) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribI2uiEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribI2uivEXT)(GLuint, const GLuint *);
#define CALL_VertexAttribI2uivEXT(disp, parameters) (* GET_VertexAttribI2uivEXT(disp)) parameters
#define GET_VertexAttribI2uivEXT(disp) ((_glptr_VertexAttribI2uivEXT)(GET_by_offset((disp), _gloffset_VertexAttribI2uivEXT)))
#define SET_VertexAttribI2uivEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribI2uivEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribI3iEXT)(GLuint, GLint, GLint, GLint);
#define CALL_VertexAttribI3iEXT(disp, parameters) (* GET_VertexAttribI3iEXT(disp)) parameters
#define GET_VertexAttribI3iEXT(disp) ((_glptr_VertexAttribI3iEXT)(GET_by_offset((disp), _gloffset_VertexAttribI3iEXT)))
#define SET_VertexAttribI3iEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLint, GLint) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribI3iEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribI3ivEXT)(GLuint, const GLint *);
#define CALL_VertexAttribI3ivEXT(disp, parameters) (* GET_VertexAttribI3ivEXT(disp)) parameters
#define GET_VertexAttribI3ivEXT(disp) ((_glptr_VertexAttribI3ivEXT)(GET_by_offset((disp), _gloffset_VertexAttribI3ivEXT)))
#define SET_VertexAttribI3ivEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLint *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribI3ivEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribI3uiEXT)(GLuint, GLuint, GLuint, GLuint);
#define CALL_VertexAttribI3uiEXT(disp, parameters) (* GET_VertexAttribI3uiEXT(disp)) parameters
#define GET_VertexAttribI3uiEXT(disp) ((_glptr_VertexAttribI3uiEXT)(GET_by_offset((disp), _gloffset_VertexAttribI3uiEXT)))
#define SET_VertexAttribI3uiEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLuint, GLuint) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribI3uiEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribI3uivEXT)(GLuint, const GLuint *);
#define CALL_VertexAttribI3uivEXT(disp, parameters) (* GET_VertexAttribI3uivEXT(disp)) parameters
#define GET_VertexAttribI3uivEXT(disp) ((_glptr_VertexAttribI3uivEXT)(GET_by_offset((disp), _gloffset_VertexAttribI3uivEXT)))
#define SET_VertexAttribI3uivEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribI3uivEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribI4iEXT)(GLuint, GLint, GLint, GLint, GLint);
#define CALL_VertexAttribI4iEXT(disp, parameters) (* GET_VertexAttribI4iEXT(disp)) parameters
#define GET_VertexAttribI4iEXT(disp) ((_glptr_VertexAttribI4iEXT)(GET_by_offset((disp), _gloffset_VertexAttribI4iEXT)))
#define SET_VertexAttribI4iEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLint, GLint, GLint) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribI4iEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribI4ivEXT)(GLuint, const GLint *);
#define CALL_VertexAttribI4ivEXT(disp, parameters) (* GET_VertexAttribI4ivEXT(disp)) parameters
#define GET_VertexAttribI4ivEXT(disp) ((_glptr_VertexAttribI4ivEXT)(GET_by_offset((disp), _gloffset_VertexAttribI4ivEXT)))
#define SET_VertexAttribI4ivEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLint *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribI4ivEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribI4uiEXT)(GLuint, GLuint, GLuint, GLuint, GLuint);
#define CALL_VertexAttribI4uiEXT(disp, parameters) (* GET_VertexAttribI4uiEXT(disp)) parameters
#define GET_VertexAttribI4uiEXT(disp) ((_glptr_VertexAttribI4uiEXT)(GET_by_offset((disp), _gloffset_VertexAttribI4uiEXT)))
#define SET_VertexAttribI4uiEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLuint, GLuint, GLuint) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribI4uiEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribI4uivEXT)(GLuint, const GLuint *);
#define CALL_VertexAttribI4uivEXT(disp, parameters) (* GET_VertexAttribI4uivEXT(disp)) parameters
#define GET_VertexAttribI4uivEXT(disp) ((_glptr_VertexAttribI4uivEXT)(GET_by_offset((disp), _gloffset_VertexAttribI4uivEXT)))
#define SET_VertexAttribI4uivEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribI4uivEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ClearColorIiEXT)(GLint, GLint, GLint, GLint);
#define CALL_ClearColorIiEXT(disp, parameters) (* GET_ClearColorIiEXT(disp)) parameters
#define GET_ClearColorIiEXT(disp) ((_glptr_ClearColorIiEXT)(GET_by_offset((disp), _gloffset_ClearColorIiEXT)))
#define SET_ClearColorIiEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, GLint, GLint, GLint) = func; \
   SET_by_offset(disp, _gloffset_ClearColorIiEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ClearColorIuiEXT)(GLuint, GLuint, GLuint, GLuint);
#define CALL_ClearColorIuiEXT(disp, parameters) (* GET_ClearColorIuiEXT(disp)) parameters
#define GET_ClearColorIuiEXT(disp) ((_glptr_ClearColorIuiEXT)(GET_by_offset((disp), _gloffset_ClearColorIuiEXT)))
#define SET_ClearColorIuiEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLuint, GLuint) = func; \
   SET_by_offset(disp, _gloffset_ClearColorIuiEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_BindBufferOffsetEXT)(GLenum, GLuint, GLuint, GLintptr);
#define CALL_BindBufferOffsetEXT(disp, parameters) (* GET_BindBufferOffsetEXT(disp)) parameters
#define GET_BindBufferOffsetEXT(disp) ((_glptr_BindBufferOffsetEXT)(GET_by_offset((disp), _gloffset_BindBufferOffsetEXT)))
#define SET_BindBufferOffsetEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint, GLuint, GLintptr) = func; \
   SET_by_offset(disp, _gloffset_BindBufferOffsetEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_BeginPerfMonitorAMD)(GLuint);
#define CALL_BeginPerfMonitorAMD(disp, parameters) (* GET_BeginPerfMonitorAMD(disp)) parameters
#define GET_BeginPerfMonitorAMD(disp) ((_glptr_BeginPerfMonitorAMD)(GET_by_offset((disp), _gloffset_BeginPerfMonitorAMD)))
#define SET_BeginPerfMonitorAMD(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint) = func; \
   SET_by_offset(disp, _gloffset_BeginPerfMonitorAMD, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DeletePerfMonitorsAMD)(GLsizei, GLuint *);
#define CALL_DeletePerfMonitorsAMD(disp, parameters) (* GET_DeletePerfMonitorsAMD(disp)) parameters
#define GET_DeletePerfMonitorsAMD(disp) ((_glptr_DeletePerfMonitorsAMD)(GET_by_offset((disp), _gloffset_DeletePerfMonitorsAMD)))
#define SET_DeletePerfMonitorsAMD(disp, func) do { \
   void (GLAPIENTRYP fn)(GLsizei, GLuint *) = func; \
   SET_by_offset(disp, _gloffset_DeletePerfMonitorsAMD, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_EndPerfMonitorAMD)(GLuint);
#define CALL_EndPerfMonitorAMD(disp, parameters) (* GET_EndPerfMonitorAMD(disp)) parameters
#define GET_EndPerfMonitorAMD(disp) ((_glptr_EndPerfMonitorAMD)(GET_by_offset((disp), _gloffset_EndPerfMonitorAMD)))
#define SET_EndPerfMonitorAMD(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint) = func; \
   SET_by_offset(disp, _gloffset_EndPerfMonitorAMD, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GenPerfMonitorsAMD)(GLsizei, GLuint *);
#define CALL_GenPerfMonitorsAMD(disp, parameters) (* GET_GenPerfMonitorsAMD(disp)) parameters
#define GET_GenPerfMonitorsAMD(disp) ((_glptr_GenPerfMonitorsAMD)(GET_by_offset((disp), _gloffset_GenPerfMonitorsAMD)))
#define SET_GenPerfMonitorsAMD(disp, func) do { \
   void (GLAPIENTRYP fn)(GLsizei, GLuint *) = func; \
   SET_by_offset(disp, _gloffset_GenPerfMonitorsAMD, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetPerfMonitorCounterDataAMD)(GLuint, GLenum, GLsizei, GLuint *, GLint *);
#define CALL_GetPerfMonitorCounterDataAMD(disp, parameters) (* GET_GetPerfMonitorCounterDataAMD(disp)) parameters
#define GET_GetPerfMonitorCounterDataAMD(disp) ((_glptr_GetPerfMonitorCounterDataAMD)(GET_by_offset((disp), _gloffset_GetPerfMonitorCounterDataAMD)))
#define SET_GetPerfMonitorCounterDataAMD(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLsizei, GLuint *, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetPerfMonitorCounterDataAMD, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetPerfMonitorCounterInfoAMD)(GLuint, GLuint, GLenum, GLvoid *);
#define CALL_GetPerfMonitorCounterInfoAMD(disp, parameters) (* GET_GetPerfMonitorCounterInfoAMD(disp)) parameters
#define GET_GetPerfMonitorCounterInfoAMD(disp) ((_glptr_GetPerfMonitorCounterInfoAMD)(GET_by_offset((disp), _gloffset_GetPerfMonitorCounterInfoAMD)))
#define SET_GetPerfMonitorCounterInfoAMD(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLenum, GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_GetPerfMonitorCounterInfoAMD, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetPerfMonitorCounterStringAMD)(GLuint, GLuint, GLsizei, GLsizei *, GLchar *);
#define CALL_GetPerfMonitorCounterStringAMD(disp, parameters) (* GET_GetPerfMonitorCounterStringAMD(disp)) parameters
#define GET_GetPerfMonitorCounterStringAMD(disp) ((_glptr_GetPerfMonitorCounterStringAMD)(GET_by_offset((disp), _gloffset_GetPerfMonitorCounterStringAMD)))
#define SET_GetPerfMonitorCounterStringAMD(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLsizei, GLsizei *, GLchar *) = func; \
   SET_by_offset(disp, _gloffset_GetPerfMonitorCounterStringAMD, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetPerfMonitorCountersAMD)(GLuint, GLint *, GLint *, GLsizei, GLuint *);
#define CALL_GetPerfMonitorCountersAMD(disp, parameters) (* GET_GetPerfMonitorCountersAMD(disp)) parameters
#define GET_GetPerfMonitorCountersAMD(disp) ((_glptr_GetPerfMonitorCountersAMD)(GET_by_offset((disp), _gloffset_GetPerfMonitorCountersAMD)))
#define SET_GetPerfMonitorCountersAMD(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint *, GLint *, GLsizei, GLuint *) = func; \
   SET_by_offset(disp, _gloffset_GetPerfMonitorCountersAMD, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetPerfMonitorGroupStringAMD)(GLuint, GLsizei, GLsizei *, GLchar *);
#define CALL_GetPerfMonitorGroupStringAMD(disp, parameters) (* GET_GetPerfMonitorGroupStringAMD(disp)) parameters
#define GET_GetPerfMonitorGroupStringAMD(disp) ((_glptr_GetPerfMonitorGroupStringAMD)(GET_by_offset((disp), _gloffset_GetPerfMonitorGroupStringAMD)))
#define SET_GetPerfMonitorGroupStringAMD(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizei, GLsizei *, GLchar *) = func; \
   SET_by_offset(disp, _gloffset_GetPerfMonitorGroupStringAMD, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetPerfMonitorGroupsAMD)(GLint *, GLsizei, GLuint *);
#define CALL_GetPerfMonitorGroupsAMD(disp, parameters) (* GET_GetPerfMonitorGroupsAMD(disp)) parameters
#define GET_GetPerfMonitorGroupsAMD(disp) ((_glptr_GetPerfMonitorGroupsAMD)(GET_by_offset((disp), _gloffset_GetPerfMonitorGroupsAMD)))
#define SET_GetPerfMonitorGroupsAMD(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint *, GLsizei, GLuint *) = func; \
   SET_by_offset(disp, _gloffset_GetPerfMonitorGroupsAMD, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_SelectPerfMonitorCountersAMD)(GLuint, GLboolean, GLuint, GLint, GLuint *);
#define CALL_SelectPerfMonitorCountersAMD(disp, parameters) (* GET_SelectPerfMonitorCountersAMD(disp)) parameters
#define GET_SelectPerfMonitorCountersAMD(disp) ((_glptr_SelectPerfMonitorCountersAMD)(GET_by_offset((disp), _gloffset_SelectPerfMonitorCountersAMD)))
#define SET_SelectPerfMonitorCountersAMD(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLboolean, GLuint, GLint, GLuint *) = func; \
   SET_by_offset(disp, _gloffset_SelectPerfMonitorCountersAMD, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetObjectParameterivAPPLE)(GLenum, GLuint, GLenum, GLint *);
#define CALL_GetObjectParameterivAPPLE(disp, parameters) (* GET_GetObjectParameterivAPPLE(disp)) parameters
#define GET_GetObjectParameterivAPPLE(disp) ((_glptr_GetObjectParameterivAPPLE)(GET_by_offset((disp), _gloffset_GetObjectParameterivAPPLE)))
#define SET_GetObjectParameterivAPPLE(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint, GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetObjectParameterivAPPLE, fn); \
} while (0)

typedef GLenum (GLAPIENTRYP _glptr_ObjectPurgeableAPPLE)(GLenum, GLuint, GLenum);
#define CALL_ObjectPurgeableAPPLE(disp, parameters) (* GET_ObjectPurgeableAPPLE(disp)) parameters
#define GET_ObjectPurgeableAPPLE(disp) ((_glptr_ObjectPurgeableAPPLE)(GET_by_offset((disp), _gloffset_ObjectPurgeableAPPLE)))
#define SET_ObjectPurgeableAPPLE(disp, func) do { \
   GLenum (GLAPIENTRYP fn)(GLenum, GLuint, GLenum) = func; \
   SET_by_offset(disp, _gloffset_ObjectPurgeableAPPLE, fn); \
} while (0)

typedef GLenum (GLAPIENTRYP _glptr_ObjectUnpurgeableAPPLE)(GLenum, GLuint, GLenum);
#define CALL_ObjectUnpurgeableAPPLE(disp, parameters) (* GET_ObjectUnpurgeableAPPLE(disp)) parameters
#define GET_ObjectUnpurgeableAPPLE(disp) ((_glptr_ObjectUnpurgeableAPPLE)(GET_by_offset((disp), _gloffset_ObjectUnpurgeableAPPLE)))
#define SET_ObjectUnpurgeableAPPLE(disp, func) do { \
   GLenum (GLAPIENTRYP fn)(GLenum, GLuint, GLenum) = func; \
   SET_by_offset(disp, _gloffset_ObjectUnpurgeableAPPLE, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ActiveProgramEXT)(GLuint);
#define CALL_ActiveProgramEXT(disp, parameters) (* GET_ActiveProgramEXT(disp)) parameters
#define GET_ActiveProgramEXT(disp) ((_glptr_ActiveProgramEXT)(GET_by_offset((disp), _gloffset_ActiveProgramEXT)))
#define SET_ActiveProgramEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint) = func; \
   SET_by_offset(disp, _gloffset_ActiveProgramEXT, fn); \
} while (0)

typedef GLuint (GLAPIENTRYP _glptr_CreateShaderProgramEXT)(GLenum, const GLchar *);
#define CALL_CreateShaderProgramEXT(disp, parameters) (* GET_CreateShaderProgramEXT(disp)) parameters
#define GET_CreateShaderProgramEXT(disp) ((_glptr_CreateShaderProgramEXT)(GET_by_offset((disp), _gloffset_CreateShaderProgramEXT)))
#define SET_CreateShaderProgramEXT(disp, func) do { \
   GLuint (GLAPIENTRYP fn)(GLenum, const GLchar *) = func; \
   SET_by_offset(disp, _gloffset_CreateShaderProgramEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_UseShaderProgramEXT)(GLenum, GLuint);
#define CALL_UseShaderProgramEXT(disp, parameters) (* GET_UseShaderProgramEXT(disp)) parameters
#define GET_UseShaderProgramEXT(disp) ((_glptr_UseShaderProgramEXT)(GET_by_offset((disp), _gloffset_UseShaderProgramEXT)))
#define SET_UseShaderProgramEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint) = func; \
   SET_by_offset(disp, _gloffset_UseShaderProgramEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TextureBarrierNV)(void);
#define CALL_TextureBarrierNV(disp, parameters) (* GET_TextureBarrierNV(disp)) parameters
#define GET_TextureBarrierNV(disp) ((_glptr_TextureBarrierNV)(GET_by_offset((disp), _gloffset_TextureBarrierNV)))
#define SET_TextureBarrierNV(disp, func) do { \
   void (GLAPIENTRYP fn)(void) = func; \
   SET_by_offset(disp, _gloffset_TextureBarrierNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VDPAUFiniNV)(void);
#define CALL_VDPAUFiniNV(disp, parameters) (* GET_VDPAUFiniNV(disp)) parameters
#define GET_VDPAUFiniNV(disp) ((_glptr_VDPAUFiniNV)(GET_by_offset((disp), _gloffset_VDPAUFiniNV)))
#define SET_VDPAUFiniNV(disp, func) do { \
   void (GLAPIENTRYP fn)(void) = func; \
   SET_by_offset(disp, _gloffset_VDPAUFiniNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VDPAUGetSurfaceivNV)(GLintptr, GLenum, GLsizei, GLsizei *, GLint *);
#define CALL_VDPAUGetSurfaceivNV(disp, parameters) (* GET_VDPAUGetSurfaceivNV(disp)) parameters
#define GET_VDPAUGetSurfaceivNV(disp) ((_glptr_VDPAUGetSurfaceivNV)(GET_by_offset((disp), _gloffset_VDPAUGetSurfaceivNV)))
#define SET_VDPAUGetSurfaceivNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLintptr, GLenum, GLsizei, GLsizei *, GLint *) = func; \
   SET_by_offset(disp, _gloffset_VDPAUGetSurfaceivNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VDPAUInitNV)(const GLvoid *, const GLvoid *);
#define CALL_VDPAUInitNV(disp, parameters) (* GET_VDPAUInitNV(disp)) parameters
#define GET_VDPAUInitNV(disp) ((_glptr_VDPAUInitNV)(GET_by_offset((disp), _gloffset_VDPAUInitNV)))
#define SET_VDPAUInitNV(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLvoid *, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_VDPAUInitNV, fn); \
} while (0)

typedef GLboolean (GLAPIENTRYP _glptr_VDPAUIsSurfaceNV)(GLintptr);
#define CALL_VDPAUIsSurfaceNV(disp, parameters) (* GET_VDPAUIsSurfaceNV(disp)) parameters
#define GET_VDPAUIsSurfaceNV(disp) ((_glptr_VDPAUIsSurfaceNV)(GET_by_offset((disp), _gloffset_VDPAUIsSurfaceNV)))
#define SET_VDPAUIsSurfaceNV(disp, func) do { \
   GLboolean (GLAPIENTRYP fn)(GLintptr) = func; \
   SET_by_offset(disp, _gloffset_VDPAUIsSurfaceNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VDPAUMapSurfacesNV)(GLsizei, const GLintptr *);
#define CALL_VDPAUMapSurfacesNV(disp, parameters) (* GET_VDPAUMapSurfacesNV(disp)) parameters
#define GET_VDPAUMapSurfacesNV(disp) ((_glptr_VDPAUMapSurfacesNV)(GET_by_offset((disp), _gloffset_VDPAUMapSurfacesNV)))
#define SET_VDPAUMapSurfacesNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLsizei, const GLintptr *) = func; \
   SET_by_offset(disp, _gloffset_VDPAUMapSurfacesNV, fn); \
} while (0)

typedef GLintptr (GLAPIENTRYP _glptr_VDPAURegisterOutputSurfaceNV)(const GLvoid *, GLenum, GLsizei, const GLuint *);
#define CALL_VDPAURegisterOutputSurfaceNV(disp, parameters) (* GET_VDPAURegisterOutputSurfaceNV(disp)) parameters
#define GET_VDPAURegisterOutputSurfaceNV(disp) ((_glptr_VDPAURegisterOutputSurfaceNV)(GET_by_offset((disp), _gloffset_VDPAURegisterOutputSurfaceNV)))
#define SET_VDPAURegisterOutputSurfaceNV(disp, func) do { \
   GLintptr (GLAPIENTRYP fn)(const GLvoid *, GLenum, GLsizei, const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_VDPAURegisterOutputSurfaceNV, fn); \
} while (0)

typedef GLintptr (GLAPIENTRYP _glptr_VDPAURegisterVideoSurfaceNV)(const GLvoid *, GLenum, GLsizei, const GLuint *);
#define CALL_VDPAURegisterVideoSurfaceNV(disp, parameters) (* GET_VDPAURegisterVideoSurfaceNV(disp)) parameters
#define GET_VDPAURegisterVideoSurfaceNV(disp) ((_glptr_VDPAURegisterVideoSurfaceNV)(GET_by_offset((disp), _gloffset_VDPAURegisterVideoSurfaceNV)))
#define SET_VDPAURegisterVideoSurfaceNV(disp, func) do { \
   GLintptr (GLAPIENTRYP fn)(const GLvoid *, GLenum, GLsizei, const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_VDPAURegisterVideoSurfaceNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VDPAUSurfaceAccessNV)(GLintptr, GLenum);
#define CALL_VDPAUSurfaceAccessNV(disp, parameters) (* GET_VDPAUSurfaceAccessNV(disp)) parameters
#define GET_VDPAUSurfaceAccessNV(disp) ((_glptr_VDPAUSurfaceAccessNV)(GET_by_offset((disp), _gloffset_VDPAUSurfaceAccessNV)))
#define SET_VDPAUSurfaceAccessNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLintptr, GLenum) = func; \
   SET_by_offset(disp, _gloffset_VDPAUSurfaceAccessNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VDPAUUnmapSurfacesNV)(GLsizei, const GLintptr *);
#define CALL_VDPAUUnmapSurfacesNV(disp, parameters) (* GET_VDPAUUnmapSurfacesNV(disp)) parameters
#define GET_VDPAUUnmapSurfacesNV(disp) ((_glptr_VDPAUUnmapSurfacesNV)(GET_by_offset((disp), _gloffset_VDPAUUnmapSurfacesNV)))
#define SET_VDPAUUnmapSurfacesNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLsizei, const GLintptr *) = func; \
   SET_by_offset(disp, _gloffset_VDPAUUnmapSurfacesNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VDPAUUnregisterSurfaceNV)(GLintptr);
#define CALL_VDPAUUnregisterSurfaceNV(disp, parameters) (* GET_VDPAUUnregisterSurfaceNV(disp)) parameters
#define GET_VDPAUUnregisterSurfaceNV(disp) ((_glptr_VDPAUUnregisterSurfaceNV)(GET_by_offset((disp), _gloffset_VDPAUUnregisterSurfaceNV)))
#define SET_VDPAUUnregisterSurfaceNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLintptr) = func; \
   SET_by_offset(disp, _gloffset_VDPAUUnregisterSurfaceNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_BeginPerfQueryINTEL)(GLuint);
#define CALL_BeginPerfQueryINTEL(disp, parameters) (* GET_BeginPerfQueryINTEL(disp)) parameters
#define GET_BeginPerfQueryINTEL(disp) ((_glptr_BeginPerfQueryINTEL)(GET_by_offset((disp), _gloffset_BeginPerfQueryINTEL)))
#define SET_BeginPerfQueryINTEL(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint) = func; \
   SET_by_offset(disp, _gloffset_BeginPerfQueryINTEL, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CreatePerfQueryINTEL)(GLuint, GLuint *);
#define CALL_CreatePerfQueryINTEL(disp, parameters) (* GET_CreatePerfQueryINTEL(disp)) parameters
#define GET_CreatePerfQueryINTEL(disp) ((_glptr_CreatePerfQueryINTEL)(GET_by_offset((disp), _gloffset_CreatePerfQueryINTEL)))
#define SET_CreatePerfQueryINTEL(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint *) = func; \
   SET_by_offset(disp, _gloffset_CreatePerfQueryINTEL, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DeletePerfQueryINTEL)(GLuint);
#define CALL_DeletePerfQueryINTEL(disp, parameters) (* GET_DeletePerfQueryINTEL(disp)) parameters
#define GET_DeletePerfQueryINTEL(disp) ((_glptr_DeletePerfQueryINTEL)(GET_by_offset((disp), _gloffset_DeletePerfQueryINTEL)))
#define SET_DeletePerfQueryINTEL(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint) = func; \
   SET_by_offset(disp, _gloffset_DeletePerfQueryINTEL, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_EndPerfQueryINTEL)(GLuint);
#define CALL_EndPerfQueryINTEL(disp, parameters) (* GET_EndPerfQueryINTEL(disp)) parameters
#define GET_EndPerfQueryINTEL(disp) ((_glptr_EndPerfQueryINTEL)(GET_by_offset((disp), _gloffset_EndPerfQueryINTEL)))
#define SET_EndPerfQueryINTEL(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint) = func; \
   SET_by_offset(disp, _gloffset_EndPerfQueryINTEL, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetFirstPerfQueryIdINTEL)(GLuint *);
#define CALL_GetFirstPerfQueryIdINTEL(disp, parameters) (* GET_GetFirstPerfQueryIdINTEL(disp)) parameters
#define GET_GetFirstPerfQueryIdINTEL(disp) ((_glptr_GetFirstPerfQueryIdINTEL)(GET_by_offset((disp), _gloffset_GetFirstPerfQueryIdINTEL)))
#define SET_GetFirstPerfQueryIdINTEL(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint *) = func; \
   SET_by_offset(disp, _gloffset_GetFirstPerfQueryIdINTEL, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetNextPerfQueryIdINTEL)(GLuint, GLuint *);
#define CALL_GetNextPerfQueryIdINTEL(disp, parameters) (* GET_GetNextPerfQueryIdINTEL(disp)) parameters
#define GET_GetNextPerfQueryIdINTEL(disp) ((_glptr_GetNextPerfQueryIdINTEL)(GET_by_offset((disp), _gloffset_GetNextPerfQueryIdINTEL)))
#define SET_GetNextPerfQueryIdINTEL(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint *) = func; \
   SET_by_offset(disp, _gloffset_GetNextPerfQueryIdINTEL, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetPerfCounterInfoINTEL)(GLuint, GLuint, GLuint, GLchar *, GLuint, GLchar *, GLuint *, GLuint *, GLuint *, GLuint *, GLuint64 *);
#define CALL_GetPerfCounterInfoINTEL(disp, parameters) (* GET_GetPerfCounterInfoINTEL(disp)) parameters
#define GET_GetPerfCounterInfoINTEL(disp) ((_glptr_GetPerfCounterInfoINTEL)(GET_by_offset((disp), _gloffset_GetPerfCounterInfoINTEL)))
#define SET_GetPerfCounterInfoINTEL(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLuint, GLchar *, GLuint, GLchar *, GLuint *, GLuint *, GLuint *, GLuint *, GLuint64 *) = func; \
   SET_by_offset(disp, _gloffset_GetPerfCounterInfoINTEL, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetPerfQueryDataINTEL)(GLuint, GLuint, GLsizei, GLvoid *, GLuint *);
#define CALL_GetPerfQueryDataINTEL(disp, parameters) (* GET_GetPerfQueryDataINTEL(disp)) parameters
#define GET_GetPerfQueryDataINTEL(disp) ((_glptr_GetPerfQueryDataINTEL)(GET_by_offset((disp), _gloffset_GetPerfQueryDataINTEL)))
#define SET_GetPerfQueryDataINTEL(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLsizei, GLvoid *, GLuint *) = func; \
   SET_by_offset(disp, _gloffset_GetPerfQueryDataINTEL, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetPerfQueryIdByNameINTEL)(GLchar *, GLuint *);
#define CALL_GetPerfQueryIdByNameINTEL(disp, parameters) (* GET_GetPerfQueryIdByNameINTEL(disp)) parameters
#define GET_GetPerfQueryIdByNameINTEL(disp) ((_glptr_GetPerfQueryIdByNameINTEL)(GET_by_offset((disp), _gloffset_GetPerfQueryIdByNameINTEL)))
#define SET_GetPerfQueryIdByNameINTEL(disp, func) do { \
   void (GLAPIENTRYP fn)(GLchar *, GLuint *) = func; \
   SET_by_offset(disp, _gloffset_GetPerfQueryIdByNameINTEL, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetPerfQueryInfoINTEL)(GLuint, GLuint, GLchar *, GLuint *, GLuint *, GLuint *, GLuint *);
#define CALL_GetPerfQueryInfoINTEL(disp, parameters) (* GET_GetPerfQueryInfoINTEL(disp)) parameters
#define GET_GetPerfQueryInfoINTEL(disp) ((_glptr_GetPerfQueryInfoINTEL)(GET_by_offset((disp), _gloffset_GetPerfQueryInfoINTEL)))
#define SET_GetPerfQueryInfoINTEL(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLchar *, GLuint *, GLuint *, GLuint *, GLuint *) = func; \
   SET_by_offset(disp, _gloffset_GetPerfQueryInfoINTEL, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_PolygonOffsetClampEXT)(GLfloat, GLfloat, GLfloat);
#define CALL_PolygonOffsetClampEXT(disp, parameters) (* GET_PolygonOffsetClampEXT(disp)) parameters
#define GET_PolygonOffsetClampEXT(disp) ((_glptr_PolygonOffsetClampEXT)(GET_by_offset((disp), _gloffset_PolygonOffsetClampEXT)))
#define SET_PolygonOffsetClampEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLfloat, GLfloat, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_PolygonOffsetClampEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_SubpixelPrecisionBiasNV)(GLuint, GLuint);
#define CALL_SubpixelPrecisionBiasNV(disp, parameters) (* GET_SubpixelPrecisionBiasNV(disp)) parameters
#define GET_SubpixelPrecisionBiasNV(disp) ((_glptr_SubpixelPrecisionBiasNV)(GET_by_offset((disp), _gloffset_SubpixelPrecisionBiasNV)))
#define SET_SubpixelPrecisionBiasNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint) = func; \
   SET_by_offset(disp, _gloffset_SubpixelPrecisionBiasNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ConservativeRasterParameterfNV)(GLenum, GLfloat);
#define CALL_ConservativeRasterParameterfNV(disp, parameters) (* GET_ConservativeRasterParameterfNV(disp)) parameters
#define GET_ConservativeRasterParameterfNV(disp) ((_glptr_ConservativeRasterParameterfNV)(GET_by_offset((disp), _gloffset_ConservativeRasterParameterfNV)))
#define SET_ConservativeRasterParameterfNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_ConservativeRasterParameterfNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ConservativeRasterParameteriNV)(GLenum, GLint);
#define CALL_ConservativeRasterParameteriNV(disp, parameters) (* GET_ConservativeRasterParameteriNV(disp)) parameters
#define GET_ConservativeRasterParameteriNV(disp) ((_glptr_ConservativeRasterParameteriNV)(GET_by_offset((disp), _gloffset_ConservativeRasterParameteriNV)))
#define SET_ConservativeRasterParameteriNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLint) = func; \
   SET_by_offset(disp, _gloffset_ConservativeRasterParameteriNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_WindowRectanglesEXT)(GLenum, GLsizei, const GLint *);
#define CALL_WindowRectanglesEXT(disp, parameters) (* GET_WindowRectanglesEXT(disp)) parameters
#define GET_WindowRectanglesEXT(disp) ((_glptr_WindowRectanglesEXT)(GET_by_offset((disp), _gloffset_WindowRectanglesEXT)))
#define SET_WindowRectanglesEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLsizei, const GLint *) = func; \
   SET_by_offset(disp, _gloffset_WindowRectanglesEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_BufferStorageMemEXT)(GLenum, GLsizeiptr, GLuint, GLuint64);
#define CALL_BufferStorageMemEXT(disp, parameters) (* GET_BufferStorageMemEXT(disp)) parameters
#define GET_BufferStorageMemEXT(disp) ((_glptr_BufferStorageMemEXT)(GET_by_offset((disp), _gloffset_BufferStorageMemEXT)))
#define SET_BufferStorageMemEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLsizeiptr, GLuint, GLuint64) = func; \
   SET_by_offset(disp, _gloffset_BufferStorageMemEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CreateMemoryObjectsEXT)(GLsizei, GLuint *);
#define CALL_CreateMemoryObjectsEXT(disp, parameters) (* GET_CreateMemoryObjectsEXT(disp)) parameters
#define GET_CreateMemoryObjectsEXT(disp) ((_glptr_CreateMemoryObjectsEXT)(GET_by_offset((disp), _gloffset_CreateMemoryObjectsEXT)))
#define SET_CreateMemoryObjectsEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLsizei, GLuint *) = func; \
   SET_by_offset(disp, _gloffset_CreateMemoryObjectsEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DeleteMemoryObjectsEXT)(GLsizei, const GLuint *);
#define CALL_DeleteMemoryObjectsEXT(disp, parameters) (* GET_DeleteMemoryObjectsEXT(disp)) parameters
#define GET_DeleteMemoryObjectsEXT(disp) ((_glptr_DeleteMemoryObjectsEXT)(GET_by_offset((disp), _gloffset_DeleteMemoryObjectsEXT)))
#define SET_DeleteMemoryObjectsEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLsizei, const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_DeleteMemoryObjectsEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DeleteSemaphoresEXT)(GLsizei, const GLuint *);
#define CALL_DeleteSemaphoresEXT(disp, parameters) (* GET_DeleteSemaphoresEXT(disp)) parameters
#define GET_DeleteSemaphoresEXT(disp) ((_glptr_DeleteSemaphoresEXT)(GET_by_offset((disp), _gloffset_DeleteSemaphoresEXT)))
#define SET_DeleteSemaphoresEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLsizei, const GLuint *) = func; \
   SET_by_offset(disp, _gloffset_DeleteSemaphoresEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GenSemaphoresEXT)(GLsizei, GLuint *);
#define CALL_GenSemaphoresEXT(disp, parameters) (* GET_GenSemaphoresEXT(disp)) parameters
#define GET_GenSemaphoresEXT(disp) ((_glptr_GenSemaphoresEXT)(GET_by_offset((disp), _gloffset_GenSemaphoresEXT)))
#define SET_GenSemaphoresEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLsizei, GLuint *) = func; \
   SET_by_offset(disp, _gloffset_GenSemaphoresEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetMemoryObjectParameterivEXT)(GLuint, GLenum, GLint *);
#define CALL_GetMemoryObjectParameterivEXT(disp, parameters) (* GET_GetMemoryObjectParameterivEXT(disp)) parameters
#define GET_GetMemoryObjectParameterivEXT(disp) ((_glptr_GetMemoryObjectParameterivEXT)(GET_by_offset((disp), _gloffset_GetMemoryObjectParameterivEXT)))
#define SET_GetMemoryObjectParameterivEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetMemoryObjectParameterivEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetSemaphoreParameterui64vEXT)(GLuint, GLenum, GLuint64 *);
#define CALL_GetSemaphoreParameterui64vEXT(disp, parameters) (* GET_GetSemaphoreParameterui64vEXT(disp)) parameters
#define GET_GetSemaphoreParameterui64vEXT(disp) ((_glptr_GetSemaphoreParameterui64vEXT)(GET_by_offset((disp), _gloffset_GetSemaphoreParameterui64vEXT)))
#define SET_GetSemaphoreParameterui64vEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLuint64 *) = func; \
   SET_by_offset(disp, _gloffset_GetSemaphoreParameterui64vEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetUnsignedBytei_vEXT)(GLenum, GLuint, GLubyte *);
#define CALL_GetUnsignedBytei_vEXT(disp, parameters) (* GET_GetUnsignedBytei_vEXT(disp)) parameters
#define GET_GetUnsignedBytei_vEXT(disp) ((_glptr_GetUnsignedBytei_vEXT)(GET_by_offset((disp), _gloffset_GetUnsignedBytei_vEXT)))
#define SET_GetUnsignedBytei_vEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint, GLubyte *) = func; \
   SET_by_offset(disp, _gloffset_GetUnsignedBytei_vEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetUnsignedBytevEXT)(GLenum, GLubyte *);
#define CALL_GetUnsignedBytevEXT(disp, parameters) (* GET_GetUnsignedBytevEXT(disp)) parameters
#define GET_GetUnsignedBytevEXT(disp) ((_glptr_GetUnsignedBytevEXT)(GET_by_offset((disp), _gloffset_GetUnsignedBytevEXT)))
#define SET_GetUnsignedBytevEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLubyte *) = func; \
   SET_by_offset(disp, _gloffset_GetUnsignedBytevEXT, fn); \
} while (0)

typedef GLboolean (GLAPIENTRYP _glptr_IsMemoryObjectEXT)(GLuint);
#define CALL_IsMemoryObjectEXT(disp, parameters) (* GET_IsMemoryObjectEXT(disp)) parameters
#define GET_IsMemoryObjectEXT(disp) ((_glptr_IsMemoryObjectEXT)(GET_by_offset((disp), _gloffset_IsMemoryObjectEXT)))
#define SET_IsMemoryObjectEXT(disp, func) do { \
   GLboolean (GLAPIENTRYP fn)(GLuint) = func; \
   SET_by_offset(disp, _gloffset_IsMemoryObjectEXT, fn); \
} while (0)

typedef GLboolean (GLAPIENTRYP _glptr_IsSemaphoreEXT)(GLuint);
#define CALL_IsSemaphoreEXT(disp, parameters) (* GET_IsSemaphoreEXT(disp)) parameters
#define GET_IsSemaphoreEXT(disp) ((_glptr_IsSemaphoreEXT)(GET_by_offset((disp), _gloffset_IsSemaphoreEXT)))
#define SET_IsSemaphoreEXT(disp, func) do { \
   GLboolean (GLAPIENTRYP fn)(GLuint) = func; \
   SET_by_offset(disp, _gloffset_IsSemaphoreEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MemoryObjectParameterivEXT)(GLuint, GLenum, const GLint *);
#define CALL_MemoryObjectParameterivEXT(disp, parameters) (* GET_MemoryObjectParameterivEXT(disp)) parameters
#define GET_MemoryObjectParameterivEXT(disp) ((_glptr_MemoryObjectParameterivEXT)(GET_by_offset((disp), _gloffset_MemoryObjectParameterivEXT)))
#define SET_MemoryObjectParameterivEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, const GLint *) = func; \
   SET_by_offset(disp, _gloffset_MemoryObjectParameterivEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_NamedBufferStorageMemEXT)(GLuint, GLsizeiptr, GLuint, GLuint64);
#define CALL_NamedBufferStorageMemEXT(disp, parameters) (* GET_NamedBufferStorageMemEXT(disp)) parameters
#define GET_NamedBufferStorageMemEXT(disp) ((_glptr_NamedBufferStorageMemEXT)(GET_by_offset((disp), _gloffset_NamedBufferStorageMemEXT)))
#define SET_NamedBufferStorageMemEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizeiptr, GLuint, GLuint64) = func; \
   SET_by_offset(disp, _gloffset_NamedBufferStorageMemEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_SemaphoreParameterui64vEXT)(GLuint, GLenum, const GLuint64 *);
#define CALL_SemaphoreParameterui64vEXT(disp, parameters) (* GET_SemaphoreParameterui64vEXT(disp)) parameters
#define GET_SemaphoreParameterui64vEXT(disp) ((_glptr_SemaphoreParameterui64vEXT)(GET_by_offset((disp), _gloffset_SemaphoreParameterui64vEXT)))
#define SET_SemaphoreParameterui64vEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, const GLuint64 *) = func; \
   SET_by_offset(disp, _gloffset_SemaphoreParameterui64vEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_SignalSemaphoreEXT)(GLuint, GLuint, const GLuint *, GLuint, const GLuint *, const GLenum *);
#define CALL_SignalSemaphoreEXT(disp, parameters) (* GET_SignalSemaphoreEXT(disp)) parameters
#define GET_SignalSemaphoreEXT(disp) ((_glptr_SignalSemaphoreEXT)(GET_by_offset((disp), _gloffset_SignalSemaphoreEXT)))
#define SET_SignalSemaphoreEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, const GLuint *, GLuint, const GLuint *, const GLenum *) = func; \
   SET_by_offset(disp, _gloffset_SignalSemaphoreEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexStorageMem1DEXT)(GLenum, GLsizei, GLenum, GLsizei, GLuint, GLuint64);
#define CALL_TexStorageMem1DEXT(disp, parameters) (* GET_TexStorageMem1DEXT(disp)) parameters
#define GET_TexStorageMem1DEXT(disp) ((_glptr_TexStorageMem1DEXT)(GET_by_offset((disp), _gloffset_TexStorageMem1DEXT)))
#define SET_TexStorageMem1DEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLsizei, GLenum, GLsizei, GLuint, GLuint64) = func; \
   SET_by_offset(disp, _gloffset_TexStorageMem1DEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexStorageMem2DEXT)(GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLuint, GLuint64);
#define CALL_TexStorageMem2DEXT(disp, parameters) (* GET_TexStorageMem2DEXT(disp)) parameters
#define GET_TexStorageMem2DEXT(disp) ((_glptr_TexStorageMem2DEXT)(GET_by_offset((disp), _gloffset_TexStorageMem2DEXT)))
#define SET_TexStorageMem2DEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLuint, GLuint64) = func; \
   SET_by_offset(disp, _gloffset_TexStorageMem2DEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexStorageMem2DMultisampleEXT)(GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLboolean, GLuint, GLuint64);
#define CALL_TexStorageMem2DMultisampleEXT(disp, parameters) (* GET_TexStorageMem2DMultisampleEXT(disp)) parameters
#define GET_TexStorageMem2DMultisampleEXT(disp) ((_glptr_TexStorageMem2DMultisampleEXT)(GET_by_offset((disp), _gloffset_TexStorageMem2DMultisampleEXT)))
#define SET_TexStorageMem2DMultisampleEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLboolean, GLuint, GLuint64) = func; \
   SET_by_offset(disp, _gloffset_TexStorageMem2DMultisampleEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexStorageMem3DEXT)(GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLsizei, GLuint, GLuint64);
#define CALL_TexStorageMem3DEXT(disp, parameters) (* GET_TexStorageMem3DEXT(disp)) parameters
#define GET_TexStorageMem3DEXT(disp) ((_glptr_TexStorageMem3DEXT)(GET_by_offset((disp), _gloffset_TexStorageMem3DEXT)))
#define SET_TexStorageMem3DEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLsizei, GLuint, GLuint64) = func; \
   SET_by_offset(disp, _gloffset_TexStorageMem3DEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexStorageMem3DMultisampleEXT)(GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLsizei, GLboolean, GLuint, GLuint64);
#define CALL_TexStorageMem3DMultisampleEXT(disp, parameters) (* GET_TexStorageMem3DMultisampleEXT(disp)) parameters
#define GET_TexStorageMem3DMultisampleEXT(disp) ((_glptr_TexStorageMem3DMultisampleEXT)(GET_by_offset((disp), _gloffset_TexStorageMem3DMultisampleEXT)))
#define SET_TexStorageMem3DMultisampleEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLsizei, GLboolean, GLuint, GLuint64) = func; \
   SET_by_offset(disp, _gloffset_TexStorageMem3DMultisampleEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TextureStorageMem1DEXT)(GLuint, GLsizei, GLenum, GLsizei, GLuint, GLuint64);
#define CALL_TextureStorageMem1DEXT(disp, parameters) (* GET_TextureStorageMem1DEXT(disp)) parameters
#define GET_TextureStorageMem1DEXT(disp) ((_glptr_TextureStorageMem1DEXT)(GET_by_offset((disp), _gloffset_TextureStorageMem1DEXT)))
#define SET_TextureStorageMem1DEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizei, GLenum, GLsizei, GLuint, GLuint64) = func; \
   SET_by_offset(disp, _gloffset_TextureStorageMem1DEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TextureStorageMem2DEXT)(GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLuint, GLuint64);
#define CALL_TextureStorageMem2DEXT(disp, parameters) (* GET_TextureStorageMem2DEXT(disp)) parameters
#define GET_TextureStorageMem2DEXT(disp) ((_glptr_TextureStorageMem2DEXT)(GET_by_offset((disp), _gloffset_TextureStorageMem2DEXT)))
#define SET_TextureStorageMem2DEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLuint, GLuint64) = func; \
   SET_by_offset(disp, _gloffset_TextureStorageMem2DEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TextureStorageMem2DMultisampleEXT)(GLuint, GLsizei, GLenum, GLsizei, GLsizei, GLboolean, GLuint, GLuint64);
#define CALL_TextureStorageMem2DMultisampleEXT(disp, parameters) (* GET_TextureStorageMem2DMultisampleEXT(disp)) parameters
#define GET_TextureStorageMem2DMultisampleEXT(disp) ((_glptr_TextureStorageMem2DMultisampleEXT)(GET_by_offset((disp), _gloffset_TextureStorageMem2DMultisampleEXT)))
#define SET_TextureStorageMem2DMultisampleEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizei, GLenum, GLsizei, GLsizei, GLboolean, GLuint, GLuint64) = func; \
   SET_by_offset(disp, _gloffset_TextureStorageMem2DMultisampleEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TextureStorageMem3DEXT)(GLuint, GLsizei, GLenum, GLsizei, GLsizei, GLsizei, GLuint, GLuint64);
#define CALL_TextureStorageMem3DEXT(disp, parameters) (* GET_TextureStorageMem3DEXT(disp)) parameters
#define GET_TextureStorageMem3DEXT(disp) ((_glptr_TextureStorageMem3DEXT)(GET_by_offset((disp), _gloffset_TextureStorageMem3DEXT)))
#define SET_TextureStorageMem3DEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizei, GLenum, GLsizei, GLsizei, GLsizei, GLuint, GLuint64) = func; \
   SET_by_offset(disp, _gloffset_TextureStorageMem3DEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TextureStorageMem3DMultisampleEXT)(GLuint, GLsizei, GLenum, GLsizei, GLsizei, GLsizei, GLboolean, GLuint, GLuint64);
#define CALL_TextureStorageMem3DMultisampleEXT(disp, parameters) (* GET_TextureStorageMem3DMultisampleEXT(disp)) parameters
#define GET_TextureStorageMem3DMultisampleEXT(disp) ((_glptr_TextureStorageMem3DMultisampleEXT)(GET_by_offset((disp), _gloffset_TextureStorageMem3DMultisampleEXT)))
#define SET_TextureStorageMem3DMultisampleEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizei, GLenum, GLsizei, GLsizei, GLsizei, GLboolean, GLuint, GLuint64) = func; \
   SET_by_offset(disp, _gloffset_TextureStorageMem3DMultisampleEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_WaitSemaphoreEXT)(GLuint, GLuint, const GLuint *, GLuint, const GLuint *, const GLenum *);
#define CALL_WaitSemaphoreEXT(disp, parameters) (* GET_WaitSemaphoreEXT(disp)) parameters
#define GET_WaitSemaphoreEXT(disp) ((_glptr_WaitSemaphoreEXT)(GET_by_offset((disp), _gloffset_WaitSemaphoreEXT)))
#define SET_WaitSemaphoreEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, const GLuint *, GLuint, const GLuint *, const GLenum *) = func; \
   SET_by_offset(disp, _gloffset_WaitSemaphoreEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ImportMemoryFdEXT)(GLuint, GLuint64, GLenum, GLint);
#define CALL_ImportMemoryFdEXT(disp, parameters) (* GET_ImportMemoryFdEXT(disp)) parameters
#define GET_ImportMemoryFdEXT(disp) ((_glptr_ImportMemoryFdEXT)(GET_by_offset((disp), _gloffset_ImportMemoryFdEXT)))
#define SET_ImportMemoryFdEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint64, GLenum, GLint) = func; \
   SET_by_offset(disp, _gloffset_ImportMemoryFdEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ImportSemaphoreFdEXT)(GLuint, GLenum, GLint);
#define CALL_ImportSemaphoreFdEXT(disp, parameters) (* GET_ImportSemaphoreFdEXT(disp)) parameters
#define GET_ImportSemaphoreFdEXT(disp) ((_glptr_ImportSemaphoreFdEXT)(GET_by_offset((disp), _gloffset_ImportSemaphoreFdEXT)))
#define SET_ImportSemaphoreFdEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLint) = func; \
   SET_by_offset(disp, _gloffset_ImportSemaphoreFdEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_FramebufferFetchBarrierEXT)(void);
#define CALL_FramebufferFetchBarrierEXT(disp, parameters) (* GET_FramebufferFetchBarrierEXT(disp)) parameters
#define GET_FramebufferFetchBarrierEXT(disp) ((_glptr_FramebufferFetchBarrierEXT)(GET_by_offset((disp), _gloffset_FramebufferFetchBarrierEXT)))
#define SET_FramebufferFetchBarrierEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(void) = func; \
   SET_by_offset(disp, _gloffset_FramebufferFetchBarrierEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_NamedRenderbufferStorageMultisampleAdvancedAMD)(GLuint, GLsizei, GLsizei, GLenum, GLsizei, GLsizei);
#define CALL_NamedRenderbufferStorageMultisampleAdvancedAMD(disp, parameters) (* GET_NamedRenderbufferStorageMultisampleAdvancedAMD(disp)) parameters
#define GET_NamedRenderbufferStorageMultisampleAdvancedAMD(disp) ((_glptr_NamedRenderbufferStorageMultisampleAdvancedAMD)(GET_by_offset((disp), _gloffset_NamedRenderbufferStorageMultisampleAdvancedAMD)))
#define SET_NamedRenderbufferStorageMultisampleAdvancedAMD(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizei, GLsizei, GLenum, GLsizei, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_NamedRenderbufferStorageMultisampleAdvancedAMD, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_RenderbufferStorageMultisampleAdvancedAMD)(GLenum, GLsizei, GLsizei, GLenum, GLsizei, GLsizei);
#define CALL_RenderbufferStorageMultisampleAdvancedAMD(disp, parameters) (* GET_RenderbufferStorageMultisampleAdvancedAMD(disp)) parameters
#define GET_RenderbufferStorageMultisampleAdvancedAMD(disp) ((_glptr_RenderbufferStorageMultisampleAdvancedAMD)(GET_by_offset((disp), _gloffset_RenderbufferStorageMultisampleAdvancedAMD)))
#define SET_RenderbufferStorageMultisampleAdvancedAMD(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLsizei, GLsizei, GLenum, GLsizei, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_RenderbufferStorageMultisampleAdvancedAMD, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_StencilFuncSeparateATI)(GLenum, GLenum, GLint, GLuint);
#define CALL_StencilFuncSeparateATI(disp, parameters) (* GET_StencilFuncSeparateATI(disp)) parameters
#define GET_StencilFuncSeparateATI(disp) ((_glptr_StencilFuncSeparateATI)(GET_by_offset((disp), _gloffset_StencilFuncSeparateATI)))
#define SET_StencilFuncSeparateATI(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLint, GLuint) = func; \
   SET_by_offset(disp, _gloffset_StencilFuncSeparateATI, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramEnvParameters4fvEXT)(GLenum, GLuint, GLsizei, const GLfloat *);
#define CALL_ProgramEnvParameters4fvEXT(disp, parameters) (* GET_ProgramEnvParameters4fvEXT(disp)) parameters
#define GET_ProgramEnvParameters4fvEXT(disp) ((_glptr_ProgramEnvParameters4fvEXT)(GET_by_offset((disp), _gloffset_ProgramEnvParameters4fvEXT)))
#define SET_ProgramEnvParameters4fvEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint, GLsizei, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_ProgramEnvParameters4fvEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ProgramLocalParameters4fvEXT)(GLenum, GLuint, GLsizei, const GLfloat *);
#define CALL_ProgramLocalParameters4fvEXT(disp, parameters) (* GET_ProgramLocalParameters4fvEXT(disp)) parameters
#define GET_ProgramLocalParameters4fvEXT(disp) ((_glptr_ProgramLocalParameters4fvEXT)(GET_by_offset((disp), _gloffset_ProgramLocalParameters4fvEXT)))
#define SET_ProgramLocalParameters4fvEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint, GLsizei, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_ProgramLocalParameters4fvEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_EGLImageTargetRenderbufferStorageOES)(GLenum, GLvoid *);
#define CALL_EGLImageTargetRenderbufferStorageOES(disp, parameters) (* GET_EGLImageTargetRenderbufferStorageOES(disp)) parameters
#define GET_EGLImageTargetRenderbufferStorageOES(disp) ((_glptr_EGLImageTargetRenderbufferStorageOES)(GET_by_offset((disp), _gloffset_EGLImageTargetRenderbufferStorageOES)))
#define SET_EGLImageTargetRenderbufferStorageOES(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_EGLImageTargetRenderbufferStorageOES, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_EGLImageTargetTexture2DOES)(GLenum, GLvoid *);
#define CALL_EGLImageTargetTexture2DOES(disp, parameters) (* GET_EGLImageTargetTexture2DOES(disp)) parameters
#define GET_EGLImageTargetTexture2DOES(disp) ((_glptr_EGLImageTargetTexture2DOES)(GET_by_offset((disp), _gloffset_EGLImageTargetTexture2DOES)))
#define SET_EGLImageTargetTexture2DOES(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_EGLImageTargetTexture2DOES, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_AlphaFuncx)(GLenum, GLclampx);
#define CALL_AlphaFuncx(disp, parameters) (* GET_AlphaFuncx(disp)) parameters
#define GET_AlphaFuncx(disp) ((_glptr_AlphaFuncx)(GET_by_offset((disp), _gloffset_AlphaFuncx)))
#define SET_AlphaFuncx(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLclampx) = func; \
   SET_by_offset(disp, _gloffset_AlphaFuncx, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ClearColorx)(GLclampx, GLclampx, GLclampx, GLclampx);
#define CALL_ClearColorx(disp, parameters) (* GET_ClearColorx(disp)) parameters
#define GET_ClearColorx(disp) ((_glptr_ClearColorx)(GET_by_offset((disp), _gloffset_ClearColorx)))
#define SET_ClearColorx(disp, func) do { \
   void (GLAPIENTRYP fn)(GLclampx, GLclampx, GLclampx, GLclampx) = func; \
   SET_by_offset(disp, _gloffset_ClearColorx, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ClearDepthx)(GLclampx);
#define CALL_ClearDepthx(disp, parameters) (* GET_ClearDepthx(disp)) parameters
#define GET_ClearDepthx(disp) ((_glptr_ClearDepthx)(GET_by_offset((disp), _gloffset_ClearDepthx)))
#define SET_ClearDepthx(disp, func) do { \
   void (GLAPIENTRYP fn)(GLclampx) = func; \
   SET_by_offset(disp, _gloffset_ClearDepthx, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Color4x)(GLfixed, GLfixed, GLfixed, GLfixed);
#define CALL_Color4x(disp, parameters) (* GET_Color4x(disp)) parameters
#define GET_Color4x(disp) ((_glptr_Color4x)(GET_by_offset((disp), _gloffset_Color4x)))
#define SET_Color4x(disp, func) do { \
   void (GLAPIENTRYP fn)(GLfixed, GLfixed, GLfixed, GLfixed) = func; \
   SET_by_offset(disp, _gloffset_Color4x, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DepthRangex)(GLclampx, GLclampx);
#define CALL_DepthRangex(disp, parameters) (* GET_DepthRangex(disp)) parameters
#define GET_DepthRangex(disp) ((_glptr_DepthRangex)(GET_by_offset((disp), _gloffset_DepthRangex)))
#define SET_DepthRangex(disp, func) do { \
   void (GLAPIENTRYP fn)(GLclampx, GLclampx) = func; \
   SET_by_offset(disp, _gloffset_DepthRangex, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Fogx)(GLenum, GLfixed);
#define CALL_Fogx(disp, parameters) (* GET_Fogx(disp)) parameters
#define GET_Fogx(disp) ((_glptr_Fogx)(GET_by_offset((disp), _gloffset_Fogx)))
#define SET_Fogx(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLfixed) = func; \
   SET_by_offset(disp, _gloffset_Fogx, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Fogxv)(GLenum, const GLfixed *);
#define CALL_Fogxv(disp, parameters) (* GET_Fogxv(disp)) parameters
#define GET_Fogxv(disp) ((_glptr_Fogxv)(GET_by_offset((disp), _gloffset_Fogxv)))
#define SET_Fogxv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, const GLfixed *) = func; \
   SET_by_offset(disp, _gloffset_Fogxv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Frustumf)(GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat);
#define CALL_Frustumf(disp, parameters) (* GET_Frustumf(disp)) parameters
#define GET_Frustumf(disp) ((_glptr_Frustumf)(GET_by_offset((disp), _gloffset_Frustumf)))
#define SET_Frustumf(disp, func) do { \
   void (GLAPIENTRYP fn)(GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_Frustumf, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Frustumx)(GLfixed, GLfixed, GLfixed, GLfixed, GLfixed, GLfixed);
#define CALL_Frustumx(disp, parameters) (* GET_Frustumx(disp)) parameters
#define GET_Frustumx(disp) ((_glptr_Frustumx)(GET_by_offset((disp), _gloffset_Frustumx)))
#define SET_Frustumx(disp, func) do { \
   void (GLAPIENTRYP fn)(GLfixed, GLfixed, GLfixed, GLfixed, GLfixed, GLfixed) = func; \
   SET_by_offset(disp, _gloffset_Frustumx, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_LightModelx)(GLenum, GLfixed);
#define CALL_LightModelx(disp, parameters) (* GET_LightModelx(disp)) parameters
#define GET_LightModelx(disp) ((_glptr_LightModelx)(GET_by_offset((disp), _gloffset_LightModelx)))
#define SET_LightModelx(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLfixed) = func; \
   SET_by_offset(disp, _gloffset_LightModelx, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_LightModelxv)(GLenum, const GLfixed *);
#define CALL_LightModelxv(disp, parameters) (* GET_LightModelxv(disp)) parameters
#define GET_LightModelxv(disp) ((_glptr_LightModelxv)(GET_by_offset((disp), _gloffset_LightModelxv)))
#define SET_LightModelxv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, const GLfixed *) = func; \
   SET_by_offset(disp, _gloffset_LightModelxv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Lightx)(GLenum, GLenum, GLfixed);
#define CALL_Lightx(disp, parameters) (* GET_Lightx(disp)) parameters
#define GET_Lightx(disp) ((_glptr_Lightx)(GET_by_offset((disp), _gloffset_Lightx)))
#define SET_Lightx(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLfixed) = func; \
   SET_by_offset(disp, _gloffset_Lightx, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Lightxv)(GLenum, GLenum, const GLfixed *);
#define CALL_Lightxv(disp, parameters) (* GET_Lightxv(disp)) parameters
#define GET_Lightxv(disp) ((_glptr_Lightxv)(GET_by_offset((disp), _gloffset_Lightxv)))
#define SET_Lightxv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, const GLfixed *) = func; \
   SET_by_offset(disp, _gloffset_Lightxv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_LineWidthx)(GLfixed);
#define CALL_LineWidthx(disp, parameters) (* GET_LineWidthx(disp)) parameters
#define GET_LineWidthx(disp) ((_glptr_LineWidthx)(GET_by_offset((disp), _gloffset_LineWidthx)))
#define SET_LineWidthx(disp, func) do { \
   void (GLAPIENTRYP fn)(GLfixed) = func; \
   SET_by_offset(disp, _gloffset_LineWidthx, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_LoadMatrixx)(const GLfixed *);
#define CALL_LoadMatrixx(disp, parameters) (* GET_LoadMatrixx(disp)) parameters
#define GET_LoadMatrixx(disp) ((_glptr_LoadMatrixx)(GET_by_offset((disp), _gloffset_LoadMatrixx)))
#define SET_LoadMatrixx(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLfixed *) = func; \
   SET_by_offset(disp, _gloffset_LoadMatrixx, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Materialx)(GLenum, GLenum, GLfixed);
#define CALL_Materialx(disp, parameters) (* GET_Materialx(disp)) parameters
#define GET_Materialx(disp) ((_glptr_Materialx)(GET_by_offset((disp), _gloffset_Materialx)))
#define SET_Materialx(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLfixed) = func; \
   SET_by_offset(disp, _gloffset_Materialx, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Materialxv)(GLenum, GLenum, const GLfixed *);
#define CALL_Materialxv(disp, parameters) (* GET_Materialxv(disp)) parameters
#define GET_Materialxv(disp) ((_glptr_Materialxv)(GET_by_offset((disp), _gloffset_Materialxv)))
#define SET_Materialxv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, const GLfixed *) = func; \
   SET_by_offset(disp, _gloffset_Materialxv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultMatrixx)(const GLfixed *);
#define CALL_MultMatrixx(disp, parameters) (* GET_MultMatrixx(disp)) parameters
#define GET_MultMatrixx(disp) ((_glptr_MultMatrixx)(GET_by_offset((disp), _gloffset_MultMatrixx)))
#define SET_MultMatrixx(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLfixed *) = func; \
   SET_by_offset(disp, _gloffset_MultMatrixx, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexCoord4x)(GLenum, GLfixed, GLfixed, GLfixed, GLfixed);
#define CALL_MultiTexCoord4x(disp, parameters) (* GET_MultiTexCoord4x(disp)) parameters
#define GET_MultiTexCoord4x(disp) ((_glptr_MultiTexCoord4x)(GET_by_offset((disp), _gloffset_MultiTexCoord4x)))
#define SET_MultiTexCoord4x(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLfixed, GLfixed, GLfixed, GLfixed) = func; \
   SET_by_offset(disp, _gloffset_MultiTexCoord4x, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Normal3x)(GLfixed, GLfixed, GLfixed);
#define CALL_Normal3x(disp, parameters) (* GET_Normal3x(disp)) parameters
#define GET_Normal3x(disp) ((_glptr_Normal3x)(GET_by_offset((disp), _gloffset_Normal3x)))
#define SET_Normal3x(disp, func) do { \
   void (GLAPIENTRYP fn)(GLfixed, GLfixed, GLfixed) = func; \
   SET_by_offset(disp, _gloffset_Normal3x, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Orthof)(GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat);
#define CALL_Orthof(disp, parameters) (* GET_Orthof(disp)) parameters
#define GET_Orthof(disp) ((_glptr_Orthof)(GET_by_offset((disp), _gloffset_Orthof)))
#define SET_Orthof(disp, func) do { \
   void (GLAPIENTRYP fn)(GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_Orthof, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Orthox)(GLfixed, GLfixed, GLfixed, GLfixed, GLfixed, GLfixed);
#define CALL_Orthox(disp, parameters) (* GET_Orthox(disp)) parameters
#define GET_Orthox(disp) ((_glptr_Orthox)(GET_by_offset((disp), _gloffset_Orthox)))
#define SET_Orthox(disp, func) do { \
   void (GLAPIENTRYP fn)(GLfixed, GLfixed, GLfixed, GLfixed, GLfixed, GLfixed) = func; \
   SET_by_offset(disp, _gloffset_Orthox, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_PointSizex)(GLfixed);
#define CALL_PointSizex(disp, parameters) (* GET_PointSizex(disp)) parameters
#define GET_PointSizex(disp) ((_glptr_PointSizex)(GET_by_offset((disp), _gloffset_PointSizex)))
#define SET_PointSizex(disp, func) do { \
   void (GLAPIENTRYP fn)(GLfixed) = func; \
   SET_by_offset(disp, _gloffset_PointSizex, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_PolygonOffsetx)(GLfixed, GLfixed);
#define CALL_PolygonOffsetx(disp, parameters) (* GET_PolygonOffsetx(disp)) parameters
#define GET_PolygonOffsetx(disp) ((_glptr_PolygonOffsetx)(GET_by_offset((disp), _gloffset_PolygonOffsetx)))
#define SET_PolygonOffsetx(disp, func) do { \
   void (GLAPIENTRYP fn)(GLfixed, GLfixed) = func; \
   SET_by_offset(disp, _gloffset_PolygonOffsetx, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Rotatex)(GLfixed, GLfixed, GLfixed, GLfixed);
#define CALL_Rotatex(disp, parameters) (* GET_Rotatex(disp)) parameters
#define GET_Rotatex(disp) ((_glptr_Rotatex)(GET_by_offset((disp), _gloffset_Rotatex)))
#define SET_Rotatex(disp, func) do { \
   void (GLAPIENTRYP fn)(GLfixed, GLfixed, GLfixed, GLfixed) = func; \
   SET_by_offset(disp, _gloffset_Rotatex, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_SampleCoveragex)(GLclampx, GLboolean);
#define CALL_SampleCoveragex(disp, parameters) (* GET_SampleCoveragex(disp)) parameters
#define GET_SampleCoveragex(disp) ((_glptr_SampleCoveragex)(GET_by_offset((disp), _gloffset_SampleCoveragex)))
#define SET_SampleCoveragex(disp, func) do { \
   void (GLAPIENTRYP fn)(GLclampx, GLboolean) = func; \
   SET_by_offset(disp, _gloffset_SampleCoveragex, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Scalex)(GLfixed, GLfixed, GLfixed);
#define CALL_Scalex(disp, parameters) (* GET_Scalex(disp)) parameters
#define GET_Scalex(disp) ((_glptr_Scalex)(GET_by_offset((disp), _gloffset_Scalex)))
#define SET_Scalex(disp, func) do { \
   void (GLAPIENTRYP fn)(GLfixed, GLfixed, GLfixed) = func; \
   SET_by_offset(disp, _gloffset_Scalex, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexEnvx)(GLenum, GLenum, GLfixed);
#define CALL_TexEnvx(disp, parameters) (* GET_TexEnvx(disp)) parameters
#define GET_TexEnvx(disp) ((_glptr_TexEnvx)(GET_by_offset((disp), _gloffset_TexEnvx)))
#define SET_TexEnvx(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLfixed) = func; \
   SET_by_offset(disp, _gloffset_TexEnvx, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexEnvxv)(GLenum, GLenum, const GLfixed *);
#define CALL_TexEnvxv(disp, parameters) (* GET_TexEnvxv(disp)) parameters
#define GET_TexEnvxv(disp) ((_glptr_TexEnvxv)(GET_by_offset((disp), _gloffset_TexEnvxv)))
#define SET_TexEnvxv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, const GLfixed *) = func; \
   SET_by_offset(disp, _gloffset_TexEnvxv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexParameterx)(GLenum, GLenum, GLfixed);
#define CALL_TexParameterx(disp, parameters) (* GET_TexParameterx(disp)) parameters
#define GET_TexParameterx(disp) ((_glptr_TexParameterx)(GET_by_offset((disp), _gloffset_TexParameterx)))
#define SET_TexParameterx(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLfixed) = func; \
   SET_by_offset(disp, _gloffset_TexParameterx, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Translatex)(GLfixed, GLfixed, GLfixed);
#define CALL_Translatex(disp, parameters) (* GET_Translatex(disp)) parameters
#define GET_Translatex(disp) ((_glptr_Translatex)(GET_by_offset((disp), _gloffset_Translatex)))
#define SET_Translatex(disp, func) do { \
   void (GLAPIENTRYP fn)(GLfixed, GLfixed, GLfixed) = func; \
   SET_by_offset(disp, _gloffset_Translatex, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ClipPlanef)(GLenum, const GLfloat *);
#define CALL_ClipPlanef(disp, parameters) (* GET_ClipPlanef(disp)) parameters
#define GET_ClipPlanef(disp) ((_glptr_ClipPlanef)(GET_by_offset((disp), _gloffset_ClipPlanef)))
#define SET_ClipPlanef(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_ClipPlanef, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ClipPlanex)(GLenum, const GLfixed *);
#define CALL_ClipPlanex(disp, parameters) (* GET_ClipPlanex(disp)) parameters
#define GET_ClipPlanex(disp) ((_glptr_ClipPlanex)(GET_by_offset((disp), _gloffset_ClipPlanex)))
#define SET_ClipPlanex(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, const GLfixed *) = func; \
   SET_by_offset(disp, _gloffset_ClipPlanex, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetClipPlanef)(GLenum, GLfloat *);
#define CALL_GetClipPlanef(disp, parameters) (* GET_GetClipPlanef(disp)) parameters
#define GET_GetClipPlanef(disp) ((_glptr_GetClipPlanef)(GET_by_offset((disp), _gloffset_GetClipPlanef)))
#define SET_GetClipPlanef(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_GetClipPlanef, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetClipPlanex)(GLenum, GLfixed *);
#define CALL_GetClipPlanex(disp, parameters) (* GET_GetClipPlanex(disp)) parameters
#define GET_GetClipPlanex(disp) ((_glptr_GetClipPlanex)(GET_by_offset((disp), _gloffset_GetClipPlanex)))
#define SET_GetClipPlanex(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLfixed *) = func; \
   SET_by_offset(disp, _gloffset_GetClipPlanex, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetFixedv)(GLenum, GLfixed *);
#define CALL_GetFixedv(disp, parameters) (* GET_GetFixedv(disp)) parameters
#define GET_GetFixedv(disp) ((_glptr_GetFixedv)(GET_by_offset((disp), _gloffset_GetFixedv)))
#define SET_GetFixedv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLfixed *) = func; \
   SET_by_offset(disp, _gloffset_GetFixedv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetLightxv)(GLenum, GLenum, GLfixed *);
#define CALL_GetLightxv(disp, parameters) (* GET_GetLightxv(disp)) parameters
#define GET_GetLightxv(disp) ((_glptr_GetLightxv)(GET_by_offset((disp), _gloffset_GetLightxv)))
#define SET_GetLightxv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLfixed *) = func; \
   SET_by_offset(disp, _gloffset_GetLightxv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetMaterialxv)(GLenum, GLenum, GLfixed *);
#define CALL_GetMaterialxv(disp, parameters) (* GET_GetMaterialxv(disp)) parameters
#define GET_GetMaterialxv(disp) ((_glptr_GetMaterialxv)(GET_by_offset((disp), _gloffset_GetMaterialxv)))
#define SET_GetMaterialxv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLfixed *) = func; \
   SET_by_offset(disp, _gloffset_GetMaterialxv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetTexEnvxv)(GLenum, GLenum, GLfixed *);
#define CALL_GetTexEnvxv(disp, parameters) (* GET_GetTexEnvxv(disp)) parameters
#define GET_GetTexEnvxv(disp) ((_glptr_GetTexEnvxv)(GET_by_offset((disp), _gloffset_GetTexEnvxv)))
#define SET_GetTexEnvxv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLfixed *) = func; \
   SET_by_offset(disp, _gloffset_GetTexEnvxv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetTexParameterxv)(GLenum, GLenum, GLfixed *);
#define CALL_GetTexParameterxv(disp, parameters) (* GET_GetTexParameterxv(disp)) parameters
#define GET_GetTexParameterxv(disp) ((_glptr_GetTexParameterxv)(GET_by_offset((disp), _gloffset_GetTexParameterxv)))
#define SET_GetTexParameterxv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLfixed *) = func; \
   SET_by_offset(disp, _gloffset_GetTexParameterxv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_PointParameterx)(GLenum, GLfixed);
#define CALL_PointParameterx(disp, parameters) (* GET_PointParameterx(disp)) parameters
#define GET_PointParameterx(disp) ((_glptr_PointParameterx)(GET_by_offset((disp), _gloffset_PointParameterx)))
#define SET_PointParameterx(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLfixed) = func; \
   SET_by_offset(disp, _gloffset_PointParameterx, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_PointParameterxv)(GLenum, const GLfixed *);
#define CALL_PointParameterxv(disp, parameters) (* GET_PointParameterxv(disp)) parameters
#define GET_PointParameterxv(disp) ((_glptr_PointParameterxv)(GET_by_offset((disp), _gloffset_PointParameterxv)))
#define SET_PointParameterxv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, const GLfixed *) = func; \
   SET_by_offset(disp, _gloffset_PointParameterxv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexParameterxv)(GLenum, GLenum, const GLfixed *);
#define CALL_TexParameterxv(disp, parameters) (* GET_TexParameterxv(disp)) parameters
#define GET_TexParameterxv(disp) ((_glptr_TexParameterxv)(GET_by_offset((disp), _gloffset_TexParameterxv)))
#define SET_TexParameterxv(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, const GLfixed *) = func; \
   SET_by_offset(disp, _gloffset_TexParameterxv, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_BlendBarrier)(void);
#define CALL_BlendBarrier(disp, parameters) (* GET_BlendBarrier(disp)) parameters
#define GET_BlendBarrier(disp) ((_glptr_BlendBarrier)(GET_by_offset((disp), _gloffset_BlendBarrier)))
#define SET_BlendBarrier(disp, func) do { \
   void (GLAPIENTRYP fn)(void) = func; \
   SET_by_offset(disp, _gloffset_BlendBarrier, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_PrimitiveBoundingBox)(GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat);
#define CALL_PrimitiveBoundingBox(disp, parameters) (* GET_PrimitiveBoundingBox(disp)) parameters
#define GET_PrimitiveBoundingBox(disp) ((_glptr_PrimitiveBoundingBox)(GET_by_offset((disp), _gloffset_PrimitiveBoundingBox)))
#define SET_PrimitiveBoundingBox(disp, func) do { \
   void (GLAPIENTRYP fn)(GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_PrimitiveBoundingBox, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MaxShaderCompilerThreadsKHR)(GLuint);
#define CALL_MaxShaderCompilerThreadsKHR(disp, parameters) (* GET_MaxShaderCompilerThreadsKHR(disp)) parameters
#define GET_MaxShaderCompilerThreadsKHR(disp) ((_glptr_MaxShaderCompilerThreadsKHR)(GET_by_offset((disp), _gloffset_MaxShaderCompilerThreadsKHR)))
#define SET_MaxShaderCompilerThreadsKHR(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint) = func; \
   SET_by_offset(disp, _gloffset_MaxShaderCompilerThreadsKHR, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MatrixLoadfEXT)(GLenum, const GLfloat *);
#define CALL_MatrixLoadfEXT(disp, parameters) (* GET_MatrixLoadfEXT(disp)) parameters
#define GET_MatrixLoadfEXT(disp) ((_glptr_MatrixLoadfEXT)(GET_by_offset((disp), _gloffset_MatrixLoadfEXT)))
#define SET_MatrixLoadfEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_MatrixLoadfEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MatrixLoaddEXT)(GLenum, const GLdouble *);
#define CALL_MatrixLoaddEXT(disp, parameters) (* GET_MatrixLoaddEXT(disp)) parameters
#define GET_MatrixLoaddEXT(disp) ((_glptr_MatrixLoaddEXT)(GET_by_offset((disp), _gloffset_MatrixLoaddEXT)))
#define SET_MatrixLoaddEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_MatrixLoaddEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MatrixMultfEXT)(GLenum, const GLfloat *);
#define CALL_MatrixMultfEXT(disp, parameters) (* GET_MatrixMultfEXT(disp)) parameters
#define GET_MatrixMultfEXT(disp) ((_glptr_MatrixMultfEXT)(GET_by_offset((disp), _gloffset_MatrixMultfEXT)))
#define SET_MatrixMultfEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_MatrixMultfEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MatrixMultdEXT)(GLenum, const GLdouble *);
#define CALL_MatrixMultdEXT(disp, parameters) (* GET_MatrixMultdEXT(disp)) parameters
#define GET_MatrixMultdEXT(disp) ((_glptr_MatrixMultdEXT)(GET_by_offset((disp), _gloffset_MatrixMultdEXT)))
#define SET_MatrixMultdEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_MatrixMultdEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MatrixLoadIdentityEXT)(GLenum);
#define CALL_MatrixLoadIdentityEXT(disp, parameters) (* GET_MatrixLoadIdentityEXT(disp)) parameters
#define GET_MatrixLoadIdentityEXT(disp) ((_glptr_MatrixLoadIdentityEXT)(GET_by_offset((disp), _gloffset_MatrixLoadIdentityEXT)))
#define SET_MatrixLoadIdentityEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum) = func; \
   SET_by_offset(disp, _gloffset_MatrixLoadIdentityEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MatrixRotatefEXT)(GLenum, GLfloat, GLfloat, GLfloat, GLfloat);
#define CALL_MatrixRotatefEXT(disp, parameters) (* GET_MatrixRotatefEXT(disp)) parameters
#define GET_MatrixRotatefEXT(disp) ((_glptr_MatrixRotatefEXT)(GET_by_offset((disp), _gloffset_MatrixRotatefEXT)))
#define SET_MatrixRotatefEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLfloat, GLfloat, GLfloat, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_MatrixRotatefEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MatrixRotatedEXT)(GLenum, GLdouble, GLdouble, GLdouble, GLdouble);
#define CALL_MatrixRotatedEXT(disp, parameters) (* GET_MatrixRotatedEXT(disp)) parameters
#define GET_MatrixRotatedEXT(disp) ((_glptr_MatrixRotatedEXT)(GET_by_offset((disp), _gloffset_MatrixRotatedEXT)))
#define SET_MatrixRotatedEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLdouble, GLdouble, GLdouble, GLdouble) = func; \
   SET_by_offset(disp, _gloffset_MatrixRotatedEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MatrixScalefEXT)(GLenum, GLfloat, GLfloat, GLfloat);
#define CALL_MatrixScalefEXT(disp, parameters) (* GET_MatrixScalefEXT(disp)) parameters
#define GET_MatrixScalefEXT(disp) ((_glptr_MatrixScalefEXT)(GET_by_offset((disp), _gloffset_MatrixScalefEXT)))
#define SET_MatrixScalefEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLfloat, GLfloat, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_MatrixScalefEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MatrixScaledEXT)(GLenum, GLdouble, GLdouble, GLdouble);
#define CALL_MatrixScaledEXT(disp, parameters) (* GET_MatrixScaledEXT(disp)) parameters
#define GET_MatrixScaledEXT(disp) ((_glptr_MatrixScaledEXT)(GET_by_offset((disp), _gloffset_MatrixScaledEXT)))
#define SET_MatrixScaledEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLdouble, GLdouble, GLdouble) = func; \
   SET_by_offset(disp, _gloffset_MatrixScaledEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MatrixTranslatefEXT)(GLenum, GLfloat, GLfloat, GLfloat);
#define CALL_MatrixTranslatefEXT(disp, parameters) (* GET_MatrixTranslatefEXT(disp)) parameters
#define GET_MatrixTranslatefEXT(disp) ((_glptr_MatrixTranslatefEXT)(GET_by_offset((disp), _gloffset_MatrixTranslatefEXT)))
#define SET_MatrixTranslatefEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLfloat, GLfloat, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_MatrixTranslatefEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MatrixTranslatedEXT)(GLenum, GLdouble, GLdouble, GLdouble);
#define CALL_MatrixTranslatedEXT(disp, parameters) (* GET_MatrixTranslatedEXT(disp)) parameters
#define GET_MatrixTranslatedEXT(disp) ((_glptr_MatrixTranslatedEXT)(GET_by_offset((disp), _gloffset_MatrixTranslatedEXT)))
#define SET_MatrixTranslatedEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLdouble, GLdouble, GLdouble) = func; \
   SET_by_offset(disp, _gloffset_MatrixTranslatedEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MatrixOrthoEXT)(GLenum, GLdouble, GLdouble, GLdouble, GLdouble, GLdouble, GLdouble);
#define CALL_MatrixOrthoEXT(disp, parameters) (* GET_MatrixOrthoEXT(disp)) parameters
#define GET_MatrixOrthoEXT(disp) ((_glptr_MatrixOrthoEXT)(GET_by_offset((disp), _gloffset_MatrixOrthoEXT)))
#define SET_MatrixOrthoEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLdouble, GLdouble, GLdouble, GLdouble, GLdouble, GLdouble) = func; \
   SET_by_offset(disp, _gloffset_MatrixOrthoEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MatrixFrustumEXT)(GLenum, GLdouble, GLdouble, GLdouble, GLdouble, GLdouble, GLdouble);
#define CALL_MatrixFrustumEXT(disp, parameters) (* GET_MatrixFrustumEXT(disp)) parameters
#define GET_MatrixFrustumEXT(disp) ((_glptr_MatrixFrustumEXT)(GET_by_offset((disp), _gloffset_MatrixFrustumEXT)))
#define SET_MatrixFrustumEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLdouble, GLdouble, GLdouble, GLdouble, GLdouble, GLdouble) = func; \
   SET_by_offset(disp, _gloffset_MatrixFrustumEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MatrixPushEXT)(GLenum);
#define CALL_MatrixPushEXT(disp, parameters) (* GET_MatrixPushEXT(disp)) parameters
#define GET_MatrixPushEXT(disp) ((_glptr_MatrixPushEXT)(GET_by_offset((disp), _gloffset_MatrixPushEXT)))
#define SET_MatrixPushEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum) = func; \
   SET_by_offset(disp, _gloffset_MatrixPushEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MatrixPopEXT)(GLenum);
#define CALL_MatrixPopEXT(disp, parameters) (* GET_MatrixPopEXT(disp)) parameters
#define GET_MatrixPopEXT(disp) ((_glptr_MatrixPopEXT)(GET_by_offset((disp), _gloffset_MatrixPopEXT)))
#define SET_MatrixPopEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum) = func; \
   SET_by_offset(disp, _gloffset_MatrixPopEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MatrixLoadTransposefEXT)(GLenum, const GLfloat *);
#define CALL_MatrixLoadTransposefEXT(disp, parameters) (* GET_MatrixLoadTransposefEXT(disp)) parameters
#define GET_MatrixLoadTransposefEXT(disp) ((_glptr_MatrixLoadTransposefEXT)(GET_by_offset((disp), _gloffset_MatrixLoadTransposefEXT)))
#define SET_MatrixLoadTransposefEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_MatrixLoadTransposefEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MatrixLoadTransposedEXT)(GLenum, const GLdouble *);
#define CALL_MatrixLoadTransposedEXT(disp, parameters) (* GET_MatrixLoadTransposedEXT(disp)) parameters
#define GET_MatrixLoadTransposedEXT(disp) ((_glptr_MatrixLoadTransposedEXT)(GET_by_offset((disp), _gloffset_MatrixLoadTransposedEXT)))
#define SET_MatrixLoadTransposedEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_MatrixLoadTransposedEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MatrixMultTransposefEXT)(GLenum, const GLfloat *);
#define CALL_MatrixMultTransposefEXT(disp, parameters) (* GET_MatrixMultTransposefEXT(disp)) parameters
#define GET_MatrixMultTransposefEXT(disp) ((_glptr_MatrixMultTransposefEXT)(GET_by_offset((disp), _gloffset_MatrixMultTransposefEXT)))
#define SET_MatrixMultTransposefEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_MatrixMultTransposefEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MatrixMultTransposedEXT)(GLenum, const GLdouble *);
#define CALL_MatrixMultTransposedEXT(disp, parameters) (* GET_MatrixMultTransposedEXT(disp)) parameters
#define GET_MatrixMultTransposedEXT(disp) ((_glptr_MatrixMultTransposedEXT)(GET_by_offset((disp), _gloffset_MatrixMultTransposedEXT)))
#define SET_MatrixMultTransposedEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, const GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_MatrixMultTransposedEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_BindMultiTextureEXT)(GLenum, GLenum, GLuint);
#define CALL_BindMultiTextureEXT(disp, parameters) (* GET_BindMultiTextureEXT(disp)) parameters
#define GET_BindMultiTextureEXT(disp) ((_glptr_BindMultiTextureEXT)(GET_by_offset((disp), _gloffset_BindMultiTextureEXT)))
#define SET_BindMultiTextureEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLuint) = func; \
   SET_by_offset(disp, _gloffset_BindMultiTextureEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_NamedBufferDataEXT)(GLuint, GLsizeiptr, const GLvoid *, GLenum);
#define CALL_NamedBufferDataEXT(disp, parameters) (* GET_NamedBufferDataEXT(disp)) parameters
#define GET_NamedBufferDataEXT(disp) ((_glptr_NamedBufferDataEXT)(GET_by_offset((disp), _gloffset_NamedBufferDataEXT)))
#define SET_NamedBufferDataEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizeiptr, const GLvoid *, GLenum) = func; \
   SET_by_offset(disp, _gloffset_NamedBufferDataEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_NamedBufferSubDataEXT)(GLuint, GLintptr, GLsizeiptr, const GLvoid *);
#define CALL_NamedBufferSubDataEXT(disp, parameters) (* GET_NamedBufferSubDataEXT(disp)) parameters
#define GET_NamedBufferSubDataEXT(disp) ((_glptr_NamedBufferSubDataEXT)(GET_by_offset((disp), _gloffset_NamedBufferSubDataEXT)))
#define SET_NamedBufferSubDataEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLintptr, GLsizeiptr, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_NamedBufferSubDataEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_NamedBufferStorageEXT)(GLuint, GLsizeiptr, const GLvoid *, GLbitfield);
#define CALL_NamedBufferStorageEXT(disp, parameters) (* GET_NamedBufferStorageEXT(disp)) parameters
#define GET_NamedBufferStorageEXT(disp) ((_glptr_NamedBufferStorageEXT)(GET_by_offset((disp), _gloffset_NamedBufferStorageEXT)))
#define SET_NamedBufferStorageEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizeiptr, const GLvoid *, GLbitfield) = func; \
   SET_by_offset(disp, _gloffset_NamedBufferStorageEXT, fn); \
} while (0)

typedef GLvoid * (GLAPIENTRYP _glptr_MapNamedBufferRangeEXT)(GLuint, GLintptr, GLsizeiptr, GLbitfield);
#define CALL_MapNamedBufferRangeEXT(disp, parameters) (* GET_MapNamedBufferRangeEXT(disp)) parameters
#define GET_MapNamedBufferRangeEXT(disp) ((_glptr_MapNamedBufferRangeEXT)(GET_by_offset((disp), _gloffset_MapNamedBufferRangeEXT)))
#define SET_MapNamedBufferRangeEXT(disp, func) do { \
   GLvoid * (GLAPIENTRYP fn)(GLuint, GLintptr, GLsizeiptr, GLbitfield) = func; \
   SET_by_offset(disp, _gloffset_MapNamedBufferRangeEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TextureImage1DEXT)(GLuint, GLenum, GLint, GLint, GLsizei, GLint, GLenum, GLenum, const GLvoid *);
#define CALL_TextureImage1DEXT(disp, parameters) (* GET_TextureImage1DEXT(disp)) parameters
#define GET_TextureImage1DEXT(disp) ((_glptr_TextureImage1DEXT)(GET_by_offset((disp), _gloffset_TextureImage1DEXT)))
#define SET_TextureImage1DEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLint, GLint, GLsizei, GLint, GLenum, GLenum, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_TextureImage1DEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TextureImage2DEXT)(GLuint, GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const GLvoid *);
#define CALL_TextureImage2DEXT(disp, parameters) (* GET_TextureImage2DEXT(disp)) parameters
#define GET_TextureImage2DEXT(disp) ((_glptr_TextureImage2DEXT)(GET_by_offset((disp), _gloffset_TextureImage2DEXT)))
#define SET_TextureImage2DEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_TextureImage2DEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TextureImage3DEXT)(GLuint, GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum, const GLvoid *);
#define CALL_TextureImage3DEXT(disp, parameters) (* GET_TextureImage3DEXT(disp)) parameters
#define GET_TextureImage3DEXT(disp) ((_glptr_TextureImage3DEXT)(GET_by_offset((disp), _gloffset_TextureImage3DEXT)))
#define SET_TextureImage3DEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_TextureImage3DEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TextureSubImage1DEXT)(GLuint, GLenum, GLint, GLint, GLsizei, GLenum, GLenum, const GLvoid *);
#define CALL_TextureSubImage1DEXT(disp, parameters) (* GET_TextureSubImage1DEXT(disp)) parameters
#define GET_TextureSubImage1DEXT(disp) ((_glptr_TextureSubImage1DEXT)(GET_by_offset((disp), _gloffset_TextureSubImage1DEXT)))
#define SET_TextureSubImage1DEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLint, GLint, GLsizei, GLenum, GLenum, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_TextureSubImage1DEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TextureSubImage2DEXT)(GLuint, GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const GLvoid *);
#define CALL_TextureSubImage2DEXT(disp, parameters) (* GET_TextureSubImage2DEXT(disp)) parameters
#define GET_TextureSubImage2DEXT(disp) ((_glptr_TextureSubImage2DEXT)(GET_by_offset((disp), _gloffset_TextureSubImage2DEXT)))
#define SET_TextureSubImage2DEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_TextureSubImage2DEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TextureSubImage3DEXT)(GLuint, GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLenum, const GLvoid *);
#define CALL_TextureSubImage3DEXT(disp, parameters) (* GET_TextureSubImage3DEXT(disp)) parameters
#define GET_TextureSubImage3DEXT(disp) ((_glptr_TextureSubImage3DEXT)(GET_by_offset((disp), _gloffset_TextureSubImage3DEXT)))
#define SET_TextureSubImage3DEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLenum, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_TextureSubImage3DEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CopyTextureImage1DEXT)(GLuint, GLenum, GLint, GLenum, GLint, GLint, GLsizei, int);
#define CALL_CopyTextureImage1DEXT(disp, parameters) (* GET_CopyTextureImage1DEXT(disp)) parameters
#define GET_CopyTextureImage1DEXT(disp) ((_glptr_CopyTextureImage1DEXT)(GET_by_offset((disp), _gloffset_CopyTextureImage1DEXT)))
#define SET_CopyTextureImage1DEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLint, GLenum, GLint, GLint, GLsizei, int) = func; \
   SET_by_offset(disp, _gloffset_CopyTextureImage1DEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CopyTextureImage2DEXT)(GLuint, GLenum, GLint, GLenum, GLint, GLint, GLsizei, GLsizei, int);
#define CALL_CopyTextureImage2DEXT(disp, parameters) (* GET_CopyTextureImage2DEXT(disp)) parameters
#define GET_CopyTextureImage2DEXT(disp) ((_glptr_CopyTextureImage2DEXT)(GET_by_offset((disp), _gloffset_CopyTextureImage2DEXT)))
#define SET_CopyTextureImage2DEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLint, GLenum, GLint, GLint, GLsizei, GLsizei, int) = func; \
   SET_by_offset(disp, _gloffset_CopyTextureImage2DEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CopyTextureSubImage1DEXT)(GLuint, GLenum, GLint, GLint, GLint, GLint, GLsizei);
#define CALL_CopyTextureSubImage1DEXT(disp, parameters) (* GET_CopyTextureSubImage1DEXT(disp)) parameters
#define GET_CopyTextureSubImage1DEXT(disp) ((_glptr_CopyTextureSubImage1DEXT)(GET_by_offset((disp), _gloffset_CopyTextureSubImage1DEXT)))
#define SET_CopyTextureSubImage1DEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLint, GLint, GLint, GLint, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_CopyTextureSubImage1DEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CopyTextureSubImage2DEXT)(GLuint, GLenum, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei);
#define CALL_CopyTextureSubImage2DEXT(disp, parameters) (* GET_CopyTextureSubImage2DEXT(disp)) parameters
#define GET_CopyTextureSubImage2DEXT(disp) ((_glptr_CopyTextureSubImage2DEXT)(GET_by_offset((disp), _gloffset_CopyTextureSubImage2DEXT)))
#define SET_CopyTextureSubImage2DEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_CopyTextureSubImage2DEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CopyTextureSubImage3DEXT)(GLuint, GLenum, GLint, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei);
#define CALL_CopyTextureSubImage3DEXT(disp, parameters) (* GET_CopyTextureSubImage3DEXT(disp)) parameters
#define GET_CopyTextureSubImage3DEXT(disp) ((_glptr_CopyTextureSubImage3DEXT)(GET_by_offset((disp), _gloffset_CopyTextureSubImage3DEXT)))
#define SET_CopyTextureSubImage3DEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLint, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_CopyTextureSubImage3DEXT, fn); \
} while (0)

typedef GLvoid * (GLAPIENTRYP _glptr_MapNamedBufferEXT)(GLuint, GLenum);
#define CALL_MapNamedBufferEXT(disp, parameters) (* GET_MapNamedBufferEXT(disp)) parameters
#define GET_MapNamedBufferEXT(disp) ((_glptr_MapNamedBufferEXT)(GET_by_offset((disp), _gloffset_MapNamedBufferEXT)))
#define SET_MapNamedBufferEXT(disp, func) do { \
   GLvoid * (GLAPIENTRYP fn)(GLuint, GLenum) = func; \
   SET_by_offset(disp, _gloffset_MapNamedBufferEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetTextureParameterivEXT)(GLuint, GLenum, GLenum, GLint *);
#define CALL_GetTextureParameterivEXT(disp, parameters) (* GET_GetTextureParameterivEXT(disp)) parameters
#define GET_GetTextureParameterivEXT(disp) ((_glptr_GetTextureParameterivEXT)(GET_by_offset((disp), _gloffset_GetTextureParameterivEXT)))
#define SET_GetTextureParameterivEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetTextureParameterivEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetTextureParameterfvEXT)(GLuint, GLenum, GLenum, float *);
#define CALL_GetTextureParameterfvEXT(disp, parameters) (* GET_GetTextureParameterfvEXT(disp)) parameters
#define GET_GetTextureParameterfvEXT(disp) ((_glptr_GetTextureParameterfvEXT)(GET_by_offset((disp), _gloffset_GetTextureParameterfvEXT)))
#define SET_GetTextureParameterfvEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLenum, float *) = func; \
   SET_by_offset(disp, _gloffset_GetTextureParameterfvEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TextureParameteriEXT)(GLuint, GLenum, GLenum, int);
#define CALL_TextureParameteriEXT(disp, parameters) (* GET_TextureParameteriEXT(disp)) parameters
#define GET_TextureParameteriEXT(disp) ((_glptr_TextureParameteriEXT)(GET_by_offset((disp), _gloffset_TextureParameteriEXT)))
#define SET_TextureParameteriEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLenum, int) = func; \
   SET_by_offset(disp, _gloffset_TextureParameteriEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TextureParameterivEXT)(GLuint, GLenum, GLenum, const GLint *);
#define CALL_TextureParameterivEXT(disp, parameters) (* GET_TextureParameterivEXT(disp)) parameters
#define GET_TextureParameterivEXT(disp) ((_glptr_TextureParameterivEXT)(GET_by_offset((disp), _gloffset_TextureParameterivEXT)))
#define SET_TextureParameterivEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLenum, const GLint *) = func; \
   SET_by_offset(disp, _gloffset_TextureParameterivEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TextureParameterfEXT)(GLuint, GLenum, GLenum, float);
#define CALL_TextureParameterfEXT(disp, parameters) (* GET_TextureParameterfEXT(disp)) parameters
#define GET_TextureParameterfEXT(disp) ((_glptr_TextureParameterfEXT)(GET_by_offset((disp), _gloffset_TextureParameterfEXT)))
#define SET_TextureParameterfEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLenum, float) = func; \
   SET_by_offset(disp, _gloffset_TextureParameterfEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TextureParameterfvEXT)(GLuint, GLenum, GLenum, const float *);
#define CALL_TextureParameterfvEXT(disp, parameters) (* GET_TextureParameterfvEXT(disp)) parameters
#define GET_TextureParameterfvEXT(disp) ((_glptr_TextureParameterfvEXT)(GET_by_offset((disp), _gloffset_TextureParameterfvEXT)))
#define SET_TextureParameterfvEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLenum, const float *) = func; \
   SET_by_offset(disp, _gloffset_TextureParameterfvEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetTextureImageEXT)(GLuint, GLenum, GLint, GLenum, GLenum, GLvoid *);
#define CALL_GetTextureImageEXT(disp, parameters) (* GET_GetTextureImageEXT(disp)) parameters
#define GET_GetTextureImageEXT(disp) ((_glptr_GetTextureImageEXT)(GET_by_offset((disp), _gloffset_GetTextureImageEXT)))
#define SET_GetTextureImageEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLint, GLenum, GLenum, GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_GetTextureImageEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetTextureLevelParameterivEXT)(GLuint, GLenum, GLint, GLenum, GLint *);
#define CALL_GetTextureLevelParameterivEXT(disp, parameters) (* GET_GetTextureLevelParameterivEXT(disp)) parameters
#define GET_GetTextureLevelParameterivEXT(disp) ((_glptr_GetTextureLevelParameterivEXT)(GET_by_offset((disp), _gloffset_GetTextureLevelParameterivEXT)))
#define SET_GetTextureLevelParameterivEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLint, GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetTextureLevelParameterivEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetTextureLevelParameterfvEXT)(GLuint, GLenum, GLint, GLenum, float *);
#define CALL_GetTextureLevelParameterfvEXT(disp, parameters) (* GET_GetTextureLevelParameterfvEXT(disp)) parameters
#define GET_GetTextureLevelParameterfvEXT(disp) ((_glptr_GetTextureLevelParameterfvEXT)(GET_by_offset((disp), _gloffset_GetTextureLevelParameterfvEXT)))
#define SET_GetTextureLevelParameterfvEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLint, GLenum, float *) = func; \
   SET_by_offset(disp, _gloffset_GetTextureLevelParameterfvEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetNamedBufferSubDataEXT)(GLuint, GLintptr, GLsizeiptr, GLvoid *);
#define CALL_GetNamedBufferSubDataEXT(disp, parameters) (* GET_GetNamedBufferSubDataEXT(disp)) parameters
#define GET_GetNamedBufferSubDataEXT(disp) ((_glptr_GetNamedBufferSubDataEXT)(GET_by_offset((disp), _gloffset_GetNamedBufferSubDataEXT)))
#define SET_GetNamedBufferSubDataEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLintptr, GLsizeiptr, GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_GetNamedBufferSubDataEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetNamedBufferPointervEXT)(GLuint, GLenum, GLvoid **);
#define CALL_GetNamedBufferPointervEXT(disp, parameters) (* GET_GetNamedBufferPointervEXT(disp)) parameters
#define GET_GetNamedBufferPointervEXT(disp) ((_glptr_GetNamedBufferPointervEXT)(GET_by_offset((disp), _gloffset_GetNamedBufferPointervEXT)))
#define SET_GetNamedBufferPointervEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLvoid **) = func; \
   SET_by_offset(disp, _gloffset_GetNamedBufferPointervEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetNamedBufferParameterivEXT)(GLuint, GLenum, GLint *);
#define CALL_GetNamedBufferParameterivEXT(disp, parameters) (* GET_GetNamedBufferParameterivEXT(disp)) parameters
#define GET_GetNamedBufferParameterivEXT(disp) ((_glptr_GetNamedBufferParameterivEXT)(GET_by_offset((disp), _gloffset_GetNamedBufferParameterivEXT)))
#define SET_GetNamedBufferParameterivEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetNamedBufferParameterivEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_FlushMappedNamedBufferRangeEXT)(GLuint, GLintptr, GLsizeiptr);
#define CALL_FlushMappedNamedBufferRangeEXT(disp, parameters) (* GET_FlushMappedNamedBufferRangeEXT(disp)) parameters
#define GET_FlushMappedNamedBufferRangeEXT(disp) ((_glptr_FlushMappedNamedBufferRangeEXT)(GET_by_offset((disp), _gloffset_FlushMappedNamedBufferRangeEXT)))
#define SET_FlushMappedNamedBufferRangeEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLintptr, GLsizeiptr) = func; \
   SET_by_offset(disp, _gloffset_FlushMappedNamedBufferRangeEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_FramebufferDrawBufferEXT)(GLuint, GLenum);
#define CALL_FramebufferDrawBufferEXT(disp, parameters) (* GET_FramebufferDrawBufferEXT(disp)) parameters
#define GET_FramebufferDrawBufferEXT(disp) ((_glptr_FramebufferDrawBufferEXT)(GET_by_offset((disp), _gloffset_FramebufferDrawBufferEXT)))
#define SET_FramebufferDrawBufferEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum) = func; \
   SET_by_offset(disp, _gloffset_FramebufferDrawBufferEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_FramebufferDrawBuffersEXT)(GLuint, GLsizei, const GLenum *);
#define CALL_FramebufferDrawBuffersEXT(disp, parameters) (* GET_FramebufferDrawBuffersEXT(disp)) parameters
#define GET_FramebufferDrawBuffersEXT(disp) ((_glptr_FramebufferDrawBuffersEXT)(GET_by_offset((disp), _gloffset_FramebufferDrawBuffersEXT)))
#define SET_FramebufferDrawBuffersEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizei, const GLenum *) = func; \
   SET_by_offset(disp, _gloffset_FramebufferDrawBuffersEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_FramebufferReadBufferEXT)(GLuint, GLenum);
#define CALL_FramebufferReadBufferEXT(disp, parameters) (* GET_FramebufferReadBufferEXT(disp)) parameters
#define GET_FramebufferReadBufferEXT(disp) ((_glptr_FramebufferReadBufferEXT)(GET_by_offset((disp), _gloffset_FramebufferReadBufferEXT)))
#define SET_FramebufferReadBufferEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum) = func; \
   SET_by_offset(disp, _gloffset_FramebufferReadBufferEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetFramebufferParameterivEXT)(GLuint, GLenum, GLint *);
#define CALL_GetFramebufferParameterivEXT(disp, parameters) (* GET_GetFramebufferParameterivEXT(disp)) parameters
#define GET_GetFramebufferParameterivEXT(disp) ((_glptr_GetFramebufferParameterivEXT)(GET_by_offset((disp), _gloffset_GetFramebufferParameterivEXT)))
#define SET_GetFramebufferParameterivEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetFramebufferParameterivEXT, fn); \
} while (0)

typedef GLenum (GLAPIENTRYP _glptr_CheckNamedFramebufferStatusEXT)(GLuint, GLenum);
#define CALL_CheckNamedFramebufferStatusEXT(disp, parameters) (* GET_CheckNamedFramebufferStatusEXT(disp)) parameters
#define GET_CheckNamedFramebufferStatusEXT(disp) ((_glptr_CheckNamedFramebufferStatusEXT)(GET_by_offset((disp), _gloffset_CheckNamedFramebufferStatusEXT)))
#define SET_CheckNamedFramebufferStatusEXT(disp, func) do { \
   GLenum (GLAPIENTRYP fn)(GLuint, GLenum) = func; \
   SET_by_offset(disp, _gloffset_CheckNamedFramebufferStatusEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_NamedFramebufferTexture1DEXT)(GLuint, GLenum, GLenum, GLuint, GLint);
#define CALL_NamedFramebufferTexture1DEXT(disp, parameters) (* GET_NamedFramebufferTexture1DEXT(disp)) parameters
#define GET_NamedFramebufferTexture1DEXT(disp) ((_glptr_NamedFramebufferTexture1DEXT)(GET_by_offset((disp), _gloffset_NamedFramebufferTexture1DEXT)))
#define SET_NamedFramebufferTexture1DEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLenum, GLuint, GLint) = func; \
   SET_by_offset(disp, _gloffset_NamedFramebufferTexture1DEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_NamedFramebufferTexture2DEXT)(GLuint, GLenum, GLenum, GLuint, GLint);
#define CALL_NamedFramebufferTexture2DEXT(disp, parameters) (* GET_NamedFramebufferTexture2DEXT(disp)) parameters
#define GET_NamedFramebufferTexture2DEXT(disp) ((_glptr_NamedFramebufferTexture2DEXT)(GET_by_offset((disp), _gloffset_NamedFramebufferTexture2DEXT)))
#define SET_NamedFramebufferTexture2DEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLenum, GLuint, GLint) = func; \
   SET_by_offset(disp, _gloffset_NamedFramebufferTexture2DEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_NamedFramebufferTexture3DEXT)(GLuint, GLenum, GLenum, GLuint, GLint, GLint);
#define CALL_NamedFramebufferTexture3DEXT(disp, parameters) (* GET_NamedFramebufferTexture3DEXT(disp)) parameters
#define GET_NamedFramebufferTexture3DEXT(disp) ((_glptr_NamedFramebufferTexture3DEXT)(GET_by_offset((disp), _gloffset_NamedFramebufferTexture3DEXT)))
#define SET_NamedFramebufferTexture3DEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLenum, GLuint, GLint, GLint) = func; \
   SET_by_offset(disp, _gloffset_NamedFramebufferTexture3DEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_NamedFramebufferRenderbufferEXT)(GLuint, GLenum, GLenum, GLuint);
#define CALL_NamedFramebufferRenderbufferEXT(disp, parameters) (* GET_NamedFramebufferRenderbufferEXT(disp)) parameters
#define GET_NamedFramebufferRenderbufferEXT(disp) ((_glptr_NamedFramebufferRenderbufferEXT)(GET_by_offset((disp), _gloffset_NamedFramebufferRenderbufferEXT)))
#define SET_NamedFramebufferRenderbufferEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLenum, GLuint) = func; \
   SET_by_offset(disp, _gloffset_NamedFramebufferRenderbufferEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetNamedFramebufferAttachmentParameterivEXT)(GLuint, GLenum, GLenum, GLint *);
#define CALL_GetNamedFramebufferAttachmentParameterivEXT(disp, parameters) (* GET_GetNamedFramebufferAttachmentParameterivEXT(disp)) parameters
#define GET_GetNamedFramebufferAttachmentParameterivEXT(disp) ((_glptr_GetNamedFramebufferAttachmentParameterivEXT)(GET_by_offset((disp), _gloffset_GetNamedFramebufferAttachmentParameterivEXT)))
#define SET_GetNamedFramebufferAttachmentParameterivEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetNamedFramebufferAttachmentParameterivEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_EnableClientStateiEXT)(GLenum, GLuint);
#define CALL_EnableClientStateiEXT(disp, parameters) (* GET_EnableClientStateiEXT(disp)) parameters
#define GET_EnableClientStateiEXT(disp) ((_glptr_EnableClientStateiEXT)(GET_by_offset((disp), _gloffset_EnableClientStateiEXT)))
#define SET_EnableClientStateiEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint) = func; \
   SET_by_offset(disp, _gloffset_EnableClientStateiEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DisableClientStateiEXT)(GLenum, GLuint);
#define CALL_DisableClientStateiEXT(disp, parameters) (* GET_DisableClientStateiEXT(disp)) parameters
#define GET_DisableClientStateiEXT(disp) ((_glptr_DisableClientStateiEXT)(GET_by_offset((disp), _gloffset_DisableClientStateiEXT)))
#define SET_DisableClientStateiEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint) = func; \
   SET_by_offset(disp, _gloffset_DisableClientStateiEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetPointerIndexedvEXT)(GLenum, GLuint, GLvoid**);
#define CALL_GetPointerIndexedvEXT(disp, parameters) (* GET_GetPointerIndexedvEXT(disp)) parameters
#define GET_GetPointerIndexedvEXT(disp) ((_glptr_GetPointerIndexedvEXT)(GET_by_offset((disp), _gloffset_GetPointerIndexedvEXT)))
#define SET_GetPointerIndexedvEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint, GLvoid**) = func; \
   SET_by_offset(disp, _gloffset_GetPointerIndexedvEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexEnviEXT)(GLenum, GLenum, GLenum, GLint);
#define CALL_MultiTexEnviEXT(disp, parameters) (* GET_MultiTexEnviEXT(disp)) parameters
#define GET_MultiTexEnviEXT(disp) ((_glptr_MultiTexEnviEXT)(GET_by_offset((disp), _gloffset_MultiTexEnviEXT)))
#define SET_MultiTexEnviEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLenum, GLint) = func; \
   SET_by_offset(disp, _gloffset_MultiTexEnviEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexEnvivEXT)(GLenum, GLenum, GLenum, const GLint *);
#define CALL_MultiTexEnvivEXT(disp, parameters) (* GET_MultiTexEnvivEXT(disp)) parameters
#define GET_MultiTexEnvivEXT(disp) ((_glptr_MultiTexEnvivEXT)(GET_by_offset((disp), _gloffset_MultiTexEnvivEXT)))
#define SET_MultiTexEnvivEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLenum, const GLint *) = func; \
   SET_by_offset(disp, _gloffset_MultiTexEnvivEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexEnvfEXT)(GLenum, GLenum, GLenum, GLfloat);
#define CALL_MultiTexEnvfEXT(disp, parameters) (* GET_MultiTexEnvfEXT(disp)) parameters
#define GET_MultiTexEnvfEXT(disp) ((_glptr_MultiTexEnvfEXT)(GET_by_offset((disp), _gloffset_MultiTexEnvfEXT)))
#define SET_MultiTexEnvfEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLenum, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_MultiTexEnvfEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexEnvfvEXT)(GLenum, GLenum, GLenum, const GLfloat *);
#define CALL_MultiTexEnvfvEXT(disp, parameters) (* GET_MultiTexEnvfvEXT(disp)) parameters
#define GET_MultiTexEnvfvEXT(disp) ((_glptr_MultiTexEnvfvEXT)(GET_by_offset((disp), _gloffset_MultiTexEnvfvEXT)))
#define SET_MultiTexEnvfvEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLenum, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_MultiTexEnvfvEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetMultiTexEnvivEXT)(GLenum, GLenum, GLenum, GLint *);
#define CALL_GetMultiTexEnvivEXT(disp, parameters) (* GET_GetMultiTexEnvivEXT(disp)) parameters
#define GET_GetMultiTexEnvivEXT(disp) ((_glptr_GetMultiTexEnvivEXT)(GET_by_offset((disp), _gloffset_GetMultiTexEnvivEXT)))
#define SET_GetMultiTexEnvivEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetMultiTexEnvivEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetMultiTexEnvfvEXT)(GLenum, GLenum, GLenum, GLfloat *);
#define CALL_GetMultiTexEnvfvEXT(disp, parameters) (* GET_GetMultiTexEnvfvEXT(disp)) parameters
#define GET_GetMultiTexEnvfvEXT(disp) ((_glptr_GetMultiTexEnvfvEXT)(GET_by_offset((disp), _gloffset_GetMultiTexEnvfvEXT)))
#define SET_GetMultiTexEnvfvEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLenum, GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_GetMultiTexEnvfvEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexParameteriEXT)(GLenum, GLenum, GLenum, GLint);
#define CALL_MultiTexParameteriEXT(disp, parameters) (* GET_MultiTexParameteriEXT(disp)) parameters
#define GET_MultiTexParameteriEXT(disp) ((_glptr_MultiTexParameteriEXT)(GET_by_offset((disp), _gloffset_MultiTexParameteriEXT)))
#define SET_MultiTexParameteriEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLenum, GLint) = func; \
   SET_by_offset(disp, _gloffset_MultiTexParameteriEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexParameterivEXT)(GLenum, GLenum, GLenum, const GLint*);
#define CALL_MultiTexParameterivEXT(disp, parameters) (* GET_MultiTexParameterivEXT(disp)) parameters
#define GET_MultiTexParameterivEXT(disp) ((_glptr_MultiTexParameterivEXT)(GET_by_offset((disp), _gloffset_MultiTexParameterivEXT)))
#define SET_MultiTexParameterivEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLenum, const GLint*) = func; \
   SET_by_offset(disp, _gloffset_MultiTexParameterivEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexParameterfEXT)(GLenum, GLenum, GLenum, GLfloat);
#define CALL_MultiTexParameterfEXT(disp, parameters) (* GET_MultiTexParameterfEXT(disp)) parameters
#define GET_MultiTexParameterfEXT(disp) ((_glptr_MultiTexParameterfEXT)(GET_by_offset((disp), _gloffset_MultiTexParameterfEXT)))
#define SET_MultiTexParameterfEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLenum, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_MultiTexParameterfEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexParameterfvEXT)(GLenum, GLenum, GLenum, const GLfloat*);
#define CALL_MultiTexParameterfvEXT(disp, parameters) (* GET_MultiTexParameterfvEXT(disp)) parameters
#define GET_MultiTexParameterfvEXT(disp) ((_glptr_MultiTexParameterfvEXT)(GET_by_offset((disp), _gloffset_MultiTexParameterfvEXT)))
#define SET_MultiTexParameterfvEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLenum, const GLfloat*) = func; \
   SET_by_offset(disp, _gloffset_MultiTexParameterfvEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetMultiTexImageEXT)(GLenum, GLenum, GLint, GLenum, GLenum, GLvoid*);
#define CALL_GetMultiTexImageEXT(disp, parameters) (* GET_GetMultiTexImageEXT(disp)) parameters
#define GET_GetMultiTexImageEXT(disp) ((_glptr_GetMultiTexImageEXT)(GET_by_offset((disp), _gloffset_GetMultiTexImageEXT)))
#define SET_GetMultiTexImageEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLint, GLenum, GLenum, GLvoid*) = func; \
   SET_by_offset(disp, _gloffset_GetMultiTexImageEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexImage1DEXT)(GLenum, GLenum, GLint, GLint, GLsizei, GLint, GLenum, GLenum, const GLvoid*);
#define CALL_MultiTexImage1DEXT(disp, parameters) (* GET_MultiTexImage1DEXT(disp)) parameters
#define GET_MultiTexImage1DEXT(disp) ((_glptr_MultiTexImage1DEXT)(GET_by_offset((disp), _gloffset_MultiTexImage1DEXT)))
#define SET_MultiTexImage1DEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLint, GLint, GLsizei, GLint, GLenum, GLenum, const GLvoid*) = func; \
   SET_by_offset(disp, _gloffset_MultiTexImage1DEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexImage2DEXT)(GLenum, GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const GLvoid*);
#define CALL_MultiTexImage2DEXT(disp, parameters) (* GET_MultiTexImage2DEXT(disp)) parameters
#define GET_MultiTexImage2DEXT(disp) ((_glptr_MultiTexImage2DEXT)(GET_by_offset((disp), _gloffset_MultiTexImage2DEXT)))
#define SET_MultiTexImage2DEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const GLvoid*) = func; \
   SET_by_offset(disp, _gloffset_MultiTexImage2DEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexImage3DEXT)(GLenum, GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum, const GLvoid*);
#define CALL_MultiTexImage3DEXT(disp, parameters) (* GET_MultiTexImage3DEXT(disp)) parameters
#define GET_MultiTexImage3DEXT(disp) ((_glptr_MultiTexImage3DEXT)(GET_by_offset((disp), _gloffset_MultiTexImage3DEXT)))
#define SET_MultiTexImage3DEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum, const GLvoid*) = func; \
   SET_by_offset(disp, _gloffset_MultiTexImage3DEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexSubImage1DEXT)(GLenum, GLenum, GLint, GLint, GLsizei, GLenum, GLenum, const GLvoid*);
#define CALL_MultiTexSubImage1DEXT(disp, parameters) (* GET_MultiTexSubImage1DEXT(disp)) parameters
#define GET_MultiTexSubImage1DEXT(disp) ((_glptr_MultiTexSubImage1DEXT)(GET_by_offset((disp), _gloffset_MultiTexSubImage1DEXT)))
#define SET_MultiTexSubImage1DEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLint, GLint, GLsizei, GLenum, GLenum, const GLvoid*) = func; \
   SET_by_offset(disp, _gloffset_MultiTexSubImage1DEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexSubImage2DEXT)(GLenum, GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const GLvoid*);
#define CALL_MultiTexSubImage2DEXT(disp, parameters) (* GET_MultiTexSubImage2DEXT(disp)) parameters
#define GET_MultiTexSubImage2DEXT(disp) ((_glptr_MultiTexSubImage2DEXT)(GET_by_offset((disp), _gloffset_MultiTexSubImage2DEXT)))
#define SET_MultiTexSubImage2DEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const GLvoid*) = func; \
   SET_by_offset(disp, _gloffset_MultiTexSubImage2DEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexSubImage3DEXT)(GLenum, GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLenum, const GLvoid*);
#define CALL_MultiTexSubImage3DEXT(disp, parameters) (* GET_MultiTexSubImage3DEXT(disp)) parameters
#define GET_MultiTexSubImage3DEXT(disp) ((_glptr_MultiTexSubImage3DEXT)(GET_by_offset((disp), _gloffset_MultiTexSubImage3DEXT)))
#define SET_MultiTexSubImage3DEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLenum, const GLvoid*) = func; \
   SET_by_offset(disp, _gloffset_MultiTexSubImage3DEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetMultiTexParameterivEXT)(GLenum, GLenum, GLenum, GLint*);
#define CALL_GetMultiTexParameterivEXT(disp, parameters) (* GET_GetMultiTexParameterivEXT(disp)) parameters
#define GET_GetMultiTexParameterivEXT(disp) ((_glptr_GetMultiTexParameterivEXT)(GET_by_offset((disp), _gloffset_GetMultiTexParameterivEXT)))
#define SET_GetMultiTexParameterivEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLenum, GLint*) = func; \
   SET_by_offset(disp, _gloffset_GetMultiTexParameterivEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetMultiTexParameterfvEXT)(GLenum, GLenum, GLenum, GLfloat*);
#define CALL_GetMultiTexParameterfvEXT(disp, parameters) (* GET_GetMultiTexParameterfvEXT(disp)) parameters
#define GET_GetMultiTexParameterfvEXT(disp) ((_glptr_GetMultiTexParameterfvEXT)(GET_by_offset((disp), _gloffset_GetMultiTexParameterfvEXT)))
#define SET_GetMultiTexParameterfvEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLenum, GLfloat*) = func; \
   SET_by_offset(disp, _gloffset_GetMultiTexParameterfvEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CopyMultiTexImage1DEXT)(GLenum, GLenum, GLint, GLenum, GLint, GLint, GLsizei, GLint);
#define CALL_CopyMultiTexImage1DEXT(disp, parameters) (* GET_CopyMultiTexImage1DEXT(disp)) parameters
#define GET_CopyMultiTexImage1DEXT(disp) ((_glptr_CopyMultiTexImage1DEXT)(GET_by_offset((disp), _gloffset_CopyMultiTexImage1DEXT)))
#define SET_CopyMultiTexImage1DEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLint, GLenum, GLint, GLint, GLsizei, GLint) = func; \
   SET_by_offset(disp, _gloffset_CopyMultiTexImage1DEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CopyMultiTexImage2DEXT)(GLenum, GLenum, GLint, GLenum, GLint, GLint, GLsizei, GLsizei, GLint);
#define CALL_CopyMultiTexImage2DEXT(disp, parameters) (* GET_CopyMultiTexImage2DEXT(disp)) parameters
#define GET_CopyMultiTexImage2DEXT(disp) ((_glptr_CopyMultiTexImage2DEXT)(GET_by_offset((disp), _gloffset_CopyMultiTexImage2DEXT)))
#define SET_CopyMultiTexImage2DEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLint, GLenum, GLint, GLint, GLsizei, GLsizei, GLint) = func; \
   SET_by_offset(disp, _gloffset_CopyMultiTexImage2DEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CopyMultiTexSubImage1DEXT)(GLenum, GLenum, GLint, GLint, GLint, GLint, GLsizei);
#define CALL_CopyMultiTexSubImage1DEXT(disp, parameters) (* GET_CopyMultiTexSubImage1DEXT(disp)) parameters
#define GET_CopyMultiTexSubImage1DEXT(disp) ((_glptr_CopyMultiTexSubImage1DEXT)(GET_by_offset((disp), _gloffset_CopyMultiTexSubImage1DEXT)))
#define SET_CopyMultiTexSubImage1DEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLint, GLint, GLint, GLint, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_CopyMultiTexSubImage1DEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CopyMultiTexSubImage2DEXT)(GLenum, GLenum, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei);
#define CALL_CopyMultiTexSubImage2DEXT(disp, parameters) (* GET_CopyMultiTexSubImage2DEXT(disp)) parameters
#define GET_CopyMultiTexSubImage2DEXT(disp) ((_glptr_CopyMultiTexSubImage2DEXT)(GET_by_offset((disp), _gloffset_CopyMultiTexSubImage2DEXT)))
#define SET_CopyMultiTexSubImage2DEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_CopyMultiTexSubImage2DEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CopyMultiTexSubImage3DEXT)(GLenum, GLenum, GLint, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei);
#define CALL_CopyMultiTexSubImage3DEXT(disp, parameters) (* GET_CopyMultiTexSubImage3DEXT(disp)) parameters
#define GET_CopyMultiTexSubImage3DEXT(disp) ((_glptr_CopyMultiTexSubImage3DEXT)(GET_by_offset((disp), _gloffset_CopyMultiTexSubImage3DEXT)))
#define SET_CopyMultiTexSubImage3DEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLint, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_CopyMultiTexSubImage3DEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexGendEXT)(GLenum, GLenum, GLenum, GLdouble);
#define CALL_MultiTexGendEXT(disp, parameters) (* GET_MultiTexGendEXT(disp)) parameters
#define GET_MultiTexGendEXT(disp) ((_glptr_MultiTexGendEXT)(GET_by_offset((disp), _gloffset_MultiTexGendEXT)))
#define SET_MultiTexGendEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLenum, GLdouble) = func; \
   SET_by_offset(disp, _gloffset_MultiTexGendEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexGendvEXT)(GLenum, GLenum, GLenum, const GLdouble*);
#define CALL_MultiTexGendvEXT(disp, parameters) (* GET_MultiTexGendvEXT(disp)) parameters
#define GET_MultiTexGendvEXT(disp) ((_glptr_MultiTexGendvEXT)(GET_by_offset((disp), _gloffset_MultiTexGendvEXT)))
#define SET_MultiTexGendvEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLenum, const GLdouble*) = func; \
   SET_by_offset(disp, _gloffset_MultiTexGendvEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexGenfEXT)(GLenum, GLenum, GLenum, GLfloat);
#define CALL_MultiTexGenfEXT(disp, parameters) (* GET_MultiTexGenfEXT(disp)) parameters
#define GET_MultiTexGenfEXT(disp) ((_glptr_MultiTexGenfEXT)(GET_by_offset((disp), _gloffset_MultiTexGenfEXT)))
#define SET_MultiTexGenfEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLenum, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_MultiTexGenfEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexGenfvEXT)(GLenum, GLenum, GLenum, const GLfloat *);
#define CALL_MultiTexGenfvEXT(disp, parameters) (* GET_MultiTexGenfvEXT(disp)) parameters
#define GET_MultiTexGenfvEXT(disp) ((_glptr_MultiTexGenfvEXT)(GET_by_offset((disp), _gloffset_MultiTexGenfvEXT)))
#define SET_MultiTexGenfvEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLenum, const GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_MultiTexGenfvEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexGeniEXT)(GLenum, GLenum, GLenum, GLint);
#define CALL_MultiTexGeniEXT(disp, parameters) (* GET_MultiTexGeniEXT(disp)) parameters
#define GET_MultiTexGeniEXT(disp) ((_glptr_MultiTexGeniEXT)(GET_by_offset((disp), _gloffset_MultiTexGeniEXT)))
#define SET_MultiTexGeniEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLenum, GLint) = func; \
   SET_by_offset(disp, _gloffset_MultiTexGeniEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexGenivEXT)(GLenum, GLenum, GLenum, const GLint *);
#define CALL_MultiTexGenivEXT(disp, parameters) (* GET_MultiTexGenivEXT(disp)) parameters
#define GET_MultiTexGenivEXT(disp) ((_glptr_MultiTexGenivEXT)(GET_by_offset((disp), _gloffset_MultiTexGenivEXT)))
#define SET_MultiTexGenivEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLenum, const GLint *) = func; \
   SET_by_offset(disp, _gloffset_MultiTexGenivEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetMultiTexGendvEXT)(GLenum, GLenum, GLenum, GLdouble *);
#define CALL_GetMultiTexGendvEXT(disp, parameters) (* GET_GetMultiTexGendvEXT(disp)) parameters
#define GET_GetMultiTexGendvEXT(disp) ((_glptr_GetMultiTexGendvEXT)(GET_by_offset((disp), _gloffset_GetMultiTexGendvEXT)))
#define SET_GetMultiTexGendvEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLenum, GLdouble *) = func; \
   SET_by_offset(disp, _gloffset_GetMultiTexGendvEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetMultiTexGenfvEXT)(GLenum, GLenum, GLenum, GLfloat *);
#define CALL_GetMultiTexGenfvEXT(disp, parameters) (* GET_GetMultiTexGenfvEXT(disp)) parameters
#define GET_GetMultiTexGenfvEXT(disp) ((_glptr_GetMultiTexGenfvEXT)(GET_by_offset((disp), _gloffset_GetMultiTexGenfvEXT)))
#define SET_GetMultiTexGenfvEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLenum, GLfloat *) = func; \
   SET_by_offset(disp, _gloffset_GetMultiTexGenfvEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetMultiTexGenivEXT)(GLenum, GLenum, GLenum, GLint *);
#define CALL_GetMultiTexGenivEXT(disp, parameters) (* GET_GetMultiTexGenivEXT(disp)) parameters
#define GET_GetMultiTexGenivEXT(disp) ((_glptr_GetMultiTexGenivEXT)(GET_by_offset((disp), _gloffset_GetMultiTexGenivEXT)))
#define SET_GetMultiTexGenivEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetMultiTexGenivEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexCoordPointerEXT)(GLenum, GLint, GLenum, GLsizei, const GLvoid *);
#define CALL_MultiTexCoordPointerEXT(disp, parameters) (* GET_MultiTexCoordPointerEXT(disp)) parameters
#define GET_MultiTexCoordPointerEXT(disp) ((_glptr_MultiTexCoordPointerEXT)(GET_by_offset((disp), _gloffset_MultiTexCoordPointerEXT)))
#define SET_MultiTexCoordPointerEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLint, GLenum, GLsizei, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_MultiTexCoordPointerEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_BindImageTextureEXT)(GLuint, GLuint, GLint, GLboolean, GLint, GLenum, GLint);
#define CALL_BindImageTextureEXT(disp, parameters) (* GET_BindImageTextureEXT(disp)) parameters
#define GET_BindImageTextureEXT(disp) ((_glptr_BindImageTextureEXT)(GET_by_offset((disp), _gloffset_BindImageTextureEXT)))
#define SET_BindImageTextureEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLint, GLboolean, GLint, GLenum, GLint) = func; \
   SET_by_offset(disp, _gloffset_BindImageTextureEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CompressedTextureImage1DEXT)(GLuint, GLenum, GLint, GLenum, GLsizei, GLsizei, GLsizei, const GLvoid *);
#define CALL_CompressedTextureImage1DEXT(disp, parameters) (* GET_CompressedTextureImage1DEXT(disp)) parameters
#define GET_CompressedTextureImage1DEXT(disp) ((_glptr_CompressedTextureImage1DEXT)(GET_by_offset((disp), _gloffset_CompressedTextureImage1DEXT)))
#define SET_CompressedTextureImage1DEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLint, GLenum, GLsizei, GLsizei, GLsizei, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_CompressedTextureImage1DEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CompressedTextureImage2DEXT)(GLuint, GLenum, GLint, GLenum, GLsizei, GLsizei, GLsizei, GLsizei, const GLvoid *);
#define CALL_CompressedTextureImage2DEXT(disp, parameters) (* GET_CompressedTextureImage2DEXT(disp)) parameters
#define GET_CompressedTextureImage2DEXT(disp) ((_glptr_CompressedTextureImage2DEXT)(GET_by_offset((disp), _gloffset_CompressedTextureImage2DEXT)))
#define SET_CompressedTextureImage2DEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLint, GLenum, GLsizei, GLsizei, GLsizei, GLsizei, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_CompressedTextureImage2DEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CompressedTextureImage3DEXT)(GLuint, GLenum, GLint, GLenum, GLsizei, GLsizei, GLsizei, GLsizei, GLsizei, const GLvoid *);
#define CALL_CompressedTextureImage3DEXT(disp, parameters) (* GET_CompressedTextureImage3DEXT(disp)) parameters
#define GET_CompressedTextureImage3DEXT(disp) ((_glptr_CompressedTextureImage3DEXT)(GET_by_offset((disp), _gloffset_CompressedTextureImage3DEXT)))
#define SET_CompressedTextureImage3DEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLint, GLenum, GLsizei, GLsizei, GLsizei, GLsizei, GLsizei, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_CompressedTextureImage3DEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CompressedTextureSubImage1DEXT)(GLuint, GLenum, GLint, GLint, GLsizei, GLenum, GLsizei, const GLvoid *);
#define CALL_CompressedTextureSubImage1DEXT(disp, parameters) (* GET_CompressedTextureSubImage1DEXT(disp)) parameters
#define GET_CompressedTextureSubImage1DEXT(disp) ((_glptr_CompressedTextureSubImage1DEXT)(GET_by_offset((disp), _gloffset_CompressedTextureSubImage1DEXT)))
#define SET_CompressedTextureSubImage1DEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLint, GLint, GLsizei, GLenum, GLsizei, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_CompressedTextureSubImage1DEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CompressedTextureSubImage2DEXT)(GLuint, GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLsizei, const GLvoid *);
#define CALL_CompressedTextureSubImage2DEXT(disp, parameters) (* GET_CompressedTextureSubImage2DEXT(disp)) parameters
#define GET_CompressedTextureSubImage2DEXT(disp) ((_glptr_CompressedTextureSubImage2DEXT)(GET_by_offset((disp), _gloffset_CompressedTextureSubImage2DEXT)))
#define SET_CompressedTextureSubImage2DEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLsizei, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_CompressedTextureSubImage2DEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CompressedTextureSubImage3DEXT)(GLuint, GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLsizei, const GLvoid *);
#define CALL_CompressedTextureSubImage3DEXT(disp, parameters) (* GET_CompressedTextureSubImage3DEXT(disp)) parameters
#define GET_CompressedTextureSubImage3DEXT(disp) ((_glptr_CompressedTextureSubImage3DEXT)(GET_by_offset((disp), _gloffset_CompressedTextureSubImage3DEXT)))
#define SET_CompressedTextureSubImage3DEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLsizei, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_CompressedTextureSubImage3DEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetCompressedTextureImageEXT)(GLuint, GLenum, GLint, GLvoid *);
#define CALL_GetCompressedTextureImageEXT(disp, parameters) (* GET_GetCompressedTextureImageEXT(disp)) parameters
#define GET_GetCompressedTextureImageEXT(disp) ((_glptr_GetCompressedTextureImageEXT)(GET_by_offset((disp), _gloffset_GetCompressedTextureImageEXT)))
#define SET_GetCompressedTextureImageEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLint, GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_GetCompressedTextureImageEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CompressedMultiTexImage1DEXT)(GLenum, GLenum, GLint, GLenum, GLsizei, GLsizei, GLsizei, const GLvoid *);
#define CALL_CompressedMultiTexImage1DEXT(disp, parameters) (* GET_CompressedMultiTexImage1DEXT(disp)) parameters
#define GET_CompressedMultiTexImage1DEXT(disp) ((_glptr_CompressedMultiTexImage1DEXT)(GET_by_offset((disp), _gloffset_CompressedMultiTexImage1DEXT)))
#define SET_CompressedMultiTexImage1DEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLint, GLenum, GLsizei, GLsizei, GLsizei, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_CompressedMultiTexImage1DEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CompressedMultiTexImage2DEXT)(GLenum, GLenum, GLint, GLenum, GLsizei, GLsizei, GLsizei, GLsizei, const GLvoid *);
#define CALL_CompressedMultiTexImage2DEXT(disp, parameters) (* GET_CompressedMultiTexImage2DEXT(disp)) parameters
#define GET_CompressedMultiTexImage2DEXT(disp) ((_glptr_CompressedMultiTexImage2DEXT)(GET_by_offset((disp), _gloffset_CompressedMultiTexImage2DEXT)))
#define SET_CompressedMultiTexImage2DEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLint, GLenum, GLsizei, GLsizei, GLsizei, GLsizei, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_CompressedMultiTexImage2DEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CompressedMultiTexImage3DEXT)(GLenum, GLenum, GLint, GLenum, GLsizei, GLsizei, GLsizei, GLsizei, GLsizei, const GLvoid *);
#define CALL_CompressedMultiTexImage3DEXT(disp, parameters) (* GET_CompressedMultiTexImage3DEXT(disp)) parameters
#define GET_CompressedMultiTexImage3DEXT(disp) ((_glptr_CompressedMultiTexImage3DEXT)(GET_by_offset((disp), _gloffset_CompressedMultiTexImage3DEXT)))
#define SET_CompressedMultiTexImage3DEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLint, GLenum, GLsizei, GLsizei, GLsizei, GLsizei, GLsizei, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_CompressedMultiTexImage3DEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CompressedMultiTexSubImage1DEXT)(GLenum, GLenum, GLint, GLint, GLsizei, GLenum, GLsizei, const GLvoid *);
#define CALL_CompressedMultiTexSubImage1DEXT(disp, parameters) (* GET_CompressedMultiTexSubImage1DEXT(disp)) parameters
#define GET_CompressedMultiTexSubImage1DEXT(disp) ((_glptr_CompressedMultiTexSubImage1DEXT)(GET_by_offset((disp), _gloffset_CompressedMultiTexSubImage1DEXT)))
#define SET_CompressedMultiTexSubImage1DEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLint, GLint, GLsizei, GLenum, GLsizei, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_CompressedMultiTexSubImage1DEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CompressedMultiTexSubImage2DEXT)(GLenum, GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLsizei, const GLvoid *);
#define CALL_CompressedMultiTexSubImage2DEXT(disp, parameters) (* GET_CompressedMultiTexSubImage2DEXT(disp)) parameters
#define GET_CompressedMultiTexSubImage2DEXT(disp) ((_glptr_CompressedMultiTexSubImage2DEXT)(GET_by_offset((disp), _gloffset_CompressedMultiTexSubImage2DEXT)))
#define SET_CompressedMultiTexSubImage2DEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLsizei, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_CompressedMultiTexSubImage2DEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CompressedMultiTexSubImage3DEXT)(GLenum, GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLsizei, const GLvoid *);
#define CALL_CompressedMultiTexSubImage3DEXT(disp, parameters) (* GET_CompressedMultiTexSubImage3DEXT(disp)) parameters
#define GET_CompressedMultiTexSubImage3DEXT(disp) ((_glptr_CompressedMultiTexSubImage3DEXT)(GET_by_offset((disp), _gloffset_CompressedMultiTexSubImage3DEXT)))
#define SET_CompressedMultiTexSubImage3DEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLsizei, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_CompressedMultiTexSubImage3DEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetCompressedMultiTexImageEXT)(GLenum, GLenum, GLint, GLvoid *);
#define CALL_GetCompressedMultiTexImageEXT(disp, parameters) (* GET_GetCompressedMultiTexImageEXT(disp)) parameters
#define GET_GetCompressedMultiTexImageEXT(disp) ((_glptr_GetCompressedMultiTexImageEXT)(GET_by_offset((disp), _gloffset_GetCompressedMultiTexImageEXT)))
#define SET_GetCompressedMultiTexImageEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLint, GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_GetCompressedMultiTexImageEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetMultiTexLevelParameterivEXT)(GLenum, GLenum, GLint, GLenum, GLint*);
#define CALL_GetMultiTexLevelParameterivEXT(disp, parameters) (* GET_GetMultiTexLevelParameterivEXT(disp)) parameters
#define GET_GetMultiTexLevelParameterivEXT(disp) ((_glptr_GetMultiTexLevelParameterivEXT)(GET_by_offset((disp), _gloffset_GetMultiTexLevelParameterivEXT)))
#define SET_GetMultiTexLevelParameterivEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLint, GLenum, GLint*) = func; \
   SET_by_offset(disp, _gloffset_GetMultiTexLevelParameterivEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetMultiTexLevelParameterfvEXT)(GLenum, GLenum, GLint, GLenum, GLfloat*);
#define CALL_GetMultiTexLevelParameterfvEXT(disp, parameters) (* GET_GetMultiTexLevelParameterfvEXT(disp)) parameters
#define GET_GetMultiTexLevelParameterfvEXT(disp) ((_glptr_GetMultiTexLevelParameterfvEXT)(GET_by_offset((disp), _gloffset_GetMultiTexLevelParameterfvEXT)))
#define SET_GetMultiTexLevelParameterfvEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLint, GLenum, GLfloat*) = func; \
   SET_by_offset(disp, _gloffset_GetMultiTexLevelParameterfvEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_FramebufferParameteriMESA)(GLenum, GLenum, GLint);
#define CALL_FramebufferParameteriMESA(disp, parameters) (* GET_FramebufferParameteriMESA(disp)) parameters
#define GET_FramebufferParameteriMESA(disp) ((_glptr_FramebufferParameteriMESA)(GET_by_offset((disp), _gloffset_FramebufferParameteriMESA)))
#define SET_FramebufferParameteriMESA(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLint) = func; \
   SET_by_offset(disp, _gloffset_FramebufferParameteriMESA, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetFramebufferParameterivMESA)(GLenum, GLenum, GLint *);
#define CALL_GetFramebufferParameterivMESA(disp, parameters) (* GET_GetFramebufferParameterivMESA(disp)) parameters
#define GET_GetFramebufferParameterivMESA(disp) ((_glptr_GetFramebufferParameterivMESA)(GET_by_offset((disp), _gloffset_GetFramebufferParameterivMESA)))
#define SET_GetFramebufferParameterivMESA(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetFramebufferParameterivMESA, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_NamedRenderbufferStorageEXT)(GLuint, GLenum, GLsizei, GLsizei);
#define CALL_NamedRenderbufferStorageEXT(disp, parameters) (* GET_NamedRenderbufferStorageEXT(disp)) parameters
#define GET_NamedRenderbufferStorageEXT(disp) ((_glptr_NamedRenderbufferStorageEXT)(GET_by_offset((disp), _gloffset_NamedRenderbufferStorageEXT)))
#define SET_NamedRenderbufferStorageEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLsizei, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_NamedRenderbufferStorageEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetNamedRenderbufferParameterivEXT)(GLuint, GLenum, GLint *);
#define CALL_GetNamedRenderbufferParameterivEXT(disp, parameters) (* GET_GetNamedRenderbufferParameterivEXT(disp)) parameters
#define GET_GetNamedRenderbufferParameterivEXT(disp) ((_glptr_GetNamedRenderbufferParameterivEXT)(GET_by_offset((disp), _gloffset_GetNamedRenderbufferParameterivEXT)))
#define SET_GetNamedRenderbufferParameterivEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetNamedRenderbufferParameterivEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ClientAttribDefaultEXT)(GLbitfield);
#define CALL_ClientAttribDefaultEXT(disp, parameters) (* GET_ClientAttribDefaultEXT(disp)) parameters
#define GET_ClientAttribDefaultEXT(disp) ((_glptr_ClientAttribDefaultEXT)(GET_by_offset((disp), _gloffset_ClientAttribDefaultEXT)))
#define SET_ClientAttribDefaultEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLbitfield) = func; \
   SET_by_offset(disp, _gloffset_ClientAttribDefaultEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_PushClientAttribDefaultEXT)(GLbitfield);
#define CALL_PushClientAttribDefaultEXT(disp, parameters) (* GET_PushClientAttribDefaultEXT(disp)) parameters
#define GET_PushClientAttribDefaultEXT(disp) ((_glptr_PushClientAttribDefaultEXT)(GET_by_offset((disp), _gloffset_PushClientAttribDefaultEXT)))
#define SET_PushClientAttribDefaultEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLbitfield) = func; \
   SET_by_offset(disp, _gloffset_PushClientAttribDefaultEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_NamedProgramStringEXT)(GLuint, GLenum, GLenum, GLsizei, const GLvoid*);
#define CALL_NamedProgramStringEXT(disp, parameters) (* GET_NamedProgramStringEXT(disp)) parameters
#define GET_NamedProgramStringEXT(disp) ((_glptr_NamedProgramStringEXT)(GET_by_offset((disp), _gloffset_NamedProgramStringEXT)))
#define SET_NamedProgramStringEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLenum, GLsizei, const GLvoid*) = func; \
   SET_by_offset(disp, _gloffset_NamedProgramStringEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetNamedProgramStringEXT)(GLuint, GLenum, GLenum, GLvoid*);
#define CALL_GetNamedProgramStringEXT(disp, parameters) (* GET_GetNamedProgramStringEXT(disp)) parameters
#define GET_GetNamedProgramStringEXT(disp) ((_glptr_GetNamedProgramStringEXT)(GET_by_offset((disp), _gloffset_GetNamedProgramStringEXT)))
#define SET_GetNamedProgramStringEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLenum, GLvoid*) = func; \
   SET_by_offset(disp, _gloffset_GetNamedProgramStringEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_NamedProgramLocalParameter4fEXT)(GLuint, GLenum, GLuint, GLfloat, GLfloat, GLfloat, GLfloat);
#define CALL_NamedProgramLocalParameter4fEXT(disp, parameters) (* GET_NamedProgramLocalParameter4fEXT(disp)) parameters
#define GET_NamedProgramLocalParameter4fEXT(disp) ((_glptr_NamedProgramLocalParameter4fEXT)(GET_by_offset((disp), _gloffset_NamedProgramLocalParameter4fEXT)))
#define SET_NamedProgramLocalParameter4fEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLuint, GLfloat, GLfloat, GLfloat, GLfloat) = func; \
   SET_by_offset(disp, _gloffset_NamedProgramLocalParameter4fEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_NamedProgramLocalParameter4fvEXT)(GLuint, GLenum, GLuint, const GLfloat*);
#define CALL_NamedProgramLocalParameter4fvEXT(disp, parameters) (* GET_NamedProgramLocalParameter4fvEXT(disp)) parameters
#define GET_NamedProgramLocalParameter4fvEXT(disp) ((_glptr_NamedProgramLocalParameter4fvEXT)(GET_by_offset((disp), _gloffset_NamedProgramLocalParameter4fvEXT)))
#define SET_NamedProgramLocalParameter4fvEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLuint, const GLfloat*) = func; \
   SET_by_offset(disp, _gloffset_NamedProgramLocalParameter4fvEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetNamedProgramLocalParameterfvEXT)(GLuint, GLenum, GLuint, GLfloat*);
#define CALL_GetNamedProgramLocalParameterfvEXT(disp, parameters) (* GET_GetNamedProgramLocalParameterfvEXT(disp)) parameters
#define GET_GetNamedProgramLocalParameterfvEXT(disp) ((_glptr_GetNamedProgramLocalParameterfvEXT)(GET_by_offset((disp), _gloffset_GetNamedProgramLocalParameterfvEXT)))
#define SET_GetNamedProgramLocalParameterfvEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLuint, GLfloat*) = func; \
   SET_by_offset(disp, _gloffset_GetNamedProgramLocalParameterfvEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_NamedProgramLocalParameter4dEXT)(GLuint, GLenum, GLuint, GLdouble, GLdouble, GLdouble, GLdouble);
#define CALL_NamedProgramLocalParameter4dEXT(disp, parameters) (* GET_NamedProgramLocalParameter4dEXT(disp)) parameters
#define GET_NamedProgramLocalParameter4dEXT(disp) ((_glptr_NamedProgramLocalParameter4dEXT)(GET_by_offset((disp), _gloffset_NamedProgramLocalParameter4dEXT)))
#define SET_NamedProgramLocalParameter4dEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLuint, GLdouble, GLdouble, GLdouble, GLdouble) = func; \
   SET_by_offset(disp, _gloffset_NamedProgramLocalParameter4dEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_NamedProgramLocalParameter4dvEXT)(GLuint, GLenum, GLuint, const GLdouble*);
#define CALL_NamedProgramLocalParameter4dvEXT(disp, parameters) (* GET_NamedProgramLocalParameter4dvEXT(disp)) parameters
#define GET_NamedProgramLocalParameter4dvEXT(disp) ((_glptr_NamedProgramLocalParameter4dvEXT)(GET_by_offset((disp), _gloffset_NamedProgramLocalParameter4dvEXT)))
#define SET_NamedProgramLocalParameter4dvEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLuint, const GLdouble*) = func; \
   SET_by_offset(disp, _gloffset_NamedProgramLocalParameter4dvEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetNamedProgramLocalParameterdvEXT)(GLuint, GLenum, GLuint, GLdouble*);
#define CALL_GetNamedProgramLocalParameterdvEXT(disp, parameters) (* GET_GetNamedProgramLocalParameterdvEXT(disp)) parameters
#define GET_GetNamedProgramLocalParameterdvEXT(disp) ((_glptr_GetNamedProgramLocalParameterdvEXT)(GET_by_offset((disp), _gloffset_GetNamedProgramLocalParameterdvEXT)))
#define SET_GetNamedProgramLocalParameterdvEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLuint, GLdouble*) = func; \
   SET_by_offset(disp, _gloffset_GetNamedProgramLocalParameterdvEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetNamedProgramivEXT)(GLuint, GLenum, GLenum, GLint*);
#define CALL_GetNamedProgramivEXT(disp, parameters) (* GET_GetNamedProgramivEXT(disp)) parameters
#define GET_GetNamedProgramivEXT(disp) ((_glptr_GetNamedProgramivEXT)(GET_by_offset((disp), _gloffset_GetNamedProgramivEXT)))
#define SET_GetNamedProgramivEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLenum, GLint*) = func; \
   SET_by_offset(disp, _gloffset_GetNamedProgramivEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TextureBufferEXT)(GLuint, GLenum, GLenum, GLuint);
#define CALL_TextureBufferEXT(disp, parameters) (* GET_TextureBufferEXT(disp)) parameters
#define GET_TextureBufferEXT(disp) ((_glptr_TextureBufferEXT)(GET_by_offset((disp), _gloffset_TextureBufferEXT)))
#define SET_TextureBufferEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLenum, GLuint) = func; \
   SET_by_offset(disp, _gloffset_TextureBufferEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexBufferEXT)(GLenum, GLenum, GLenum, GLuint);
#define CALL_MultiTexBufferEXT(disp, parameters) (* GET_MultiTexBufferEXT(disp)) parameters
#define GET_MultiTexBufferEXT(disp) ((_glptr_MultiTexBufferEXT)(GET_by_offset((disp), _gloffset_MultiTexBufferEXT)))
#define SET_MultiTexBufferEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLenum, GLuint) = func; \
   SET_by_offset(disp, _gloffset_MultiTexBufferEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TextureParameterIivEXT)(GLuint, GLenum, GLenum, const GLint*);
#define CALL_TextureParameterIivEXT(disp, parameters) (* GET_TextureParameterIivEXT(disp)) parameters
#define GET_TextureParameterIivEXT(disp) ((_glptr_TextureParameterIivEXT)(GET_by_offset((disp), _gloffset_TextureParameterIivEXT)))
#define SET_TextureParameterIivEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLenum, const GLint*) = func; \
   SET_by_offset(disp, _gloffset_TextureParameterIivEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TextureParameterIuivEXT)(GLuint, GLenum, GLenum, const GLuint*);
#define CALL_TextureParameterIuivEXT(disp, parameters) (* GET_TextureParameterIuivEXT(disp)) parameters
#define GET_TextureParameterIuivEXT(disp) ((_glptr_TextureParameterIuivEXT)(GET_by_offset((disp), _gloffset_TextureParameterIuivEXT)))
#define SET_TextureParameterIuivEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLenum, const GLuint*) = func; \
   SET_by_offset(disp, _gloffset_TextureParameterIuivEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetTextureParameterIivEXT)(GLuint, GLenum, GLenum, GLint*);
#define CALL_GetTextureParameterIivEXT(disp, parameters) (* GET_GetTextureParameterIivEXT(disp)) parameters
#define GET_GetTextureParameterIivEXT(disp) ((_glptr_GetTextureParameterIivEXT)(GET_by_offset((disp), _gloffset_GetTextureParameterIivEXT)))
#define SET_GetTextureParameterIivEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLenum, GLint*) = func; \
   SET_by_offset(disp, _gloffset_GetTextureParameterIivEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetTextureParameterIuivEXT)(GLuint, GLenum, GLenum, GLuint*);
#define CALL_GetTextureParameterIuivEXT(disp, parameters) (* GET_GetTextureParameterIuivEXT(disp)) parameters
#define GET_GetTextureParameterIuivEXT(disp) ((_glptr_GetTextureParameterIuivEXT)(GET_by_offset((disp), _gloffset_GetTextureParameterIuivEXT)))
#define SET_GetTextureParameterIuivEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLenum, GLuint*) = func; \
   SET_by_offset(disp, _gloffset_GetTextureParameterIuivEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexParameterIivEXT)(GLenum, GLenum, GLenum, const GLint*);
#define CALL_MultiTexParameterIivEXT(disp, parameters) (* GET_MultiTexParameterIivEXT(disp)) parameters
#define GET_MultiTexParameterIivEXT(disp) ((_glptr_MultiTexParameterIivEXT)(GET_by_offset((disp), _gloffset_MultiTexParameterIivEXT)))
#define SET_MultiTexParameterIivEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLenum, const GLint*) = func; \
   SET_by_offset(disp, _gloffset_MultiTexParameterIivEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexParameterIuivEXT)(GLenum, GLenum, GLenum, const GLuint*);
#define CALL_MultiTexParameterIuivEXT(disp, parameters) (* GET_MultiTexParameterIuivEXT(disp)) parameters
#define GET_MultiTexParameterIuivEXT(disp) ((_glptr_MultiTexParameterIuivEXT)(GET_by_offset((disp), _gloffset_MultiTexParameterIuivEXT)))
#define SET_MultiTexParameterIuivEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLenum, const GLuint*) = func; \
   SET_by_offset(disp, _gloffset_MultiTexParameterIuivEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetMultiTexParameterIivEXT)(GLenum, GLenum, GLenum, GLint*);
#define CALL_GetMultiTexParameterIivEXT(disp, parameters) (* GET_GetMultiTexParameterIivEXT(disp)) parameters
#define GET_GetMultiTexParameterIivEXT(disp) ((_glptr_GetMultiTexParameterIivEXT)(GET_by_offset((disp), _gloffset_GetMultiTexParameterIivEXT)))
#define SET_GetMultiTexParameterIivEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLenum, GLint*) = func; \
   SET_by_offset(disp, _gloffset_GetMultiTexParameterIivEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetMultiTexParameterIuivEXT)(GLenum, GLenum, GLenum, GLuint*);
#define CALL_GetMultiTexParameterIuivEXT(disp, parameters) (* GET_GetMultiTexParameterIuivEXT(disp)) parameters
#define GET_GetMultiTexParameterIuivEXT(disp) ((_glptr_GetMultiTexParameterIuivEXT)(GET_by_offset((disp), _gloffset_GetMultiTexParameterIuivEXT)))
#define SET_GetMultiTexParameterIuivEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLenum, GLuint*) = func; \
   SET_by_offset(disp, _gloffset_GetMultiTexParameterIuivEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_NamedProgramLocalParameters4fvEXT)(GLuint, GLenum, GLuint, GLsizei, const GLfloat*);
#define CALL_NamedProgramLocalParameters4fvEXT(disp, parameters) (* GET_NamedProgramLocalParameters4fvEXT(disp)) parameters
#define GET_NamedProgramLocalParameters4fvEXT(disp) ((_glptr_NamedProgramLocalParameters4fvEXT)(GET_by_offset((disp), _gloffset_NamedProgramLocalParameters4fvEXT)))
#define SET_NamedProgramLocalParameters4fvEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLuint, GLsizei, const GLfloat*) = func; \
   SET_by_offset(disp, _gloffset_NamedProgramLocalParameters4fvEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GenerateTextureMipmapEXT)(GLuint, GLenum);
#define CALL_GenerateTextureMipmapEXT(disp, parameters) (* GET_GenerateTextureMipmapEXT(disp)) parameters
#define GET_GenerateTextureMipmapEXT(disp) ((_glptr_GenerateTextureMipmapEXT)(GET_by_offset((disp), _gloffset_GenerateTextureMipmapEXT)))
#define SET_GenerateTextureMipmapEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum) = func; \
   SET_by_offset(disp, _gloffset_GenerateTextureMipmapEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GenerateMultiTexMipmapEXT)(GLenum, GLenum);
#define CALL_GenerateMultiTexMipmapEXT(disp, parameters) (* GET_GenerateMultiTexMipmapEXT(disp)) parameters
#define GET_GenerateMultiTexMipmapEXT(disp) ((_glptr_GenerateMultiTexMipmapEXT)(GET_by_offset((disp), _gloffset_GenerateMultiTexMipmapEXT)))
#define SET_GenerateMultiTexMipmapEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum) = func; \
   SET_by_offset(disp, _gloffset_GenerateMultiTexMipmapEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_NamedRenderbufferStorageMultisampleEXT)(GLuint, GLsizei, GLenum, GLsizei, GLsizei);
#define CALL_NamedRenderbufferStorageMultisampleEXT(disp, parameters) (* GET_NamedRenderbufferStorageMultisampleEXT(disp)) parameters
#define GET_NamedRenderbufferStorageMultisampleEXT(disp) ((_glptr_NamedRenderbufferStorageMultisampleEXT)(GET_by_offset((disp), _gloffset_NamedRenderbufferStorageMultisampleEXT)))
#define SET_NamedRenderbufferStorageMultisampleEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizei, GLenum, GLsizei, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_NamedRenderbufferStorageMultisampleEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_NamedCopyBufferSubDataEXT)(GLuint, GLuint, GLintptr, GLintptr, GLsizeiptr);
#define CALL_NamedCopyBufferSubDataEXT(disp, parameters) (* GET_NamedCopyBufferSubDataEXT(disp)) parameters
#define GET_NamedCopyBufferSubDataEXT(disp) ((_glptr_NamedCopyBufferSubDataEXT)(GET_by_offset((disp), _gloffset_NamedCopyBufferSubDataEXT)))
#define SET_NamedCopyBufferSubDataEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLintptr, GLintptr, GLsizeiptr) = func; \
   SET_by_offset(disp, _gloffset_NamedCopyBufferSubDataEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexArrayVertexOffsetEXT)(GLuint, GLuint, GLint, GLenum, GLsizei, GLintptr);
#define CALL_VertexArrayVertexOffsetEXT(disp, parameters) (* GET_VertexArrayVertexOffsetEXT(disp)) parameters
#define GET_VertexArrayVertexOffsetEXT(disp) ((_glptr_VertexArrayVertexOffsetEXT)(GET_by_offset((disp), _gloffset_VertexArrayVertexOffsetEXT)))
#define SET_VertexArrayVertexOffsetEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLint, GLenum, GLsizei, GLintptr) = func; \
   SET_by_offset(disp, _gloffset_VertexArrayVertexOffsetEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexArrayColorOffsetEXT)(GLuint, GLuint, GLint, GLenum, GLsizei, GLintptr);
#define CALL_VertexArrayColorOffsetEXT(disp, parameters) (* GET_VertexArrayColorOffsetEXT(disp)) parameters
#define GET_VertexArrayColorOffsetEXT(disp) ((_glptr_VertexArrayColorOffsetEXT)(GET_by_offset((disp), _gloffset_VertexArrayColorOffsetEXT)))
#define SET_VertexArrayColorOffsetEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLint, GLenum, GLsizei, GLintptr) = func; \
   SET_by_offset(disp, _gloffset_VertexArrayColorOffsetEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexArrayEdgeFlagOffsetEXT)(GLuint, GLuint, GLsizei, GLintptr);
#define CALL_VertexArrayEdgeFlagOffsetEXT(disp, parameters) (* GET_VertexArrayEdgeFlagOffsetEXT(disp)) parameters
#define GET_VertexArrayEdgeFlagOffsetEXT(disp) ((_glptr_VertexArrayEdgeFlagOffsetEXT)(GET_by_offset((disp), _gloffset_VertexArrayEdgeFlagOffsetEXT)))
#define SET_VertexArrayEdgeFlagOffsetEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLsizei, GLintptr) = func; \
   SET_by_offset(disp, _gloffset_VertexArrayEdgeFlagOffsetEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexArrayIndexOffsetEXT)(GLuint, GLuint, GLenum, GLsizei, GLintptr);
#define CALL_VertexArrayIndexOffsetEXT(disp, parameters) (* GET_VertexArrayIndexOffsetEXT(disp)) parameters
#define GET_VertexArrayIndexOffsetEXT(disp) ((_glptr_VertexArrayIndexOffsetEXT)(GET_by_offset((disp), _gloffset_VertexArrayIndexOffsetEXT)))
#define SET_VertexArrayIndexOffsetEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLenum, GLsizei, GLintptr) = func; \
   SET_by_offset(disp, _gloffset_VertexArrayIndexOffsetEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexArrayNormalOffsetEXT)(GLuint, GLuint, GLenum, GLsizei, GLintptr);
#define CALL_VertexArrayNormalOffsetEXT(disp, parameters) (* GET_VertexArrayNormalOffsetEXT(disp)) parameters
#define GET_VertexArrayNormalOffsetEXT(disp) ((_glptr_VertexArrayNormalOffsetEXT)(GET_by_offset((disp), _gloffset_VertexArrayNormalOffsetEXT)))
#define SET_VertexArrayNormalOffsetEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLenum, GLsizei, GLintptr) = func; \
   SET_by_offset(disp, _gloffset_VertexArrayNormalOffsetEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexArrayTexCoordOffsetEXT)(GLuint, GLuint, GLint, GLenum, GLsizei, GLintptr);
#define CALL_VertexArrayTexCoordOffsetEXT(disp, parameters) (* GET_VertexArrayTexCoordOffsetEXT(disp)) parameters
#define GET_VertexArrayTexCoordOffsetEXT(disp) ((_glptr_VertexArrayTexCoordOffsetEXT)(GET_by_offset((disp), _gloffset_VertexArrayTexCoordOffsetEXT)))
#define SET_VertexArrayTexCoordOffsetEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLint, GLenum, GLsizei, GLintptr) = func; \
   SET_by_offset(disp, _gloffset_VertexArrayTexCoordOffsetEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexArrayMultiTexCoordOffsetEXT)(GLuint, GLuint, GLenum, GLint, GLenum, GLsizei, GLintptr);
#define CALL_VertexArrayMultiTexCoordOffsetEXT(disp, parameters) (* GET_VertexArrayMultiTexCoordOffsetEXT(disp)) parameters
#define GET_VertexArrayMultiTexCoordOffsetEXT(disp) ((_glptr_VertexArrayMultiTexCoordOffsetEXT)(GET_by_offset((disp), _gloffset_VertexArrayMultiTexCoordOffsetEXT)))
#define SET_VertexArrayMultiTexCoordOffsetEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLenum, GLint, GLenum, GLsizei, GLintptr) = func; \
   SET_by_offset(disp, _gloffset_VertexArrayMultiTexCoordOffsetEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexArrayFogCoordOffsetEXT)(GLuint, GLuint, GLenum, GLsizei, GLintptr);
#define CALL_VertexArrayFogCoordOffsetEXT(disp, parameters) (* GET_VertexArrayFogCoordOffsetEXT(disp)) parameters
#define GET_VertexArrayFogCoordOffsetEXT(disp) ((_glptr_VertexArrayFogCoordOffsetEXT)(GET_by_offset((disp), _gloffset_VertexArrayFogCoordOffsetEXT)))
#define SET_VertexArrayFogCoordOffsetEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLenum, GLsizei, GLintptr) = func; \
   SET_by_offset(disp, _gloffset_VertexArrayFogCoordOffsetEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexArraySecondaryColorOffsetEXT)(GLuint, GLuint, GLint, GLenum, GLsizei, GLintptr);
#define CALL_VertexArraySecondaryColorOffsetEXT(disp, parameters) (* GET_VertexArraySecondaryColorOffsetEXT(disp)) parameters
#define GET_VertexArraySecondaryColorOffsetEXT(disp) ((_glptr_VertexArraySecondaryColorOffsetEXT)(GET_by_offset((disp), _gloffset_VertexArraySecondaryColorOffsetEXT)))
#define SET_VertexArraySecondaryColorOffsetEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLint, GLenum, GLsizei, GLintptr) = func; \
   SET_by_offset(disp, _gloffset_VertexArraySecondaryColorOffsetEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexArrayVertexAttribOffsetEXT)(GLuint, GLuint, GLuint, GLint, GLenum, GLboolean, GLsizei, GLintptr);
#define CALL_VertexArrayVertexAttribOffsetEXT(disp, parameters) (* GET_VertexArrayVertexAttribOffsetEXT(disp)) parameters
#define GET_VertexArrayVertexAttribOffsetEXT(disp) ((_glptr_VertexArrayVertexAttribOffsetEXT)(GET_by_offset((disp), _gloffset_VertexArrayVertexAttribOffsetEXT)))
#define SET_VertexArrayVertexAttribOffsetEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLuint, GLint, GLenum, GLboolean, GLsizei, GLintptr) = func; \
   SET_by_offset(disp, _gloffset_VertexArrayVertexAttribOffsetEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexArrayVertexAttribIOffsetEXT)(GLuint, GLuint, GLuint, GLint, GLenum, GLsizei, GLintptr);
#define CALL_VertexArrayVertexAttribIOffsetEXT(disp, parameters) (* GET_VertexArrayVertexAttribIOffsetEXT(disp)) parameters
#define GET_VertexArrayVertexAttribIOffsetEXT(disp) ((_glptr_VertexArrayVertexAttribIOffsetEXT)(GET_by_offset((disp), _gloffset_VertexArrayVertexAttribIOffsetEXT)))
#define SET_VertexArrayVertexAttribIOffsetEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLuint, GLint, GLenum, GLsizei, GLintptr) = func; \
   SET_by_offset(disp, _gloffset_VertexArrayVertexAttribIOffsetEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_EnableVertexArrayEXT)(GLuint, GLenum);
#define CALL_EnableVertexArrayEXT(disp, parameters) (* GET_EnableVertexArrayEXT(disp)) parameters
#define GET_EnableVertexArrayEXT(disp) ((_glptr_EnableVertexArrayEXT)(GET_by_offset((disp), _gloffset_EnableVertexArrayEXT)))
#define SET_EnableVertexArrayEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum) = func; \
   SET_by_offset(disp, _gloffset_EnableVertexArrayEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DisableVertexArrayEXT)(GLuint, GLenum);
#define CALL_DisableVertexArrayEXT(disp, parameters) (* GET_DisableVertexArrayEXT(disp)) parameters
#define GET_DisableVertexArrayEXT(disp) ((_glptr_DisableVertexArrayEXT)(GET_by_offset((disp), _gloffset_DisableVertexArrayEXT)))
#define SET_DisableVertexArrayEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum) = func; \
   SET_by_offset(disp, _gloffset_DisableVertexArrayEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_EnableVertexArrayAttribEXT)(GLuint, GLuint);
#define CALL_EnableVertexArrayAttribEXT(disp, parameters) (* GET_EnableVertexArrayAttribEXT(disp)) parameters
#define GET_EnableVertexArrayAttribEXT(disp) ((_glptr_EnableVertexArrayAttribEXT)(GET_by_offset((disp), _gloffset_EnableVertexArrayAttribEXT)))
#define SET_EnableVertexArrayAttribEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint) = func; \
   SET_by_offset(disp, _gloffset_EnableVertexArrayAttribEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DisableVertexArrayAttribEXT)(GLuint, GLuint);
#define CALL_DisableVertexArrayAttribEXT(disp, parameters) (* GET_DisableVertexArrayAttribEXT(disp)) parameters
#define GET_DisableVertexArrayAttribEXT(disp) ((_glptr_DisableVertexArrayAttribEXT)(GET_by_offset((disp), _gloffset_DisableVertexArrayAttribEXT)))
#define SET_DisableVertexArrayAttribEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint) = func; \
   SET_by_offset(disp, _gloffset_DisableVertexArrayAttribEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetVertexArrayIntegervEXT)(GLuint, GLenum, GLint*);
#define CALL_GetVertexArrayIntegervEXT(disp, parameters) (* GET_GetVertexArrayIntegervEXT(disp)) parameters
#define GET_GetVertexArrayIntegervEXT(disp) ((_glptr_GetVertexArrayIntegervEXT)(GET_by_offset((disp), _gloffset_GetVertexArrayIntegervEXT)))
#define SET_GetVertexArrayIntegervEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLint*) = func; \
   SET_by_offset(disp, _gloffset_GetVertexArrayIntegervEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetVertexArrayPointervEXT)(GLuint, GLenum, GLvoid**);
#define CALL_GetVertexArrayPointervEXT(disp, parameters) (* GET_GetVertexArrayPointervEXT(disp)) parameters
#define GET_GetVertexArrayPointervEXT(disp) ((_glptr_GetVertexArrayPointervEXT)(GET_by_offset((disp), _gloffset_GetVertexArrayPointervEXT)))
#define SET_GetVertexArrayPointervEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLvoid**) = func; \
   SET_by_offset(disp, _gloffset_GetVertexArrayPointervEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetVertexArrayIntegeri_vEXT)(GLuint, GLuint, GLenum, GLint*);
#define CALL_GetVertexArrayIntegeri_vEXT(disp, parameters) (* GET_GetVertexArrayIntegeri_vEXT(disp)) parameters
#define GET_GetVertexArrayIntegeri_vEXT(disp) ((_glptr_GetVertexArrayIntegeri_vEXT)(GET_by_offset((disp), _gloffset_GetVertexArrayIntegeri_vEXT)))
#define SET_GetVertexArrayIntegeri_vEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLenum, GLint*) = func; \
   SET_by_offset(disp, _gloffset_GetVertexArrayIntegeri_vEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetVertexArrayPointeri_vEXT)(GLuint, GLuint, GLenum, GLvoid**);
#define CALL_GetVertexArrayPointeri_vEXT(disp, parameters) (* GET_GetVertexArrayPointeri_vEXT(disp)) parameters
#define GET_GetVertexArrayPointeri_vEXT(disp) ((_glptr_GetVertexArrayPointeri_vEXT)(GET_by_offset((disp), _gloffset_GetVertexArrayPointeri_vEXT)))
#define SET_GetVertexArrayPointeri_vEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLenum, GLvoid**) = func; \
   SET_by_offset(disp, _gloffset_GetVertexArrayPointeri_vEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ClearNamedBufferDataEXT)(GLuint, GLenum, GLenum, GLenum, const GLvoid *);
#define CALL_ClearNamedBufferDataEXT(disp, parameters) (* GET_ClearNamedBufferDataEXT(disp)) parameters
#define GET_ClearNamedBufferDataEXT(disp) ((_glptr_ClearNamedBufferDataEXT)(GET_by_offset((disp), _gloffset_ClearNamedBufferDataEXT)))
#define SET_ClearNamedBufferDataEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLenum, GLenum, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_ClearNamedBufferDataEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ClearNamedBufferSubDataEXT)(GLuint, GLenum, GLintptr, GLsizeiptr, GLenum, GLenum, const GLvoid *);
#define CALL_ClearNamedBufferSubDataEXT(disp, parameters) (* GET_ClearNamedBufferSubDataEXT(disp)) parameters
#define GET_ClearNamedBufferSubDataEXT(disp) ((_glptr_ClearNamedBufferSubDataEXT)(GET_by_offset((disp), _gloffset_ClearNamedBufferSubDataEXT)))
#define SET_ClearNamedBufferSubDataEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLintptr, GLsizeiptr, GLenum, GLenum, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_ClearNamedBufferSubDataEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_NamedFramebufferParameteriEXT)(GLuint, GLenum, GLint);
#define CALL_NamedFramebufferParameteriEXT(disp, parameters) (* GET_NamedFramebufferParameteriEXT(disp)) parameters
#define GET_NamedFramebufferParameteriEXT(disp) ((_glptr_NamedFramebufferParameteriEXT)(GET_by_offset((disp), _gloffset_NamedFramebufferParameteriEXT)))
#define SET_NamedFramebufferParameteriEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLint) = func; \
   SET_by_offset(disp, _gloffset_NamedFramebufferParameteriEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetNamedFramebufferParameterivEXT)(GLuint, GLenum, GLint*);
#define CALL_GetNamedFramebufferParameterivEXT(disp, parameters) (* GET_GetNamedFramebufferParameterivEXT(disp)) parameters
#define GET_GetNamedFramebufferParameterivEXT(disp) ((_glptr_GetNamedFramebufferParameterivEXT)(GET_by_offset((disp), _gloffset_GetNamedFramebufferParameterivEXT)))
#define SET_GetNamedFramebufferParameterivEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLint*) = func; \
   SET_by_offset(disp, _gloffset_GetNamedFramebufferParameterivEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexArrayVertexAttribLOffsetEXT)(GLuint, GLuint, GLuint, GLint, GLenum, GLsizei, GLintptr);
#define CALL_VertexArrayVertexAttribLOffsetEXT(disp, parameters) (* GET_VertexArrayVertexAttribLOffsetEXT(disp)) parameters
#define GET_VertexArrayVertexAttribLOffsetEXT(disp) ((_glptr_VertexArrayVertexAttribLOffsetEXT)(GET_by_offset((disp), _gloffset_VertexArrayVertexAttribLOffsetEXT)))
#define SET_VertexArrayVertexAttribLOffsetEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLuint, GLint, GLenum, GLsizei, GLintptr) = func; \
   SET_by_offset(disp, _gloffset_VertexArrayVertexAttribLOffsetEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexArrayVertexAttribDivisorEXT)(GLuint, GLuint, GLuint);
#define CALL_VertexArrayVertexAttribDivisorEXT(disp, parameters) (* GET_VertexArrayVertexAttribDivisorEXT(disp)) parameters
#define GET_VertexArrayVertexAttribDivisorEXT(disp) ((_glptr_VertexArrayVertexAttribDivisorEXT)(GET_by_offset((disp), _gloffset_VertexArrayVertexAttribDivisorEXT)))
#define SET_VertexArrayVertexAttribDivisorEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLuint) = func; \
   SET_by_offset(disp, _gloffset_VertexArrayVertexAttribDivisorEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TextureBufferRangeEXT)(GLuint, GLenum, GLenum, GLuint, GLintptr, GLsizeiptr);
#define CALL_TextureBufferRangeEXT(disp, parameters) (* GET_TextureBufferRangeEXT(disp)) parameters
#define GET_TextureBufferRangeEXT(disp) ((_glptr_TextureBufferRangeEXT)(GET_by_offset((disp), _gloffset_TextureBufferRangeEXT)))
#define SET_TextureBufferRangeEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLenum, GLuint, GLintptr, GLsizeiptr) = func; \
   SET_by_offset(disp, _gloffset_TextureBufferRangeEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TextureStorage2DMultisampleEXT)(GLuint, GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLboolean);
#define CALL_TextureStorage2DMultisampleEXT(disp, parameters) (* GET_TextureStorage2DMultisampleEXT(disp)) parameters
#define GET_TextureStorage2DMultisampleEXT(disp) ((_glptr_TextureStorage2DMultisampleEXT)(GET_by_offset((disp), _gloffset_TextureStorage2DMultisampleEXT)))
#define SET_TextureStorage2DMultisampleEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLboolean) = func; \
   SET_by_offset(disp, _gloffset_TextureStorage2DMultisampleEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TextureStorage3DMultisampleEXT)(GLuint, GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLsizei, GLboolean);
#define CALL_TextureStorage3DMultisampleEXT(disp, parameters) (* GET_TextureStorage3DMultisampleEXT(disp)) parameters
#define GET_TextureStorage3DMultisampleEXT(disp) ((_glptr_TextureStorage3DMultisampleEXT)(GET_by_offset((disp), _gloffset_TextureStorage3DMultisampleEXT)))
#define SET_TextureStorage3DMultisampleEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLsizei, GLboolean) = func; \
   SET_by_offset(disp, _gloffset_TextureStorage3DMultisampleEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexArrayBindVertexBufferEXT)(GLuint, GLuint, GLuint, GLintptr, GLsizei);
#define CALL_VertexArrayBindVertexBufferEXT(disp, parameters) (* GET_VertexArrayBindVertexBufferEXT(disp)) parameters
#define GET_VertexArrayBindVertexBufferEXT(disp) ((_glptr_VertexArrayBindVertexBufferEXT)(GET_by_offset((disp), _gloffset_VertexArrayBindVertexBufferEXT)))
#define SET_VertexArrayBindVertexBufferEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLuint, GLintptr, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_VertexArrayBindVertexBufferEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexArrayVertexAttribFormatEXT)(GLuint, GLuint, GLint, GLenum, GLboolean, GLuint);
#define CALL_VertexArrayVertexAttribFormatEXT(disp, parameters) (* GET_VertexArrayVertexAttribFormatEXT(disp)) parameters
#define GET_VertexArrayVertexAttribFormatEXT(disp) ((_glptr_VertexArrayVertexAttribFormatEXT)(GET_by_offset((disp), _gloffset_VertexArrayVertexAttribFormatEXT)))
#define SET_VertexArrayVertexAttribFormatEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLint, GLenum, GLboolean, GLuint) = func; \
   SET_by_offset(disp, _gloffset_VertexArrayVertexAttribFormatEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexArrayVertexAttribIFormatEXT)(GLuint, GLuint, GLint, GLenum, GLuint);
#define CALL_VertexArrayVertexAttribIFormatEXT(disp, parameters) (* GET_VertexArrayVertexAttribIFormatEXT(disp)) parameters
#define GET_VertexArrayVertexAttribIFormatEXT(disp) ((_glptr_VertexArrayVertexAttribIFormatEXT)(GET_by_offset((disp), _gloffset_VertexArrayVertexAttribIFormatEXT)))
#define SET_VertexArrayVertexAttribIFormatEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLint, GLenum, GLuint) = func; \
   SET_by_offset(disp, _gloffset_VertexArrayVertexAttribIFormatEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexArrayVertexAttribLFormatEXT)(GLuint, GLuint, GLint, GLenum, GLuint);
#define CALL_VertexArrayVertexAttribLFormatEXT(disp, parameters) (* GET_VertexArrayVertexAttribLFormatEXT(disp)) parameters
#define GET_VertexArrayVertexAttribLFormatEXT(disp) ((_glptr_VertexArrayVertexAttribLFormatEXT)(GET_by_offset((disp), _gloffset_VertexArrayVertexAttribLFormatEXT)))
#define SET_VertexArrayVertexAttribLFormatEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLint, GLenum, GLuint) = func; \
   SET_by_offset(disp, _gloffset_VertexArrayVertexAttribLFormatEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexArrayVertexAttribBindingEXT)(GLuint, GLuint, GLuint);
#define CALL_VertexArrayVertexAttribBindingEXT(disp, parameters) (* GET_VertexArrayVertexAttribBindingEXT(disp)) parameters
#define GET_VertexArrayVertexAttribBindingEXT(disp) ((_glptr_VertexArrayVertexAttribBindingEXT)(GET_by_offset((disp), _gloffset_VertexArrayVertexAttribBindingEXT)))
#define SET_VertexArrayVertexAttribBindingEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLuint) = func; \
   SET_by_offset(disp, _gloffset_VertexArrayVertexAttribBindingEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexArrayVertexBindingDivisorEXT)(GLuint, GLuint, GLuint);
#define CALL_VertexArrayVertexBindingDivisorEXT(disp, parameters) (* GET_VertexArrayVertexBindingDivisorEXT(disp)) parameters
#define GET_VertexArrayVertexBindingDivisorEXT(disp) ((_glptr_VertexArrayVertexBindingDivisorEXT)(GET_by_offset((disp), _gloffset_VertexArrayVertexBindingDivisorEXT)))
#define SET_VertexArrayVertexBindingDivisorEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint, GLuint) = func; \
   SET_by_offset(disp, _gloffset_VertexArrayVertexBindingDivisorEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_NamedBufferPageCommitmentEXT)(GLuint, GLintptr, GLsizeiptr, GLboolean);
#define CALL_NamedBufferPageCommitmentEXT(disp, parameters) (* GET_NamedBufferPageCommitmentEXT(disp)) parameters
#define GET_NamedBufferPageCommitmentEXT(disp) ((_glptr_NamedBufferPageCommitmentEXT)(GET_by_offset((disp), _gloffset_NamedBufferPageCommitmentEXT)))
#define SET_NamedBufferPageCommitmentEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLintptr, GLsizeiptr, GLboolean) = func; \
   SET_by_offset(disp, _gloffset_NamedBufferPageCommitmentEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_NamedStringARB)(GLenum, GLint, const GLchar *, GLint, const GLchar *);
#define CALL_NamedStringARB(disp, parameters) (* GET_NamedStringARB(disp)) parameters
#define GET_NamedStringARB(disp) ((_glptr_NamedStringARB)(GET_by_offset((disp), _gloffset_NamedStringARB)))
#define SET_NamedStringARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLint, const GLchar *, GLint, const GLchar *) = func; \
   SET_by_offset(disp, _gloffset_NamedStringARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DeleteNamedStringARB)(GLint, const GLchar *);
#define CALL_DeleteNamedStringARB(disp, parameters) (* GET_DeleteNamedStringARB(disp)) parameters
#define GET_DeleteNamedStringARB(disp) ((_glptr_DeleteNamedStringARB)(GET_by_offset((disp), _gloffset_DeleteNamedStringARB)))
#define SET_DeleteNamedStringARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, const GLchar *) = func; \
   SET_by_offset(disp, _gloffset_DeleteNamedStringARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CompileShaderIncludeARB)(GLuint, GLsizei, const GLchar * const *, const GLint *);
#define CALL_CompileShaderIncludeARB(disp, parameters) (* GET_CompileShaderIncludeARB(disp)) parameters
#define GET_CompileShaderIncludeARB(disp) ((_glptr_CompileShaderIncludeARB)(GET_by_offset((disp), _gloffset_CompileShaderIncludeARB)))
#define SET_CompileShaderIncludeARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizei, const GLchar * const *, const GLint *) = func; \
   SET_by_offset(disp, _gloffset_CompileShaderIncludeARB, fn); \
} while (0)

typedef GLboolean (GLAPIENTRYP _glptr_IsNamedStringARB)(GLint, const GLchar *);
#define CALL_IsNamedStringARB(disp, parameters) (* GET_IsNamedStringARB(disp)) parameters
#define GET_IsNamedStringARB(disp) ((_glptr_IsNamedStringARB)(GET_by_offset((disp), _gloffset_IsNamedStringARB)))
#define SET_IsNamedStringARB(disp, func) do { \
   GLboolean (GLAPIENTRYP fn)(GLint, const GLchar *) = func; \
   SET_by_offset(disp, _gloffset_IsNamedStringARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetNamedStringARB)(GLint, const GLchar *, GLsizei, GLint *, GLchar *);
#define CALL_GetNamedStringARB(disp, parameters) (* GET_GetNamedStringARB(disp)) parameters
#define GET_GetNamedStringARB(disp) ((_glptr_GetNamedStringARB)(GET_by_offset((disp), _gloffset_GetNamedStringARB)))
#define SET_GetNamedStringARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, const GLchar *, GLsizei, GLint *, GLchar *) = func; \
   SET_by_offset(disp, _gloffset_GetNamedStringARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetNamedStringivARB)(GLint, const GLchar *, GLenum, GLint *);
#define CALL_GetNamedStringivARB(disp, parameters) (* GET_GetNamedStringivARB(disp)) parameters
#define GET_GetNamedStringivARB(disp) ((_glptr_GetNamedStringivARB)(GET_by_offset((disp), _gloffset_GetNamedStringivARB)))
#define SET_GetNamedStringivARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLint, const GLchar *, GLenum, GLint *) = func; \
   SET_by_offset(disp, _gloffset_GetNamedStringivARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_EGLImageTargetTexStorageEXT)(GLenum, GLvoid *, const GLint *);
#define CALL_EGLImageTargetTexStorageEXT(disp, parameters) (* GET_EGLImageTargetTexStorageEXT(disp)) parameters
#define GET_EGLImageTargetTexStorageEXT(disp) ((_glptr_EGLImageTargetTexStorageEXT)(GET_by_offset((disp), _gloffset_EGLImageTargetTexStorageEXT)))
#define SET_EGLImageTargetTexStorageEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLvoid *, const GLint *) = func; \
   SET_by_offset(disp, _gloffset_EGLImageTargetTexStorageEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_EGLImageTargetTextureStorageEXT)(GLuint, GLvoid *, const GLint *);
#define CALL_EGLImageTargetTextureStorageEXT(disp, parameters) (* GET_EGLImageTargetTextureStorageEXT(disp)) parameters
#define GET_EGLImageTargetTextureStorageEXT(disp) ((_glptr_EGLImageTargetTextureStorageEXT)(GET_by_offset((disp), _gloffset_EGLImageTargetTextureStorageEXT)))
#define SET_EGLImageTargetTextureStorageEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLvoid *, const GLint *) = func; \
   SET_by_offset(disp, _gloffset_EGLImageTargetTextureStorageEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_CopyImageSubDataNV)(GLuint, GLenum, GLint, GLint, GLint, GLint, GLuint, GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei);
#define CALL_CopyImageSubDataNV(disp, parameters) (* GET_CopyImageSubDataNV(disp)) parameters
#define GET_CopyImageSubDataNV(disp) ((_glptr_CopyImageSubDataNV)(GET_by_offset((disp), _gloffset_CopyImageSubDataNV)))
#define SET_CopyImageSubDataNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLint, GLint, GLint, GLint, GLuint, GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_CopyImageSubDataNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ViewportSwizzleNV)(GLuint, GLenum, GLenum, GLenum, GLenum);
#define CALL_ViewportSwizzleNV(disp, parameters) (* GET_ViewportSwizzleNV(disp)) parameters
#define GET_ViewportSwizzleNV(disp) ((_glptr_ViewportSwizzleNV)(GET_by_offset((disp), _gloffset_ViewportSwizzleNV)))
#define SET_ViewportSwizzleNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLenum, GLenum, GLenum) = func; \
   SET_by_offset(disp, _gloffset_ViewportSwizzleNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_AlphaToCoverageDitherControlNV)(GLenum);
#define CALL_AlphaToCoverageDitherControlNV(disp, parameters) (* GET_AlphaToCoverageDitherControlNV(disp)) parameters
#define GET_AlphaToCoverageDitherControlNV(disp) ((_glptr_AlphaToCoverageDitherControlNV)(GET_by_offset((disp), _gloffset_AlphaToCoverageDitherControlNV)))
#define SET_AlphaToCoverageDitherControlNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum) = func; \
   SET_by_offset(disp, _gloffset_AlphaToCoverageDitherControlNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_InternalBufferSubDataCopyMESA)(GLintptr, GLuint, GLuint, GLintptr, GLsizeiptr, GLboolean, GLboolean);
#define CALL_InternalBufferSubDataCopyMESA(disp, parameters) (* GET_InternalBufferSubDataCopyMESA(disp)) parameters
#define GET_InternalBufferSubDataCopyMESA(disp) ((_glptr_InternalBufferSubDataCopyMESA)(GET_by_offset((disp), _gloffset_InternalBufferSubDataCopyMESA)))
#define SET_InternalBufferSubDataCopyMESA(disp, func) do { \
   void (GLAPIENTRYP fn)(GLintptr, GLuint, GLuint, GLintptr, GLsizeiptr, GLboolean, GLboolean) = func; \
   SET_by_offset(disp, _gloffset_InternalBufferSubDataCopyMESA, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Vertex2hNV)(GLhalfNV, GLhalfNV);
#define CALL_Vertex2hNV(disp, parameters) (* GET_Vertex2hNV(disp)) parameters
#define GET_Vertex2hNV(disp) ((_glptr_Vertex2hNV)(GET_by_offset((disp), _gloffset_Vertex2hNV)))
#define SET_Vertex2hNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLhalfNV, GLhalfNV) = func; \
   SET_by_offset(disp, _gloffset_Vertex2hNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Vertex2hvNV)(const GLhalfNV *);
#define CALL_Vertex2hvNV(disp, parameters) (* GET_Vertex2hvNV(disp)) parameters
#define GET_Vertex2hvNV(disp) ((_glptr_Vertex2hvNV)(GET_by_offset((disp), _gloffset_Vertex2hvNV)))
#define SET_Vertex2hvNV(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLhalfNV *) = func; \
   SET_by_offset(disp, _gloffset_Vertex2hvNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Vertex3hNV)(GLhalfNV, GLhalfNV, GLhalfNV);
#define CALL_Vertex3hNV(disp, parameters) (* GET_Vertex3hNV(disp)) parameters
#define GET_Vertex3hNV(disp) ((_glptr_Vertex3hNV)(GET_by_offset((disp), _gloffset_Vertex3hNV)))
#define SET_Vertex3hNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLhalfNV, GLhalfNV, GLhalfNV) = func; \
   SET_by_offset(disp, _gloffset_Vertex3hNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Vertex3hvNV)(const GLhalfNV *);
#define CALL_Vertex3hvNV(disp, parameters) (* GET_Vertex3hvNV(disp)) parameters
#define GET_Vertex3hvNV(disp) ((_glptr_Vertex3hvNV)(GET_by_offset((disp), _gloffset_Vertex3hvNV)))
#define SET_Vertex3hvNV(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLhalfNV *) = func; \
   SET_by_offset(disp, _gloffset_Vertex3hvNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Vertex4hNV)(GLhalfNV, GLhalfNV, GLhalfNV, GLhalfNV);
#define CALL_Vertex4hNV(disp, parameters) (* GET_Vertex4hNV(disp)) parameters
#define GET_Vertex4hNV(disp) ((_glptr_Vertex4hNV)(GET_by_offset((disp), _gloffset_Vertex4hNV)))
#define SET_Vertex4hNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLhalfNV, GLhalfNV, GLhalfNV, GLhalfNV) = func; \
   SET_by_offset(disp, _gloffset_Vertex4hNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Vertex4hvNV)(const GLhalfNV *);
#define CALL_Vertex4hvNV(disp, parameters) (* GET_Vertex4hvNV(disp)) parameters
#define GET_Vertex4hvNV(disp) ((_glptr_Vertex4hvNV)(GET_by_offset((disp), _gloffset_Vertex4hvNV)))
#define SET_Vertex4hvNV(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLhalfNV *) = func; \
   SET_by_offset(disp, _gloffset_Vertex4hvNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Normal3hNV)(GLhalfNV, GLhalfNV, GLhalfNV);
#define CALL_Normal3hNV(disp, parameters) (* GET_Normal3hNV(disp)) parameters
#define GET_Normal3hNV(disp) ((_glptr_Normal3hNV)(GET_by_offset((disp), _gloffset_Normal3hNV)))
#define SET_Normal3hNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLhalfNV, GLhalfNV, GLhalfNV) = func; \
   SET_by_offset(disp, _gloffset_Normal3hNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Normal3hvNV)(const GLhalfNV *);
#define CALL_Normal3hvNV(disp, parameters) (* GET_Normal3hvNV(disp)) parameters
#define GET_Normal3hvNV(disp) ((_glptr_Normal3hvNV)(GET_by_offset((disp), _gloffset_Normal3hvNV)))
#define SET_Normal3hvNV(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLhalfNV *) = func; \
   SET_by_offset(disp, _gloffset_Normal3hvNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Color3hNV)(GLhalfNV, GLhalfNV, GLhalfNV);
#define CALL_Color3hNV(disp, parameters) (* GET_Color3hNV(disp)) parameters
#define GET_Color3hNV(disp) ((_glptr_Color3hNV)(GET_by_offset((disp), _gloffset_Color3hNV)))
#define SET_Color3hNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLhalfNV, GLhalfNV, GLhalfNV) = func; \
   SET_by_offset(disp, _gloffset_Color3hNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Color3hvNV)(const GLhalfNV *);
#define CALL_Color3hvNV(disp, parameters) (* GET_Color3hvNV(disp)) parameters
#define GET_Color3hvNV(disp) ((_glptr_Color3hvNV)(GET_by_offset((disp), _gloffset_Color3hvNV)))
#define SET_Color3hvNV(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLhalfNV *) = func; \
   SET_by_offset(disp, _gloffset_Color3hvNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Color4hNV)(GLhalfNV, GLhalfNV, GLhalfNV, GLhalfNV);
#define CALL_Color4hNV(disp, parameters) (* GET_Color4hNV(disp)) parameters
#define GET_Color4hNV(disp) ((_glptr_Color4hNV)(GET_by_offset((disp), _gloffset_Color4hNV)))
#define SET_Color4hNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLhalfNV, GLhalfNV, GLhalfNV, GLhalfNV) = func; \
   SET_by_offset(disp, _gloffset_Color4hNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_Color4hvNV)(const GLhalfNV *);
#define CALL_Color4hvNV(disp, parameters) (* GET_Color4hvNV(disp)) parameters
#define GET_Color4hvNV(disp) ((_glptr_Color4hvNV)(GET_by_offset((disp), _gloffset_Color4hvNV)))
#define SET_Color4hvNV(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLhalfNV *) = func; \
   SET_by_offset(disp, _gloffset_Color4hvNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexCoord1hNV)(GLhalfNV);
#define CALL_TexCoord1hNV(disp, parameters) (* GET_TexCoord1hNV(disp)) parameters
#define GET_TexCoord1hNV(disp) ((_glptr_TexCoord1hNV)(GET_by_offset((disp), _gloffset_TexCoord1hNV)))
#define SET_TexCoord1hNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLhalfNV) = func; \
   SET_by_offset(disp, _gloffset_TexCoord1hNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexCoord1hvNV)(const GLhalfNV *);
#define CALL_TexCoord1hvNV(disp, parameters) (* GET_TexCoord1hvNV(disp)) parameters
#define GET_TexCoord1hvNV(disp) ((_glptr_TexCoord1hvNV)(GET_by_offset((disp), _gloffset_TexCoord1hvNV)))
#define SET_TexCoord1hvNV(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLhalfNV *) = func; \
   SET_by_offset(disp, _gloffset_TexCoord1hvNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexCoord2hNV)(GLhalfNV, GLhalfNV);
#define CALL_TexCoord2hNV(disp, parameters) (* GET_TexCoord2hNV(disp)) parameters
#define GET_TexCoord2hNV(disp) ((_glptr_TexCoord2hNV)(GET_by_offset((disp), _gloffset_TexCoord2hNV)))
#define SET_TexCoord2hNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLhalfNV, GLhalfNV) = func; \
   SET_by_offset(disp, _gloffset_TexCoord2hNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexCoord2hvNV)(const GLhalfNV *);
#define CALL_TexCoord2hvNV(disp, parameters) (* GET_TexCoord2hvNV(disp)) parameters
#define GET_TexCoord2hvNV(disp) ((_glptr_TexCoord2hvNV)(GET_by_offset((disp), _gloffset_TexCoord2hvNV)))
#define SET_TexCoord2hvNV(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLhalfNV *) = func; \
   SET_by_offset(disp, _gloffset_TexCoord2hvNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexCoord3hNV)(GLhalfNV, GLhalfNV, GLhalfNV);
#define CALL_TexCoord3hNV(disp, parameters) (* GET_TexCoord3hNV(disp)) parameters
#define GET_TexCoord3hNV(disp) ((_glptr_TexCoord3hNV)(GET_by_offset((disp), _gloffset_TexCoord3hNV)))
#define SET_TexCoord3hNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLhalfNV, GLhalfNV, GLhalfNV) = func; \
   SET_by_offset(disp, _gloffset_TexCoord3hNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexCoord3hvNV)(const GLhalfNV *);
#define CALL_TexCoord3hvNV(disp, parameters) (* GET_TexCoord3hvNV(disp)) parameters
#define GET_TexCoord3hvNV(disp) ((_glptr_TexCoord3hvNV)(GET_by_offset((disp), _gloffset_TexCoord3hvNV)))
#define SET_TexCoord3hvNV(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLhalfNV *) = func; \
   SET_by_offset(disp, _gloffset_TexCoord3hvNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexCoord4hNV)(GLhalfNV, GLhalfNV, GLhalfNV, GLhalfNV);
#define CALL_TexCoord4hNV(disp, parameters) (* GET_TexCoord4hNV(disp)) parameters
#define GET_TexCoord4hNV(disp) ((_glptr_TexCoord4hNV)(GET_by_offset((disp), _gloffset_TexCoord4hNV)))
#define SET_TexCoord4hNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLhalfNV, GLhalfNV, GLhalfNV, GLhalfNV) = func; \
   SET_by_offset(disp, _gloffset_TexCoord4hNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexCoord4hvNV)(const GLhalfNV *);
#define CALL_TexCoord4hvNV(disp, parameters) (* GET_TexCoord4hvNV(disp)) parameters
#define GET_TexCoord4hvNV(disp) ((_glptr_TexCoord4hvNV)(GET_by_offset((disp), _gloffset_TexCoord4hvNV)))
#define SET_TexCoord4hvNV(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLhalfNV *) = func; \
   SET_by_offset(disp, _gloffset_TexCoord4hvNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexCoord1hNV)(GLenum, GLhalfNV);
#define CALL_MultiTexCoord1hNV(disp, parameters) (* GET_MultiTexCoord1hNV(disp)) parameters
#define GET_MultiTexCoord1hNV(disp) ((_glptr_MultiTexCoord1hNV)(GET_by_offset((disp), _gloffset_MultiTexCoord1hNV)))
#define SET_MultiTexCoord1hNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLhalfNV) = func; \
   SET_by_offset(disp, _gloffset_MultiTexCoord1hNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexCoord1hvNV)(GLenum, const GLhalfNV *);
#define CALL_MultiTexCoord1hvNV(disp, parameters) (* GET_MultiTexCoord1hvNV(disp)) parameters
#define GET_MultiTexCoord1hvNV(disp) ((_glptr_MultiTexCoord1hvNV)(GET_by_offset((disp), _gloffset_MultiTexCoord1hvNV)))
#define SET_MultiTexCoord1hvNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, const GLhalfNV *) = func; \
   SET_by_offset(disp, _gloffset_MultiTexCoord1hvNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexCoord2hNV)(GLenum, GLhalfNV, GLhalfNV);
#define CALL_MultiTexCoord2hNV(disp, parameters) (* GET_MultiTexCoord2hNV(disp)) parameters
#define GET_MultiTexCoord2hNV(disp) ((_glptr_MultiTexCoord2hNV)(GET_by_offset((disp), _gloffset_MultiTexCoord2hNV)))
#define SET_MultiTexCoord2hNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLhalfNV, GLhalfNV) = func; \
   SET_by_offset(disp, _gloffset_MultiTexCoord2hNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexCoord2hvNV)(GLenum, const GLhalfNV *);
#define CALL_MultiTexCoord2hvNV(disp, parameters) (* GET_MultiTexCoord2hvNV(disp)) parameters
#define GET_MultiTexCoord2hvNV(disp) ((_glptr_MultiTexCoord2hvNV)(GET_by_offset((disp), _gloffset_MultiTexCoord2hvNV)))
#define SET_MultiTexCoord2hvNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, const GLhalfNV *) = func; \
   SET_by_offset(disp, _gloffset_MultiTexCoord2hvNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexCoord3hNV)(GLenum, GLhalfNV, GLhalfNV, GLhalfNV);
#define CALL_MultiTexCoord3hNV(disp, parameters) (* GET_MultiTexCoord3hNV(disp)) parameters
#define GET_MultiTexCoord3hNV(disp) ((_glptr_MultiTexCoord3hNV)(GET_by_offset((disp), _gloffset_MultiTexCoord3hNV)))
#define SET_MultiTexCoord3hNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLhalfNV, GLhalfNV, GLhalfNV) = func; \
   SET_by_offset(disp, _gloffset_MultiTexCoord3hNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexCoord3hvNV)(GLenum, const GLhalfNV *);
#define CALL_MultiTexCoord3hvNV(disp, parameters) (* GET_MultiTexCoord3hvNV(disp)) parameters
#define GET_MultiTexCoord3hvNV(disp) ((_glptr_MultiTexCoord3hvNV)(GET_by_offset((disp), _gloffset_MultiTexCoord3hvNV)))
#define SET_MultiTexCoord3hvNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, const GLhalfNV *) = func; \
   SET_by_offset(disp, _gloffset_MultiTexCoord3hvNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexCoord4hNV)(GLenum, GLhalfNV, GLhalfNV, GLhalfNV, GLhalfNV);
#define CALL_MultiTexCoord4hNV(disp, parameters) (* GET_MultiTexCoord4hNV(disp)) parameters
#define GET_MultiTexCoord4hNV(disp) ((_glptr_MultiTexCoord4hNV)(GET_by_offset((disp), _gloffset_MultiTexCoord4hNV)))
#define SET_MultiTexCoord4hNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLhalfNV, GLhalfNV, GLhalfNV, GLhalfNV) = func; \
   SET_by_offset(disp, _gloffset_MultiTexCoord4hNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiTexCoord4hvNV)(GLenum, const GLhalfNV *);
#define CALL_MultiTexCoord4hvNV(disp, parameters) (* GET_MultiTexCoord4hvNV(disp)) parameters
#define GET_MultiTexCoord4hvNV(disp) ((_glptr_MultiTexCoord4hvNV)(GET_by_offset((disp), _gloffset_MultiTexCoord4hvNV)))
#define SET_MultiTexCoord4hvNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, const GLhalfNV *) = func; \
   SET_by_offset(disp, _gloffset_MultiTexCoord4hvNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_FogCoordhNV)(GLhalfNV);
#define CALL_FogCoordhNV(disp, parameters) (* GET_FogCoordhNV(disp)) parameters
#define GET_FogCoordhNV(disp) ((_glptr_FogCoordhNV)(GET_by_offset((disp), _gloffset_FogCoordhNV)))
#define SET_FogCoordhNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLhalfNV) = func; \
   SET_by_offset(disp, _gloffset_FogCoordhNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_FogCoordhvNV)(const GLhalfNV *);
#define CALL_FogCoordhvNV(disp, parameters) (* GET_FogCoordhvNV(disp)) parameters
#define GET_FogCoordhvNV(disp) ((_glptr_FogCoordhvNV)(GET_by_offset((disp), _gloffset_FogCoordhvNV)))
#define SET_FogCoordhvNV(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLhalfNV *) = func; \
   SET_by_offset(disp, _gloffset_FogCoordhvNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_SecondaryColor3hNV)(GLhalfNV, GLhalfNV, GLhalfNV);
#define CALL_SecondaryColor3hNV(disp, parameters) (* GET_SecondaryColor3hNV(disp)) parameters
#define GET_SecondaryColor3hNV(disp) ((_glptr_SecondaryColor3hNV)(GET_by_offset((disp), _gloffset_SecondaryColor3hNV)))
#define SET_SecondaryColor3hNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLhalfNV, GLhalfNV, GLhalfNV) = func; \
   SET_by_offset(disp, _gloffset_SecondaryColor3hNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_SecondaryColor3hvNV)(const GLhalfNV *);
#define CALL_SecondaryColor3hvNV(disp, parameters) (* GET_SecondaryColor3hvNV(disp)) parameters
#define GET_SecondaryColor3hvNV(disp) ((_glptr_SecondaryColor3hvNV)(GET_by_offset((disp), _gloffset_SecondaryColor3hvNV)))
#define SET_SecondaryColor3hvNV(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLhalfNV *) = func; \
   SET_by_offset(disp, _gloffset_SecondaryColor3hvNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_InternalSetError)(GLenum);
#define CALL_InternalSetError(disp, parameters) (* GET_InternalSetError(disp)) parameters
#define GET_InternalSetError(disp) ((_glptr_InternalSetError)(GET_by_offset((disp), _gloffset_InternalSetError)))
#define SET_InternalSetError(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum) = func; \
   SET_by_offset(disp, _gloffset_InternalSetError, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib1hNV)(GLuint, GLhalfNV);
#define CALL_VertexAttrib1hNV(disp, parameters) (* GET_VertexAttrib1hNV(disp)) parameters
#define GET_VertexAttrib1hNV(disp) ((_glptr_VertexAttrib1hNV)(GET_by_offset((disp), _gloffset_VertexAttrib1hNV)))
#define SET_VertexAttrib1hNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLhalfNV) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib1hNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib1hvNV)(GLuint, const GLhalfNV *);
#define CALL_VertexAttrib1hvNV(disp, parameters) (* GET_VertexAttrib1hvNV(disp)) parameters
#define GET_VertexAttrib1hvNV(disp) ((_glptr_VertexAttrib1hvNV)(GET_by_offset((disp), _gloffset_VertexAttrib1hvNV)))
#define SET_VertexAttrib1hvNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLhalfNV *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib1hvNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib2hNV)(GLuint, GLhalfNV, GLhalfNV);
#define CALL_VertexAttrib2hNV(disp, parameters) (* GET_VertexAttrib2hNV(disp)) parameters
#define GET_VertexAttrib2hNV(disp) ((_glptr_VertexAttrib2hNV)(GET_by_offset((disp), _gloffset_VertexAttrib2hNV)))
#define SET_VertexAttrib2hNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLhalfNV, GLhalfNV) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib2hNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib2hvNV)(GLuint, const GLhalfNV *);
#define CALL_VertexAttrib2hvNV(disp, parameters) (* GET_VertexAttrib2hvNV(disp)) parameters
#define GET_VertexAttrib2hvNV(disp) ((_glptr_VertexAttrib2hvNV)(GET_by_offset((disp), _gloffset_VertexAttrib2hvNV)))
#define SET_VertexAttrib2hvNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLhalfNV *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib2hvNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib3hNV)(GLuint, GLhalfNV, GLhalfNV, GLhalfNV);
#define CALL_VertexAttrib3hNV(disp, parameters) (* GET_VertexAttrib3hNV(disp)) parameters
#define GET_VertexAttrib3hNV(disp) ((_glptr_VertexAttrib3hNV)(GET_by_offset((disp), _gloffset_VertexAttrib3hNV)))
#define SET_VertexAttrib3hNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLhalfNV, GLhalfNV, GLhalfNV) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib3hNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib3hvNV)(GLuint, const GLhalfNV *);
#define CALL_VertexAttrib3hvNV(disp, parameters) (* GET_VertexAttrib3hvNV(disp)) parameters
#define GET_VertexAttrib3hvNV(disp) ((_glptr_VertexAttrib3hvNV)(GET_by_offset((disp), _gloffset_VertexAttrib3hvNV)))
#define SET_VertexAttrib3hvNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLhalfNV *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib3hvNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib4hNV)(GLuint, GLhalfNV, GLhalfNV, GLhalfNV, GLhalfNV);
#define CALL_VertexAttrib4hNV(disp, parameters) (* GET_VertexAttrib4hNV(disp)) parameters
#define GET_VertexAttrib4hNV(disp) ((_glptr_VertexAttrib4hNV)(GET_by_offset((disp), _gloffset_VertexAttrib4hNV)))
#define SET_VertexAttrib4hNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLhalfNV, GLhalfNV, GLhalfNV, GLhalfNV) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib4hNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttrib4hvNV)(GLuint, const GLhalfNV *);
#define CALL_VertexAttrib4hvNV(disp, parameters) (* GET_VertexAttrib4hvNV(disp)) parameters
#define GET_VertexAttrib4hvNV(disp) ((_glptr_VertexAttrib4hvNV)(GET_by_offset((disp), _gloffset_VertexAttrib4hvNV)))
#define SET_VertexAttrib4hvNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, const GLhalfNV *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttrib4hvNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribs1hvNV)(GLuint, GLsizei, const GLhalfNV *);
#define CALL_VertexAttribs1hvNV(disp, parameters) (* GET_VertexAttribs1hvNV(disp)) parameters
#define GET_VertexAttribs1hvNV(disp) ((_glptr_VertexAttribs1hvNV)(GET_by_offset((disp), _gloffset_VertexAttribs1hvNV)))
#define SET_VertexAttribs1hvNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizei, const GLhalfNV *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribs1hvNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribs2hvNV)(GLuint, GLsizei, const GLhalfNV *);
#define CALL_VertexAttribs2hvNV(disp, parameters) (* GET_VertexAttribs2hvNV(disp)) parameters
#define GET_VertexAttribs2hvNV(disp) ((_glptr_VertexAttribs2hvNV)(GET_by_offset((disp), _gloffset_VertexAttribs2hvNV)))
#define SET_VertexAttribs2hvNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizei, const GLhalfNV *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribs2hvNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribs3hvNV)(GLuint, GLsizei, const GLhalfNV *);
#define CALL_VertexAttribs3hvNV(disp, parameters) (* GET_VertexAttribs3hvNV(disp)) parameters
#define GET_VertexAttribs3hvNV(disp) ((_glptr_VertexAttribs3hvNV)(GET_by_offset((disp), _gloffset_VertexAttribs3hvNV)))
#define SET_VertexAttribs3hvNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizei, const GLhalfNV *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribs3hvNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_VertexAttribs4hvNV)(GLuint, GLsizei, const GLhalfNV *);
#define CALL_VertexAttribs4hvNV(disp, parameters) (* GET_VertexAttribs4hvNV(disp)) parameters
#define GET_VertexAttribs4hvNV(disp) ((_glptr_VertexAttribs4hvNV)(GET_by_offset((disp), _gloffset_VertexAttribs4hvNV)))
#define SET_VertexAttribs4hvNV(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLsizei, const GLhalfNV *) = func; \
   SET_by_offset(disp, _gloffset_VertexAttribs4hvNV, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexPageCommitmentARB)(GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLboolean);
#define CALL_TexPageCommitmentARB(disp, parameters) (* GET_TexPageCommitmentARB(disp)) parameters
#define GET_TexPageCommitmentARB(disp) ((_glptr_TexPageCommitmentARB)(GET_by_offset((disp), _gloffset_TexPageCommitmentARB)))
#define SET_TexPageCommitmentARB(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLboolean) = func; \
   SET_by_offset(disp, _gloffset_TexPageCommitmentARB, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexturePageCommitmentEXT)(GLuint, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLboolean);
#define CALL_TexturePageCommitmentEXT(disp, parameters) (* GET_TexturePageCommitmentEXT(disp)) parameters
#define GET_TexturePageCommitmentEXT(disp) ((_glptr_TexturePageCommitmentEXT)(GET_by_offset((disp), _gloffset_TexturePageCommitmentEXT)))
#define SET_TexturePageCommitmentEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLboolean) = func; \
   SET_by_offset(disp, _gloffset_TexturePageCommitmentEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ImportMemoryWin32HandleEXT)(GLuint, GLuint64, GLenum, GLvoid *);
#define CALL_ImportMemoryWin32HandleEXT(disp, parameters) (* GET_ImportMemoryWin32HandleEXT(disp)) parameters
#define GET_ImportMemoryWin32HandleEXT(disp) ((_glptr_ImportMemoryWin32HandleEXT)(GET_by_offset((disp), _gloffset_ImportMemoryWin32HandleEXT)))
#define SET_ImportMemoryWin32HandleEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint64, GLenum, GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_ImportMemoryWin32HandleEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ImportSemaphoreWin32HandleEXT)(GLuint, GLenum, GLvoid *);
#define CALL_ImportSemaphoreWin32HandleEXT(disp, parameters) (* GET_ImportSemaphoreWin32HandleEXT(disp)) parameters
#define GET_ImportSemaphoreWin32HandleEXT(disp) ((_glptr_ImportSemaphoreWin32HandleEXT)(GET_by_offset((disp), _gloffset_ImportSemaphoreWin32HandleEXT)))
#define SET_ImportSemaphoreWin32HandleEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_ImportSemaphoreWin32HandleEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ImportMemoryWin32NameEXT)(GLuint, GLuint64, GLenum, const GLvoid *);
#define CALL_ImportMemoryWin32NameEXT(disp, parameters) (* GET_ImportMemoryWin32NameEXT(disp)) parameters
#define GET_ImportMemoryWin32NameEXT(disp) ((_glptr_ImportMemoryWin32NameEXT)(GET_by_offset((disp), _gloffset_ImportMemoryWin32NameEXT)))
#define SET_ImportMemoryWin32NameEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLuint64, GLenum, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_ImportMemoryWin32NameEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_ImportSemaphoreWin32NameEXT)(GLuint, GLenum, const GLvoid *);
#define CALL_ImportSemaphoreWin32NameEXT(disp, parameters) (* GET_ImportSemaphoreWin32NameEXT(disp)) parameters
#define GET_ImportSemaphoreWin32NameEXT(disp) ((_glptr_ImportSemaphoreWin32NameEXT)(GET_by_offset((disp), _gloffset_ImportSemaphoreWin32NameEXT)))
#define SET_ImportSemaphoreWin32NameEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_ImportSemaphoreWin32NameEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_GetObjectLabelEXT)(GLenum, GLuint, GLsizei, GLsizei *, GLchar *);
#define CALL_GetObjectLabelEXT(disp, parameters) (* GET_GetObjectLabelEXT(disp)) parameters
#define GET_GetObjectLabelEXT(disp) ((_glptr_GetObjectLabelEXT)(GET_by_offset((disp), _gloffset_GetObjectLabelEXT)))
#define SET_GetObjectLabelEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint, GLsizei, GLsizei *, GLchar *) = func; \
   SET_by_offset(disp, _gloffset_GetObjectLabelEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_LabelObjectEXT)(GLenum, GLuint, GLsizei, const GLchar *);
#define CALL_LabelObjectEXT(disp, parameters) (* GET_LabelObjectEXT(disp)) parameters
#define GET_LabelObjectEXT(disp) ((_glptr_LabelObjectEXT)(GET_by_offset((disp), _gloffset_LabelObjectEXT)))
#define SET_LabelObjectEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLuint, GLsizei, const GLchar *) = func; \
   SET_by_offset(disp, _gloffset_LabelObjectEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DrawArraysUserBuf)(void);
#define CALL_DrawArraysUserBuf(disp, parameters) (* GET_DrawArraysUserBuf(disp)) parameters
#define GET_DrawArraysUserBuf(disp) ((_glptr_DrawArraysUserBuf)(GET_by_offset((disp), _gloffset_DrawArraysUserBuf)))
#define SET_DrawArraysUserBuf(disp, func) do { \
   void (GLAPIENTRYP fn)(void) = func; \
   SET_by_offset(disp, _gloffset_DrawArraysUserBuf, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DrawElementsUserBuf)(const GLvoid *);
#define CALL_DrawElementsUserBuf(disp, parameters) (* GET_DrawElementsUserBuf(disp)) parameters
#define GET_DrawElementsUserBuf(disp) ((_glptr_DrawElementsUserBuf)(GET_by_offset((disp), _gloffset_DrawElementsUserBuf)))
#define SET_DrawElementsUserBuf(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_DrawElementsUserBuf, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiDrawArraysUserBuf)(void);
#define CALL_MultiDrawArraysUserBuf(disp, parameters) (* GET_MultiDrawArraysUserBuf(disp)) parameters
#define GET_MultiDrawArraysUserBuf(disp) ((_glptr_MultiDrawArraysUserBuf)(GET_by_offset((disp), _gloffset_MultiDrawArraysUserBuf)))
#define SET_MultiDrawArraysUserBuf(disp, func) do { \
   void (GLAPIENTRYP fn)(void) = func; \
   SET_by_offset(disp, _gloffset_MultiDrawArraysUserBuf, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_MultiDrawElementsUserBuf)(GLintptr, GLenum, const GLsizei *, GLenum, const GLvoid * const *, GLsizei, const GLint *);
#define CALL_MultiDrawElementsUserBuf(disp, parameters) (* GET_MultiDrawElementsUserBuf(disp)) parameters
#define GET_MultiDrawElementsUserBuf(disp) ((_glptr_MultiDrawElementsUserBuf)(GET_by_offset((disp), _gloffset_MultiDrawElementsUserBuf)))
#define SET_MultiDrawElementsUserBuf(disp, func) do { \
   void (GLAPIENTRYP fn)(GLintptr, GLenum, const GLsizei *, GLenum, const GLvoid * const *, GLsizei, const GLint *) = func; \
   SET_by_offset(disp, _gloffset_MultiDrawElementsUserBuf, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DrawArraysInstancedBaseInstanceDrawID)(void);
#define CALL_DrawArraysInstancedBaseInstanceDrawID(disp, parameters) (* GET_DrawArraysInstancedBaseInstanceDrawID(disp)) parameters
#define GET_DrawArraysInstancedBaseInstanceDrawID(disp) ((_glptr_DrawArraysInstancedBaseInstanceDrawID)(GET_by_offset((disp), _gloffset_DrawArraysInstancedBaseInstanceDrawID)))
#define SET_DrawArraysInstancedBaseInstanceDrawID(disp, func) do { \
   void (GLAPIENTRYP fn)(void) = func; \
   SET_by_offset(disp, _gloffset_DrawArraysInstancedBaseInstanceDrawID, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DrawElementsInstancedBaseVertexBaseInstanceDrawID)(GLenum, GLsizei, GLenum, const GLvoid *, GLsizei, GLint, GLuint, GLuint);
#define CALL_DrawElementsInstancedBaseVertexBaseInstanceDrawID(disp, parameters) (* GET_DrawElementsInstancedBaseVertexBaseInstanceDrawID(disp)) parameters
#define GET_DrawElementsInstancedBaseVertexBaseInstanceDrawID(disp) ((_glptr_DrawElementsInstancedBaseVertexBaseInstanceDrawID)(GET_by_offset((disp), _gloffset_DrawElementsInstancedBaseVertexBaseInstanceDrawID)))
#define SET_DrawElementsInstancedBaseVertexBaseInstanceDrawID(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLsizei, GLenum, const GLvoid *, GLsizei, GLint, GLuint, GLuint) = func; \
   SET_by_offset(disp, _gloffset_DrawElementsInstancedBaseVertexBaseInstanceDrawID, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_InternalInvalidateFramebufferAncillaryMESA)(void);
#define CALL_InternalInvalidateFramebufferAncillaryMESA(disp, parameters) (* GET_InternalInvalidateFramebufferAncillaryMESA(disp)) parameters
#define GET_InternalInvalidateFramebufferAncillaryMESA(disp) ((_glptr_InternalInvalidateFramebufferAncillaryMESA)(GET_by_offset((disp), _gloffset_InternalInvalidateFramebufferAncillaryMESA)))
#define SET_InternalInvalidateFramebufferAncillaryMESA(disp, func) do { \
   void (GLAPIENTRYP fn)(void) = func; \
   SET_by_offset(disp, _gloffset_InternalInvalidateFramebufferAncillaryMESA, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DrawElementsPacked)(GLenum, GLenum, GLushort, GLushort);
#define CALL_DrawElementsPacked(disp, parameters) (* GET_DrawElementsPacked(disp)) parameters
#define GET_DrawElementsPacked(disp) ((_glptr_DrawElementsPacked)(GET_by_offset((disp), _gloffset_DrawElementsPacked)))
#define SET_DrawElementsPacked(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLushort, GLushort) = func; \
   SET_by_offset(disp, _gloffset_DrawElementsPacked, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_DrawElementsUserBufPacked)(const GLvoid *);
#define CALL_DrawElementsUserBufPacked(disp, parameters) (* GET_DrawElementsUserBufPacked(disp)) parameters
#define GET_DrawElementsUserBufPacked(disp) ((_glptr_DrawElementsUserBufPacked)(GET_by_offset((disp), _gloffset_DrawElementsUserBufPacked)))
#define SET_DrawElementsUserBufPacked(disp, func) do { \
   void (GLAPIENTRYP fn)(const GLvoid *) = func; \
   SET_by_offset(disp, _gloffset_DrawElementsUserBufPacked, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexStorageAttribs2DEXT)(GLenum, GLsizei, GLenum, GLsizei, GLsizei, const GLint *);
#define CALL_TexStorageAttribs2DEXT(disp, parameters) (* GET_TexStorageAttribs2DEXT(disp)) parameters
#define GET_TexStorageAttribs2DEXT(disp) ((_glptr_TexStorageAttribs2DEXT)(GET_by_offset((disp), _gloffset_TexStorageAttribs2DEXT)))
#define SET_TexStorageAttribs2DEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLsizei, GLenum, GLsizei, GLsizei, const GLint *) = func; \
   SET_by_offset(disp, _gloffset_TexStorageAttribs2DEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_TexStorageAttribs3DEXT)(GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLsizei, const GLint *);
#define CALL_TexStorageAttribs3DEXT(disp, parameters) (* GET_TexStorageAttribs3DEXT(disp)) parameters
#define GET_TexStorageAttribs3DEXT(disp) ((_glptr_TexStorageAttribs3DEXT)(GET_by_offset((disp), _gloffset_TexStorageAttribs3DEXT)))
#define SET_TexStorageAttribs3DEXT(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLsizei, const GLint *) = func; \
   SET_by_offset(disp, _gloffset_TexStorageAttribs3DEXT, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_FramebufferTextureMultiviewOVR)(GLenum, GLenum, GLuint, GLint, GLint, GLsizei);
#define CALL_FramebufferTextureMultiviewOVR(disp, parameters) (* GET_FramebufferTextureMultiviewOVR(disp)) parameters
#define GET_FramebufferTextureMultiviewOVR(disp) ((_glptr_FramebufferTextureMultiviewOVR)(GET_by_offset((disp), _gloffset_FramebufferTextureMultiviewOVR)))
#define SET_FramebufferTextureMultiviewOVR(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLuint, GLint, GLint, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_FramebufferTextureMultiviewOVR, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_NamedFramebufferTextureMultiviewOVR)(GLuint, GLenum, GLuint, GLint, GLint, GLsizei);
#define CALL_NamedFramebufferTextureMultiviewOVR(disp, parameters) (* GET_NamedFramebufferTextureMultiviewOVR(disp)) parameters
#define GET_NamedFramebufferTextureMultiviewOVR(disp) ((_glptr_NamedFramebufferTextureMultiviewOVR)(GET_by_offset((disp), _gloffset_NamedFramebufferTextureMultiviewOVR)))
#define SET_NamedFramebufferTextureMultiviewOVR(disp, func) do { \
   void (GLAPIENTRYP fn)(GLuint, GLenum, GLuint, GLint, GLint, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_NamedFramebufferTextureMultiviewOVR, fn); \
} while (0)

typedef void (GLAPIENTRYP _glptr_FramebufferTextureMultisampleMultiviewOVR)(GLenum, GLenum, GLuint, GLint, GLsizei, GLint, GLsizei);
#define CALL_FramebufferTextureMultisampleMultiviewOVR(disp, parameters) (* GET_FramebufferTextureMultisampleMultiviewOVR(disp)) parameters
#define GET_FramebufferTextureMultisampleMultiviewOVR(disp) ((_glptr_FramebufferTextureMultisampleMultiviewOVR)(GET_by_offset((disp), _gloffset_FramebufferTextureMultisampleMultiviewOVR)))
#define SET_FramebufferTextureMultisampleMultiviewOVR(disp, func) do { \
   void (GLAPIENTRYP fn)(GLenum, GLenum, GLuint, GLint, GLsizei, GLint, GLsizei) = func; \
   SET_by_offset(disp, _gloffset_FramebufferTextureMultisampleMultiviewOVR, fn); \
} while (0)


#endif /* !defined( _DISPATCH_H_ ) */
