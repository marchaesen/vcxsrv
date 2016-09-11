/*
 * Copyright 2013 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Marek Olšák <maraeo@gmail.com>
 */

#ifndef R600D_COMMON_H
#define R600D_COMMON_H

#define R600_CONFIG_REG_OFFSET	0x08000
#define R600_CONTEXT_REG_OFFSET 0x28000
#define SI_SH_REG_OFFSET                     0x0000B000
#define SI_SH_REG_END                        0x0000C000
#define CIK_UCONFIG_REG_OFFSET               0x00030000
#define CIK_UCONFIG_REG_END                  0x00038000

#define PKT_TYPE_S(x)                   (((unsigned)(x) & 0x3) << 30)
#define PKT_COUNT_S(x)                  (((unsigned)(x) & 0x3FFF) << 16)
#define PKT3_IT_OPCODE_S(x)             (((unsigned)(x) & 0xFF) << 8)
#define PKT3_PREDICATE(x)               (((x) >> 0) & 0x1)
#define PKT3(op, count, predicate) (PKT_TYPE_S(3) | PKT_COUNT_S(count) | PKT3_IT_OPCODE_S(op) | PKT3_PREDICATE(predicate))

#define RADEON_CP_PACKET3_COMPUTE_MODE 0x00000002

#define PKT3_NOP                               0x10
#define PKT3_SET_PREDICATION                   0x20
#define PKT3_STRMOUT_BUFFER_UPDATE             0x34
#define		STRMOUT_STORE_BUFFER_FILLED_SIZE	1
#define		STRMOUT_OFFSET_SOURCE(x)	(((unsigned)(x) & 0x3) << 1)
#define			STRMOUT_OFFSET_FROM_PACKET		0
#define			STRMOUT_OFFSET_FROM_VGT_FILLED_SIZE	1
#define			STRMOUT_OFFSET_FROM_MEM			2
#define			STRMOUT_OFFSET_NONE			3
#define		STRMOUT_SELECT_BUFFER(x)	(((unsigned)(x) & 0x3) << 8)
#define PKT3_WAIT_REG_MEM                      0x3C
#define		WAIT_REG_MEM_EQUAL		3
#define         WAIT_REG_MEM_MEM_SPACE(x)       (((unsigned)(x) & 0x3) << 4)
#define PKT3_EVENT_WRITE                       0x46
#define PKT3_EVENT_WRITE_EOP                   0x47
#define         EOP_DATA_SEL(x)                         ((x) << 29)
		/* 0 - discard
		 * 1 - send low 32bit data
		 * 2 - send 64bit data
		 * 3 - send 64bit GPU counter value
		 * 4 - send 64bit sys counter value
		 */
#define PKT3_SET_CONFIG_REG		       0x68
#define PKT3_SET_CONTEXT_REG		       0x69
#define PKT3_STRMOUT_BASE_UPDATE	       0x72 /* r700 only */
#define PKT3_SURFACE_BASE_UPDATE               0x73 /* r600 only */
#define		SURFACE_BASE_UPDATE_DEPTH      (1 << 0)
#define		SURFACE_BASE_UPDATE_COLOR(x)   (2 << (x))
#define		SURFACE_BASE_UPDATE_COLOR_NUM(x) (((1 << x) - 1) << 1)
#define		SURFACE_BASE_UPDATE_STRMOUT(x) (0x200 << (x))
#define PKT3_SET_SH_REG                        0x76 /* SI and later */
#define PKT3_SET_UCONFIG_REG                   0x79 /* CIK and later */

#define EVENT_TYPE_SAMPLE_STREAMOUTSTATS1      0x1 /* EG and later */
#define EVENT_TYPE_SAMPLE_STREAMOUTSTATS2      0x2 /* EG and later */
#define EVENT_TYPE_SAMPLE_STREAMOUTSTATS3      0x3 /* EG and later */
#define EVENT_TYPE_PS_PARTIAL_FLUSH            0x10
#define EVENT_TYPE_CACHE_FLUSH_AND_INV_TS_EVENT 0x14
#define EVENT_TYPE_ZPASS_DONE                  0x15
#define EVENT_TYPE_CACHE_FLUSH_AND_INV_EVENT   0x16
#define EVENT_TYPE_PERFCOUNTER_START            0x17
#define EVENT_TYPE_PERFCOUNTER_STOP             0x18
#define EVENT_TYPE_PIPELINESTAT_START		25
#define EVENT_TYPE_PIPELINESTAT_STOP		26
#define EVENT_TYPE_PERFCOUNTER_SAMPLE           0x1B
#define EVENT_TYPE_SAMPLE_PIPELINESTAT		30
#define EVENT_TYPE_SO_VGTSTREAMOUT_FLUSH	0x1f
#define EVENT_TYPE_SAMPLE_STREAMOUTSTATS	0x20
#define EVENT_TYPE_BOTTOM_OF_PIPE_TS		40
#define EVENT_TYPE_FLUSH_AND_INV_DB_META       0x2c /* supported on r700+ */
#define EVENT_TYPE_FLUSH_AND_INV_CB_META	46 /* supported on r700+ */
#define		EVENT_TYPE(x)                           ((x) << 0)
#define		EVENT_INDEX(x)                          ((x) << 8)
                /* 0 - any non-TS event
		 * 1 - ZPASS_DONE
		 * 2 - SAMPLE_PIPELINESTAT
		 * 3 - SAMPLE_STREAMOUTSTAT*
		 * 4 - *S_PARTIAL_FLUSH
		 * 5 - TS events
		 */

#define PREDICATION_OP_CLEAR 0x0
#define PREDICATION_OP_ZPASS 0x1
#define PREDICATION_OP_PRIMCOUNT 0x2
#define PRED_OP(x) ((x) << 16)
#define PREDICATION_CONTINUE (1 << 31)
#define PREDICATION_HINT_WAIT (0 << 12)
#define PREDICATION_HINT_NOWAIT_DRAW (1 << 12)
#define PREDICATION_DRAW_NOT_VISIBLE (0 << 8)
#define PREDICATION_DRAW_VISIBLE (1 << 8)

/* R600-R700*/
#define R_008490_CP_STRMOUT_CNTL		     0x008490
#define   S_008490_OFFSET_UPDATE_DONE(x)		(((unsigned)(x) & 0x1) << 0)
#define R_028AB0_VGT_STRMOUT_EN                      0x028AB0
#define   S_028AB0_STREAMOUT(x)                        (((unsigned)(x) & 0x1) << 0)
#define   G_028AB0_STREAMOUT(x)                        (((x) >> 0) & 0x1)
#define   C_028AB0_STREAMOUT                           0xFFFFFFFE
#define R_028B20_VGT_STRMOUT_BUFFER_EN               0x028B20
#define   S_028B20_BUFFER_0_EN(x)                      (((unsigned)(x) & 0x1) << 0)
#define   G_028B20_BUFFER_0_EN(x)                      (((x) >> 0) & 0x1)
#define   C_028B20_BUFFER_0_EN                         0xFFFFFFFE
#define   S_028B20_BUFFER_1_EN(x)                      (((unsigned)(x) & 0x1) << 1)
#define   G_028B20_BUFFER_1_EN(x)                      (((x) >> 1) & 0x1)
#define   C_028B20_BUFFER_1_EN                         0xFFFFFFFD
#define   S_028B20_BUFFER_2_EN(x)                      (((unsigned)(x) & 0x1) << 2)
#define   G_028B20_BUFFER_2_EN(x)                      (((x) >> 2) & 0x1)
#define   C_028B20_BUFFER_2_EN                         0xFFFFFFFB
#define   S_028B20_BUFFER_3_EN(x)                      (((unsigned)(x) & 0x1) << 3)
#define   G_028B20_BUFFER_3_EN(x)                      (((x) >> 3) & 0x1)
#define   C_028B20_BUFFER_3_EN                         0xFFFFFFF7
#define R_028AD0_VGT_STRMOUT_BUFFER_SIZE_0                              0x028AD0

#define     V_0280A0_SWAP_STD                          0x00000000
#define     V_0280A0_SWAP_ALT                          0x00000001
#define     V_0280A0_SWAP_STD_REV                      0x00000002
#define     V_0280A0_SWAP_ALT_REV                      0x00000003

/* EG+ */
#define R_0084FC_CP_STRMOUT_CNTL		     0x0084FC
#define   S_0084FC_OFFSET_UPDATE_DONE(x)		(((unsigned)(x) & 0x1) << 0)
#define R_028B94_VGT_STRMOUT_CONFIG                                     0x028B94
#define   S_028B94_STREAMOUT_0_EN(x)                                  (((unsigned)(x) & 0x1) << 0)
#define   G_028B94_STREAMOUT_0_EN(x)                                  (((x) >> 0) & 0x1)
#define   C_028B94_STREAMOUT_0_EN                                     0xFFFFFFFE
#define   S_028B94_STREAMOUT_1_EN(x)                                  (((unsigned)(x) & 0x1) << 1)
#define   G_028B94_STREAMOUT_1_EN(x)                                  (((x) >> 1) & 0x1)
#define   C_028B94_STREAMOUT_1_EN                                     0xFFFFFFFD
#define   S_028B94_STREAMOUT_2_EN(x)                                  (((unsigned)(x) & 0x1) << 2)
#define   G_028B94_STREAMOUT_2_EN(x)                                  (((x) >> 2) & 0x1)
#define   C_028B94_STREAMOUT_2_EN                                     0xFFFFFFFB
#define   S_028B94_STREAMOUT_3_EN(x)                                  (((unsigned)(x) & 0x1) << 3)
#define   G_028B94_STREAMOUT_3_EN(x)                                  (((x) >> 3) & 0x1)
#define   C_028B94_STREAMOUT_3_EN                                     0xFFFFFFF7
#define   S_028B94_RAST_STREAM(x)                                     (((unsigned)(x) & 0x07) << 4)
#define   G_028B94_RAST_STREAM(x)                                     (((x) >> 4) & 0x07)
#define   C_028B94_RAST_STREAM                                        0xFFFFFF8F
#define   S_028B94_RAST_STREAM_MASK(x)                                (((unsigned)(x) & 0x0F) << 8) /* SI+ */
#define   G_028B94_RAST_STREAM_MASK(x)                                (((x) >> 8) & 0x0F)
#define   C_028B94_RAST_STREAM_MASK                                   0xFFFFF0FF
#define   S_028B94_USE_RAST_STREAM_MASK(x)                            (((unsigned)(x) & 0x1) << 31) /* SI+ */
#define   G_028B94_USE_RAST_STREAM_MASK(x)                            (((x) >> 31) & 0x1)
#define   C_028B94_USE_RAST_STREAM_MASK                               0x7FFFFFFF
#define R_028B98_VGT_STRMOUT_BUFFER_CONFIG                              0x028B98
#define   S_028B98_STREAM_0_BUFFER_EN(x)                              (((unsigned)(x) & 0x0F) << 0)
#define   G_028B98_STREAM_0_BUFFER_EN(x)                              (((x) >> 0) & 0x0F)
#define   C_028B98_STREAM_0_BUFFER_EN                                 0xFFFFFFF0
#define   S_028B98_STREAM_1_BUFFER_EN(x)                              (((unsigned)(x) & 0x0F) << 4)
#define   G_028B98_STREAM_1_BUFFER_EN(x)                              (((x) >> 4) & 0x0F)
#define   C_028B98_STREAM_1_BUFFER_EN                                 0xFFFFFF0F
#define   S_028B98_STREAM_2_BUFFER_EN(x)                              (((unsigned)(x) & 0x0F) << 8)
#define   G_028B98_STREAM_2_BUFFER_EN(x)                              (((x) >> 8) & 0x0F)
#define   C_028B98_STREAM_2_BUFFER_EN                                 0xFFFFF0FF
#define   S_028B98_STREAM_3_BUFFER_EN(x)                              (((unsigned)(x) & 0x0F) << 12)
#define   G_028B98_STREAM_3_BUFFER_EN(x)                              (((x) >> 12) & 0x0F)
#define   C_028B98_STREAM_3_BUFFER_EN                                 0xFFFF0FFF

#define EG_R_028A4C_PA_SC_MODE_CNTL_1                0x028A4C
#define   EG_S_028A4C_PS_ITER_SAMPLE(x)                 (((unsigned)(x) & 0x1) << 16)
#define   EG_S_028A4C_FORCE_EOV_CNTDWN_ENABLE(x)        (((unsigned)(x) & 0x1) << 25)
#define   EG_S_028A4C_FORCE_EOV_REZ_ENABLE(x)           (((unsigned)(x) & 0x1) << 26)

#define CM_R_028804_DB_EQAA                          0x00028804
#define   S_028804_MAX_ANCHOR_SAMPLES(x)		(((unsigned)(x) & 0x7) << 0)
#define   S_028804_PS_ITER_SAMPLES(x)			(((unsigned)(x) & 0x7) << 4)
#define   S_028804_MASK_EXPORT_NUM_SAMPLES(x)		(((unsigned)(x) & 0x7) << 8)
#define   S_028804_ALPHA_TO_MASK_NUM_SAMPLES(x)		(((unsigned)(x) & 0x7) << 12)
#define   S_028804_HIGH_QUALITY_INTERSECTIONS(x)	(((unsigned)(x) & 0x1) << 16)
#define   S_028804_INCOHERENT_EQAA_READS(x)		(((unsigned)(x) & 0x1) << 17)
#define   S_028804_INTERPOLATE_COMP_Z(x)		(((unsigned)(x) & 0x1) << 18)
#define   S_028804_INTERPOLATE_SRC_Z(x)			(((unsigned)(x) & 0x1) << 19)
#define   S_028804_STATIC_ANCHOR_ASSOCIATIONS(x)	(((unsigned)(x) & 0x1) << 20)
#define   S_028804_ALPHA_TO_MASK_EQAA_DISABLE(x)	(((unsigned)(x) & 0x1) << 21)
#define   S_028804_OVERRASTERIZATION_AMOUNT(x)		(((unsigned)(x) & 0x07) << 24)
#define   S_028804_ENABLE_POSTZ_OVERRASTERIZATION(x)	(((unsigned)(x) & 0x1) << 27)
#define CM_R_028BDC_PA_SC_LINE_CNTL                  0x28bdc
#define   S_028BDC_EXPAND_LINE_WIDTH(x)                (((unsigned)(x) & 0x1) << 9)
#define   G_028BDC_EXPAND_LINE_WIDTH(x)                (((x) >> 9) & 0x1)
#define   C_028BDC_EXPAND_LINE_WIDTH                   0xFFFFFDFF
#define   S_028BDC_LAST_PIXEL(x)                       (((unsigned)(x) & 0x1) << 10)
#define   G_028BDC_LAST_PIXEL(x)                       (((x) >> 10) & 0x1)
#define   C_028BDC_LAST_PIXEL                          0xFFFFFBFF
#define   S_028BDC_PERPENDICULAR_ENDCAP_ENA(x)         (((unsigned)(x) & 0x1) << 11)
#define   G_028BDC_PERPENDICULAR_ENDCAP_ENA(x)         (((x) >> 11) & 0x1)
#define   C_028BDC_PERPENDICULAR_ENDCAP_ENA            0xFFFFF7FF
#define   S_028BDC_DX10_DIAMOND_TEST_ENA(x)            (((unsigned)(x) & 0x1) << 12)
#define   G_028BDC_DX10_DIAMOND_TEST_ENA(x)            (((x) >> 12) & 0x1)
#define   C_028BDC_DX10_DIAMOND_TEST_ENA               0xFFFFEFFF
#define CM_R_028BE0_PA_SC_AA_CONFIG                  0x28be0
#define   S_028BE0_MSAA_NUM_SAMPLES(x)                  (((unsigned)(x) & 0x7) << 0)
#define   S_028BE0_AA_MASK_CENTROID_DTMN(x)		(((unsigned)(x) & 0x1) << 4)
#define   S_028BE0_MAX_SAMPLE_DIST(x)			(((unsigned)(x) & 0xf) << 13)
#define   S_028BE0_MSAA_EXPOSED_SAMPLES(x)		(((unsigned)(x) & 0x7) << 20)
#define   S_028BE0_DETAIL_TO_EXPOSED_MODE(x)		(((unsigned)(x) & 0x3) << 24)
#define CM_R_028BF8_PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y0_0 0x28bf8
#define CM_R_028C08_PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y0_0 0x28c08
#define CM_R_028C18_PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y1_0 0x28c18
#define CM_R_028C28_PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y1_0 0x28c28

#define   EG_S_028C70_FAST_CLEAR(x)                       (((unsigned)(x) & 0x1) << 17)
#define   SI_S_028C70_FAST_CLEAR(x)                       (((unsigned)(x) & 0x1) << 13)

/*CIK+*/
#define R_0300FC_CP_STRMOUT_CNTL		     0x0300FC

#define R600_R_028C0C_PA_CL_GB_VERT_CLIP_ADJ         0x028C0C
#define CM_R_028BE8_PA_CL_GB_VERT_CLIP_ADJ           0x28be8
#define R_02843C_PA_CL_VPORT_XSCALE                  0x02843C

#define R_028250_PA_SC_VPORT_SCISSOR_0_TL                               0x028250
#define   S_028250_TL_X(x)                                            (((unsigned)(x) & 0x7FFF) << 0)
#define   G_028250_TL_X(x)                                            (((x) >> 0) & 0x7FFF)
#define   C_028250_TL_X                                               0xFFFF8000
#define   S_028250_TL_Y(x)                                            (((unsigned)(x) & 0x7FFF) << 16)
#define   G_028250_TL_Y(x)                                            (((x) >> 16) & 0x7FFF)
#define   C_028250_TL_Y                                               0x8000FFFF
#define   S_028250_WINDOW_OFFSET_DISABLE(x)                           (((unsigned)(x) & 0x1) << 31)
#define   G_028250_WINDOW_OFFSET_DISABLE(x)                           (((x) >> 31) & 0x1)
#define   C_028250_WINDOW_OFFSET_DISABLE                              0x7FFFFFFF
#define   S_028254_BR_X(x)                                            (((unsigned)(x) & 0x7FFF) << 0)
#define   G_028254_BR_X(x)                                            (((x) >> 0) & 0x7FFF)
#define   C_028254_BR_X                                               0xFFFF8000
#define   S_028254_BR_Y(x)                                            (((unsigned)(x) & 0x7FFF) << 16)
#define   G_028254_BR_Y(x)                                            (((x) >> 16) & 0x7FFF)
#define   C_028254_BR_Y                                               0x8000FFFF
#define R_0282D0_PA_SC_VPORT_ZMIN_0                                     0x0282D0
#define R_0282D4_PA_SC_VPORT_ZMAX_0                                     0x0282D4

#endif
