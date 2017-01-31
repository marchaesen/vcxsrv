/*
 * Southern Islands Register documentation
 *
 * Copyright (C) 2011  Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef SID_H
#define SID_H

/* si values */
#define SI_CONFIG_REG_OFFSET                 0x00008000
#define SI_CONFIG_REG_END                    0x0000B000
#define SI_SH_REG_OFFSET                     0x0000B000
#define SI_SH_REG_END                        0x0000C000
#define SI_CONTEXT_REG_OFFSET                0x00028000
#define SI_CONTEXT_REG_END                   0x00029000
#define CIK_UCONFIG_REG_OFFSET               0x00030000
#define CIK_UCONFIG_REG_END                  0x00038000

#define EVENT_TYPE_CACHE_FLUSH                  0x6
#define EVENT_TYPE_PS_PARTIAL_FLUSH            0x10
#define EVENT_TYPE_CACHE_FLUSH_AND_INV_TS_EVENT 0x14
#define EVENT_TYPE_ZPASS_DONE                  0x15
#define EVENT_TYPE_CACHE_FLUSH_AND_INV_EVENT   0x16
#define EVENT_TYPE_SO_VGTSTREAMOUT_FLUSH	0x1f
#define EVENT_TYPE_SAMPLE_STREAMOUTSTATS	0x20
#define		EVENT_TYPE(x)                           ((x) << 0)
#define		EVENT_INDEX(x)                          ((x) << 8)
                /* 0 - any non-TS event
		 * 1 - ZPASS_DONE
		 * 2 - SAMPLE_PIPELINESTAT
		 * 3 - SAMPLE_STREAMOUTSTAT*
		 * 4 - *S_PARTIAL_FLUSH
		 * 5 - TS events
		 */
#define EVENT_WRITE_INV_L2                   0x100000


#define PREDICATION_OP_CLEAR 0x0
#define PREDICATION_OP_ZPASS 0x1
#define PREDICATION_OP_PRIMCOUNT 0x2

#define PRED_OP(x) ((x) << 16)

#define PREDICATION_CONTINUE (1 << 31)

#define PREDICATION_HINT_WAIT (0 << 12)
#define PREDICATION_HINT_NOWAIT_DRAW (1 << 12)

#define PREDICATION_DRAW_NOT_VISIBLE (0 << 8)
#define PREDICATION_DRAW_VISIBLE (1 << 8)

#define R600_TEXEL_PITCH_ALIGNMENT_MASK        0x7

/* All registers defined in this packet section don't exist and the only
 * purpose of these definitions is to define packet encoding that
 * the IB parser understands, and also to have an accurate documentation.
 */
#define PKT3_NOP                               0x10
#define PKT3_SET_BASE                          0x11
#define PKT3_CLEAR_STATE                       0x12
#define PKT3_INDEX_BUFFER_SIZE                 0x13
#define PKT3_DISPATCH_DIRECT                   0x15
#define PKT3_DISPATCH_INDIRECT                 0x16
#define PKT3_OCCLUSION_QUERY                   0x1F /* new for CIK */
#define PKT3_SET_PREDICATION                   0x20
#define PKT3_COND_EXEC                         0x22
#define PKT3_PRED_EXEC                         0x23
#define PKT3_DRAW_INDIRECT                     0x24
#define PKT3_DRAW_INDEX_INDIRECT               0x25
#define PKT3_INDEX_BASE                        0x26
#define PKT3_DRAW_INDEX_2                      0x27
#define PKT3_CONTEXT_CONTROL                   0x28
#define     CONTEXT_CONTROL_LOAD_ENABLE(x)     (((unsigned)(x) & 0x1) << 31)
#define     CONTEXT_CONTROL_LOAD_CE_RAM(x)     (((unsigned)(x) & 0x1) << 28)
#define     CONTEXT_CONTROL_SHADOW_ENABLE(x)   (((unsigned)(x) & 0x1) << 31)
#define PKT3_INDEX_TYPE                        0x2A
#define PKT3_DRAW_INDIRECT_MULTI               0x2C
#define   R_2C3_DRAW_INDEX_LOC                  0x2C3
#define     S_2C3_COUNT_INDIRECT_ENABLE(x)      (((unsigned)(x) & 0x1) << 30)
#define     S_2C3_DRAW_INDEX_ENABLE(x)          (((unsigned)(x) & 0x1) << 31)
#define PKT3_DRAW_INDEX_AUTO                   0x2D
#define PKT3_DRAW_INDEX_IMMD                   0x2E /* not on CIK */
#define PKT3_NUM_INSTANCES                     0x2F
#define PKT3_DRAW_INDEX_MULTI_AUTO             0x30
#define PKT3_INDIRECT_BUFFER_SI                0x32 /* not on CIK */
#define PKT3_INDIRECT_BUFFER_CONST             0x33
#define PKT3_STRMOUT_BUFFER_UPDATE             0x34
#define PKT3_DRAW_INDEX_OFFSET_2               0x35
#define PKT3_WRITE_DATA                        0x37
#define   R_370_CONTROL				0x370 /* 0x[packet number][word index] */
#define     S_370_ENGINE_SEL(x)			(((unsigned)(x) & 0x3) << 30)
#define       V_370_ME				0
#define       V_370_PFP				1
#define       V_370_CE				2
#define       V_370_DE				3
#define     S_370_WR_CONFIRM(x)			(((unsigned)(x) & 0x1) << 20)
#define     S_370_WR_ONE_ADDR(x)		(((unsigned)(x) & 0x1) << 16)
#define     S_370_DST_SEL(x)			(((unsigned)(x) & 0xf) << 8)
#define       V_370_MEM_MAPPED_REGISTER		0
#define       V_370_MEMORY_SYNC			1
#define       V_370_TC_L2			2
#define       V_370_GDS				3
#define       V_370_RESERVED			4
#define       V_370_MEM_ASYNC			5
#define   R_371_DST_ADDR_LO			0x371
#define   R_372_DST_ADDR_HI			0x372
#define PKT3_DRAW_INDEX_INDIRECT_MULTI         0x38
#define PKT3_MEM_SEMAPHORE                     0x39
#define PKT3_MPEG_INDEX                        0x3A /* not on CIK */
#define PKT3_WAIT_REG_MEM                      0x3C
#define		WAIT_REG_MEM_EQUAL		3
#define PKT3_MEM_WRITE                         0x3D /* not on CIK */
#define PKT3_INDIRECT_BUFFER_CIK               0x3F /* new on CIK */
#define   R_3F0_IB_BASE_LO                     0x3F0
#define   R_3F1_IB_BASE_HI                     0x3F1
#define   R_3F2_CONTROL                        0x3F2
#define     S_3F2_IB_SIZE(x)                   (((unsigned)(x) & 0xfffff) << 0)
#define     G_3F2_IB_SIZE(x)                   (((unsigned)(x) >> 0) & 0xfffff)
#define     S_3F2_CHAIN(x)                     (((unsigned)(x) & 0x1) << 20)
#define     G_3F2_CHAIN(x)                     (((unsigned)(x) >> 20) & 0x1)
#define     S_3F2_VALID(x)                     (((unsigned)(x) & 0x1) << 23)

#define PKT3_COPY_DATA			       0x40
#define		COPY_DATA_SRC_SEL(x)		((x) & 0xf)
#define			COPY_DATA_REG		0
#define			COPY_DATA_MEM		1
#define                 COPY_DATA_PERF          4
#define                 COPY_DATA_IMM           5
#define		COPY_DATA_DST_SEL(x)		(((unsigned)(x) & 0xf) << 8)
#define		COPY_DATA_COUNT_SEL		(1 << 16)
#define		COPY_DATA_WR_CONFIRM		(1 << 20)
#define PKT3_PFP_SYNC_ME		       0x42
#define PKT3_SURFACE_SYNC                      0x43 /* deprecated on CIK, use ACQUIRE_MEM */
#define PKT3_ME_INITIALIZE                     0x44 /* not on CIK */
#define PKT3_COND_WRITE                        0x45
#define PKT3_EVENT_WRITE                       0x46
#define PKT3_EVENT_WRITE_EOP                   0x47
/* CP DMA bug: Any use of CP_DMA.DST_SEL=TC must be avoided when EOS packets
 * are used. Use DST_SEL=MC instead. For prefetch, use SRC_SEL=TC and
 * DST_SEL=MC. Only CIK chips are affected.
 */
/*#define PKT3_EVENT_WRITE_EOS                   0x48*/ /* fix CP DMA before uncommenting */
#define PKT3_RELEASE_MEM                       0x49
#define PKT3_ONE_REG_WRITE                     0x57 /* not on CIK */
#define PKT3_ACQUIRE_MEM                       0x58 /* new for CIK */
#define PKT3_SET_CONFIG_REG                    0x68
#define PKT3_SET_CONTEXT_REG                   0x69
#define PKT3_SET_SH_REG                        0x76
#define PKT3_SET_SH_REG_OFFSET                 0x77
#define PKT3_SET_UCONFIG_REG                   0x79 /* new for CIK */
#define PKT3_LOAD_CONST_RAM                    0x80
#define PKT3_WRITE_CONST_RAM                   0x81
#define PKT3_DUMP_CONST_RAM                    0x83
#define PKT3_INCREMENT_CE_COUNTER              0x84
#define PKT3_INCREMENT_DE_COUNTER              0x85
#define PKT3_WAIT_ON_CE_COUNTER                0x86

#define PKT_TYPE_S(x)                   (((unsigned)(x) & 0x3) << 30)
#define PKT_TYPE_G(x)                   (((x) >> 30) & 0x3)
#define PKT_TYPE_C                      0x3FFFFFFF
#define PKT_COUNT_S(x)                  (((unsigned)(x) & 0x3FFF) << 16)
#define PKT_COUNT_G(x)                  (((x) >> 16) & 0x3FFF)
#define PKT_COUNT_C                     0xC000FFFF
#define PKT0_BASE_INDEX_S(x)            (((unsigned)(x) & 0xFFFF) << 0)
#define PKT0_BASE_INDEX_G(x)            (((x) >> 0) & 0xFFFF)
#define PKT0_BASE_INDEX_C               0xFFFF0000
#define PKT3_IT_OPCODE_S(x)             (((unsigned)(x) & 0xFF) << 8)
#define PKT3_IT_OPCODE_G(x)             (((x) >> 8) & 0xFF)
#define PKT3_IT_OPCODE_C                0xFFFF00FF
#define PKT3_PREDICATE(x)               (((x) >> 0) & 0x1)
#define PKT3_SHADER_TYPE_S(x)           (((unsigned)(x) & 0x1) << 1)
#define PKT0(index, count) (PKT_TYPE_S(0) | PKT0_BASE_INDEX_S(index) | PKT_COUNT_S(count))
#define PKT3(op, count, predicate) (PKT_TYPE_S(3) | PKT_COUNT_S(count) | PKT3_IT_OPCODE_S(op) | PKT3_PREDICATE(predicate))

#define PKT3_CP_DMA					0x41
/* 1. header
 * 2. SRC_ADDR_LO [31:0] or DATA [31:0]
 * 3. CP_SYNC [31] | SRC_SEL [30:29] | ENGINE [27] | DST_SEL [21:20] | SRC_ADDR_HI [15:0]
 * 4. DST_ADDR_LO [31:0]
 * 5. DST_ADDR_HI [15:0]
 * 6. COMMAND [29:22] | BYTE_COUNT [20:0]
 */
#define   R_410_CP_DMA_WORD0		0x410 /* 0x[packet number][word index] */
#define     S_410_SRC_ADDR_LO(x)	((x) & 0xffffffff)
#define   R_411_CP_DMA_WORD1		0x411
#define     S_411_CP_SYNC(x)		(((unsigned)(x) & 0x1) << 31)
#define     S_411_SRC_SEL(x)		(((unsigned)(x) & 0x3) << 29)
#define       V_411_SRC_ADDR		0
#define       V_411_GDS			1 /* program SAS to 1 as well */
#define       V_411_DATA		2
#define       V_411_SRC_ADDR_TC_L2	3 /* new for CIK */
#define     S_411_ENGINE(x)		(((unsigned)(x) & 0x1) << 27)
#define       V_411_ME			0
#define       V_411_PFP			1
#define     S_411_DSL_SEL(x)		(((unsigned)(x) & 0x3) << 20)
#define       V_411_DST_ADDR		0
#define       V_411_GDS			1 /* program DAS to 1 as well */
#define       V_411_DST_ADDR_TC_L2	3 /* new for CIK */
#define     S_411_SRC_ADDR_HI(x)	((x) & 0xffff)
#define   R_412_CP_DMA_WORD2		0x412 /* 0x[packet number][word index] */
#define     S_412_DST_ADDR_LO(x)	((x) & 0xffffffff)
#define   R_413_CP_DMA_WORD3		0x413 /* 0x[packet number][word index] */
#define     S_413_DST_ADDR_HI(x)	((x) & 0xffff)
#define   R_414_COMMAND			0x414
#define     S_414_BYTE_COUNT(x)		((x) & 0x1fffff)
#define     S_414_DISABLE_WR_CONFIRM(x)	(((unsigned)(x) & 0x1) << 21)
#define     S_414_SRC_SWAP(x)		(((unsigned)(x) & 0x3) << 22)
#define       V_414_NONE		0
#define       V_414_8_IN_16		1
#define       V_414_8_IN_32		2
#define       V_414_8_IN_64		3
#define     S_414_DST_SWAP(x)		(((unsigned)(x) & 0x3) << 24)
#define       V_414_NONE		0
#define       V_414_8_IN_16		1
#define       V_414_8_IN_32		2
#define       V_414_8_IN_64		3
#define     S_414_SAS(x)		(((unsigned)(x) & 0x1) << 26)
#define       V_414_MEMORY		0
#define       V_414_REGISTER		1
#define     S_414_DAS(x)		(((unsigned)(x) & 0x1) << 27)
#define       V_414_MEMORY		0
#define       V_414_REGISTER		1
#define     S_414_SAIC(x)		(((unsigned)(x) & 0x1) << 28)
#define       V_414_INCREMENT		0
#define       V_414_NO_INCREMENT	1
#define     S_414_DAIC(x)		(((unsigned)(x) & 0x1) << 29)
#define       V_414_INCREMENT		0
#define       V_414_NO_INCREMENT	1
#define     S_414_RAW_WAIT(x)		(((unsigned)(x) & 0x1) << 30)

#define PKT3_DMA_DATA					0x50 /* new for CIK */
/* 1. header
 * 2. CP_SYNC [31] | SRC_SEL [30:29] | DST_SEL [21:20] | ENGINE [0]
 * 2. SRC_ADDR_LO [31:0] or DATA [31:0]
 * 3. SRC_ADDR_HI [31:0]
 * 4. DST_ADDR_LO [31:0]
 * 5. DST_ADDR_HI [31:0]
 * 6. COMMAND [29:22] | BYTE_COUNT [20:0]
 */
#define   R_500_DMA_DATA_WORD0		0x500 /* 0x[packet number][word index] */
#define     S_500_CP_SYNC(x)		(((unsigned)(x) & 0x1) << 31)
#define     S_500_SRC_SEL(x)		(((unsigned)(x) & 0x3) << 29)
#define       V_500_SRC_ADDR		0
#define       V_500_GDS			1 /* program SAS to 1 as well */
#define       V_500_DATA		2
#define       V_500_SRC_ADDR_TC_L2	3 /* new for CIK */
#define     S_500_DSL_SEL(x)		(((unsigned)(x) & 0x3) << 20)
#define       V_500_DST_ADDR		0
#define       V_500_GDS			1 /* program DAS to 1 as well */
#define       V_500_DST_ADDR_TC_L2	3 /* new for CIK */
#define     S_500_ENGINE(x)		((x) & 0x1)
#define       V_500_ME			0
#define       V_500_PFP			1
#define   R_501_SRC_ADDR_LO		0x501
#define   R_502_SRC_ADDR_HI		0x502
#define   R_503_DST_ADDR_LO		0x503
#define   R_504_DST_ADDR_HI		0x504

#define R_000E4C_SRBM_STATUS2                                           0x000E4C
#define   S_000E4C_SDMA_RQ_PENDING(x)                                 (((unsigned)(x) & 0x1) << 0)
#define   G_000E4C_SDMA_RQ_PENDING(x)                                 (((x) >> 0) & 0x1)
#define   C_000E4C_SDMA_RQ_PENDING                                    0xFFFFFFFE
#define   S_000E4C_TST_RQ_PENDING(x)                                  (((unsigned)(x) & 0x1) << 1)
#define   G_000E4C_TST_RQ_PENDING(x)                                  (((x) >> 1) & 0x1)
#define   C_000E4C_TST_RQ_PENDING                                     0xFFFFFFFD
#define   S_000E4C_SDMA1_RQ_PENDING(x)                                (((unsigned)(x) & 0x1) << 2)
#define   G_000E4C_SDMA1_RQ_PENDING(x)                                (((x) >> 2) & 0x1)
#define   C_000E4C_SDMA1_RQ_PENDING                                   0xFFFFFFFB
#define   S_000E4C_VCE0_RQ_PENDING(x)                                 (((unsigned)(x) & 0x1) << 3)
#define   G_000E4C_VCE0_RQ_PENDING(x)                                 (((x) >> 3) & 0x1)
#define   C_000E4C_VCE0_RQ_PENDING                                    0xFFFFFFF7
#define   S_000E4C_VP8_BUSY(x)                                        (((unsigned)(x) & 0x1) << 4)
#define   G_000E4C_VP8_BUSY(x)                                        (((x) >> 4) & 0x1)
#define   C_000E4C_VP8_BUSY                                           0xFFFFFFEF
#define   S_000E4C_SDMA_BUSY(x)                                       (((unsigned)(x) & 0x1) << 5)
#define   G_000E4C_SDMA_BUSY(x)                                       (((x) >> 5) & 0x1)
#define   C_000E4C_SDMA_BUSY                                          0xFFFFFFDF
#define   S_000E4C_SDMA1_BUSY(x)                                      (((unsigned)(x) & 0x1) << 6)
#define   G_000E4C_SDMA1_BUSY(x)                                      (((x) >> 6) & 0x1)
#define   C_000E4C_SDMA1_BUSY                                         0xFFFFFFBF
#define   S_000E4C_VCE0_BUSY(x)                                       (((unsigned)(x) & 0x1) << 7)
#define   G_000E4C_VCE0_BUSY(x)                                       (((x) >> 7) & 0x1)
#define   C_000E4C_VCE0_BUSY                                          0xFFFFFF7F
#define   S_000E4C_XDMA_BUSY(x)                                       (((unsigned)(x) & 0x1) << 8)
#define   G_000E4C_XDMA_BUSY(x)                                       (((x) >> 8) & 0x1)
#define   C_000E4C_XDMA_BUSY                                          0xFFFFFEFF
#define   S_000E4C_CHUB_BUSY(x)                                       (((unsigned)(x) & 0x1) << 9)
#define   G_000E4C_CHUB_BUSY(x)                                       (((x) >> 9) & 0x1)
#define   C_000E4C_CHUB_BUSY                                          0xFFFFFDFF
#define   S_000E4C_SDMA2_BUSY(x)                                      (((unsigned)(x) & 0x1) << 10)
#define   G_000E4C_SDMA2_BUSY(x)                                      (((x) >> 10) & 0x1)
#define   C_000E4C_SDMA2_BUSY                                         0xFFFFFBFF
#define   S_000E4C_SDMA3_BUSY(x)                                      (((unsigned)(x) & 0x1) << 11)
#define   G_000E4C_SDMA3_BUSY(x)                                      (((x) >> 11) & 0x1)
#define   C_000E4C_SDMA3_BUSY                                         0xFFFFF7FF
#define   S_000E4C_SAMSCP_BUSY(x)                                     (((unsigned)(x) & 0x1) << 12)
#define   G_000E4C_SAMSCP_BUSY(x)                                     (((x) >> 12) & 0x1)
#define   C_000E4C_SAMSCP_BUSY                                        0xFFFFEFFF
#define   S_000E4C_ISP_BUSY(x)                                        (((unsigned)(x) & 0x1) << 13)
#define   G_000E4C_ISP_BUSY(x)                                        (((x) >> 13) & 0x1)
#define   C_000E4C_ISP_BUSY                                           0xFFFFDFFF
#define   S_000E4C_VCE1_BUSY(x)                                       (((unsigned)(x) & 0x1) << 14)
#define   G_000E4C_VCE1_BUSY(x)                                       (((x) >> 14) & 0x1)
#define   C_000E4C_VCE1_BUSY                                          0xFFFFBFFF
#define   S_000E4C_ODE_BUSY(x)                                        (((unsigned)(x) & 0x1) << 15)
#define   G_000E4C_ODE_BUSY(x)                                        (((x) >> 15) & 0x1)
#define   C_000E4C_ODE_BUSY                                           0xFFFF7FFF
#define   S_000E4C_SDMA2_RQ_PENDING(x)                                (((unsigned)(x) & 0x1) << 16)
#define   G_000E4C_SDMA2_RQ_PENDING(x)                                (((x) >> 16) & 0x1)
#define   C_000E4C_SDMA2_RQ_PENDING                                   0xFFFEFFFF
#define   S_000E4C_SDMA3_RQ_PENDING(x)                                (((unsigned)(x) & 0x1) << 17)
#define   G_000E4C_SDMA3_RQ_PENDING(x)                                (((x) >> 17) & 0x1)
#define   C_000E4C_SDMA3_RQ_PENDING                                   0xFFFDFFFF
#define   S_000E4C_SAMSCP_RQ_PENDING(x)                               (((unsigned)(x) & 0x1) << 18)
#define   G_000E4C_SAMSCP_RQ_PENDING(x)                               (((x) >> 18) & 0x1)
#define   C_000E4C_SAMSCP_RQ_PENDING                                  0xFFFBFFFF
#define   S_000E4C_ISP_RQ_PENDING(x)                                  (((unsigned)(x) & 0x1) << 19)
#define   G_000E4C_ISP_RQ_PENDING(x)                                  (((x) >> 19) & 0x1)
#define   C_000E4C_ISP_RQ_PENDING                                     0xFFF7FFFF
#define   S_000E4C_VCE1_RQ_PENDING(x)                                 (((unsigned)(x) & 0x1) << 20)
#define   G_000E4C_VCE1_RQ_PENDING(x)                                 (((x) >> 20) & 0x1)
#define   C_000E4C_VCE1_RQ_PENDING                                    0xFFEFFFFF
#define R_000E50_SRBM_STATUS                                            0x000E50
#define   S_000E50_UVD_RQ_PENDING(x)                                  (((unsigned)(x) & 0x1) << 1)
#define   G_000E50_UVD_RQ_PENDING(x)                                  (((x) >> 1) & 0x1)
#define   C_000E50_UVD_RQ_PENDING                                     0xFFFFFFFD
#define   S_000E50_SAMMSP_RQ_PENDING(x)                               (((unsigned)(x) & 0x1) << 2)
#define   G_000E50_SAMMSP_RQ_PENDING(x)                               (((x) >> 2) & 0x1)
#define   C_000E50_SAMMSP_RQ_PENDING                                  0xFFFFFFFB
#define   S_000E50_ACP_RQ_PENDING(x)                                  (((unsigned)(x) & 0x1) << 3)
#define   G_000E50_ACP_RQ_PENDING(x)                                  (((x) >> 3) & 0x1)
#define   C_000E50_ACP_RQ_PENDING                                     0xFFFFFFF7
#define   S_000E50_SMU_RQ_PENDING(x)                                  (((unsigned)(x) & 0x1) << 4)
#define   G_000E50_SMU_RQ_PENDING(x)                                  (((x) >> 4) & 0x1)
#define   C_000E50_SMU_RQ_PENDING                                     0xFFFFFFEF
#define   S_000E50_GRBM_RQ_PENDING(x)                                 (((unsigned)(x) & 0x1) << 5)
#define   G_000E50_GRBM_RQ_PENDING(x)                                 (((x) >> 5) & 0x1)
#define   C_000E50_GRBM_RQ_PENDING                                    0xFFFFFFDF
#define   S_000E50_HI_RQ_PENDING(x)                                   (((unsigned)(x) & 0x1) << 6)
#define   G_000E50_HI_RQ_PENDING(x)                                   (((x) >> 6) & 0x1)
#define   C_000E50_HI_RQ_PENDING                                      0xFFFFFFBF
#define   S_000E50_VMC_BUSY(x)                                        (((unsigned)(x) & 0x1) << 8)
#define   G_000E50_VMC_BUSY(x)                                        (((x) >> 8) & 0x1)
#define   C_000E50_VMC_BUSY                                           0xFFFFFEFF
#define   S_000E50_MCB_BUSY(x)                                        (((unsigned)(x) & 0x1) << 9)
#define   G_000E50_MCB_BUSY(x)                                        (((x) >> 9) & 0x1)
#define   C_000E50_MCB_BUSY                                           0xFFFFFDFF
#define   S_000E50_MCB_NON_DISPLAY_BUSY(x)                            (((unsigned)(x) & 0x1) << 10)
#define   G_000E50_MCB_NON_DISPLAY_BUSY(x)                            (((x) >> 10) & 0x1)
#define   C_000E50_MCB_NON_DISPLAY_BUSY                               0xFFFFFBFF
#define   S_000E50_MCC_BUSY(x)                                        (((unsigned)(x) & 0x1) << 11)
#define   G_000E50_MCC_BUSY(x)                                        (((x) >> 11) & 0x1)
#define   C_000E50_MCC_BUSY                                           0xFFFFF7FF
#define   S_000E50_MCD_BUSY(x)                                        (((unsigned)(x) & 0x1) << 12)
#define   G_000E50_MCD_BUSY(x)                                        (((x) >> 12) & 0x1)
#define   C_000E50_MCD_BUSY                                           0xFFFFEFFF
#define   S_000E50_VMC1_BUSY(x)                                       (((unsigned)(x) & 0x1) << 13)
#define   G_000E50_VMC1_BUSY(x)                                       (((x) >> 13) & 0x1)
#define   C_000E50_VMC1_BUSY                                          0xFFFFDFFF
#define   S_000E50_SEM_BUSY(x)                                        (((unsigned)(x) & 0x1) << 14)
#define   G_000E50_SEM_BUSY(x)                                        (((x) >> 14) & 0x1)
#define   C_000E50_SEM_BUSY                                           0xFFFFBFFF
#define   S_000E50_ACP_BUSY(x)                                        (((unsigned)(x) & 0x1) << 16)
#define   G_000E50_ACP_BUSY(x)                                        (((x) >> 16) & 0x1)
#define   C_000E50_ACP_BUSY                                           0xFFFEFFFF
#define   S_000E50_IH_BUSY(x)                                         (((unsigned)(x) & 0x1) << 17)
#define   G_000E50_IH_BUSY(x)                                         (((x) >> 17) & 0x1)
#define   C_000E50_IH_BUSY                                            0xFFFDFFFF
#define   S_000E50_UVD_BUSY(x)                                        (((unsigned)(x) & 0x1) << 19)
#define   G_000E50_UVD_BUSY(x)                                        (((x) >> 19) & 0x1)
#define   C_000E50_UVD_BUSY                                           0xFFF7FFFF
#define   S_000E50_SAMMSP_BUSY(x)                                     (((unsigned)(x) & 0x1) << 20)
#define   G_000E50_SAMMSP_BUSY(x)                                     (((x) >> 20) & 0x1)
#define   C_000E50_SAMMSP_BUSY                                        0xFFEFFFFF
#define   S_000E50_GCATCL2_BUSY(x)                                    (((unsigned)(x) & 0x1) << 21)
#define   G_000E50_GCATCL2_BUSY(x)                                    (((x) >> 21) & 0x1)
#define   C_000E50_GCATCL2_BUSY                                       0xFFDFFFFF
#define   S_000E50_OSATCL2_BUSY(x)                                    (((unsigned)(x) & 0x1) << 22)
#define   G_000E50_OSATCL2_BUSY(x)                                    (((x) >> 22) & 0x1)
#define   C_000E50_OSATCL2_BUSY                                       0xFFBFFFFF
#define   S_000E50_BIF_BUSY(x)                                        (((unsigned)(x) & 0x1) << 29)
#define   G_000E50_BIF_BUSY(x)                                        (((x) >> 29) & 0x1)
#define   C_000E50_BIF_BUSY                                           0xDFFFFFFF
#define R_000E54_SRBM_STATUS3                                           0x000E54
#define   S_000E54_MCC0_BUSY(x)                                       (((unsigned)(x) & 0x1) << 0)
#define   G_000E54_MCC0_BUSY(x)                                       (((x) >> 0) & 0x1)
#define   C_000E54_MCC0_BUSY                                          0xFFFFFFFE
#define   S_000E54_MCC1_BUSY(x)                                       (((unsigned)(x) & 0x1) << 1)
#define   G_000E54_MCC1_BUSY(x)                                       (((x) >> 1) & 0x1)
#define   C_000E54_MCC1_BUSY                                          0xFFFFFFFD
#define   S_000E54_MCC2_BUSY(x)                                       (((unsigned)(x) & 0x1) << 2)
#define   G_000E54_MCC2_BUSY(x)                                       (((x) >> 2) & 0x1)
#define   C_000E54_MCC2_BUSY                                          0xFFFFFFFB
#define   S_000E54_MCC3_BUSY(x)                                       (((unsigned)(x) & 0x1) << 3)
#define   G_000E54_MCC3_BUSY(x)                                       (((x) >> 3) & 0x1)
#define   C_000E54_MCC3_BUSY                                          0xFFFFFFF7
#define   S_000E54_MCC4_BUSY(x)                                       (((unsigned)(x) & 0x1) << 4)
#define   G_000E54_MCC4_BUSY(x)                                       (((x) >> 4) & 0x1)
#define   C_000E54_MCC4_BUSY                                          0xFFFFFFEF
#define   S_000E54_MCC5_BUSY(x)                                       (((unsigned)(x) & 0x1) << 5)
#define   G_000E54_MCC5_BUSY(x)                                       (((x) >> 5) & 0x1)
#define   C_000E54_MCC5_BUSY                                          0xFFFFFFDF
#define   S_000E54_MCC6_BUSY(x)                                       (((unsigned)(x) & 0x1) << 6)
#define   G_000E54_MCC6_BUSY(x)                                       (((x) >> 6) & 0x1)
#define   C_000E54_MCC6_BUSY                                          0xFFFFFFBF
#define   S_000E54_MCC7_BUSY(x)                                       (((unsigned)(x) & 0x1) << 7)
#define   G_000E54_MCC7_BUSY(x)                                       (((x) >> 7) & 0x1)
#define   C_000E54_MCC7_BUSY                                          0xFFFFFF7F
#define   S_000E54_MCD0_BUSY(x)                                       (((unsigned)(x) & 0x1) << 8)
#define   G_000E54_MCD0_BUSY(x)                                       (((x) >> 8) & 0x1)
#define   C_000E54_MCD0_BUSY                                          0xFFFFFEFF
#define   S_000E54_MCD1_BUSY(x)                                       (((unsigned)(x) & 0x1) << 9)
#define   G_000E54_MCD1_BUSY(x)                                       (((x) >> 9) & 0x1)
#define   C_000E54_MCD1_BUSY                                          0xFFFFFDFF
#define   S_000E54_MCD2_BUSY(x)                                       (((unsigned)(x) & 0x1) << 10)
#define   G_000E54_MCD2_BUSY(x)                                       (((x) >> 10) & 0x1)
#define   C_000E54_MCD2_BUSY                                          0xFFFFFBFF
#define   S_000E54_MCD3_BUSY(x)                                       (((unsigned)(x) & 0x1) << 11)
#define   G_000E54_MCD3_BUSY(x)                                       (((x) >> 11) & 0x1)
#define   C_000E54_MCD3_BUSY                                          0xFFFFF7FF
#define   S_000E54_MCD4_BUSY(x)                                       (((unsigned)(x) & 0x1) << 12)
#define   G_000E54_MCD4_BUSY(x)                                       (((x) >> 12) & 0x1)
#define   C_000E54_MCD4_BUSY                                          0xFFFFEFFF
#define   S_000E54_MCD5_BUSY(x)                                       (((unsigned)(x) & 0x1) << 13)
#define   G_000E54_MCD5_BUSY(x)                                       (((x) >> 13) & 0x1)
#define   C_000E54_MCD5_BUSY                                          0xFFFFDFFF
#define   S_000E54_MCD6_BUSY(x)                                       (((unsigned)(x) & 0x1) << 14)
#define   G_000E54_MCD6_BUSY(x)                                       (((x) >> 14) & 0x1)
#define   C_000E54_MCD6_BUSY                                          0xFFFFBFFF
#define   S_000E54_MCD7_BUSY(x)                                       (((unsigned)(x) & 0x1) << 15)
#define   G_000E54_MCD7_BUSY(x)                                       (((x) >> 15) & 0x1)
#define   C_000E54_MCD7_BUSY                                          0xFFFF7FFF
#define R_00D034_SDMA0_STATUS_REG                                       0x00D034
#define   S_00D034_IDLE(x)                                            (((unsigned)(x) & 0x1) << 0)
#define   G_00D034_IDLE(x)                                            (((x) >> 0) & 0x1)
#define   C_00D034_IDLE                                               0xFFFFFFFE
#define   S_00D034_REG_IDLE(x)                                        (((unsigned)(x) & 0x1) << 1)
#define   G_00D034_REG_IDLE(x)                                        (((x) >> 1) & 0x1)
#define   C_00D034_REG_IDLE                                           0xFFFFFFFD
#define   S_00D034_RB_EMPTY(x)                                        (((unsigned)(x) & 0x1) << 2)
#define   G_00D034_RB_EMPTY(x)                                        (((x) >> 2) & 0x1)
#define   C_00D034_RB_EMPTY                                           0xFFFFFFFB
#define   S_00D034_RB_FULL(x)                                         (((unsigned)(x) & 0x1) << 3)
#define   G_00D034_RB_FULL(x)                                         (((x) >> 3) & 0x1)
#define   C_00D034_RB_FULL                                            0xFFFFFFF7
#define   S_00D034_RB_CMD_IDLE(x)                                     (((unsigned)(x) & 0x1) << 4)
#define   G_00D034_RB_CMD_IDLE(x)                                     (((x) >> 4) & 0x1)
#define   C_00D034_RB_CMD_IDLE                                        0xFFFFFFEF
#define   S_00D034_RB_CMD_FULL(x)                                     (((unsigned)(x) & 0x1) << 5)
#define   G_00D034_RB_CMD_FULL(x)                                     (((x) >> 5) & 0x1)
#define   C_00D034_RB_CMD_FULL                                        0xFFFFFFDF
#define   S_00D034_IB_CMD_IDLE(x)                                     (((unsigned)(x) & 0x1) << 6)
#define   G_00D034_IB_CMD_IDLE(x)                                     (((x) >> 6) & 0x1)
#define   C_00D034_IB_CMD_IDLE                                        0xFFFFFFBF
#define   S_00D034_IB_CMD_FULL(x)                                     (((unsigned)(x) & 0x1) << 7)
#define   G_00D034_IB_CMD_FULL(x)                                     (((x) >> 7) & 0x1)
#define   C_00D034_IB_CMD_FULL                                        0xFFFFFF7F
#define   S_00D034_BLOCK_IDLE(x)                                      (((unsigned)(x) & 0x1) << 8)
#define   G_00D034_BLOCK_IDLE(x)                                      (((x) >> 8) & 0x1)
#define   C_00D034_BLOCK_IDLE                                         0xFFFFFEFF
#define   S_00D034_INSIDE_IB(x)                                       (((unsigned)(x) & 0x1) << 9)
#define   G_00D034_INSIDE_IB(x)                                       (((x) >> 9) & 0x1)
#define   C_00D034_INSIDE_IB                                          0xFFFFFDFF
#define   S_00D034_EX_IDLE(x)                                         (((unsigned)(x) & 0x1) << 10)
#define   G_00D034_EX_IDLE(x)                                         (((x) >> 10) & 0x1)
#define   C_00D034_EX_IDLE                                            0xFFFFFBFF
#define   S_00D034_EX_IDLE_POLL_TIMER_EXPIRE(x)                       (((unsigned)(x) & 0x1) << 11)
#define   G_00D034_EX_IDLE_POLL_TIMER_EXPIRE(x)                       (((x) >> 11) & 0x1)
#define   C_00D034_EX_IDLE_POLL_TIMER_EXPIRE                          0xFFFFF7FF
#define   S_00D034_PACKET_READY(x)                                    (((unsigned)(x) & 0x1) << 12)
#define   G_00D034_PACKET_READY(x)                                    (((x) >> 12) & 0x1)
#define   C_00D034_PACKET_READY                                       0xFFFFEFFF
#define   S_00D034_MC_WR_IDLE(x)                                      (((unsigned)(x) & 0x1) << 13)
#define   G_00D034_MC_WR_IDLE(x)                                      (((x) >> 13) & 0x1)
#define   C_00D034_MC_WR_IDLE                                         0xFFFFDFFF
#define   S_00D034_SRBM_IDLE(x)                                       (((unsigned)(x) & 0x1) << 14)
#define   G_00D034_SRBM_IDLE(x)                                       (((x) >> 14) & 0x1)
#define   C_00D034_SRBM_IDLE                                          0xFFFFBFFF
#define   S_00D034_CONTEXT_EMPTY(x)                                   (((unsigned)(x) & 0x1) << 15)
#define   G_00D034_CONTEXT_EMPTY(x)                                   (((x) >> 15) & 0x1)
#define   C_00D034_CONTEXT_EMPTY                                      0xFFFF7FFF
#define   S_00D034_DELTA_RPTR_FULL(x)                                 (((unsigned)(x) & 0x1) << 16)
#define   G_00D034_DELTA_RPTR_FULL(x)                                 (((x) >> 16) & 0x1)
#define   C_00D034_DELTA_RPTR_FULL                                    0xFFFEFFFF
#define   S_00D034_RB_MC_RREQ_IDLE(x)                                 (((unsigned)(x) & 0x1) << 17)
#define   G_00D034_RB_MC_RREQ_IDLE(x)                                 (((x) >> 17) & 0x1)
#define   C_00D034_RB_MC_RREQ_IDLE                                    0xFFFDFFFF
#define   S_00D034_IB_MC_RREQ_IDLE(x)                                 (((unsigned)(x) & 0x1) << 18)
#define   G_00D034_IB_MC_RREQ_IDLE(x)                                 (((x) >> 18) & 0x1)
#define   C_00D034_IB_MC_RREQ_IDLE                                    0xFFFBFFFF
#define   S_00D034_MC_RD_IDLE(x)                                      (((unsigned)(x) & 0x1) << 19)
#define   G_00D034_MC_RD_IDLE(x)                                      (((x) >> 19) & 0x1)
#define   C_00D034_MC_RD_IDLE                                         0xFFF7FFFF
#define   S_00D034_DELTA_RPTR_EMPTY(x)                                (((unsigned)(x) & 0x1) << 20)
#define   G_00D034_DELTA_RPTR_EMPTY(x)                                (((x) >> 20) & 0x1)
#define   C_00D034_DELTA_RPTR_EMPTY                                   0xFFEFFFFF
#define   S_00D034_MC_RD_RET_STALL(x)                                 (((unsigned)(x) & 0x1) << 21)
#define   G_00D034_MC_RD_RET_STALL(x)                                 (((x) >> 21) & 0x1)
#define   C_00D034_MC_RD_RET_STALL                                    0xFFDFFFFF
#define   S_00D034_MC_RD_NO_POLL_IDLE(x)                              (((unsigned)(x) & 0x1) << 22)
#define   G_00D034_MC_RD_NO_POLL_IDLE(x)                              (((x) >> 22) & 0x1)
#define   C_00D034_MC_RD_NO_POLL_IDLE                                 0xFFBFFFFF
#define   S_00D034_PREV_CMD_IDLE(x)                                   (((unsigned)(x) & 0x1) << 25)
#define   G_00D034_PREV_CMD_IDLE(x)                                   (((x) >> 25) & 0x1)
#define   C_00D034_PREV_CMD_IDLE                                      0xFDFFFFFF
#define   S_00D034_SEM_IDLE(x)                                        (((unsigned)(x) & 0x1) << 26)
#define   G_00D034_SEM_IDLE(x)                                        (((x) >> 26) & 0x1)
#define   C_00D034_SEM_IDLE                                           0xFBFFFFFF
#define   S_00D034_SEM_REQ_STALL(x)                                   (((unsigned)(x) & 0x1) << 27)
#define   G_00D034_SEM_REQ_STALL(x)                                   (((x) >> 27) & 0x1)
#define   C_00D034_SEM_REQ_STALL                                      0xF7FFFFFF
#define   S_00D034_SEM_RESP_STATE(x)                                  (((unsigned)(x) & 0x03) << 28)
#define   G_00D034_SEM_RESP_STATE(x)                                  (((x) >> 28) & 0x03)
#define   C_00D034_SEM_RESP_STATE                                     0xCFFFFFFF
#define   S_00D034_INT_IDLE(x)                                        (((unsigned)(x) & 0x1) << 30)
#define   G_00D034_INT_IDLE(x)                                        (((x) >> 30) & 0x1)
#define   C_00D034_INT_IDLE                                           0xBFFFFFFF
#define   S_00D034_INT_REQ_STALL(x)                                   (((unsigned)(x) & 0x1) << 31)
#define   G_00D034_INT_REQ_STALL(x)                                   (((x) >> 31) & 0x1)
#define   C_00D034_INT_REQ_STALL                                      0x7FFFFFFF
#define R_00D834_SDMA1_STATUS_REG                                       0x00D834
#define R_008008_GRBM_STATUS2                                           0x008008
#define   S_008008_ME0PIPE1_CMDFIFO_AVAIL(x)                          (((unsigned)(x) & 0x0F) << 0)
#define   G_008008_ME0PIPE1_CMDFIFO_AVAIL(x)                          (((x) >> 0) & 0x0F)
#define   C_008008_ME0PIPE1_CMDFIFO_AVAIL                             0xFFFFFFF0
#define   S_008008_ME0PIPE1_CF_RQ_PENDING(x)                          (((unsigned)(x) & 0x1) << 4)
#define   G_008008_ME0PIPE1_CF_RQ_PENDING(x)                          (((x) >> 4) & 0x1)
#define   C_008008_ME0PIPE1_CF_RQ_PENDING                             0xFFFFFFEF
#define   S_008008_ME0PIPE1_PF_RQ_PENDING(x)                          (((unsigned)(x) & 0x1) << 5)
#define   G_008008_ME0PIPE1_PF_RQ_PENDING(x)                          (((x) >> 5) & 0x1)
#define   C_008008_ME0PIPE1_PF_RQ_PENDING                             0xFFFFFFDF
#define   S_008008_ME1PIPE0_RQ_PENDING(x)                             (((unsigned)(x) & 0x1) << 6)
#define   G_008008_ME1PIPE0_RQ_PENDING(x)                             (((x) >> 6) & 0x1)
#define   C_008008_ME1PIPE0_RQ_PENDING                                0xFFFFFFBF
#define   S_008008_ME1PIPE1_RQ_PENDING(x)                             (((unsigned)(x) & 0x1) << 7)
#define   G_008008_ME1PIPE1_RQ_PENDING(x)                             (((x) >> 7) & 0x1)
#define   C_008008_ME1PIPE1_RQ_PENDING                                0xFFFFFF7F
#define   S_008008_ME1PIPE2_RQ_PENDING(x)                             (((unsigned)(x) & 0x1) << 8)
#define   G_008008_ME1PIPE2_RQ_PENDING(x)                             (((x) >> 8) & 0x1)
#define   C_008008_ME1PIPE2_RQ_PENDING                                0xFFFFFEFF
#define   S_008008_ME1PIPE3_RQ_PENDING(x)                             (((unsigned)(x) & 0x1) << 9)
#define   G_008008_ME1PIPE3_RQ_PENDING(x)                             (((x) >> 9) & 0x1)
#define   C_008008_ME1PIPE3_RQ_PENDING                                0xFFFFFDFF
#define   S_008008_ME2PIPE0_RQ_PENDING(x)                             (((unsigned)(x) & 0x1) << 10)
#define   G_008008_ME2PIPE0_RQ_PENDING(x)                             (((x) >> 10) & 0x1)
#define   C_008008_ME2PIPE0_RQ_PENDING                                0xFFFFFBFF
#define   S_008008_ME2PIPE1_RQ_PENDING(x)                             (((unsigned)(x) & 0x1) << 11)
#define   G_008008_ME2PIPE1_RQ_PENDING(x)                             (((x) >> 11) & 0x1)
#define   C_008008_ME2PIPE1_RQ_PENDING                                0xFFFFF7FF
#define   S_008008_ME2PIPE2_RQ_PENDING(x)                             (((unsigned)(x) & 0x1) << 12)
#define   G_008008_ME2PIPE2_RQ_PENDING(x)                             (((x) >> 12) & 0x1)
#define   C_008008_ME2PIPE2_RQ_PENDING                                0xFFFFEFFF
#define   S_008008_ME2PIPE3_RQ_PENDING(x)                             (((unsigned)(x) & 0x1) << 13)
#define   G_008008_ME2PIPE3_RQ_PENDING(x)                             (((x) >> 13) & 0x1)
#define   C_008008_ME2PIPE3_RQ_PENDING                                0xFFFFDFFF
#define   S_008008_RLC_RQ_PENDING(x)                                  (((unsigned)(x) & 0x1) << 14)
#define   G_008008_RLC_RQ_PENDING(x)                                  (((x) >> 14) & 0x1)
#define   C_008008_RLC_RQ_PENDING                                     0xFFFFBFFF
#define   S_008008_RLC_BUSY(x)                                        (((unsigned)(x) & 0x1) << 24)
#define   G_008008_RLC_BUSY(x)                                        (((x) >> 24) & 0x1)
#define   C_008008_RLC_BUSY                                           0xFEFFFFFF
#define   S_008008_TC_BUSY(x)                                         (((unsigned)(x) & 0x1) << 25)
#define   G_008008_TC_BUSY(x)                                         (((x) >> 25) & 0x1)
#define   C_008008_TC_BUSY                                            0xFDFFFFFF
#define   S_008008_TCC_CC_RESIDENT(x)                                 (((unsigned)(x) & 0x1) << 26)
#define   G_008008_TCC_CC_RESIDENT(x)                                 (((x) >> 26) & 0x1)
#define   C_008008_TCC_CC_RESIDENT                                    0xFBFFFFFF
#define   S_008008_CPF_BUSY(x)                                        (((unsigned)(x) & 0x1) << 28)
#define   G_008008_CPF_BUSY(x)                                        (((x) >> 28) & 0x1)
#define   C_008008_CPF_BUSY                                           0xEFFFFFFF
#define   S_008008_CPC_BUSY(x)                                        (((unsigned)(x) & 0x1) << 29)
#define   G_008008_CPC_BUSY(x)                                        (((x) >> 29) & 0x1)
#define   C_008008_CPC_BUSY                                           0xDFFFFFFF
#define   S_008008_CPG_BUSY(x)                                        (((unsigned)(x) & 0x1) << 30)
#define   G_008008_CPG_BUSY(x)                                        (((x) >> 30) & 0x1)
#define   C_008008_CPG_BUSY                                           0xBFFFFFFF
#define R_008010_GRBM_STATUS                                            0x008010
#define   S_008010_ME0PIPE0_CMDFIFO_AVAIL(x)                          (((unsigned)(x) & 0x0F) << 0)
#define   G_008010_ME0PIPE0_CMDFIFO_AVAIL(x)                          (((x) >> 0) & 0x0F)
#define   C_008010_ME0PIPE0_CMDFIFO_AVAIL                             0xFFFFFFF0
#define   S_008010_SRBM_RQ_PENDING(x)                                 (((unsigned)(x) & 0x1) << 5)
#define   G_008010_SRBM_RQ_PENDING(x)                                 (((x) >> 5) & 0x1)
#define   C_008010_SRBM_RQ_PENDING                                    0xFFFFFFDF
#define   S_008010_ME0PIPE0_CF_RQ_PENDING(x)                          (((unsigned)(x) & 0x1) << 7)
#define   G_008010_ME0PIPE0_CF_RQ_PENDING(x)                          (((x) >> 7) & 0x1)
#define   C_008010_ME0PIPE0_CF_RQ_PENDING                             0xFFFFFF7F
#define   S_008010_ME0PIPE0_PF_RQ_PENDING(x)                          (((unsigned)(x) & 0x1) << 8)
#define   G_008010_ME0PIPE0_PF_RQ_PENDING(x)                          (((x) >> 8) & 0x1)
#define   C_008010_ME0PIPE0_PF_RQ_PENDING                             0xFFFFFEFF
#define   S_008010_GDS_DMA_RQ_PENDING(x)                              (((unsigned)(x) & 0x1) << 9)
#define   G_008010_GDS_DMA_RQ_PENDING(x)                              (((x) >> 9) & 0x1)
#define   C_008010_GDS_DMA_RQ_PENDING                                 0xFFFFFDFF
#define   S_008010_DB_CLEAN(x)                                        (((unsigned)(x) & 0x1) << 12)
#define   G_008010_DB_CLEAN(x)                                        (((x) >> 12) & 0x1)
#define   C_008010_DB_CLEAN                                           0xFFFFEFFF
#define   S_008010_CB_CLEAN(x)                                        (((unsigned)(x) & 0x1) << 13)
#define   G_008010_CB_CLEAN(x)                                        (((x) >> 13) & 0x1)
#define   C_008010_CB_CLEAN                                           0xFFFFDFFF
#define   S_008010_TA_BUSY(x)                                         (((unsigned)(x) & 0x1) << 14)
#define   G_008010_TA_BUSY(x)                                         (((x) >> 14) & 0x1)
#define   C_008010_TA_BUSY                                            0xFFFFBFFF
#define   S_008010_GDS_BUSY(x)                                        (((unsigned)(x) & 0x1) << 15)
#define   G_008010_GDS_BUSY(x)                                        (((x) >> 15) & 0x1)
#define   C_008010_GDS_BUSY                                           0xFFFF7FFF
#define   S_008010_WD_BUSY_NO_DMA(x)                                  (((unsigned)(x) & 0x1) << 16)
#define   G_008010_WD_BUSY_NO_DMA(x)                                  (((x) >> 16) & 0x1)
#define   C_008010_WD_BUSY_NO_DMA                                     0xFFFEFFFF
#define   S_008010_VGT_BUSY(x)                                        (((unsigned)(x) & 0x1) << 17)
#define   G_008010_VGT_BUSY(x)                                        (((x) >> 17) & 0x1)
#define   C_008010_VGT_BUSY                                           0xFFFDFFFF
#define   S_008010_IA_BUSY_NO_DMA(x)                                  (((unsigned)(x) & 0x1) << 18)
#define   G_008010_IA_BUSY_NO_DMA(x)                                  (((x) >> 18) & 0x1)
#define   C_008010_IA_BUSY_NO_DMA                                     0xFFFBFFFF
#define   S_008010_IA_BUSY(x)                                         (((unsigned)(x) & 0x1) << 19)
#define   G_008010_IA_BUSY(x)                                         (((x) >> 19) & 0x1)
#define   C_008010_IA_BUSY                                            0xFFF7FFFF
#define   S_008010_SX_BUSY(x)                                         (((unsigned)(x) & 0x1) << 20)
#define   G_008010_SX_BUSY(x)                                         (((x) >> 20) & 0x1)
#define   C_008010_SX_BUSY                                            0xFFEFFFFF
#define   S_008010_WD_BUSY(x)                                         (((unsigned)(x) & 0x1) << 21)
#define   G_008010_WD_BUSY(x)                                         (((x) >> 21) & 0x1)
#define   C_008010_WD_BUSY                                            0xFFDFFFFF
#define   S_008010_SPI_BUSY(x)                                        (((unsigned)(x) & 0x1) << 22)
#define   G_008010_SPI_BUSY(x)                                        (((x) >> 22) & 0x1)
#define   C_008010_SPI_BUSY                                           0xFFBFFFFF
#define   S_008010_BCI_BUSY(x)                                        (((unsigned)(x) & 0x1) << 23)
#define   G_008010_BCI_BUSY(x)                                        (((x) >> 23) & 0x1)
#define   C_008010_BCI_BUSY                                           0xFF7FFFFF
#define   S_008010_SC_BUSY(x)                                         (((unsigned)(x) & 0x1) << 24)
#define   G_008010_SC_BUSY(x)                                         (((x) >> 24) & 0x1)
#define   C_008010_SC_BUSY                                            0xFEFFFFFF
#define   S_008010_PA_BUSY(x)                                         (((unsigned)(x) & 0x1) << 25)
#define   G_008010_PA_BUSY(x)                                         (((x) >> 25) & 0x1)
#define   C_008010_PA_BUSY                                            0xFDFFFFFF
#define   S_008010_DB_BUSY(x)                                         (((unsigned)(x) & 0x1) << 26)
#define   G_008010_DB_BUSY(x)                                         (((x) >> 26) & 0x1)
#define   C_008010_DB_BUSY                                            0xFBFFFFFF
#define   S_008010_CP_COHERENCY_BUSY(x)                               (((unsigned)(x) & 0x1) << 28)
#define   G_008010_CP_COHERENCY_BUSY(x)                               (((x) >> 28) & 0x1)
#define   C_008010_CP_COHERENCY_BUSY                                  0xEFFFFFFF
#define   S_008010_CP_BUSY(x)                                         (((unsigned)(x) & 0x1) << 29)
#define   G_008010_CP_BUSY(x)                                         (((x) >> 29) & 0x1)
#define   C_008010_CP_BUSY                                            0xDFFFFFFF
#define   S_008010_CB_BUSY(x)                                         (((unsigned)(x) & 0x1) << 30)
#define   G_008010_CB_BUSY(x)                                         (((x) >> 30) & 0x1)
#define   C_008010_CB_BUSY                                            0xBFFFFFFF
#define   S_008010_GUI_ACTIVE(x)                                      (((unsigned)(x) & 0x1) << 31)
#define   G_008010_GUI_ACTIVE(x)                                      (((x) >> 31) & 0x1)
#define   C_008010_GUI_ACTIVE                                         0x7FFFFFFF
#define GRBM_GFX_INDEX                                                  0x802C
#define         INSTANCE_INDEX(x)                                     ((x) << 0)
#define         SH_INDEX(x)                                           ((x) << 8)
#define         SE_INDEX(x)                                           ((x) << 16)
#define         SH_BROADCAST_WRITES                                   (1 << 29)
#define         INSTANCE_BROADCAST_WRITES                             (1 << 30)
#define         SE_BROADCAST_WRITES                                   (1 << 31)
#define R_0084FC_CP_STRMOUT_CNTL		                        0x0084FC
#define   S_0084FC_OFFSET_UPDATE_DONE(x)		              (((unsigned)(x) & 0x1) << 0)
#define R_0085F0_CP_COHER_CNTL                                          0x0085F0
#define   S_0085F0_DEST_BASE_0_ENA(x)                                 (((unsigned)(x) & 0x1) << 0)
#define   G_0085F0_DEST_BASE_0_ENA(x)                                 (((x) >> 0) & 0x1)
#define   C_0085F0_DEST_BASE_0_ENA                                    0xFFFFFFFE
#define   S_0085F0_DEST_BASE_1_ENA(x)                                 (((unsigned)(x) & 0x1) << 1)
#define   G_0085F0_DEST_BASE_1_ENA(x)                                 (((x) >> 1) & 0x1)
#define   C_0085F0_DEST_BASE_1_ENA                                    0xFFFFFFFD
#define   S_0085F0_CB0_DEST_BASE_ENA(x)                               (((unsigned)(x) & 0x1) << 6)
#define   G_0085F0_CB0_DEST_BASE_ENA(x)                               (((x) >> 6) & 0x1)
#define   C_0085F0_CB0_DEST_BASE_ENA                                  0xFFFFFFBF
#define   S_0085F0_CB1_DEST_BASE_ENA(x)                               (((unsigned)(x) & 0x1) << 7)
#define   G_0085F0_CB1_DEST_BASE_ENA(x)                               (((x) >> 7) & 0x1)
#define   C_0085F0_CB1_DEST_BASE_ENA                                  0xFFFFFF7F
#define   S_0085F0_CB2_DEST_BASE_ENA(x)                               (((unsigned)(x) & 0x1) << 8)
#define   G_0085F0_CB2_DEST_BASE_ENA(x)                               (((x) >> 8) & 0x1)
#define   C_0085F0_CB2_DEST_BASE_ENA                                  0xFFFFFEFF
#define   S_0085F0_CB3_DEST_BASE_ENA(x)                               (((unsigned)(x) & 0x1) << 9)
#define   G_0085F0_CB3_DEST_BASE_ENA(x)                               (((x) >> 9) & 0x1)
#define   C_0085F0_CB3_DEST_BASE_ENA                                  0xFFFFFDFF
#define   S_0085F0_CB4_DEST_BASE_ENA(x)                               (((unsigned)(x) & 0x1) << 10)
#define   G_0085F0_CB4_DEST_BASE_ENA(x)                               (((x) >> 10) & 0x1)
#define   C_0085F0_CB4_DEST_BASE_ENA                                  0xFFFFFBFF
#define   S_0085F0_CB5_DEST_BASE_ENA(x)                               (((unsigned)(x) & 0x1) << 11)
#define   G_0085F0_CB5_DEST_BASE_ENA(x)                               (((x) >> 11) & 0x1)
#define   C_0085F0_CB5_DEST_BASE_ENA                                  0xFFFFF7FF
#define   S_0085F0_CB6_DEST_BASE_ENA(x)                               (((unsigned)(x) & 0x1) << 12)
#define   G_0085F0_CB6_DEST_BASE_ENA(x)                               (((x) >> 12) & 0x1)
#define   C_0085F0_CB6_DEST_BASE_ENA                                  0xFFFFEFFF
#define   S_0085F0_CB7_DEST_BASE_ENA(x)                               (((unsigned)(x) & 0x1) << 13)
#define   G_0085F0_CB7_DEST_BASE_ENA(x)                               (((x) >> 13) & 0x1)
#define   C_0085F0_CB7_DEST_BASE_ENA                                  0xFFFFDFFF
#define   S_0085F0_DB_DEST_BASE_ENA(x)                                (((unsigned)(x) & 0x1) << 14)
#define   G_0085F0_DB_DEST_BASE_ENA(x)                                (((x) >> 14) & 0x1)
#define   C_0085F0_DB_DEST_BASE_ENA                                   0xFFFFBFFF
#define   S_0085F0_DEST_BASE_2_ENA(x)                                 (((unsigned)(x) & 0x1) << 19)
#define   G_0085F0_DEST_BASE_2_ENA(x)                                 (((x) >> 19) & 0x1)
#define   C_0085F0_DEST_BASE_2_ENA                                    0xFFF7FFFF
#define   S_0085F0_DEST_BASE_3_ENA(x)                                 (((unsigned)(x) & 0x1) << 21)
#define   G_0085F0_DEST_BASE_3_ENA(x)                                 (((x) >> 21) & 0x1)
#define   C_0085F0_DEST_BASE_3_ENA                                    0xFFDFFFFF
#define   S_0085F0_TCL1_ACTION_ENA(x)                                 (((unsigned)(x) & 0x1) << 22)
#define   G_0085F0_TCL1_ACTION_ENA(x)                                 (((x) >> 22) & 0x1)
#define   C_0085F0_TCL1_ACTION_ENA                                    0xFFBFFFFF
#define   S_0085F0_TC_ACTION_ENA(x)                                   (((unsigned)(x) & 0x1) << 23)
#define   G_0085F0_TC_ACTION_ENA(x)                                   (((x) >> 23) & 0x1)
#define   C_0085F0_TC_ACTION_ENA                                      0xFF7FFFFF
#define   S_0085F0_CB_ACTION_ENA(x)                                   (((unsigned)(x) & 0x1) << 25)
#define   G_0085F0_CB_ACTION_ENA(x)                                   (((x) >> 25) & 0x1)
#define   C_0085F0_CB_ACTION_ENA                                      0xFDFFFFFF
#define   S_0085F0_DB_ACTION_ENA(x)                                   (((unsigned)(x) & 0x1) << 26)
#define   G_0085F0_DB_ACTION_ENA(x)                                   (((x) >> 26) & 0x1)
#define   C_0085F0_DB_ACTION_ENA                                      0xFBFFFFFF
#define   S_0085F0_SH_KCACHE_ACTION_ENA(x)                            (((unsigned)(x) & 0x1) << 27)
#define   G_0085F0_SH_KCACHE_ACTION_ENA(x)                            (((x) >> 27) & 0x1)
#define   C_0085F0_SH_KCACHE_ACTION_ENA                               0xF7FFFFFF
#define   S_0085F0_SH_ICACHE_ACTION_ENA(x)                            (((unsigned)(x) & 0x1) << 29)
#define   G_0085F0_SH_ICACHE_ACTION_ENA(x)                            (((x) >> 29) & 0x1)
#define   C_0085F0_SH_ICACHE_ACTION_ENA                               0xDFFFFFFF
#define R_0085F4_CP_COHER_SIZE                                          0x0085F4
#define R_0085F8_CP_COHER_BASE                                          0x0085F8
#define R_008014_GRBM_STATUS_SE0                                        0x008014
#define   S_008014_DB_CLEAN(x)                                        (((unsigned)(x) & 0x1) << 1)
#define   G_008014_DB_CLEAN(x)                                        (((x) >> 1) & 0x1)
#define   C_008014_DB_CLEAN                                           0xFFFFFFFD
#define   S_008014_CB_CLEAN(x)                                        (((unsigned)(x) & 0x1) << 2)
#define   G_008014_CB_CLEAN(x)                                        (((x) >> 2) & 0x1)
#define   C_008014_CB_CLEAN                                           0xFFFFFFFB
#define   S_008014_BCI_BUSY(x)                                        (((unsigned)(x) & 0x1) << 22)
#define   G_008014_BCI_BUSY(x)                                        (((x) >> 22) & 0x1)
#define   C_008014_BCI_BUSY                                           0xFFBFFFFF
#define   S_008014_VGT_BUSY(x)                                        (((unsigned)(x) & 0x1) << 23)
#define   G_008014_VGT_BUSY(x)                                        (((x) >> 23) & 0x1)
#define   C_008014_VGT_BUSY                                           0xFF7FFFFF
#define   S_008014_PA_BUSY(x)                                         (((unsigned)(x) & 0x1) << 24)
#define   G_008014_PA_BUSY(x)                                         (((x) >> 24) & 0x1)
#define   C_008014_PA_BUSY                                            0xFEFFFFFF
#define   S_008014_TA_BUSY(x)                                         (((unsigned)(x) & 0x1) << 25)
#define   G_008014_TA_BUSY(x)                                         (((x) >> 25) & 0x1)
#define   C_008014_TA_BUSY                                            0xFDFFFFFF
#define   S_008014_SX_BUSY(x)                                         (((unsigned)(x) & 0x1) << 26)
#define   G_008014_SX_BUSY(x)                                         (((x) >> 26) & 0x1)
#define   C_008014_SX_BUSY                                            0xFBFFFFFF
#define   S_008014_SPI_BUSY(x)                                        (((unsigned)(x) & 0x1) << 27)
#define   G_008014_SPI_BUSY(x)                                        (((x) >> 27) & 0x1)
#define   C_008014_SPI_BUSY                                           0xF7FFFFFF
#define   S_008014_SC_BUSY(x)                                         (((unsigned)(x) & 0x1) << 29)
#define   G_008014_SC_BUSY(x)                                         (((x) >> 29) & 0x1)
#define   C_008014_SC_BUSY                                            0xDFFFFFFF
#define   S_008014_DB_BUSY(x)                                         (((unsigned)(x) & 0x1) << 30)
#define   G_008014_DB_BUSY(x)                                         (((x) >> 30) & 0x1)
#define   C_008014_DB_BUSY                                            0xBFFFFFFF
#define   S_008014_CB_BUSY(x)                                         (((unsigned)(x) & 0x1) << 31)
#define   G_008014_CB_BUSY(x)                                         (((x) >> 31) & 0x1)
#define   C_008014_CB_BUSY                                            0x7FFFFFFF
#define R_008018_GRBM_STATUS_SE1                                        0x008018
#define   S_008018_DB_CLEAN(x)                                        (((unsigned)(x) & 0x1) << 1)
#define   G_008018_DB_CLEAN(x)                                        (((x) >> 1) & 0x1)
#define   C_008018_DB_CLEAN                                           0xFFFFFFFD
#define   S_008018_CB_CLEAN(x)                                        (((unsigned)(x) & 0x1) << 2)
#define   G_008018_CB_CLEAN(x)                                        (((x) >> 2) & 0x1)
#define   C_008018_CB_CLEAN                                           0xFFFFFFFB
#define   S_008018_BCI_BUSY(x)                                        (((unsigned)(x) & 0x1) << 22)
#define   G_008018_BCI_BUSY(x)                                        (((x) >> 22) & 0x1)
#define   C_008018_BCI_BUSY                                           0xFFBFFFFF
#define   S_008018_VGT_BUSY(x)                                        (((unsigned)(x) & 0x1) << 23)
#define   G_008018_VGT_BUSY(x)                                        (((x) >> 23) & 0x1)
#define   C_008018_VGT_BUSY                                           0xFF7FFFFF
#define   S_008018_PA_BUSY(x)                                         (((unsigned)(x) & 0x1) << 24)
#define   G_008018_PA_BUSY(x)                                         (((x) >> 24) & 0x1)
#define   C_008018_PA_BUSY                                            0xFEFFFFFF
#define   S_008018_TA_BUSY(x)                                         (((unsigned)(x) & 0x1) << 25)
#define   G_008018_TA_BUSY(x)                                         (((x) >> 25) & 0x1)
#define   C_008018_TA_BUSY                                            0xFDFFFFFF
#define   S_008018_SX_BUSY(x)                                         (((unsigned)(x) & 0x1) << 26)
#define   G_008018_SX_BUSY(x)                                         (((x) >> 26) & 0x1)
#define   C_008018_SX_BUSY                                            0xFBFFFFFF
#define   S_008018_SPI_BUSY(x)                                        (((unsigned)(x) & 0x1) << 27)
#define   G_008018_SPI_BUSY(x)                                        (((x) >> 27) & 0x1)
#define   C_008018_SPI_BUSY                                           0xF7FFFFFF
#define   S_008018_SC_BUSY(x)                                         (((unsigned)(x) & 0x1) << 29)
#define   G_008018_SC_BUSY(x)                                         (((x) >> 29) & 0x1)
#define   C_008018_SC_BUSY                                            0xDFFFFFFF
#define   S_008018_DB_BUSY(x)                                         (((unsigned)(x) & 0x1) << 30)
#define   G_008018_DB_BUSY(x)                                         (((x) >> 30) & 0x1)
#define   C_008018_DB_BUSY                                            0xBFFFFFFF
#define   S_008018_CB_BUSY(x)                                         (((unsigned)(x) & 0x1) << 31)
#define   G_008018_CB_BUSY(x)                                         (((x) >> 31) & 0x1)
#define   C_008018_CB_BUSY                                            0x7FFFFFFF
#define R_008038_GRBM_STATUS_SE2                                        0x008038
#define   S_008038_DB_CLEAN(x)                                        (((unsigned)(x) & 0x1) << 1)
#define   G_008038_DB_CLEAN(x)                                        (((x) >> 1) & 0x1)
#define   C_008038_DB_CLEAN                                           0xFFFFFFFD
#define   S_008038_CB_CLEAN(x)                                        (((unsigned)(x) & 0x1) << 2)
#define   G_008038_CB_CLEAN(x)                                        (((x) >> 2) & 0x1)
#define   C_008038_CB_CLEAN                                           0xFFFFFFFB
#define   S_008038_BCI_BUSY(x)                                        (((unsigned)(x) & 0x1) << 22)
#define   G_008038_BCI_BUSY(x)                                        (((x) >> 22) & 0x1)
#define   C_008038_BCI_BUSY                                           0xFFBFFFFF
#define   S_008038_VGT_BUSY(x)                                        (((unsigned)(x) & 0x1) << 23)
#define   G_008038_VGT_BUSY(x)                                        (((x) >> 23) & 0x1)
#define   C_008038_VGT_BUSY                                           0xFF7FFFFF
#define   S_008038_PA_BUSY(x)                                         (((unsigned)(x) & 0x1) << 24)
#define   G_008038_PA_BUSY(x)                                         (((x) >> 24) & 0x1)
#define   C_008038_PA_BUSY                                            0xFEFFFFFF
#define   S_008038_TA_BUSY(x)                                         (((unsigned)(x) & 0x1) << 25)
#define   G_008038_TA_BUSY(x)                                         (((x) >> 25) & 0x1)
#define   C_008038_TA_BUSY                                            0xFDFFFFFF
#define   S_008038_SX_BUSY(x)                                         (((unsigned)(x) & 0x1) << 26)
#define   G_008038_SX_BUSY(x)                                         (((x) >> 26) & 0x1)
#define   C_008038_SX_BUSY                                            0xFBFFFFFF
#define   S_008038_SPI_BUSY(x)                                        (((unsigned)(x) & 0x1) << 27)
#define   G_008038_SPI_BUSY(x)                                        (((x) >> 27) & 0x1)
#define   C_008038_SPI_BUSY                                           0xF7FFFFFF
#define   S_008038_SC_BUSY(x)                                         (((unsigned)(x) & 0x1) << 29)
#define   G_008038_SC_BUSY(x)                                         (((x) >> 29) & 0x1)
#define   C_008038_SC_BUSY                                            0xDFFFFFFF
#define   S_008038_DB_BUSY(x)                                         (((unsigned)(x) & 0x1) << 30)
#define   G_008038_DB_BUSY(x)                                         (((x) >> 30) & 0x1)
#define   C_008038_DB_BUSY                                            0xBFFFFFFF
#define   S_008038_CB_BUSY(x)                                         (((unsigned)(x) & 0x1) << 31)
#define   G_008038_CB_BUSY(x)                                         (((x) >> 31) & 0x1)
#define   C_008038_CB_BUSY                                            0x7FFFFFFF
#define R_00803C_GRBM_STATUS_SE3                                        0x00803C
#define   S_00803C_DB_CLEAN(x)                                        (((unsigned)(x) & 0x1) << 1)
#define   G_00803C_DB_CLEAN(x)                                        (((x) >> 1) & 0x1)
#define   C_00803C_DB_CLEAN                                           0xFFFFFFFD
#define   S_00803C_CB_CLEAN(x)                                        (((unsigned)(x) & 0x1) << 2)
#define   G_00803C_CB_CLEAN(x)                                        (((x) >> 2) & 0x1)
#define   C_00803C_CB_CLEAN                                           0xFFFFFFFB
#define   S_00803C_BCI_BUSY(x)                                        (((unsigned)(x) & 0x1) << 22)
#define   G_00803C_BCI_BUSY(x)                                        (((x) >> 22) & 0x1)
#define   C_00803C_BCI_BUSY                                           0xFFBFFFFF
#define   S_00803C_VGT_BUSY(x)                                        (((unsigned)(x) & 0x1) << 23)
#define   G_00803C_VGT_BUSY(x)                                        (((x) >> 23) & 0x1)
#define   C_00803C_VGT_BUSY                                           0xFF7FFFFF
#define   S_00803C_PA_BUSY(x)                                         (((unsigned)(x) & 0x1) << 24)
#define   G_00803C_PA_BUSY(x)                                         (((x) >> 24) & 0x1)
#define   C_00803C_PA_BUSY                                            0xFEFFFFFF
#define   S_00803C_TA_BUSY(x)                                         (((unsigned)(x) & 0x1) << 25)
#define   G_00803C_TA_BUSY(x)                                         (((x) >> 25) & 0x1)
#define   C_00803C_TA_BUSY                                            0xFDFFFFFF
#define   S_00803C_SX_BUSY(x)                                         (((unsigned)(x) & 0x1) << 26)
#define   G_00803C_SX_BUSY(x)                                         (((x) >> 26) & 0x1)
#define   C_00803C_SX_BUSY                                            0xFBFFFFFF
#define   S_00803C_SPI_BUSY(x)                                        (((unsigned)(x) & 0x1) << 27)
#define   G_00803C_SPI_BUSY(x)                                        (((x) >> 27) & 0x1)
#define   C_00803C_SPI_BUSY                                           0xF7FFFFFF
#define   S_00803C_SC_BUSY(x)                                         (((unsigned)(x) & 0x1) << 29)
#define   G_00803C_SC_BUSY(x)                                         (((x) >> 29) & 0x1)
#define   C_00803C_SC_BUSY                                            0xDFFFFFFF
#define   S_00803C_DB_BUSY(x)                                         (((unsigned)(x) & 0x1) << 30)
#define   G_00803C_DB_BUSY(x)                                         (((x) >> 30) & 0x1)
#define   C_00803C_DB_BUSY                                            0xBFFFFFFF
#define   S_00803C_CB_BUSY(x)                                         (((unsigned)(x) & 0x1) << 31)
#define   G_00803C_CB_BUSY(x)                                         (((x) >> 31) & 0x1)
#define   C_00803C_CB_BUSY                                            0x7FFFFFFF
/* CIK */
#define R_0300FC_CP_STRMOUT_CNTL                                        0x0300FC
#define   S_0300FC_OFFSET_UPDATE_DONE(x)                              (((unsigned)(x) & 0x1) << 0)
#define   G_0300FC_OFFSET_UPDATE_DONE(x)                              (((x) >> 0) & 0x1)
#define   C_0300FC_OFFSET_UPDATE_DONE                                 0xFFFFFFFE
#define R_0301E4_CP_COHER_BASE_HI                                       0x0301E4
#define   S_0301E4_COHER_BASE_HI_256B(x)                              (((unsigned)(x) & 0xFF) << 0)
#define   G_0301E4_COHER_BASE_HI_256B(x)                              (((x) >> 0) & 0xFF)
#define   C_0301E4_COHER_BASE_HI_256B                                 0xFFFFFF00
#define R_0301EC_CP_COHER_START_DELAY                                   0x0301EC
#define   S_0301EC_START_DELAY_COUNT(x)                               (((unsigned)(x) & 0x3F) << 0)
#define   G_0301EC_START_DELAY_COUNT(x)                               (((x) >> 0) & 0x3F)
#define   C_0301EC_START_DELAY_COUNT                                  0xFFFFFFC0
#define R_0301F0_CP_COHER_CNTL                                          0x0301F0
#define   S_0301F0_DEST_BASE_0_ENA(x)                                 (((unsigned)(x) & 0x1) << 0)
#define   G_0301F0_DEST_BASE_0_ENA(x)                                 (((x) >> 0) & 0x1)
#define   C_0301F0_DEST_BASE_0_ENA                                    0xFFFFFFFE
#define   S_0301F0_DEST_BASE_1_ENA(x)                                 (((unsigned)(x) & 0x1) << 1)
#define   G_0301F0_DEST_BASE_1_ENA(x)                                 (((x) >> 1) & 0x1)
#define   C_0301F0_DEST_BASE_1_ENA                                    0xFFFFFFFD
/* VI */
#define   S_0301F0_TC_SD_ACTION_ENA(x)                                (((unsigned)(x) & 0x1) << 2)
#define   G_0301F0_TC_SD_ACTION_ENA(x)                                (((x) >> 2) & 0x1)
#define   C_0301F0_TC_SD_ACTION_ENA                                   0xFFFFFFFB
#define   S_0301F0_TC_NC_ACTION_ENA(x)                                (((unsigned)(x) & 0x1) << 3)
#define   G_0301F0_TC_NC_ACTION_ENA(x)                                (((x) >> 3) & 0x1)
#define   C_0301F0_TC_NC_ACTION_ENA                                   0xFFFFFFF7
/*    */
#define   S_0301F0_CB0_DEST_BASE_ENA(x)                               (((unsigned)(x) & 0x1) << 6)
#define   G_0301F0_CB0_DEST_BASE_ENA(x)                               (((x) >> 6) & 0x1)
#define   C_0301F0_CB0_DEST_BASE_ENA                                  0xFFFFFFBF
#define   S_0301F0_CB1_DEST_BASE_ENA(x)                               (((unsigned)(x) & 0x1) << 7)
#define   G_0301F0_CB1_DEST_BASE_ENA(x)                               (((x) >> 7) & 0x1)
#define   C_0301F0_CB1_DEST_BASE_ENA                                  0xFFFFFF7F
#define   S_0301F0_CB2_DEST_BASE_ENA(x)                               (((unsigned)(x) & 0x1) << 8)
#define   G_0301F0_CB2_DEST_BASE_ENA(x)                               (((x) >> 8) & 0x1)
#define   C_0301F0_CB2_DEST_BASE_ENA                                  0xFFFFFEFF
#define   S_0301F0_CB3_DEST_BASE_ENA(x)                               (((unsigned)(x) & 0x1) << 9)
#define   G_0301F0_CB3_DEST_BASE_ENA(x)                               (((x) >> 9) & 0x1)
#define   C_0301F0_CB3_DEST_BASE_ENA                                  0xFFFFFDFF
#define   S_0301F0_CB4_DEST_BASE_ENA(x)                               (((unsigned)(x) & 0x1) << 10)
#define   G_0301F0_CB4_DEST_BASE_ENA(x)                               (((x) >> 10) & 0x1)
#define   C_0301F0_CB4_DEST_BASE_ENA                                  0xFFFFFBFF
#define   S_0301F0_CB5_DEST_BASE_ENA(x)                               (((unsigned)(x) & 0x1) << 11)
#define   G_0301F0_CB5_DEST_BASE_ENA(x)                               (((x) >> 11) & 0x1)
#define   C_0301F0_CB5_DEST_BASE_ENA                                  0xFFFFF7FF
#define   S_0301F0_CB6_DEST_BASE_ENA(x)                               (((unsigned)(x) & 0x1) << 12)
#define   G_0301F0_CB6_DEST_BASE_ENA(x)                               (((x) >> 12) & 0x1)
#define   C_0301F0_CB6_DEST_BASE_ENA                                  0xFFFFEFFF
#define   S_0301F0_CB7_DEST_BASE_ENA(x)                               (((unsigned)(x) & 0x1) << 13)
#define   G_0301F0_CB7_DEST_BASE_ENA(x)                               (((x) >> 13) & 0x1)
#define   C_0301F0_CB7_DEST_BASE_ENA                                  0xFFFFDFFF
#define   S_0301F0_DB_DEST_BASE_ENA(x)                                (((unsigned)(x) & 0x1) << 14)
#define   G_0301F0_DB_DEST_BASE_ENA(x)                                (((x) >> 14) & 0x1)
#define   C_0301F0_DB_DEST_BASE_ENA                                   0xFFFFBFFF
#define   S_0301F0_TCL1_VOL_ACTION_ENA(x)                             (((unsigned)(x) & 0x1) << 15)
#define   G_0301F0_TCL1_VOL_ACTION_ENA(x)                             (((x) >> 15) & 0x1)
#define   C_0301F0_TCL1_VOL_ACTION_ENA                                0xFFFF7FFF
#define   S_0301F0_TC_VOL_ACTION_ENA(x)                               (((unsigned)(x) & 0x1) << 16) /* not on VI */
#define   G_0301F0_TC_VOL_ACTION_ENA(x)                               (((x) >> 16) & 0x1)
#define   C_0301F0_TC_VOL_ACTION_ENA                                  0xFFFEFFFF
#define   S_0301F0_TC_WB_ACTION_ENA(x)                                (((unsigned)(x) & 0x1) << 18)
#define   G_0301F0_TC_WB_ACTION_ENA(x)                                (((x) >> 18) & 0x1)
#define   C_0301F0_TC_WB_ACTION_ENA                                   0xFFFBFFFF
#define   S_0301F0_DEST_BASE_2_ENA(x)                                 (((unsigned)(x) & 0x1) << 19)
#define   G_0301F0_DEST_BASE_2_ENA(x)                                 (((x) >> 19) & 0x1)
#define   C_0301F0_DEST_BASE_2_ENA                                    0xFFF7FFFF
#define   S_0301F0_DEST_BASE_3_ENA(x)                                 (((unsigned)(x) & 0x1) << 21)
#define   G_0301F0_DEST_BASE_3_ENA(x)                                 (((x) >> 21) & 0x1)
#define   C_0301F0_DEST_BASE_3_ENA                                    0xFFDFFFFF
#define   S_0301F0_TCL1_ACTION_ENA(x)                                 (((unsigned)(x) & 0x1) << 22)
#define   G_0301F0_TCL1_ACTION_ENA(x)                                 (((x) >> 22) & 0x1)
#define   C_0301F0_TCL1_ACTION_ENA                                    0xFFBFFFFF
#define   S_0301F0_TC_ACTION_ENA(x)                                   (((unsigned)(x) & 0x1) << 23)
#define   G_0301F0_TC_ACTION_ENA(x)                                   (((x) >> 23) & 0x1)
#define   C_0301F0_TC_ACTION_ENA                                      0xFF7FFFFF
#define   S_0301F0_CB_ACTION_ENA(x)                                   (((unsigned)(x) & 0x1) << 25)
#define   G_0301F0_CB_ACTION_ENA(x)                                   (((x) >> 25) & 0x1)
#define   C_0301F0_CB_ACTION_ENA                                      0xFDFFFFFF
#define   S_0301F0_DB_ACTION_ENA(x)                                   (((unsigned)(x) & 0x1) << 26)
#define   G_0301F0_DB_ACTION_ENA(x)                                   (((x) >> 26) & 0x1)
#define   C_0301F0_DB_ACTION_ENA                                      0xFBFFFFFF
#define   S_0301F0_SH_KCACHE_ACTION_ENA(x)                            (((unsigned)(x) & 0x1) << 27)
#define   G_0301F0_SH_KCACHE_ACTION_ENA(x)                            (((x) >> 27) & 0x1)
#define   C_0301F0_SH_KCACHE_ACTION_ENA                               0xF7FFFFFF
#define   S_0301F0_SH_KCACHE_VOL_ACTION_ENA(x)                        (((unsigned)(x) & 0x1) << 28)
#define   G_0301F0_SH_KCACHE_VOL_ACTION_ENA(x)                        (((x) >> 28) & 0x1)
#define   C_0301F0_SH_KCACHE_VOL_ACTION_ENA                           0xEFFFFFFF
#define   S_0301F0_SH_ICACHE_ACTION_ENA(x)                            (((unsigned)(x) & 0x1) << 29)
#define   G_0301F0_SH_ICACHE_ACTION_ENA(x)                            (((x) >> 29) & 0x1)
#define   C_0301F0_SH_ICACHE_ACTION_ENA                               0xDFFFFFFF
/* VI */
#define   S_0301F0_SH_KCACHE_WB_ACTION_ENA(x)                         (((unsigned)(x) & 0x1) << 30)
#define   G_0301F0_SH_KCACHE_WB_ACTION_ENA(x)                         (((x) >> 30) & 0x1)
#define   C_0301F0_SH_KCACHE_WB_ACTION_ENA                            0xBFFFFFFF
#define   S_0301F0_SH_SD_ACTION_ENA(x)                                (((unsigned)(x) & 0x1) << 31)
#define   G_0301F0_SH_SD_ACTION_ENA(x)                                (((x) >> 31) & 0x1)
#define   C_0301F0_SH_SD_ACTION_ENA                                   0x7FFFFFFF
/*    */
#define R_0301F4_CP_COHER_SIZE                                          0x0301F4
#define R_0301F8_CP_COHER_BASE                                          0x0301F8
#define R_0301FC_CP_COHER_STATUS                                        0x0301FC
#define   S_0301FC_MATCHING_GFX_CNTX(x)                               (((unsigned)(x) & 0xFF) << 0)
#define   G_0301FC_MATCHING_GFX_CNTX(x)                               (((x) >> 0) & 0xFF)
#define   C_0301FC_MATCHING_GFX_CNTX                                  0xFFFFFF00
#define   S_0301FC_MEID(x)                                            (((unsigned)(x) & 0x03) << 24)
#define   G_0301FC_MEID(x)                                            (((x) >> 24) & 0x03)
#define   C_0301FC_MEID                                               0xFCFFFFFF
#define   S_0301FC_PHASE1_STATUS(x)                                   (((unsigned)(x) & 0x1) << 30)
#define   G_0301FC_PHASE1_STATUS(x)                                   (((x) >> 30) & 0x1)
#define   C_0301FC_PHASE1_STATUS                                      0xBFFFFFFF
#define   S_0301FC_STATUS(x)                                          (((unsigned)(x) & 0x1) << 31)
#define   G_0301FC_STATUS(x)                                          (((x) >> 31) & 0x1)
#define   C_0301FC_STATUS                                             0x7FFFFFFF
#define R_008210_CP_CPC_STATUS                                          0x008210
#define   S_008210_MEC1_BUSY(x)                                       (((unsigned)(x) & 0x1) << 0)
#define   G_008210_MEC1_BUSY(x)                                       (((x) >> 0) & 0x1)
#define   C_008210_MEC1_BUSY                                          0xFFFFFFFE
#define   S_008210_MEC2_BUSY(x)                                       (((unsigned)(x) & 0x1) << 1)
#define   G_008210_MEC2_BUSY(x)                                       (((x) >> 1) & 0x1)
#define   C_008210_MEC2_BUSY                                          0xFFFFFFFD
#define   S_008210_DC0_BUSY(x)                                        (((unsigned)(x) & 0x1) << 2)
#define   G_008210_DC0_BUSY(x)                                        (((x) >> 2) & 0x1)
#define   C_008210_DC0_BUSY                                           0xFFFFFFFB
#define   S_008210_DC1_BUSY(x)                                        (((unsigned)(x) & 0x1) << 3)
#define   G_008210_DC1_BUSY(x)                                        (((x) >> 3) & 0x1)
#define   C_008210_DC1_BUSY                                           0xFFFFFFF7
#define   S_008210_RCIU1_BUSY(x)                                      (((unsigned)(x) & 0x1) << 4)
#define   G_008210_RCIU1_BUSY(x)                                      (((x) >> 4) & 0x1)
#define   C_008210_RCIU1_BUSY                                         0xFFFFFFEF
#define   S_008210_RCIU2_BUSY(x)                                      (((unsigned)(x) & 0x1) << 5)
#define   G_008210_RCIU2_BUSY(x)                                      (((x) >> 5) & 0x1)
#define   C_008210_RCIU2_BUSY                                         0xFFFFFFDF
#define   S_008210_ROQ1_BUSY(x)                                       (((unsigned)(x) & 0x1) << 6)
#define   G_008210_ROQ1_BUSY(x)                                       (((x) >> 6) & 0x1)
#define   C_008210_ROQ1_BUSY                                          0xFFFFFFBF
#define   S_008210_ROQ2_BUSY(x)                                       (((unsigned)(x) & 0x1) << 7)
#define   G_008210_ROQ2_BUSY(x)                                       (((x) >> 7) & 0x1)
#define   C_008210_ROQ2_BUSY                                          0xFFFFFF7F
#define   S_008210_TCIU_BUSY(x)                                       (((unsigned)(x) & 0x1) << 10)
#define   G_008210_TCIU_BUSY(x)                                       (((x) >> 10) & 0x1)
#define   C_008210_TCIU_BUSY                                          0xFFFFFBFF
#define   S_008210_SCRATCH_RAM_BUSY(x)                                (((unsigned)(x) & 0x1) << 11)
#define   G_008210_SCRATCH_RAM_BUSY(x)                                (((x) >> 11) & 0x1)
#define   C_008210_SCRATCH_RAM_BUSY                                   0xFFFFF7FF
#define   S_008210_QU_BUSY(x)                                         (((unsigned)(x) & 0x1) << 12)
#define   G_008210_QU_BUSY(x)                                         (((x) >> 12) & 0x1)
#define   C_008210_QU_BUSY                                            0xFFFFEFFF
#define   S_008210_ATCL2IU_BUSY(x)                                    (((unsigned)(x) & 0x1) << 13)
#define   G_008210_ATCL2IU_BUSY(x)                                    (((x) >> 13) & 0x1)
#define   C_008210_ATCL2IU_BUSY                                       0xFFFFDFFF
#define   S_008210_CPG_CPC_BUSY(x)                                    (((unsigned)(x) & 0x1) << 29)
#define   G_008210_CPG_CPC_BUSY(x)                                    (((x) >> 29) & 0x1)
#define   C_008210_CPG_CPC_BUSY                                       0xDFFFFFFF
#define   S_008210_CPF_CPC_BUSY(x)                                    (((unsigned)(x) & 0x1) << 30)
#define   G_008210_CPF_CPC_BUSY(x)                                    (((x) >> 30) & 0x1)
#define   C_008210_CPF_CPC_BUSY                                       0xBFFFFFFF
#define   S_008210_CPC_BUSY(x)                                        (((unsigned)(x) & 0x1) << 31)
#define   G_008210_CPC_BUSY(x)                                        (((x) >> 31) & 0x1)
#define   C_008210_CPC_BUSY                                           0x7FFFFFFF
#define R_008214_CP_CPC_BUSY_STAT                                       0x008214
#define   S_008214_MEC1_LOAD_BUSY(x)                                  (((unsigned)(x) & 0x1) << 0)
#define   G_008214_MEC1_LOAD_BUSY(x)                                  (((x) >> 0) & 0x1)
#define   C_008214_MEC1_LOAD_BUSY                                     0xFFFFFFFE
#define   S_008214_MEC1_SEMAPOHRE_BUSY(x)                             (((unsigned)(x) & 0x1) << 1)
#define   G_008214_MEC1_SEMAPOHRE_BUSY(x)                             (((x) >> 1) & 0x1)
#define   C_008214_MEC1_SEMAPOHRE_BUSY                                0xFFFFFFFD
#define   S_008214_MEC1_MUTEX_BUSY(x)                                 (((unsigned)(x) & 0x1) << 2)
#define   G_008214_MEC1_MUTEX_BUSY(x)                                 (((x) >> 2) & 0x1)
#define   C_008214_MEC1_MUTEX_BUSY                                    0xFFFFFFFB
#define   S_008214_MEC1_MESSAGE_BUSY(x)                               (((unsigned)(x) & 0x1) << 3)
#define   G_008214_MEC1_MESSAGE_BUSY(x)                               (((x) >> 3) & 0x1)
#define   C_008214_MEC1_MESSAGE_BUSY                                  0xFFFFFFF7
#define   S_008214_MEC1_EOP_QUEUE_BUSY(x)                             (((unsigned)(x) & 0x1) << 4)
#define   G_008214_MEC1_EOP_QUEUE_BUSY(x)                             (((x) >> 4) & 0x1)
#define   C_008214_MEC1_EOP_QUEUE_BUSY                                0xFFFFFFEF
#define   S_008214_MEC1_IQ_QUEUE_BUSY(x)                              (((unsigned)(x) & 0x1) << 5)
#define   G_008214_MEC1_IQ_QUEUE_BUSY(x)                              (((x) >> 5) & 0x1)
#define   C_008214_MEC1_IQ_QUEUE_BUSY                                 0xFFFFFFDF
#define   S_008214_MEC1_IB_QUEUE_BUSY(x)                              (((unsigned)(x) & 0x1) << 6)
#define   G_008214_MEC1_IB_QUEUE_BUSY(x)                              (((x) >> 6) & 0x1)
#define   C_008214_MEC1_IB_QUEUE_BUSY                                 0xFFFFFFBF
#define   S_008214_MEC1_TC_BUSY(x)                                    (((unsigned)(x) & 0x1) << 7)
#define   G_008214_MEC1_TC_BUSY(x)                                    (((x) >> 7) & 0x1)
#define   C_008214_MEC1_TC_BUSY                                       0xFFFFFF7F
#define   S_008214_MEC1_DMA_BUSY(x)                                   (((unsigned)(x) & 0x1) << 8)
#define   G_008214_MEC1_DMA_BUSY(x)                                   (((x) >> 8) & 0x1)
#define   C_008214_MEC1_DMA_BUSY                                      0xFFFFFEFF
#define   S_008214_MEC1_PARTIAL_FLUSH_BUSY(x)                         (((unsigned)(x) & 0x1) << 9)
#define   G_008214_MEC1_PARTIAL_FLUSH_BUSY(x)                         (((x) >> 9) & 0x1)
#define   C_008214_MEC1_PARTIAL_FLUSH_BUSY                            0xFFFFFDFF
#define   S_008214_MEC1_PIPE0_BUSY(x)                                 (((unsigned)(x) & 0x1) << 10)
#define   G_008214_MEC1_PIPE0_BUSY(x)                                 (((x) >> 10) & 0x1)
#define   C_008214_MEC1_PIPE0_BUSY                                    0xFFFFFBFF
#define   S_008214_MEC1_PIPE1_BUSY(x)                                 (((unsigned)(x) & 0x1) << 11)
#define   G_008214_MEC1_PIPE1_BUSY(x)                                 (((x) >> 11) & 0x1)
#define   C_008214_MEC1_PIPE1_BUSY                                    0xFFFFF7FF
#define   S_008214_MEC1_PIPE2_BUSY(x)                                 (((unsigned)(x) & 0x1) << 12)
#define   G_008214_MEC1_PIPE2_BUSY(x)                                 (((x) >> 12) & 0x1)
#define   C_008214_MEC1_PIPE2_BUSY                                    0xFFFFEFFF
#define   S_008214_MEC1_PIPE3_BUSY(x)                                 (((unsigned)(x) & 0x1) << 13)
#define   G_008214_MEC1_PIPE3_BUSY(x)                                 (((x) >> 13) & 0x1)
#define   C_008214_MEC1_PIPE3_BUSY                                    0xFFFFDFFF
#define   S_008214_MEC2_LOAD_BUSY(x)                                  (((unsigned)(x) & 0x1) << 16)
#define   G_008214_MEC2_LOAD_BUSY(x)                                  (((x) >> 16) & 0x1)
#define   C_008214_MEC2_LOAD_BUSY                                     0xFFFEFFFF
#define   S_008214_MEC2_SEMAPOHRE_BUSY(x)                             (((unsigned)(x) & 0x1) << 17)
#define   G_008214_MEC2_SEMAPOHRE_BUSY(x)                             (((x) >> 17) & 0x1)
#define   C_008214_MEC2_SEMAPOHRE_BUSY                                0xFFFDFFFF
#define   S_008214_MEC2_MUTEX_BUSY(x)                                 (((unsigned)(x) & 0x1) << 18)
#define   G_008214_MEC2_MUTEX_BUSY(x)                                 (((x) >> 18) & 0x1)
#define   C_008214_MEC2_MUTEX_BUSY                                    0xFFFBFFFF
#define   S_008214_MEC2_MESSAGE_BUSY(x)                               (((unsigned)(x) & 0x1) << 19)
#define   G_008214_MEC2_MESSAGE_BUSY(x)                               (((x) >> 19) & 0x1)
#define   C_008214_MEC2_MESSAGE_BUSY                                  0xFFF7FFFF
#define   S_008214_MEC2_EOP_QUEUE_BUSY(x)                             (((unsigned)(x) & 0x1) << 20)
#define   G_008214_MEC2_EOP_QUEUE_BUSY(x)                             (((x) >> 20) & 0x1)
#define   C_008214_MEC2_EOP_QUEUE_BUSY                                0xFFEFFFFF
#define   S_008214_MEC2_IQ_QUEUE_BUSY(x)                              (((unsigned)(x) & 0x1) << 21)
#define   G_008214_MEC2_IQ_QUEUE_BUSY(x)                              (((x) >> 21) & 0x1)
#define   C_008214_MEC2_IQ_QUEUE_BUSY                                 0xFFDFFFFF
#define   S_008214_MEC2_IB_QUEUE_BUSY(x)                              (((unsigned)(x) & 0x1) << 22)
#define   G_008214_MEC2_IB_QUEUE_BUSY(x)                              (((x) >> 22) & 0x1)
#define   C_008214_MEC2_IB_QUEUE_BUSY                                 0xFFBFFFFF
#define   S_008214_MEC2_TC_BUSY(x)                                    (((unsigned)(x) & 0x1) << 23)
#define   G_008214_MEC2_TC_BUSY(x)                                    (((x) >> 23) & 0x1)
#define   C_008214_MEC2_TC_BUSY                                       0xFF7FFFFF
#define   S_008214_MEC2_DMA_BUSY(x)                                   (((unsigned)(x) & 0x1) << 24)
#define   G_008214_MEC2_DMA_BUSY(x)                                   (((x) >> 24) & 0x1)
#define   C_008214_MEC2_DMA_BUSY                                      0xFEFFFFFF
#define   S_008214_MEC2_PARTIAL_FLUSH_BUSY(x)                         (((unsigned)(x) & 0x1) << 25)
#define   G_008214_MEC2_PARTIAL_FLUSH_BUSY(x)                         (((x) >> 25) & 0x1)
#define   C_008214_MEC2_PARTIAL_FLUSH_BUSY                            0xFDFFFFFF
#define   S_008214_MEC2_PIPE0_BUSY(x)                                 (((unsigned)(x) & 0x1) << 26)
#define   G_008214_MEC2_PIPE0_BUSY(x)                                 (((x) >> 26) & 0x1)
#define   C_008214_MEC2_PIPE0_BUSY                                    0xFBFFFFFF
#define   S_008214_MEC2_PIPE1_BUSY(x)                                 (((unsigned)(x) & 0x1) << 27)
#define   G_008214_MEC2_PIPE1_BUSY(x)                                 (((x) >> 27) & 0x1)
#define   C_008214_MEC2_PIPE1_BUSY                                    0xF7FFFFFF
#define   S_008214_MEC2_PIPE2_BUSY(x)                                 (((unsigned)(x) & 0x1) << 28)
#define   G_008214_MEC2_PIPE2_BUSY(x)                                 (((x) >> 28) & 0x1)
#define   C_008214_MEC2_PIPE2_BUSY                                    0xEFFFFFFF
#define   S_008214_MEC2_PIPE3_BUSY(x)                                 (((unsigned)(x) & 0x1) << 29)
#define   G_008214_MEC2_PIPE3_BUSY(x)                                 (((x) >> 29) & 0x1)
#define   C_008214_MEC2_PIPE3_BUSY                                    0xDFFFFFFF
#define R_008218_CP_CPC_STALLED_STAT1                                   0x008218
#define   S_008218_RCIU_TX_FREE_STALL(x)                              (((unsigned)(x) & 0x1) << 3)
#define   G_008218_RCIU_TX_FREE_STALL(x)                              (((x) >> 3) & 0x1)
#define   C_008218_RCIU_TX_FREE_STALL                                 0xFFFFFFF7
#define   S_008218_RCIU_PRIV_VIOLATION(x)                             (((unsigned)(x) & 0x1) << 4)
#define   G_008218_RCIU_PRIV_VIOLATION(x)                             (((x) >> 4) & 0x1)
#define   C_008218_RCIU_PRIV_VIOLATION                                0xFFFFFFEF
#define   S_008218_TCIU_TX_FREE_STALL(x)                              (((unsigned)(x) & 0x1) << 6)
#define   G_008218_TCIU_TX_FREE_STALL(x)                              (((x) >> 6) & 0x1)
#define   C_008218_TCIU_TX_FREE_STALL                                 0xFFFFFFBF
#define   S_008218_MEC1_DECODING_PACKET(x)                            (((unsigned)(x) & 0x1) << 8)
#define   G_008218_MEC1_DECODING_PACKET(x)                            (((x) >> 8) & 0x1)
#define   C_008218_MEC1_DECODING_PACKET                               0xFFFFFEFF
#define   S_008218_MEC1_WAIT_ON_RCIU(x)                               (((unsigned)(x) & 0x1) << 9)
#define   G_008218_MEC1_WAIT_ON_RCIU(x)                               (((x) >> 9) & 0x1)
#define   C_008218_MEC1_WAIT_ON_RCIU                                  0xFFFFFDFF
#define   S_008218_MEC1_WAIT_ON_RCIU_READ(x)                          (((unsigned)(x) & 0x1) << 10)
#define   G_008218_MEC1_WAIT_ON_RCIU_READ(x)                          (((x) >> 10) & 0x1)
#define   C_008218_MEC1_WAIT_ON_RCIU_READ                             0xFFFFFBFF
#define   S_008218_MEC1_WAIT_ON_ROQ_DATA(x)                           (((unsigned)(x) & 0x1) << 13)
#define   G_008218_MEC1_WAIT_ON_ROQ_DATA(x)                           (((x) >> 13) & 0x1)
#define   C_008218_MEC1_WAIT_ON_ROQ_DATA                              0xFFFFDFFF
#define   S_008218_MEC2_DECODING_PACKET(x)                            (((unsigned)(x) & 0x1) << 16)
#define   G_008218_MEC2_DECODING_PACKET(x)                            (((x) >> 16) & 0x1)
#define   C_008218_MEC2_DECODING_PACKET                               0xFFFEFFFF
#define   S_008218_MEC2_WAIT_ON_RCIU(x)                               (((unsigned)(x) & 0x1) << 17)
#define   G_008218_MEC2_WAIT_ON_RCIU(x)                               (((x) >> 17) & 0x1)
#define   C_008218_MEC2_WAIT_ON_RCIU                                  0xFFFDFFFF
#define   S_008218_MEC2_WAIT_ON_RCIU_READ(x)                          (((unsigned)(x) & 0x1) << 18)
#define   G_008218_MEC2_WAIT_ON_RCIU_READ(x)                          (((x) >> 18) & 0x1)
#define   C_008218_MEC2_WAIT_ON_RCIU_READ                             0xFFFBFFFF
#define   S_008218_MEC2_WAIT_ON_ROQ_DATA(x)                           (((unsigned)(x) & 0x1) << 21)
#define   G_008218_MEC2_WAIT_ON_ROQ_DATA(x)                           (((x) >> 21) & 0x1)
#define   C_008218_MEC2_WAIT_ON_ROQ_DATA                              0xFFDFFFFF
#define   S_008218_ATCL2IU_WAITING_ON_FREE(x)                         (((unsigned)(x) & 0x1) << 22)
#define   G_008218_ATCL2IU_WAITING_ON_FREE(x)                         (((x) >> 22) & 0x1)
#define   C_008218_ATCL2IU_WAITING_ON_FREE                            0xFFBFFFFF
#define   S_008218_ATCL2IU_WAITING_ON_TAGS(x)                         (((unsigned)(x) & 0x1) << 23)
#define   G_008218_ATCL2IU_WAITING_ON_TAGS(x)                         (((x) >> 23) & 0x1)
#define   C_008218_ATCL2IU_WAITING_ON_TAGS                            0xFF7FFFFF
#define   S_008218_ATCL1_WAITING_ON_TRANS(x)                          (((unsigned)(x) & 0x1) << 24)
#define   G_008218_ATCL1_WAITING_ON_TRANS(x)                          (((x) >> 24) & 0x1)
#define   C_008218_ATCL1_WAITING_ON_TRANS                             0xFEFFFFFF
#define R_00821C_CP_CPF_STATUS                                          0x00821C
#define   S_00821C_POST_WPTR_GFX_BUSY(x)                              (((unsigned)(x) & 0x1) << 0)
#define   G_00821C_POST_WPTR_GFX_BUSY(x)                              (((x) >> 0) & 0x1)
#define   C_00821C_POST_WPTR_GFX_BUSY                                 0xFFFFFFFE
#define   S_00821C_CSF_BUSY(x)                                        (((unsigned)(x) & 0x1) << 1)
#define   G_00821C_CSF_BUSY(x)                                        (((x) >> 1) & 0x1)
#define   C_00821C_CSF_BUSY                                           0xFFFFFFFD
#define   S_00821C_ROQ_ALIGN_BUSY(x)                                  (((unsigned)(x) & 0x1) << 4)
#define   G_00821C_ROQ_ALIGN_BUSY(x)                                  (((x) >> 4) & 0x1)
#define   C_00821C_ROQ_ALIGN_BUSY                                     0xFFFFFFEF
#define   S_00821C_ROQ_RING_BUSY(x)                                   (((unsigned)(x) & 0x1) << 5)
#define   G_00821C_ROQ_RING_BUSY(x)                                   (((x) >> 5) & 0x1)
#define   C_00821C_ROQ_RING_BUSY                                      0xFFFFFFDF
#define   S_00821C_ROQ_INDIRECT1_BUSY(x)                              (((unsigned)(x) & 0x1) << 6)
#define   G_00821C_ROQ_INDIRECT1_BUSY(x)                              (((x) >> 6) & 0x1)
#define   C_00821C_ROQ_INDIRECT1_BUSY                                 0xFFFFFFBF
#define   S_00821C_ROQ_INDIRECT2_BUSY(x)                              (((unsigned)(x) & 0x1) << 7)
#define   G_00821C_ROQ_INDIRECT2_BUSY(x)                              (((x) >> 7) & 0x1)
#define   C_00821C_ROQ_INDIRECT2_BUSY                                 0xFFFFFF7F
#define   S_00821C_ROQ_STATE_BUSY(x)                                  (((unsigned)(x) & 0x1) << 8)
#define   G_00821C_ROQ_STATE_BUSY(x)                                  (((x) >> 8) & 0x1)
#define   C_00821C_ROQ_STATE_BUSY                                     0xFFFFFEFF
#define   S_00821C_ROQ_CE_RING_BUSY(x)                                (((unsigned)(x) & 0x1) << 9)
#define   G_00821C_ROQ_CE_RING_BUSY(x)                                (((x) >> 9) & 0x1)
#define   C_00821C_ROQ_CE_RING_BUSY                                   0xFFFFFDFF
#define   S_00821C_ROQ_CE_INDIRECT1_BUSY(x)                           (((unsigned)(x) & 0x1) << 10)
#define   G_00821C_ROQ_CE_INDIRECT1_BUSY(x)                           (((x) >> 10) & 0x1)
#define   C_00821C_ROQ_CE_INDIRECT1_BUSY                              0xFFFFFBFF
#define   S_00821C_ROQ_CE_INDIRECT2_BUSY(x)                           (((unsigned)(x) & 0x1) << 11)
#define   G_00821C_ROQ_CE_INDIRECT2_BUSY(x)                           (((x) >> 11) & 0x1)
#define   C_00821C_ROQ_CE_INDIRECT2_BUSY                              0xFFFFF7FF
#define   S_00821C_SEMAPHORE_BUSY(x)                                  (((unsigned)(x) & 0x1) << 12)
#define   G_00821C_SEMAPHORE_BUSY(x)                                  (((x) >> 12) & 0x1)
#define   C_00821C_SEMAPHORE_BUSY                                     0xFFFFEFFF
#define   S_00821C_INTERRUPT_BUSY(x)                                  (((unsigned)(x) & 0x1) << 13)
#define   G_00821C_INTERRUPT_BUSY(x)                                  (((x) >> 13) & 0x1)
#define   C_00821C_INTERRUPT_BUSY                                     0xFFFFDFFF
#define   S_00821C_TCIU_BUSY(x)                                       (((unsigned)(x) & 0x1) << 14)
#define   G_00821C_TCIU_BUSY(x)                                       (((x) >> 14) & 0x1)
#define   C_00821C_TCIU_BUSY                                          0xFFFFBFFF
#define   S_00821C_HQD_BUSY(x)                                        (((unsigned)(x) & 0x1) << 15)
#define   G_00821C_HQD_BUSY(x)                                        (((x) >> 15) & 0x1)
#define   C_00821C_HQD_BUSY                                           0xFFFF7FFF
#define   S_00821C_PRT_BUSY(x)                                        (((unsigned)(x) & 0x1) << 16)
#define   G_00821C_PRT_BUSY(x)                                        (((x) >> 16) & 0x1)
#define   C_00821C_PRT_BUSY                                           0xFFFEFFFF
#define   S_00821C_ATCL2IU_BUSY(x)                                    (((unsigned)(x) & 0x1) << 17)
#define   G_00821C_ATCL2IU_BUSY(x)                                    (((x) >> 17) & 0x1)
#define   C_00821C_ATCL2IU_BUSY                                       0xFFFDFFFF
#define   S_00821C_CPF_GFX_BUSY(x)                                    (((unsigned)(x) & 0x1) << 26)
#define   G_00821C_CPF_GFX_BUSY(x)                                    (((x) >> 26) & 0x1)
#define   C_00821C_CPF_GFX_BUSY                                       0xFBFFFFFF
#define   S_00821C_CPF_CMP_BUSY(x)                                    (((unsigned)(x) & 0x1) << 27)
#define   G_00821C_CPF_CMP_BUSY(x)                                    (((x) >> 27) & 0x1)
#define   C_00821C_CPF_CMP_BUSY                                       0xF7FFFFFF
#define   S_00821C_GRBM_CPF_STAT_BUSY(x)                              (((unsigned)(x) & 0x03) << 28)
#define   G_00821C_GRBM_CPF_STAT_BUSY(x)                              (((x) >> 28) & 0x03)
#define   C_00821C_GRBM_CPF_STAT_BUSY                                 0xCFFFFFFF
#define   S_00821C_CPC_CPF_BUSY(x)                                    (((unsigned)(x) & 0x1) << 30)
#define   G_00821C_CPC_CPF_BUSY(x)                                    (((x) >> 30) & 0x1)
#define   C_00821C_CPC_CPF_BUSY                                       0xBFFFFFFF
#define   S_00821C_CPF_BUSY(x)                                        (((unsigned)(x) & 0x1) << 31)
#define   G_00821C_CPF_BUSY(x)                                        (((x) >> 31) & 0x1)
#define   C_00821C_CPF_BUSY                                           0x7FFFFFFF
#define R_008220_CP_CPF_BUSY_STAT                                       0x008220
#define   S_008220_REG_BUS_FIFO_BUSY(x)                               (((unsigned)(x) & 0x1) << 0)
#define   G_008220_REG_BUS_FIFO_BUSY(x)                               (((x) >> 0) & 0x1)
#define   C_008220_REG_BUS_FIFO_BUSY                                  0xFFFFFFFE
#define   S_008220_CSF_RING_BUSY(x)                                   (((unsigned)(x) & 0x1) << 1)
#define   G_008220_CSF_RING_BUSY(x)                                   (((x) >> 1) & 0x1)
#define   C_008220_CSF_RING_BUSY                                      0xFFFFFFFD
#define   S_008220_CSF_INDIRECT1_BUSY(x)                              (((unsigned)(x) & 0x1) << 2)
#define   G_008220_CSF_INDIRECT1_BUSY(x)                              (((x) >> 2) & 0x1)
#define   C_008220_CSF_INDIRECT1_BUSY                                 0xFFFFFFFB
#define   S_008220_CSF_INDIRECT2_BUSY(x)                              (((unsigned)(x) & 0x1) << 3)
#define   G_008220_CSF_INDIRECT2_BUSY(x)                              (((x) >> 3) & 0x1)
#define   C_008220_CSF_INDIRECT2_BUSY                                 0xFFFFFFF7
#define   S_008220_CSF_STATE_BUSY(x)                                  (((unsigned)(x) & 0x1) << 4)
#define   G_008220_CSF_STATE_BUSY(x)                                  (((x) >> 4) & 0x1)
#define   C_008220_CSF_STATE_BUSY                                     0xFFFFFFEF
#define   S_008220_CSF_CE_INDR1_BUSY(x)                               (((unsigned)(x) & 0x1) << 5)
#define   G_008220_CSF_CE_INDR1_BUSY(x)                               (((x) >> 5) & 0x1)
#define   C_008220_CSF_CE_INDR1_BUSY                                  0xFFFFFFDF
#define   S_008220_CSF_CE_INDR2_BUSY(x)                               (((unsigned)(x) & 0x1) << 6)
#define   G_008220_CSF_CE_INDR2_BUSY(x)                               (((x) >> 6) & 0x1)
#define   C_008220_CSF_CE_INDR2_BUSY                                  0xFFFFFFBF
#define   S_008220_CSF_ARBITER_BUSY(x)                                (((unsigned)(x) & 0x1) << 7)
#define   G_008220_CSF_ARBITER_BUSY(x)                                (((x) >> 7) & 0x1)
#define   C_008220_CSF_ARBITER_BUSY                                   0xFFFFFF7F
#define   S_008220_CSF_INPUT_BUSY(x)                                  (((unsigned)(x) & 0x1) << 8)
#define   G_008220_CSF_INPUT_BUSY(x)                                  (((x) >> 8) & 0x1)
#define   C_008220_CSF_INPUT_BUSY                                     0xFFFFFEFF
#define   S_008220_OUTSTANDING_READ_TAGS(x)                           (((unsigned)(x) & 0x1) << 9)
#define   G_008220_OUTSTANDING_READ_TAGS(x)                           (((x) >> 9) & 0x1)
#define   C_008220_OUTSTANDING_READ_TAGS                              0xFFFFFDFF
#define   S_008220_HPD_PROCESSING_EOP_BUSY(x)                         (((unsigned)(x) & 0x1) << 11)
#define   G_008220_HPD_PROCESSING_EOP_BUSY(x)                         (((x) >> 11) & 0x1)
#define   C_008220_HPD_PROCESSING_EOP_BUSY                            0xFFFFF7FF
#define   S_008220_HQD_DISPATCH_BUSY(x)                               (((unsigned)(x) & 0x1) << 12)
#define   G_008220_HQD_DISPATCH_BUSY(x)                               (((x) >> 12) & 0x1)
#define   C_008220_HQD_DISPATCH_BUSY                                  0xFFFFEFFF
#define   S_008220_HQD_IQ_TIMER_BUSY(x)                               (((unsigned)(x) & 0x1) << 13)
#define   G_008220_HQD_IQ_TIMER_BUSY(x)                               (((x) >> 13) & 0x1)
#define   C_008220_HQD_IQ_TIMER_BUSY                                  0xFFFFDFFF
#define   S_008220_HQD_DMA_OFFLOAD_BUSY(x)                            (((unsigned)(x) & 0x1) << 14)
#define   G_008220_HQD_DMA_OFFLOAD_BUSY(x)                            (((x) >> 14) & 0x1)
#define   C_008220_HQD_DMA_OFFLOAD_BUSY                               0xFFFFBFFF
#define   S_008220_HQD_WAIT_SEMAPHORE_BUSY(x)                         (((unsigned)(x) & 0x1) << 15)
#define   G_008220_HQD_WAIT_SEMAPHORE_BUSY(x)                         (((x) >> 15) & 0x1)
#define   C_008220_HQD_WAIT_SEMAPHORE_BUSY                            0xFFFF7FFF
#define   S_008220_HQD_SIGNAL_SEMAPHORE_BUSY(x)                       (((unsigned)(x) & 0x1) << 16)
#define   G_008220_HQD_SIGNAL_SEMAPHORE_BUSY(x)                       (((x) >> 16) & 0x1)
#define   C_008220_HQD_SIGNAL_SEMAPHORE_BUSY                          0xFFFEFFFF
#define   S_008220_HQD_MESSAGE_BUSY(x)                                (((unsigned)(x) & 0x1) << 17)
#define   G_008220_HQD_MESSAGE_BUSY(x)                                (((x) >> 17) & 0x1)
#define   C_008220_HQD_MESSAGE_BUSY                                   0xFFFDFFFF
#define   S_008220_HQD_PQ_FETCHER_BUSY(x)                             (((unsigned)(x) & 0x1) << 18)
#define   G_008220_HQD_PQ_FETCHER_BUSY(x)                             (((x) >> 18) & 0x1)
#define   C_008220_HQD_PQ_FETCHER_BUSY                                0xFFFBFFFF
#define   S_008220_HQD_IB_FETCHER_BUSY(x)                             (((unsigned)(x) & 0x1) << 19)
#define   G_008220_HQD_IB_FETCHER_BUSY(x)                             (((x) >> 19) & 0x1)
#define   C_008220_HQD_IB_FETCHER_BUSY                                0xFFF7FFFF
#define   S_008220_HQD_IQ_FETCHER_BUSY(x)                             (((unsigned)(x) & 0x1) << 20)
#define   G_008220_HQD_IQ_FETCHER_BUSY(x)                             (((x) >> 20) & 0x1)
#define   C_008220_HQD_IQ_FETCHER_BUSY                                0xFFEFFFFF
#define   S_008220_HQD_EOP_FETCHER_BUSY(x)                            (((unsigned)(x) & 0x1) << 21)
#define   G_008220_HQD_EOP_FETCHER_BUSY(x)                            (((x) >> 21) & 0x1)
#define   C_008220_HQD_EOP_FETCHER_BUSY                               0xFFDFFFFF
#define   S_008220_HQD_CONSUMED_RPTR_BUSY(x)                          (((unsigned)(x) & 0x1) << 22)
#define   G_008220_HQD_CONSUMED_RPTR_BUSY(x)                          (((x) >> 22) & 0x1)
#define   C_008220_HQD_CONSUMED_RPTR_BUSY                             0xFFBFFFFF
#define   S_008220_HQD_FETCHER_ARB_BUSY(x)                            (((unsigned)(x) & 0x1) << 23)
#define   G_008220_HQD_FETCHER_ARB_BUSY(x)                            (((x) >> 23) & 0x1)
#define   C_008220_HQD_FETCHER_ARB_BUSY                               0xFF7FFFFF
#define   S_008220_HQD_ROQ_ALIGN_BUSY(x)                              (((unsigned)(x) & 0x1) << 24)
#define   G_008220_HQD_ROQ_ALIGN_BUSY(x)                              (((x) >> 24) & 0x1)
#define   C_008220_HQD_ROQ_ALIGN_BUSY                                 0xFEFFFFFF
#define   S_008220_HQD_ROQ_EOP_BUSY(x)                                (((unsigned)(x) & 0x1) << 25)
#define   G_008220_HQD_ROQ_EOP_BUSY(x)                                (((x) >> 25) & 0x1)
#define   C_008220_HQD_ROQ_EOP_BUSY                                   0xFDFFFFFF
#define   S_008220_HQD_ROQ_IQ_BUSY(x)                                 (((unsigned)(x) & 0x1) << 26)
#define   G_008220_HQD_ROQ_IQ_BUSY(x)                                 (((x) >> 26) & 0x1)
#define   C_008220_HQD_ROQ_IQ_BUSY                                    0xFBFFFFFF
#define   S_008220_HQD_ROQ_PQ_BUSY(x)                                 (((unsigned)(x) & 0x1) << 27)
#define   G_008220_HQD_ROQ_PQ_BUSY(x)                                 (((x) >> 27) & 0x1)
#define   C_008220_HQD_ROQ_PQ_BUSY                                    0xF7FFFFFF
#define   S_008220_HQD_ROQ_IB_BUSY(x)                                 (((unsigned)(x) & 0x1) << 28)
#define   G_008220_HQD_ROQ_IB_BUSY(x)                                 (((x) >> 28) & 0x1)
#define   C_008220_HQD_ROQ_IB_BUSY                                    0xEFFFFFFF
#define   S_008220_HQD_WPTR_POLL_BUSY(x)                              (((unsigned)(x) & 0x1) << 29)
#define   G_008220_HQD_WPTR_POLL_BUSY(x)                              (((x) >> 29) & 0x1)
#define   C_008220_HQD_WPTR_POLL_BUSY                                 0xDFFFFFFF
#define   S_008220_HQD_PQ_BUSY(x)                                     (((unsigned)(x) & 0x1) << 30)
#define   G_008220_HQD_PQ_BUSY(x)                                     (((x) >> 30) & 0x1)
#define   C_008220_HQD_PQ_BUSY                                        0xBFFFFFFF
#define   S_008220_HQD_IB_BUSY(x)                                     (((unsigned)(x) & 0x1) << 31)
#define   G_008220_HQD_IB_BUSY(x)                                     (((x) >> 31) & 0x1)
#define   C_008220_HQD_IB_BUSY                                        0x7FFFFFFF
#define R_008224_CP_CPF_STALLED_STAT1                                   0x008224
#define   S_008224_RING_FETCHING_DATA(x)                              (((unsigned)(x) & 0x1) << 0)
#define   G_008224_RING_FETCHING_DATA(x)                              (((x) >> 0) & 0x1)
#define   C_008224_RING_FETCHING_DATA                                 0xFFFFFFFE
#define   S_008224_INDR1_FETCHING_DATA(x)                             (((unsigned)(x) & 0x1) << 1)
#define   G_008224_INDR1_FETCHING_DATA(x)                             (((x) >> 1) & 0x1)
#define   C_008224_INDR1_FETCHING_DATA                                0xFFFFFFFD
#define   S_008224_INDR2_FETCHING_DATA(x)                             (((unsigned)(x) & 0x1) << 2)
#define   G_008224_INDR2_FETCHING_DATA(x)                             (((x) >> 2) & 0x1)
#define   C_008224_INDR2_FETCHING_DATA                                0xFFFFFFFB
#define   S_008224_STATE_FETCHING_DATA(x)                             (((unsigned)(x) & 0x1) << 3)
#define   G_008224_STATE_FETCHING_DATA(x)                             (((x) >> 3) & 0x1)
#define   C_008224_STATE_FETCHING_DATA                                0xFFFFFFF7
#define   S_008224_TCIU_WAITING_ON_FREE(x)                            (((unsigned)(x) & 0x1) << 5)
#define   G_008224_TCIU_WAITING_ON_FREE(x)                            (((x) >> 5) & 0x1)
#define   C_008224_TCIU_WAITING_ON_FREE                               0xFFFFFFDF
#define   S_008224_TCIU_WAITING_ON_TAGS(x)                            (((unsigned)(x) & 0x1) << 6)
#define   G_008224_TCIU_WAITING_ON_TAGS(x)                            (((x) >> 6) & 0x1)
#define   C_008224_TCIU_WAITING_ON_TAGS                               0xFFFFFFBF
#define   S_008224_ATCL2IU_WAITING_ON_FREE(x)                         (((unsigned)(x) & 0x1) << 7)
#define   G_008224_ATCL2IU_WAITING_ON_FREE(x)                         (((x) >> 7) & 0x1)
#define   C_008224_ATCL2IU_WAITING_ON_FREE                            0xFFFFFF7F
#define   S_008224_ATCL2IU_WAITING_ON_TAGS(x)                         (((unsigned)(x) & 0x1) << 8)
#define   G_008224_ATCL2IU_WAITING_ON_TAGS(x)                         (((x) >> 8) & 0x1)
#define   C_008224_ATCL2IU_WAITING_ON_TAGS                            0xFFFFFEFF
#define   S_008224_ATCL1_WAITING_ON_TRANS(x)                          (((unsigned)(x) & 0x1) << 9)
#define   G_008224_ATCL1_WAITING_ON_TRANS(x)                          (((x) >> 9) & 0x1)
#define   C_008224_ATCL1_WAITING_ON_TRANS                             0xFFFFFDFF
#define R_030230_CP_COHER_SIZE_HI                                       0x030230
#define   S_030230_COHER_SIZE_HI_256B(x)                              (((unsigned)(x) & 0xFF) << 0)
#define   G_030230_COHER_SIZE_HI_256B(x)                              (((x) >> 0) & 0xFF)
#define   C_030230_COHER_SIZE_HI_256B                                 0xFFFFFF00
/*     */
#define R_0088B0_VGT_VTX_VECT_EJECT_REG                                 0x0088B0
#define   S_0088B0_PRIM_COUNT(x)                                      (((unsigned)(x) & 0x3FF) << 0)
#define   G_0088B0_PRIM_COUNT(x)                                      (((x) >> 0) & 0x3FF)
#define   C_0088B0_PRIM_COUNT                                         0xFFFFFC00
#define R_0088C4_VGT_CACHE_INVALIDATION                                 0x0088C4
#define   S_0088C4_VS_NO_EXTRA_BUFFER(x)                              (((unsigned)(x) & 0x1) << 5)
#define   G_0088C4_VS_NO_EXTRA_BUFFER(x)                              (((x) >> 5) & 0x1)
#define   C_0088C4_VS_NO_EXTRA_BUFFER                                 0xFFFFFFDF
#define   S_0088C4_STREAMOUT_FULL_FLUSH(x)                            (((unsigned)(x) & 0x1) << 13)
#define   G_0088C4_STREAMOUT_FULL_FLUSH(x)                            (((x) >> 13) & 0x1)
#define   C_0088C4_STREAMOUT_FULL_FLUSH                               0xFFFFDFFF
#define   S_0088C4_ES_LIMIT(x)                                        (((unsigned)(x) & 0x1F) << 16)
#define   G_0088C4_ES_LIMIT(x)                                        (((x) >> 16) & 0x1F)
#define   C_0088C4_ES_LIMIT                                           0xFFE0FFFF
#define R_0088C8_VGT_ESGS_RING_SIZE                                     0x0088C8
#define R_0088CC_VGT_GSVS_RING_SIZE                                     0x0088CC
#define R_0088D4_VGT_GS_VERTEX_REUSE                                    0x0088D4
#define   S_0088D4_VERT_REUSE(x)                                      (((unsigned)(x) & 0x1F) << 0)
#define   G_0088D4_VERT_REUSE(x)                                      (((x) >> 0) & 0x1F)
#define   C_0088D4_VERT_REUSE                                         0xFFFFFFE0
#define R_008958_VGT_PRIMITIVE_TYPE                                     0x008958
#define   S_008958_PRIM_TYPE(x)                                       (((unsigned)(x) & 0x3F) << 0)
#define   G_008958_PRIM_TYPE(x)                                       (((x) >> 0) & 0x3F)
#define   C_008958_PRIM_TYPE                                          0xFFFFFFC0
#define     V_008958_DI_PT_NONE                                     0x00
#define     V_008958_DI_PT_POINTLIST                                0x01
#define     V_008958_DI_PT_LINELIST                                 0x02
#define     V_008958_DI_PT_LINESTRIP                                0x03
#define     V_008958_DI_PT_TRILIST                                  0x04
#define     V_008958_DI_PT_TRIFAN                                   0x05
#define     V_008958_DI_PT_TRISTRIP                                 0x06
#define     V_008958_DI_PT_UNUSED_0                                 0x07
#define     V_008958_DI_PT_UNUSED_1                                 0x08
#define     V_008958_DI_PT_PATCH                                    0x09
#define     V_008958_DI_PT_LINELIST_ADJ                             0x0A
#define     V_008958_DI_PT_LINESTRIP_ADJ                            0x0B
#define     V_008958_DI_PT_TRILIST_ADJ                              0x0C
#define     V_008958_DI_PT_TRISTRIP_ADJ                             0x0D
#define     V_008958_DI_PT_UNUSED_3                                 0x0E
#define     V_008958_DI_PT_UNUSED_4                                 0x0F
#define     V_008958_DI_PT_TRI_WITH_WFLAGS                          0x10
#define     V_008958_DI_PT_RECTLIST                                 0x11
#define     V_008958_DI_PT_LINELOOP                                 0x12
#define     V_008958_DI_PT_QUADLIST                                 0x13
#define     V_008958_DI_PT_QUADSTRIP                                0x14
#define     V_008958_DI_PT_POLYGON                                  0x15
#define     V_008958_DI_PT_2D_COPY_RECT_LIST_V0                     0x16
#define     V_008958_DI_PT_2D_COPY_RECT_LIST_V1                     0x17
#define     V_008958_DI_PT_2D_COPY_RECT_LIST_V2                     0x18
#define     V_008958_DI_PT_2D_COPY_RECT_LIST_V3                     0x19
#define     V_008958_DI_PT_2D_FILL_RECT_LIST                        0x1A
#define     V_008958_DI_PT_2D_LINE_STRIP                            0x1B
#define     V_008958_DI_PT_2D_TRI_STRIP                             0x1C
#define R_00895C_VGT_INDEX_TYPE                                         0x00895C
#define   S_00895C_INDEX_TYPE(x)                                      (((unsigned)(x) & 0x03) << 0)
#define   G_00895C_INDEX_TYPE(x)                                      (((x) >> 0) & 0x03)
#define   C_00895C_INDEX_TYPE                                         0xFFFFFFFC
#define     V_00895C_DI_INDEX_SIZE_16_BIT                           0x00
#define     V_00895C_DI_INDEX_SIZE_32_BIT                           0x01
#define R_008960_VGT_STRMOUT_BUFFER_FILLED_SIZE_0                       0x008960
#define R_008964_VGT_STRMOUT_BUFFER_FILLED_SIZE_1                       0x008964
#define R_008968_VGT_STRMOUT_BUFFER_FILLED_SIZE_2                       0x008968
#define R_00896C_VGT_STRMOUT_BUFFER_FILLED_SIZE_3                       0x00896C
#define R_008970_VGT_NUM_INDICES                                        0x008970
#define R_008974_VGT_NUM_INSTANCES                                      0x008974
#define R_008988_VGT_TF_RING_SIZE                                       0x008988
#define   S_008988_SIZE(x)                                            (((unsigned)(x) & 0xFFFF) << 0)
#define   G_008988_SIZE(x)                                            (((x) >> 0) & 0xFFFF)
#define   C_008988_SIZE                                               0xFFFF0000
#define R_0089B0_VGT_HS_OFFCHIP_PARAM                                   0x0089B0
#define   S_0089B0_OFFCHIP_BUFFERING(x)                               (((unsigned)(x) & 0x7F) << 0)
#define   G_0089B0_OFFCHIP_BUFFERING(x)                               (((x) >> 0) & 0x7F)
#define   C_0089B0_OFFCHIP_BUFFERING                                  0xFFFFFF80
#define R_0089B8_VGT_TF_MEMORY_BASE                                     0x0089B8
#define R_008A14_PA_CL_ENHANCE                                          0x008A14
#define   S_008A14_CLIP_VTX_REORDER_ENA(x)                            (((unsigned)(x) & 0x1) << 0)
#define   G_008A14_CLIP_VTX_REORDER_ENA(x)                            (((x) >> 0) & 0x1)
#define   C_008A14_CLIP_VTX_REORDER_ENA                               0xFFFFFFFE
#define   S_008A14_NUM_CLIP_SEQ(x)                                    (((unsigned)(x) & 0x03) << 1)
#define   G_008A14_NUM_CLIP_SEQ(x)                                    (((x) >> 1) & 0x03)
#define   C_008A14_NUM_CLIP_SEQ                                       0xFFFFFFF9
#define   S_008A14_CLIPPED_PRIM_SEQ_STALL(x)                          (((unsigned)(x) & 0x1) << 3)
#define   G_008A14_CLIPPED_PRIM_SEQ_STALL(x)                          (((x) >> 3) & 0x1)
#define   C_008A14_CLIPPED_PRIM_SEQ_STALL                             0xFFFFFFF7
#define   S_008A14_VE_NAN_PROC_DISABLE(x)                             (((unsigned)(x) & 0x1) << 4)
#define   G_008A14_VE_NAN_PROC_DISABLE(x)                             (((x) >> 4) & 0x1)
#define   C_008A14_VE_NAN_PROC_DISABLE                                0xFFFFFFEF
#define R_008A60_PA_SU_LINE_STIPPLE_VALUE                               0x008A60
#define   S_008A60_LINE_STIPPLE_VALUE(x)                              (((unsigned)(x) & 0xFFFFFF) << 0)
#define   G_008A60_LINE_STIPPLE_VALUE(x)                              (((x) >> 0) & 0xFFFFFF)
#define   C_008A60_LINE_STIPPLE_VALUE                                 0xFF000000
#define R_008B10_PA_SC_LINE_STIPPLE_STATE                               0x008B10
#define   S_008B10_CURRENT_PTR(x)                                     (((unsigned)(x) & 0x0F) << 0)
#define   G_008B10_CURRENT_PTR(x)                                     (((x) >> 0) & 0x0F)
#define   C_008B10_CURRENT_PTR                                        0xFFFFFFF0
#define   S_008B10_CURRENT_COUNT(x)                                   (((unsigned)(x) & 0xFF) << 8)
#define   G_008B10_CURRENT_COUNT(x)                                   (((x) >> 8) & 0xFF)
#define   C_008B10_CURRENT_COUNT                                      0xFFFF00FF
#define R_008670_CP_STALLED_STAT3                                       0x008670
#define   S_008670_CE_TO_CSF_NOT_RDY_TO_RCV(x)                        (((unsigned)(x) & 0x1) << 0)
#define   G_008670_CE_TO_CSF_NOT_RDY_TO_RCV(x)                        (((x) >> 0) & 0x1)
#define   C_008670_CE_TO_CSF_NOT_RDY_TO_RCV                           0xFFFFFFFE
#define   S_008670_CE_TO_RAM_INIT_FETCHER_NOT_RDY_TO_RCV(x)           (((unsigned)(x) & 0x1) << 1)
#define   G_008670_CE_TO_RAM_INIT_FETCHER_NOT_RDY_TO_RCV(x)           (((x) >> 1) & 0x1)
#define   C_008670_CE_TO_RAM_INIT_FETCHER_NOT_RDY_TO_RCV              0xFFFFFFFD
#define   S_008670_CE_WAITING_ON_DATA_FROM_RAM_INIT_FETCHER(x)        (((unsigned)(x) & 0x1) << 2)
#define   G_008670_CE_WAITING_ON_DATA_FROM_RAM_INIT_FETCHER(x)        (((x) >> 2) & 0x1)
#define   C_008670_CE_WAITING_ON_DATA_FROM_RAM_INIT_FETCHER           0xFFFFFFFB
#define   S_008670_CE_TO_RAM_INIT_NOT_RDY(x)                          (((unsigned)(x) & 0x1) << 3)
#define   G_008670_CE_TO_RAM_INIT_NOT_RDY(x)                          (((x) >> 3) & 0x1)
#define   C_008670_CE_TO_RAM_INIT_NOT_RDY                             0xFFFFFFF7
#define   S_008670_CE_TO_RAM_DUMP_NOT_RDY(x)                          (((unsigned)(x) & 0x1) << 4)
#define   G_008670_CE_TO_RAM_DUMP_NOT_RDY(x)                          (((x) >> 4) & 0x1)
#define   C_008670_CE_TO_RAM_DUMP_NOT_RDY                             0xFFFFFFEF
#define   S_008670_CE_TO_RAM_WRITE_NOT_RDY(x)                         (((unsigned)(x) & 0x1) << 5)
#define   G_008670_CE_TO_RAM_WRITE_NOT_RDY(x)                         (((x) >> 5) & 0x1)
#define   C_008670_CE_TO_RAM_WRITE_NOT_RDY                            0xFFFFFFDF
#define   S_008670_CE_TO_INC_FIFO_NOT_RDY_TO_RCV(x)                   (((unsigned)(x) & 0x1) << 6)
#define   G_008670_CE_TO_INC_FIFO_NOT_RDY_TO_RCV(x)                   (((x) >> 6) & 0x1)
#define   C_008670_CE_TO_INC_FIFO_NOT_RDY_TO_RCV                      0xFFFFFFBF
#define   S_008670_CE_TO_WR_FIFO_NOT_RDY_TO_RCV(x)                    (((unsigned)(x) & 0x1) << 7)
#define   G_008670_CE_TO_WR_FIFO_NOT_RDY_TO_RCV(x)                    (((x) >> 7) & 0x1)
#define   C_008670_CE_TO_WR_FIFO_NOT_RDY_TO_RCV                       0xFFFFFF7F
#define   S_008670_CE_WAITING_ON_BUFFER_DATA(x)                       (((unsigned)(x) & 0x1) << 10)
#define   G_008670_CE_WAITING_ON_BUFFER_DATA(x)                       (((x) >> 10) & 0x1)
#define   C_008670_CE_WAITING_ON_BUFFER_DATA                          0xFFFFFBFF
#define   S_008670_CE_WAITING_ON_CE_BUFFER_FLAG(x)                    (((unsigned)(x) & 0x1) << 11)
#define   G_008670_CE_WAITING_ON_CE_BUFFER_FLAG(x)                    (((x) >> 11) & 0x1)
#define   C_008670_CE_WAITING_ON_CE_BUFFER_FLAG                       0xFFFFF7FF
#define   S_008670_CE_WAITING_ON_DE_COUNTER(x)                        (((unsigned)(x) & 0x1) << 12)
#define   G_008670_CE_WAITING_ON_DE_COUNTER(x)                        (((x) >> 12) & 0x1)
#define   C_008670_CE_WAITING_ON_DE_COUNTER                           0xFFFFEFFF
#define   S_008670_CE_WAITING_ON_DE_COUNTER_UNDERFLOW(x)              (((unsigned)(x) & 0x1) << 13)
#define   G_008670_CE_WAITING_ON_DE_COUNTER_UNDERFLOW(x)              (((x) >> 13) & 0x1)
#define   C_008670_CE_WAITING_ON_DE_COUNTER_UNDERFLOW                 0xFFFFDFFF
#define   S_008670_TCIU_WAITING_ON_FREE(x)                            (((unsigned)(x) & 0x1) << 14)
#define   G_008670_TCIU_WAITING_ON_FREE(x)                            (((x) >> 14) & 0x1)
#define   C_008670_TCIU_WAITING_ON_FREE                               0xFFFFBFFF
#define   S_008670_TCIU_WAITING_ON_TAGS(x)                            (((unsigned)(x) & 0x1) << 15)
#define   G_008670_TCIU_WAITING_ON_TAGS(x)                            (((x) >> 15) & 0x1)
#define   C_008670_TCIU_WAITING_ON_TAGS                               0xFFFF7FFF
#define   S_008670_CE_STALLED_ON_TC_WR_CONFIRM(x)                     (((unsigned)(x) & 0x1) << 16)
#define   G_008670_CE_STALLED_ON_TC_WR_CONFIRM(x)                     (((x) >> 16) & 0x1)
#define   C_008670_CE_STALLED_ON_TC_WR_CONFIRM                        0xFFFEFFFF
#define   S_008670_CE_STALLED_ON_ATOMIC_RTN_DATA(x)                   (((unsigned)(x) & 0x1) << 17)
#define   G_008670_CE_STALLED_ON_ATOMIC_RTN_DATA(x)                   (((x) >> 17) & 0x1)
#define   C_008670_CE_STALLED_ON_ATOMIC_RTN_DATA                      0xFFFDFFFF
#define   S_008670_ATCL2IU_WAITING_ON_FREE(x)                         (((unsigned)(x) & 0x1) << 18)
#define   G_008670_ATCL2IU_WAITING_ON_FREE(x)                         (((x) >> 18) & 0x1)
#define   C_008670_ATCL2IU_WAITING_ON_FREE                            0xFFFBFFFF
#define   S_008670_ATCL2IU_WAITING_ON_TAGS(x)                         (((unsigned)(x) & 0x1) << 19)
#define   G_008670_ATCL2IU_WAITING_ON_TAGS(x)                         (((x) >> 19) & 0x1)
#define   C_008670_ATCL2IU_WAITING_ON_TAGS                            0xFFF7FFFF
#define   S_008670_ATCL1_WAITING_ON_TRANS(x)                          (((unsigned)(x) & 0x1) << 20)
#define   G_008670_ATCL1_WAITING_ON_TRANS(x)                          (((x) >> 20) & 0x1)
#define   C_008670_ATCL1_WAITING_ON_TRANS                             0xFFEFFFFF
#define R_008674_CP_STALLED_STAT1                                       0x008674
#define   S_008674_RBIU_TO_DMA_NOT_RDY_TO_RCV(x)                      (((unsigned)(x) & 0x1) << 0)
#define   G_008674_RBIU_TO_DMA_NOT_RDY_TO_RCV(x)                      (((x) >> 0) & 0x1)
#define   C_008674_RBIU_TO_DMA_NOT_RDY_TO_RCV                         0xFFFFFFFE
#define   S_008674_RBIU_TO_SEM_NOT_RDY_TO_RCV(x)                      (((unsigned)(x) & 0x1) << 2)
#define   G_008674_RBIU_TO_SEM_NOT_RDY_TO_RCV(x)                      (((x) >> 2) & 0x1)
#define   C_008674_RBIU_TO_SEM_NOT_RDY_TO_RCV                         0xFFFFFFFB
#define   S_008674_RBIU_TO_MEMWR_NOT_RDY_TO_RCV(x)                    (((unsigned)(x) & 0x1) << 4)
#define   G_008674_RBIU_TO_MEMWR_NOT_RDY_TO_RCV(x)                    (((x) >> 4) & 0x1)
#define   C_008674_RBIU_TO_MEMWR_NOT_RDY_TO_RCV                       0xFFFFFFEF
#define   S_008674_ME_HAS_ACTIVE_CE_BUFFER_FLAG(x)                    (((unsigned)(x) & 0x1) << 10)
#define   G_008674_ME_HAS_ACTIVE_CE_BUFFER_FLAG(x)                    (((x) >> 10) & 0x1)
#define   C_008674_ME_HAS_ACTIVE_CE_BUFFER_FLAG                       0xFFFFFBFF
#define   S_008674_ME_HAS_ACTIVE_DE_BUFFER_FLAG(x)                    (((unsigned)(x) & 0x1) << 11)
#define   G_008674_ME_HAS_ACTIVE_DE_BUFFER_FLAG(x)                    (((x) >> 11) & 0x1)
#define   C_008674_ME_HAS_ACTIVE_DE_BUFFER_FLAG                       0xFFFFF7FF
#define   S_008674_ME_STALLED_ON_TC_WR_CONFIRM(x)                     (((unsigned)(x) & 0x1) << 12)
#define   G_008674_ME_STALLED_ON_TC_WR_CONFIRM(x)                     (((x) >> 12) & 0x1)
#define   C_008674_ME_STALLED_ON_TC_WR_CONFIRM                        0xFFFFEFFF
#define   S_008674_ME_STALLED_ON_ATOMIC_RTN_DATA(x)                   (((unsigned)(x) & 0x1) << 13)
#define   G_008674_ME_STALLED_ON_ATOMIC_RTN_DATA(x)                   (((x) >> 13) & 0x1)
#define   C_008674_ME_STALLED_ON_ATOMIC_RTN_DATA                      0xFFFFDFFF
#define   S_008674_ME_WAITING_ON_TC_READ_DATA(x)                      (((unsigned)(x) & 0x1) << 14)
#define   G_008674_ME_WAITING_ON_TC_READ_DATA(x)                      (((x) >> 14) & 0x1)
#define   C_008674_ME_WAITING_ON_TC_READ_DATA                         0xFFFFBFFF
#define   S_008674_ME_WAITING_ON_REG_READ_DATA(x)                     (((unsigned)(x) & 0x1) << 15)
#define   G_008674_ME_WAITING_ON_REG_READ_DATA(x)                     (((x) >> 15) & 0x1)
#define   C_008674_ME_WAITING_ON_REG_READ_DATA                        0xFFFF7FFF
#define   S_008674_RCIU_WAITING_ON_GDS_FREE(x)                        (((unsigned)(x) & 0x1) << 23)
#define   G_008674_RCIU_WAITING_ON_GDS_FREE(x)                        (((x) >> 23) & 0x1)
#define   C_008674_RCIU_WAITING_ON_GDS_FREE                           0xFF7FFFFF
#define   S_008674_RCIU_WAITING_ON_GRBM_FREE(x)                       (((unsigned)(x) & 0x1) << 24)
#define   G_008674_RCIU_WAITING_ON_GRBM_FREE(x)                       (((x) >> 24) & 0x1)
#define   C_008674_RCIU_WAITING_ON_GRBM_FREE                          0xFEFFFFFF
#define   S_008674_RCIU_WAITING_ON_VGT_FREE(x)                        (((unsigned)(x) & 0x1) << 25)
#define   G_008674_RCIU_WAITING_ON_VGT_FREE(x)                        (((x) >> 25) & 0x1)
#define   C_008674_RCIU_WAITING_ON_VGT_FREE                           0xFDFFFFFF
#define   S_008674_RCIU_STALLED_ON_ME_READ(x)                         (((unsigned)(x) & 0x1) << 26)
#define   G_008674_RCIU_STALLED_ON_ME_READ(x)                         (((x) >> 26) & 0x1)
#define   C_008674_RCIU_STALLED_ON_ME_READ                            0xFBFFFFFF
#define   S_008674_RCIU_STALLED_ON_DMA_READ(x)                        (((unsigned)(x) & 0x1) << 27)
#define   G_008674_RCIU_STALLED_ON_DMA_READ(x)                        (((x) >> 27) & 0x1)
#define   C_008674_RCIU_STALLED_ON_DMA_READ                           0xF7FFFFFF
#define   S_008674_RCIU_STALLED_ON_APPEND_READ(x)                     (((unsigned)(x) & 0x1) << 28)
#define   G_008674_RCIU_STALLED_ON_APPEND_READ(x)                     (((x) >> 28) & 0x1)
#define   C_008674_RCIU_STALLED_ON_APPEND_READ                        0xEFFFFFFF
#define   S_008674_RCIU_HALTED_BY_REG_VIOLATION(x)                    (((unsigned)(x) & 0x1) << 29)
#define   G_008674_RCIU_HALTED_BY_REG_VIOLATION(x)                    (((x) >> 29) & 0x1)
#define   C_008674_RCIU_HALTED_BY_REG_VIOLATION                       0xDFFFFFFF
#define R_008678_CP_STALLED_STAT2                                       0x008678
#define   S_008678_PFP_TO_CSF_NOT_RDY_TO_RCV(x)                       (((unsigned)(x) & 0x1) << 0)
#define   G_008678_PFP_TO_CSF_NOT_RDY_TO_RCV(x)                       (((x) >> 0) & 0x1)
#define   C_008678_PFP_TO_CSF_NOT_RDY_TO_RCV                          0xFFFFFFFE
#define   S_008678_PFP_TO_MEQ_NOT_RDY_TO_RCV(x)                       (((unsigned)(x) & 0x1) << 1)
#define   G_008678_PFP_TO_MEQ_NOT_RDY_TO_RCV(x)                       (((x) >> 1) & 0x1)
#define   C_008678_PFP_TO_MEQ_NOT_RDY_TO_RCV                          0xFFFFFFFD
#define   S_008678_PFP_TO_RCIU_NOT_RDY_TO_RCV(x)                      (((unsigned)(x) & 0x1) << 2)
#define   G_008678_PFP_TO_RCIU_NOT_RDY_TO_RCV(x)                      (((x) >> 2) & 0x1)
#define   C_008678_PFP_TO_RCIU_NOT_RDY_TO_RCV                         0xFFFFFFFB
#define   S_008678_PFP_TO_VGT_WRITES_PENDING(x)                       (((unsigned)(x) & 0x1) << 4)
#define   G_008678_PFP_TO_VGT_WRITES_PENDING(x)                       (((x) >> 4) & 0x1)
#define   C_008678_PFP_TO_VGT_WRITES_PENDING                          0xFFFFFFEF
#define   S_008678_PFP_RCIU_READ_PENDING(x)                           (((unsigned)(x) & 0x1) << 5)
#define   G_008678_PFP_RCIU_READ_PENDING(x)                           (((x) >> 5) & 0x1)
#define   C_008678_PFP_RCIU_READ_PENDING                              0xFFFFFFDF
#define   S_008678_PFP_WAITING_ON_BUFFER_DATA(x)                      (((unsigned)(x) & 0x1) << 8)
#define   G_008678_PFP_WAITING_ON_BUFFER_DATA(x)                      (((x) >> 8) & 0x1)
#define   C_008678_PFP_WAITING_ON_BUFFER_DATA                         0xFFFFFEFF
#define   S_008678_ME_WAIT_ON_CE_COUNTER(x)                           (((unsigned)(x) & 0x1) << 9)
#define   G_008678_ME_WAIT_ON_CE_COUNTER(x)                           (((x) >> 9) & 0x1)
#define   C_008678_ME_WAIT_ON_CE_COUNTER                              0xFFFFFDFF
#define   S_008678_ME_WAIT_ON_AVAIL_BUFFER(x)                         (((unsigned)(x) & 0x1) << 10)
#define   G_008678_ME_WAIT_ON_AVAIL_BUFFER(x)                         (((x) >> 10) & 0x1)
#define   C_008678_ME_WAIT_ON_AVAIL_BUFFER                            0xFFFFFBFF
#define   S_008678_GFX_CNTX_NOT_AVAIL_TO_ME(x)                        (((unsigned)(x) & 0x1) << 11)
#define   G_008678_GFX_CNTX_NOT_AVAIL_TO_ME(x)                        (((x) >> 11) & 0x1)
#define   C_008678_GFX_CNTX_NOT_AVAIL_TO_ME                           0xFFFFF7FF
#define   S_008678_ME_RCIU_NOT_RDY_TO_RCV(x)                          (((unsigned)(x) & 0x1) << 12)
#define   G_008678_ME_RCIU_NOT_RDY_TO_RCV(x)                          (((x) >> 12) & 0x1)
#define   C_008678_ME_RCIU_NOT_RDY_TO_RCV                             0xFFFFEFFF
#define   S_008678_ME_TO_CONST_NOT_RDY_TO_RCV(x)                      (((unsigned)(x) & 0x1) << 13)
#define   G_008678_ME_TO_CONST_NOT_RDY_TO_RCV(x)                      (((x) >> 13) & 0x1)
#define   C_008678_ME_TO_CONST_NOT_RDY_TO_RCV                         0xFFFFDFFF
#define   S_008678_ME_WAITING_DATA_FROM_PFP(x)                        (((unsigned)(x) & 0x1) << 14)
#define   G_008678_ME_WAITING_DATA_FROM_PFP(x)                        (((x) >> 14) & 0x1)
#define   C_008678_ME_WAITING_DATA_FROM_PFP                           0xFFFFBFFF
#define   S_008678_ME_WAITING_ON_PARTIAL_FLUSH(x)                     (((unsigned)(x) & 0x1) << 15)
#define   G_008678_ME_WAITING_ON_PARTIAL_FLUSH(x)                     (((x) >> 15) & 0x1)
#define   C_008678_ME_WAITING_ON_PARTIAL_FLUSH                        0xFFFF7FFF
#define   S_008678_MEQ_TO_ME_NOT_RDY_TO_RCV(x)                        (((unsigned)(x) & 0x1) << 16)
#define   G_008678_MEQ_TO_ME_NOT_RDY_TO_RCV(x)                        (((x) >> 16) & 0x1)
#define   C_008678_MEQ_TO_ME_NOT_RDY_TO_RCV                           0xFFFEFFFF
#define   S_008678_STQ_TO_ME_NOT_RDY_TO_RCV(x)                        (((unsigned)(x) & 0x1) << 17)
#define   G_008678_STQ_TO_ME_NOT_RDY_TO_RCV(x)                        (((x) >> 17) & 0x1)
#define   C_008678_STQ_TO_ME_NOT_RDY_TO_RCV                           0xFFFDFFFF
#define   S_008678_ME_WAITING_DATA_FROM_STQ(x)                        (((unsigned)(x) & 0x1) << 18)
#define   G_008678_ME_WAITING_DATA_FROM_STQ(x)                        (((x) >> 18) & 0x1)
#define   C_008678_ME_WAITING_DATA_FROM_STQ                           0xFFFBFFFF
#define   S_008678_PFP_STALLED_ON_TC_WR_CONFIRM(x)                    (((unsigned)(x) & 0x1) << 19)
#define   G_008678_PFP_STALLED_ON_TC_WR_CONFIRM(x)                    (((x) >> 19) & 0x1)
#define   C_008678_PFP_STALLED_ON_TC_WR_CONFIRM                       0xFFF7FFFF
#define   S_008678_PFP_STALLED_ON_ATOMIC_RTN_DATA(x)                  (((unsigned)(x) & 0x1) << 20)
#define   G_008678_PFP_STALLED_ON_ATOMIC_RTN_DATA(x)                  (((x) >> 20) & 0x1)
#define   C_008678_PFP_STALLED_ON_ATOMIC_RTN_DATA                     0xFFEFFFFF
#define   S_008678_EOPD_FIFO_NEEDS_SC_EOP_DONE(x)                     (((unsigned)(x) & 0x1) << 21)
#define   G_008678_EOPD_FIFO_NEEDS_SC_EOP_DONE(x)                     (((x) >> 21) & 0x1)
#define   C_008678_EOPD_FIFO_NEEDS_SC_EOP_DONE                        0xFFDFFFFF
#define   S_008678_EOPD_FIFO_NEEDS_WR_CONFIRM(x)                      (((unsigned)(x) & 0x1) << 22)
#define   G_008678_EOPD_FIFO_NEEDS_WR_CONFIRM(x)                      (((x) >> 22) & 0x1)
#define   C_008678_EOPD_FIFO_NEEDS_WR_CONFIRM                         0xFFBFFFFF
#define   S_008678_STRMO_WR_OF_PRIM_DATA_PENDING(x)                   (((unsigned)(x) & 0x1) << 23)
#define   G_008678_STRMO_WR_OF_PRIM_DATA_PENDING(x)                   (((x) >> 23) & 0x1)
#define   C_008678_STRMO_WR_OF_PRIM_DATA_PENDING                      0xFF7FFFFF
#define   S_008678_PIPE_STATS_WR_DATA_PENDING(x)                      (((unsigned)(x) & 0x1) << 24)
#define   G_008678_PIPE_STATS_WR_DATA_PENDING(x)                      (((x) >> 24) & 0x1)
#define   C_008678_PIPE_STATS_WR_DATA_PENDING                         0xFEFFFFFF
#define   S_008678_APPEND_RDY_WAIT_ON_CS_DONE(x)                      (((unsigned)(x) & 0x1) << 25)
#define   G_008678_APPEND_RDY_WAIT_ON_CS_DONE(x)                      (((x) >> 25) & 0x1)
#define   C_008678_APPEND_RDY_WAIT_ON_CS_DONE                         0xFDFFFFFF
#define   S_008678_APPEND_RDY_WAIT_ON_PS_DONE(x)                      (((unsigned)(x) & 0x1) << 26)
#define   G_008678_APPEND_RDY_WAIT_ON_PS_DONE(x)                      (((x) >> 26) & 0x1)
#define   C_008678_APPEND_RDY_WAIT_ON_PS_DONE                         0xFBFFFFFF
#define   S_008678_APPEND_WAIT_ON_WR_CONFIRM(x)                       (((unsigned)(x) & 0x1) << 27)
#define   G_008678_APPEND_WAIT_ON_WR_CONFIRM(x)                       (((x) >> 27) & 0x1)
#define   C_008678_APPEND_WAIT_ON_WR_CONFIRM                          0xF7FFFFFF
#define   S_008678_APPEND_ACTIVE_PARTITION(x)                         (((unsigned)(x) & 0x1) << 28)
#define   G_008678_APPEND_ACTIVE_PARTITION(x)                         (((x) >> 28) & 0x1)
#define   C_008678_APPEND_ACTIVE_PARTITION                            0xEFFFFFFF
#define   S_008678_APPEND_WAITING_TO_SEND_MEMWRITE(x)                 (((unsigned)(x) & 0x1) << 29)
#define   G_008678_APPEND_WAITING_TO_SEND_MEMWRITE(x)                 (((x) >> 29) & 0x1)
#define   C_008678_APPEND_WAITING_TO_SEND_MEMWRITE                    0xDFFFFFFF
#define   S_008678_SURF_SYNC_NEEDS_IDLE_CNTXS(x)                      (((unsigned)(x) & 0x1) << 30)
#define   G_008678_SURF_SYNC_NEEDS_IDLE_CNTXS(x)                      (((x) >> 30) & 0x1)
#define   C_008678_SURF_SYNC_NEEDS_IDLE_CNTXS                         0xBFFFFFFF
#define   S_008678_SURF_SYNC_NEEDS_ALL_CLEAN(x)                       (((unsigned)(x) & 0x1) << 31)
#define   G_008678_SURF_SYNC_NEEDS_ALL_CLEAN(x)                       (((x) >> 31) & 0x1)
#define   C_008678_SURF_SYNC_NEEDS_ALL_CLEAN                          0x7FFFFFFF
#define R_008680_CP_STAT                                                0x008680
#define   S_008680_ROQ_RING_BUSY(x)                                   (((unsigned)(x) & 0x1) << 9)
#define   G_008680_ROQ_RING_BUSY(x)                                   (((x) >> 9) & 0x1)
#define   C_008680_ROQ_RING_BUSY                                      0xFFFFFDFF
#define   S_008680_ROQ_INDIRECT1_BUSY(x)                              (((unsigned)(x) & 0x1) << 10)
#define   G_008680_ROQ_INDIRECT1_BUSY(x)                              (((x) >> 10) & 0x1)
#define   C_008680_ROQ_INDIRECT1_BUSY                                 0xFFFFFBFF
#define   S_008680_ROQ_INDIRECT2_BUSY(x)                              (((unsigned)(x) & 0x1) << 11)
#define   G_008680_ROQ_INDIRECT2_BUSY(x)                              (((x) >> 11) & 0x1)
#define   C_008680_ROQ_INDIRECT2_BUSY                                 0xFFFFF7FF
#define   S_008680_ROQ_STATE_BUSY(x)                                  (((unsigned)(x) & 0x1) << 12)
#define   G_008680_ROQ_STATE_BUSY(x)                                  (((x) >> 12) & 0x1)
#define   C_008680_ROQ_STATE_BUSY                                     0xFFFFEFFF
#define   S_008680_DC_BUSY(x)                                         (((unsigned)(x) & 0x1) << 13)
#define   G_008680_DC_BUSY(x)                                         (((x) >> 13) & 0x1)
#define   C_008680_DC_BUSY                                            0xFFFFDFFF
#define   S_008680_ATCL2IU_BUSY(x)                                    (((unsigned)(x) & 0x1) << 14)
#define   G_008680_ATCL2IU_BUSY(x)                                    (((x) >> 14) & 0x1)
#define   C_008680_ATCL2IU_BUSY                                       0xFFFFBFFF
#define   S_008680_PFP_BUSY(x)                                        (((unsigned)(x) & 0x1) << 15)
#define   G_008680_PFP_BUSY(x)                                        (((x) >> 15) & 0x1)
#define   C_008680_PFP_BUSY                                           0xFFFF7FFF
#define   S_008680_MEQ_BUSY(x)                                        (((unsigned)(x) & 0x1) << 16)
#define   G_008680_MEQ_BUSY(x)                                        (((x) >> 16) & 0x1)
#define   C_008680_MEQ_BUSY                                           0xFFFEFFFF
#define   S_008680_ME_BUSY(x)                                         (((unsigned)(x) & 0x1) << 17)
#define   G_008680_ME_BUSY(x)                                         (((x) >> 17) & 0x1)
#define   C_008680_ME_BUSY                                            0xFFFDFFFF
#define   S_008680_QUERY_BUSY(x)                                      (((unsigned)(x) & 0x1) << 18)
#define   G_008680_QUERY_BUSY(x)                                      (((x) >> 18) & 0x1)
#define   C_008680_QUERY_BUSY                                         0xFFFBFFFF
#define   S_008680_SEMAPHORE_BUSY(x)                                  (((unsigned)(x) & 0x1) << 19)
#define   G_008680_SEMAPHORE_BUSY(x)                                  (((x) >> 19) & 0x1)
#define   C_008680_SEMAPHORE_BUSY                                     0xFFF7FFFF
#define   S_008680_INTERRUPT_BUSY(x)                                  (((unsigned)(x) & 0x1) << 20)
#define   G_008680_INTERRUPT_BUSY(x)                                  (((x) >> 20) & 0x1)
#define   C_008680_INTERRUPT_BUSY                                     0xFFEFFFFF
#define   S_008680_SURFACE_SYNC_BUSY(x)                               (((unsigned)(x) & 0x1) << 21)
#define   G_008680_SURFACE_SYNC_BUSY(x)                               (((x) >> 21) & 0x1)
#define   C_008680_SURFACE_SYNC_BUSY                                  0xFFDFFFFF
#define   S_008680_DMA_BUSY(x)                                        (((unsigned)(x) & 0x1) << 22)
#define   G_008680_DMA_BUSY(x)                                        (((x) >> 22) & 0x1)
#define   C_008680_DMA_BUSY                                           0xFFBFFFFF
#define   S_008680_RCIU_BUSY(x)                                       (((unsigned)(x) & 0x1) << 23)
#define   G_008680_RCIU_BUSY(x)                                       (((x) >> 23) & 0x1)
#define   C_008680_RCIU_BUSY                                          0xFF7FFFFF
#define   S_008680_SCRATCH_RAM_BUSY(x)                                (((unsigned)(x) & 0x1) << 24)
#define   G_008680_SCRATCH_RAM_BUSY(x)                                (((x) >> 24) & 0x1)
#define   C_008680_SCRATCH_RAM_BUSY                                   0xFEFFFFFF
#define   S_008680_CPC_CPG_BUSY(x)                                    (((unsigned)(x) & 0x1) << 25)
#define   G_008680_CPC_CPG_BUSY(x)                                    (((x) >> 25) & 0x1)
#define   C_008680_CPC_CPG_BUSY                                       0xFDFFFFFF
#define   S_008680_CE_BUSY(x)                                         (((unsigned)(x) & 0x1) << 26)
#define   G_008680_CE_BUSY(x)                                         (((x) >> 26) & 0x1)
#define   C_008680_CE_BUSY                                            0xFBFFFFFF
#define   S_008680_TCIU_BUSY(x)                                       (((unsigned)(x) & 0x1) << 27)
#define   G_008680_TCIU_BUSY(x)                                       (((x) >> 27) & 0x1)
#define   C_008680_TCIU_BUSY                                          0xF7FFFFFF
#define   S_008680_ROQ_CE_RING_BUSY(x)                                (((unsigned)(x) & 0x1) << 28)
#define   G_008680_ROQ_CE_RING_BUSY(x)                                (((x) >> 28) & 0x1)
#define   C_008680_ROQ_CE_RING_BUSY                                   0xEFFFFFFF
#define   S_008680_ROQ_CE_INDIRECT1_BUSY(x)                           (((unsigned)(x) & 0x1) << 29)
#define   G_008680_ROQ_CE_INDIRECT1_BUSY(x)                           (((x) >> 29) & 0x1)
#define   C_008680_ROQ_CE_INDIRECT1_BUSY                              0xDFFFFFFF
#define   S_008680_ROQ_CE_INDIRECT2_BUSY(x)                           (((unsigned)(x) & 0x1) << 30)
#define   G_008680_ROQ_CE_INDIRECT2_BUSY(x)                           (((x) >> 30) & 0x1)
#define   C_008680_ROQ_CE_INDIRECT2_BUSY                              0xBFFFFFFF
#define   S_008680_CP_BUSY(x)                                         (((unsigned)(x) & 0x1) << 31)
#define   G_008680_CP_BUSY(x)                                         (((x) >> 31) & 0x1)
#define   C_008680_CP_BUSY                                            0x7FFFFFFF
/* CIK */
#define R_030800_GRBM_GFX_INDEX                                         0x030800
#define   S_030800_INSTANCE_INDEX(x)                                  (((unsigned)(x) & 0xFF) << 0)
#define   G_030800_INSTANCE_INDEX(x)                                  (((x) >> 0) & 0xFF)
#define   C_030800_INSTANCE_INDEX                                     0xFFFFFF00
#define   S_030800_SH_INDEX(x)                                        (((unsigned)(x) & 0xFF) << 8)
#define   G_030800_SH_INDEX(x)                                        (((x) >> 8) & 0xFF)
#define   C_030800_SH_INDEX                                           0xFFFF00FF
#define   S_030800_SE_INDEX(x)                                        (((unsigned)(x) & 0xFF) << 16)
#define   G_030800_SE_INDEX(x)                                        (((x) >> 16) & 0xFF)
#define   C_030800_SE_INDEX                                           0xFF00FFFF
#define   S_030800_SH_BROADCAST_WRITES(x)                             (((unsigned)(x) & 0x1) << 29)
#define   G_030800_SH_BROADCAST_WRITES(x)                             (((x) >> 29) & 0x1)
#define   C_030800_SH_BROADCAST_WRITES                                0xDFFFFFFF
#define   S_030800_INSTANCE_BROADCAST_WRITES(x)                       (((unsigned)(x) & 0x1) << 30)
#define   G_030800_INSTANCE_BROADCAST_WRITES(x)                       (((x) >> 30) & 0x1)
#define   C_030800_INSTANCE_BROADCAST_WRITES                          0xBFFFFFFF
#define   S_030800_SE_BROADCAST_WRITES(x)                             (((unsigned)(x) & 0x1) << 31)
#define   G_030800_SE_BROADCAST_WRITES(x)                             (((x) >> 31) & 0x1)
#define   C_030800_SE_BROADCAST_WRITES                                0x7FFFFFFF
#define R_030900_VGT_ESGS_RING_SIZE                                     0x030900
#define R_030904_VGT_GSVS_RING_SIZE                                     0x030904
#define R_030908_VGT_PRIMITIVE_TYPE                                     0x030908
#define   S_030908_PRIM_TYPE(x)                                       (((unsigned)(x) & 0x3F) << 0)
#define   G_030908_PRIM_TYPE(x)                                       (((x) >> 0) & 0x3F)
#define   C_030908_PRIM_TYPE                                          0xFFFFFFC0
#define     V_030908_DI_PT_NONE                                     0x00
#define     V_030908_DI_PT_POINTLIST                                0x01
#define     V_030908_DI_PT_LINELIST                                 0x02
#define     V_030908_DI_PT_LINESTRIP                                0x03
#define     V_030908_DI_PT_TRILIST                                  0x04
#define     V_030908_DI_PT_TRIFAN                                   0x05
#define     V_030908_DI_PT_TRISTRIP                                 0x06
#define     V_030908_DI_PT_PATCH                                    0x09
#define     V_030908_DI_PT_LINELIST_ADJ                             0x0A
#define     V_030908_DI_PT_LINESTRIP_ADJ                            0x0B
#define     V_030908_DI_PT_TRILIST_ADJ                              0x0C
#define     V_030908_DI_PT_TRISTRIP_ADJ                             0x0D
#define     V_030908_DI_PT_TRI_WITH_WFLAGS                          0x10
#define     V_030908_DI_PT_RECTLIST                                 0x11
#define     V_030908_DI_PT_LINELOOP                                 0x12
#define     V_030908_DI_PT_QUADLIST                                 0x13
#define     V_030908_DI_PT_QUADSTRIP                                0x14
#define     V_030908_DI_PT_POLYGON                                  0x15
#define     V_030908_DI_PT_2D_COPY_RECT_LIST_V0                     0x16
#define     V_030908_DI_PT_2D_COPY_RECT_LIST_V1                     0x17
#define     V_030908_DI_PT_2D_COPY_RECT_LIST_V2                     0x18
#define     V_030908_DI_PT_2D_COPY_RECT_LIST_V3                     0x19
#define     V_030908_DI_PT_2D_FILL_RECT_LIST                        0x1A
#define     V_030908_DI_PT_2D_LINE_STRIP                            0x1B
#define     V_030908_DI_PT_2D_TRI_STRIP                             0x1C
#define R_03090C_VGT_INDEX_TYPE                                         0x03090C
#define   S_03090C_INDEX_TYPE(x)                                      (((unsigned)(x) & 0x03) << 0)
#define   G_03090C_INDEX_TYPE(x)                                      (((x) >> 0) & 0x03)
#define   C_03090C_INDEX_TYPE                                         0xFFFFFFFC
#define     V_03090C_DI_INDEX_SIZE_16_BIT                           0x00
#define     V_03090C_DI_INDEX_SIZE_32_BIT                           0x01
#define R_030910_VGT_STRMOUT_BUFFER_FILLED_SIZE_0                       0x030910
#define R_030914_VGT_STRMOUT_BUFFER_FILLED_SIZE_1                       0x030914
#define R_030918_VGT_STRMOUT_BUFFER_FILLED_SIZE_2                       0x030918
#define R_03091C_VGT_STRMOUT_BUFFER_FILLED_SIZE_3                       0x03091C
#define R_030930_VGT_NUM_INDICES                                        0x030930
#define R_030934_VGT_NUM_INSTANCES                                      0x030934
#define R_030938_VGT_TF_RING_SIZE                                       0x030938
#define   S_030938_SIZE(x)                                            (((unsigned)(x) & 0xFFFF) << 0)
#define   G_030938_SIZE(x)                                            (((x) >> 0) & 0xFFFF)
#define   C_030938_SIZE                                               0xFFFF0000
#define R_03093C_VGT_HS_OFFCHIP_PARAM                                   0x03093C
#define   S_03093C_OFFCHIP_BUFFERING(x)                               (((unsigned)(x) & 0x1FF) << 0)
#define   G_03093C_OFFCHIP_BUFFERING(x)                               (((x) >> 0) & 0x1FF)
#define   C_03093C_OFFCHIP_BUFFERING                                  0xFFFFFE00
#define   S_03093C_OFFCHIP_GRANULARITY(x)                             (((unsigned)(x) & 0x03) << 9)
#define   G_03093C_OFFCHIP_GRANULARITY(x)                             (((x) >> 9) & 0x03)
#define   C_03093C_OFFCHIP_GRANULARITY                                0xFFFFF9FF
#define     V_03093C_X_8K_DWORDS                                    0x00
#define     V_03093C_X_4K_DWORDS                                    0x01
#define     V_03093C_X_2K_DWORDS                                    0x02
#define     V_03093C_X_1K_DWORDS                                    0x03
#define R_030940_VGT_TF_MEMORY_BASE                                     0x030940
#define R_030A00_PA_SU_LINE_STIPPLE_VALUE                               0x030A00
#define   S_030A00_LINE_STIPPLE_VALUE(x)                              (((unsigned)(x) & 0xFFFFFF) << 0)
#define   G_030A00_LINE_STIPPLE_VALUE(x)                              (((x) >> 0) & 0xFFFFFF)
#define   C_030A00_LINE_STIPPLE_VALUE                                 0xFF000000
#define R_030A04_PA_SC_LINE_STIPPLE_STATE                               0x030A04
#define   S_030A04_CURRENT_PTR(x)                                     (((unsigned)(x) & 0x0F) << 0)
#define   G_030A04_CURRENT_PTR(x)                                     (((x) >> 0) & 0x0F)
#define   C_030A04_CURRENT_PTR                                        0xFFFFFFF0
#define   S_030A04_CURRENT_COUNT(x)                                   (((unsigned)(x) & 0xFF) << 8)
#define   G_030A04_CURRENT_COUNT(x)                                   (((x) >> 8) & 0xFF)
#define   C_030A04_CURRENT_COUNT                                      0xFFFF00FF
#define R_030A10_PA_SC_SCREEN_EXTENT_MIN_0                              0x030A10
#define   S_030A10_X(x)                                               (((unsigned)(x) & 0xFFFF) << 0)
#define   G_030A10_X(x)                                               (((x) >> 0) & 0xFFFF)
#define   C_030A10_X                                                  0xFFFF0000
#define   S_030A10_Y(x)                                               (((unsigned)(x) & 0xFFFF) << 16)
#define   G_030A10_Y(x)                                               (((x) >> 16) & 0xFFFF)
#define   C_030A10_Y                                                  0x0000FFFF
#define R_030A14_PA_SC_SCREEN_EXTENT_MAX_0                              0x030A14
#define   S_030A14_X(x)                                               (((unsigned)(x) & 0xFFFF) << 0)
#define   G_030A14_X(x)                                               (((x) >> 0) & 0xFFFF)
#define   C_030A14_X                                                  0xFFFF0000
#define   S_030A14_Y(x)                                               (((unsigned)(x) & 0xFFFF) << 16)
#define   G_030A14_Y(x)                                               (((x) >> 16) & 0xFFFF)
#define   C_030A14_Y                                                  0x0000FFFF
#define R_030A18_PA_SC_SCREEN_EXTENT_MIN_1                              0x030A18
#define   S_030A18_X(x)                                               (((unsigned)(x) & 0xFFFF) << 0)
#define   G_030A18_X(x)                                               (((x) >> 0) & 0xFFFF)
#define   C_030A18_X                                                  0xFFFF0000
#define   S_030A18_Y(x)                                               (((unsigned)(x) & 0xFFFF) << 16)
#define   G_030A18_Y(x)                                               (((x) >> 16) & 0xFFFF)
#define   C_030A18_Y                                                  0x0000FFFF
#define R_030A2C_PA_SC_SCREEN_EXTENT_MAX_1                              0x030A2C
#define   S_030A2C_X(x)                                               (((unsigned)(x) & 0xFFFF) << 0)
#define   G_030A2C_X(x)                                               (((x) >> 0) & 0xFFFF)
#define   C_030A2C_X                                                  0xFFFF0000
#define   S_030A2C_Y(x)                                               (((unsigned)(x) & 0xFFFF) << 16)
#define   G_030A2C_Y(x)                                               (((x) >> 16) & 0xFFFF)
#define   C_030A2C_Y                                                  0x0000FFFF
/*     */
#define R_008BF0_PA_SC_ENHANCE                                          0x008BF0
#define   S_008BF0_ENABLE_PA_SC_OUT_OF_ORDER(x)                       (((unsigned)(x) & 0x1) << 0)
#define   G_008BF0_ENABLE_PA_SC_OUT_OF_ORDER(x)                       (((x) >> 0) & 0x1)
#define   C_008BF0_ENABLE_PA_SC_OUT_OF_ORDER                          0xFFFFFFFE
#define   S_008BF0_DISABLE_SC_DB_TILE_FIX(x)                          (((unsigned)(x) & 0x1) << 1)
#define   G_008BF0_DISABLE_SC_DB_TILE_FIX(x)                          (((x) >> 1) & 0x1)
#define   C_008BF0_DISABLE_SC_DB_TILE_FIX                             0xFFFFFFFD
#define   S_008BF0_DISABLE_AA_MASK_FULL_FIX(x)                        (((unsigned)(x) & 0x1) << 2)
#define   G_008BF0_DISABLE_AA_MASK_FULL_FIX(x)                        (((x) >> 2) & 0x1)
#define   C_008BF0_DISABLE_AA_MASK_FULL_FIX                           0xFFFFFFFB
#define   S_008BF0_ENABLE_1XMSAA_SAMPLE_LOCATIONS(x)                  (((unsigned)(x) & 0x1) << 3)
#define   G_008BF0_ENABLE_1XMSAA_SAMPLE_LOCATIONS(x)                  (((x) >> 3) & 0x1)
#define   C_008BF0_ENABLE_1XMSAA_SAMPLE_LOCATIONS                     0xFFFFFFF7
#define   S_008BF0_ENABLE_1XMSAA_SAMPLE_LOC_CENTROID(x)               (((unsigned)(x) & 0x1) << 4)
#define   G_008BF0_ENABLE_1XMSAA_SAMPLE_LOC_CENTROID(x)               (((x) >> 4) & 0x1)
#define   C_008BF0_ENABLE_1XMSAA_SAMPLE_LOC_CENTROID                  0xFFFFFFEF
#define   S_008BF0_DISABLE_SCISSOR_FIX(x)                             (((unsigned)(x) & 0x1) << 5)
#define   G_008BF0_DISABLE_SCISSOR_FIX(x)                             (((x) >> 5) & 0x1)
#define   C_008BF0_DISABLE_SCISSOR_FIX                                0xFFFFFFDF
#define   S_008BF0_DISABLE_PW_BUBBLE_COLLAPSE(x)                      (((unsigned)(x) & 0x03) << 6)
#define   G_008BF0_DISABLE_PW_BUBBLE_COLLAPSE(x)                      (((x) >> 6) & 0x03)
#define   C_008BF0_DISABLE_PW_BUBBLE_COLLAPSE                         0xFFFFFF3F
#define   S_008BF0_SEND_UNLIT_STILES_TO_PACKER(x)                     (((unsigned)(x) & 0x1) << 8)
#define   G_008BF0_SEND_UNLIT_STILES_TO_PACKER(x)                     (((x) >> 8) & 0x1)
#define   C_008BF0_SEND_UNLIT_STILES_TO_PACKER                        0xFFFFFEFF
#define   S_008BF0_DISABLE_DUALGRAD_PERF_OPTIMIZATION(x)              (((unsigned)(x) & 0x1) << 9)
#define   G_008BF0_DISABLE_DUALGRAD_PERF_OPTIMIZATION(x)              (((x) >> 9) & 0x1)
#define   C_008BF0_DISABLE_DUALGRAD_PERF_OPTIMIZATION                 0xFFFFFDFF
#define R_008C08_SQC_CACHES                                             0x008C08
#define   S_008C08_INST_INVALIDATE(x)                                 (((unsigned)(x) & 0x1) << 0)
#define   G_008C08_INST_INVALIDATE(x)                                 (((x) >> 0) & 0x1)
#define   C_008C08_INST_INVALIDATE                                    0xFFFFFFFE
#define   S_008C08_DATA_INVALIDATE(x)                                 (((unsigned)(x) & 0x1) << 1)
#define   G_008C08_DATA_INVALIDATE(x)                                 (((x) >> 1) & 0x1)
#define   C_008C08_DATA_INVALIDATE                                    0xFFFFFFFD
/* CIK */
#define R_030D20_SQC_CACHES                                             0x030D20
#define   S_030D20_INST_INVALIDATE(x)                                 (((unsigned)(x) & 0x1) << 0)
#define   G_030D20_INST_INVALIDATE(x)                                 (((x) >> 0) & 0x1)
#define   C_030D20_INST_INVALIDATE                                    0xFFFFFFFE
#define   S_030D20_DATA_INVALIDATE(x)                                 (((unsigned)(x) & 0x1) << 1)
#define   G_030D20_DATA_INVALIDATE(x)                                 (((x) >> 1) & 0x1)
#define   C_030D20_DATA_INVALIDATE                                    0xFFFFFFFD
#define   S_030D20_INVALIDATE_VOLATILE(x)                             (((unsigned)(x) & 0x1) << 2)
#define   G_030D20_INVALIDATE_VOLATILE(x)                             (((x) >> 2) & 0x1)
#define   C_030D20_INVALIDATE_VOLATILE                                0xFFFFFFFB
/*     */
#define R_008C0C_SQ_RANDOM_WAVE_PRI                                     0x008C0C
#define   S_008C0C_RET(x)                                             (((unsigned)(x) & 0x7F) << 0)
#define   G_008C0C_RET(x)                                             (((x) >> 0) & 0x7F)
#define   C_008C0C_RET                                                0xFFFFFF80
#define   S_008C0C_RUI(x)                                             (((unsigned)(x) & 0x07) << 7)
#define   G_008C0C_RUI(x)                                             (((x) >> 7) & 0x07)
#define   C_008C0C_RUI                                                0xFFFFFC7F
#define   S_008C0C_RNG(x)                                             (((unsigned)(x) & 0x7FF) << 10)
#define   G_008C0C_RNG(x)                                             (((x) >> 10) & 0x7FF)
#define   C_008C0C_RNG                                                0xFFE003FF
#define R_008DFC_SQ_EXP_0                                               0x008DFC
#define   S_008DFC_EN(x)                                              (((unsigned)(x) & 0x0F) << 0)
#define   G_008DFC_EN(x)                                              (((x) >> 0) & 0x0F)
#define   C_008DFC_EN                                                 0xFFFFFFF0
#define   S_008DFC_TGT(x)                                             (((unsigned)(x) & 0x3F) << 4)
#define   G_008DFC_TGT(x)                                             (((x) >> 4) & 0x3F)
#define   C_008DFC_TGT                                                0xFFFFFC0F
#define     V_008DFC_SQ_EXP_MRT                                     0x00
#define     V_008DFC_SQ_EXP_MRTZ                                    0x08
#define     V_008DFC_SQ_EXP_NULL                                    0x09
#define     V_008DFC_SQ_EXP_POS                                     0x0C
#define     V_008DFC_SQ_EXP_PARAM                                   0x20
#define   S_008DFC_COMPR(x)                                           (((unsigned)(x) & 0x1) << 10)
#define   G_008DFC_COMPR(x)                                           (((x) >> 10) & 0x1)
#define   C_008DFC_COMPR                                              0xFFFFFBFF
#define   S_008DFC_DONE(x)                                            (((unsigned)(x) & 0x1) << 11)
#define   G_008DFC_DONE(x)                                            (((x) >> 11) & 0x1)
#define   C_008DFC_DONE                                               0xFFFFF7FF
#define   S_008DFC_VM(x)                                              (((unsigned)(x) & 0x1) << 12)
#define   G_008DFC_VM(x)                                              (((x) >> 12) & 0x1)
#define   C_008DFC_VM                                                 0xFFFFEFFF
#define   S_008DFC_ENCODING(x)                                        (((unsigned)(x) & 0x3F) << 26)
#define   G_008DFC_ENCODING(x)                                        (((x) >> 26) & 0x3F)
#define   C_008DFC_ENCODING                                           0x03FFFFFF
#define     V_008DFC_SQ_ENC_EXP_FIELD                               0x3E
#define R_030E00_TA_CS_BC_BASE_ADDR                                     0x030E00
#define R_030E04_TA_CS_BC_BASE_ADDR_HI                                  0x030E04
#define   S_030E04_ADDRESS(x)                                         (((unsigned)(x) & 0xFF) << 0)
#define   G_030E04_ADDRESS(x)                                         (((x) >> 0) & 0xFF)
#define   C_030E04_ADDRESS                                            0xFFFFFF00
#define R_030F00_DB_OCCLUSION_COUNT0_LOW                                0x030F00
#define R_008F00_SQ_BUF_RSRC_WORD0                                      0x008F00
#define R_030F04_DB_OCCLUSION_COUNT0_HI                                 0x030F04
#define   S_030F04_COUNT_HI(x)                                        (((unsigned)(x) & 0x7FFFFFFF) << 0)
#define   G_030F04_COUNT_HI(x)                                        (((x) >> 0) & 0x7FFFFFFF)
#define   C_030F04_COUNT_HI                                           0x80000000
#define R_008F04_SQ_BUF_RSRC_WORD1                                      0x008F04
#define   S_008F04_BASE_ADDRESS_HI(x)                                 (((unsigned)(x) & 0xFFFF) << 0)
#define   G_008F04_BASE_ADDRESS_HI(x)                                 (((x) >> 0) & 0xFFFF)
#define   C_008F04_BASE_ADDRESS_HI                                    0xFFFF0000
#define   S_008F04_STRIDE(x)                                          (((unsigned)(x) & 0x3FFF) << 16)
#define   G_008F04_STRIDE(x)                                          (((x) >> 16) & 0x3FFF)
#define   C_008F04_STRIDE                                             0xC000FFFF
#define   S_008F04_CACHE_SWIZZLE(x)                                   (((unsigned)(x) & 0x1) << 30)
#define   G_008F04_CACHE_SWIZZLE(x)                                   (((x) >> 30) & 0x1)
#define   C_008F04_CACHE_SWIZZLE                                      0xBFFFFFFF
#define   S_008F04_SWIZZLE_ENABLE(x)                                  (((unsigned)(x) & 0x1) << 31)
#define   G_008F04_SWIZZLE_ENABLE(x)                                  (((x) >> 31) & 0x1)
#define   C_008F04_SWIZZLE_ENABLE                                     0x7FFFFFFF
#define R_030F08_DB_OCCLUSION_COUNT1_LOW                                0x030F08
#define R_008F08_SQ_BUF_RSRC_WORD2                                      0x008F08
#define R_030F0C_DB_OCCLUSION_COUNT1_HI                                 0x030F0C
#define   S_030F0C_COUNT_HI(x)                                        (((unsigned)(x) & 0x7FFFFFFF) << 0)
#define   G_030F0C_COUNT_HI(x)                                        (((x) >> 0) & 0x7FFFFFFF)
#define   C_030F0C_COUNT_HI                                           0x80000000
#define R_008F0C_SQ_BUF_RSRC_WORD3                                      0x008F0C
#define   S_008F0C_DST_SEL_X(x)                                       (((unsigned)(x) & 0x07) << 0)
#define   G_008F0C_DST_SEL_X(x)                                       (((x) >> 0) & 0x07)
#define   C_008F0C_DST_SEL_X                                          0xFFFFFFF8
#define     V_008F0C_SQ_SEL_0                                       0x00
#define     V_008F0C_SQ_SEL_1                                       0x01
#define     V_008F0C_SQ_SEL_RESERVED_0                              0x02
#define     V_008F0C_SQ_SEL_RESERVED_1                              0x03
#define     V_008F0C_SQ_SEL_X                                       0x04
#define     V_008F0C_SQ_SEL_Y                                       0x05
#define     V_008F0C_SQ_SEL_Z                                       0x06
#define     V_008F0C_SQ_SEL_W                                       0x07
#define   S_008F0C_DST_SEL_Y(x)                                       (((unsigned)(x) & 0x07) << 3)
#define   G_008F0C_DST_SEL_Y(x)                                       (((x) >> 3) & 0x07)
#define   C_008F0C_DST_SEL_Y                                          0xFFFFFFC7
#define     V_008F0C_SQ_SEL_0                                       0x00
#define     V_008F0C_SQ_SEL_1                                       0x01
#define     V_008F0C_SQ_SEL_RESERVED_0                              0x02
#define     V_008F0C_SQ_SEL_RESERVED_1                              0x03
#define     V_008F0C_SQ_SEL_X                                       0x04
#define     V_008F0C_SQ_SEL_Y                                       0x05
#define     V_008F0C_SQ_SEL_Z                                       0x06
#define     V_008F0C_SQ_SEL_W                                       0x07
#define   S_008F0C_DST_SEL_Z(x)                                       (((unsigned)(x) & 0x07) << 6)
#define   G_008F0C_DST_SEL_Z(x)                                       (((x) >> 6) & 0x07)
#define   C_008F0C_DST_SEL_Z                                          0xFFFFFE3F
#define     V_008F0C_SQ_SEL_0                                       0x00
#define     V_008F0C_SQ_SEL_1                                       0x01
#define     V_008F0C_SQ_SEL_RESERVED_0                              0x02
#define     V_008F0C_SQ_SEL_RESERVED_1                              0x03
#define     V_008F0C_SQ_SEL_X                                       0x04
#define     V_008F0C_SQ_SEL_Y                                       0x05
#define     V_008F0C_SQ_SEL_Z                                       0x06
#define     V_008F0C_SQ_SEL_W                                       0x07
#define   S_008F0C_DST_SEL_W(x)                                       (((unsigned)(x) & 0x07) << 9)
#define   G_008F0C_DST_SEL_W(x)                                       (((x) >> 9) & 0x07)
#define   C_008F0C_DST_SEL_W                                          0xFFFFF1FF
#define     V_008F0C_SQ_SEL_0                                       0x00
#define     V_008F0C_SQ_SEL_1                                       0x01
#define     V_008F0C_SQ_SEL_RESERVED_0                              0x02
#define     V_008F0C_SQ_SEL_RESERVED_1                              0x03
#define     V_008F0C_SQ_SEL_X                                       0x04
#define     V_008F0C_SQ_SEL_Y                                       0x05
#define     V_008F0C_SQ_SEL_Z                                       0x06
#define     V_008F0C_SQ_SEL_W                                       0x07
#define   S_008F0C_NUM_FORMAT(x)                                      (((unsigned)(x) & 0x07) << 12)
#define   G_008F0C_NUM_FORMAT(x)                                      (((x) >> 12) & 0x07)
#define   C_008F0C_NUM_FORMAT                                         0xFFFF8FFF
#define     V_008F0C_BUF_NUM_FORMAT_UNORM                           0x00
#define     V_008F0C_BUF_NUM_FORMAT_SNORM                           0x01
#define     V_008F0C_BUF_NUM_FORMAT_USCALED                         0x02
#define     V_008F0C_BUF_NUM_FORMAT_SSCALED                         0x03
#define     V_008F0C_BUF_NUM_FORMAT_UINT                            0x04
#define     V_008F0C_BUF_NUM_FORMAT_SINT                            0x05
#define     V_008F0C_BUF_NUM_FORMAT_SNORM_OGL                       0x06
#define     V_008F0C_BUF_NUM_FORMAT_FLOAT                           0x07
#define   S_008F0C_DATA_FORMAT(x)                                     (((unsigned)(x) & 0x0F) << 15)
#define   G_008F0C_DATA_FORMAT(x)                                     (((x) >> 15) & 0x0F)
#define   C_008F0C_DATA_FORMAT                                        0xFFF87FFF
#define     V_008F0C_BUF_DATA_FORMAT_INVALID                        0x00
#define     V_008F0C_BUF_DATA_FORMAT_8                              0x01
#define     V_008F0C_BUF_DATA_FORMAT_16                             0x02
#define     V_008F0C_BUF_DATA_FORMAT_8_8                            0x03
#define     V_008F0C_BUF_DATA_FORMAT_32                             0x04
#define     V_008F0C_BUF_DATA_FORMAT_16_16                          0x05
#define     V_008F0C_BUF_DATA_FORMAT_10_11_11                       0x06
#define     V_008F0C_BUF_DATA_FORMAT_11_11_10                       0x07
#define     V_008F0C_BUF_DATA_FORMAT_10_10_10_2                     0x08
#define     V_008F0C_BUF_DATA_FORMAT_2_10_10_10                     0x09
#define     V_008F0C_BUF_DATA_FORMAT_8_8_8_8                        0x0A
#define     V_008F0C_BUF_DATA_FORMAT_32_32                          0x0B
#define     V_008F0C_BUF_DATA_FORMAT_16_16_16_16                    0x0C
#define     V_008F0C_BUF_DATA_FORMAT_32_32_32                       0x0D
#define     V_008F0C_BUF_DATA_FORMAT_32_32_32_32                    0x0E
#define     V_008F0C_BUF_DATA_FORMAT_RESERVED_15                    0x0F
#define   S_008F0C_ELEMENT_SIZE(x)                                    (((unsigned)(x) & 0x03) << 19)
#define   G_008F0C_ELEMENT_SIZE(x)                                    (((x) >> 19) & 0x03)
#define   C_008F0C_ELEMENT_SIZE                                       0xFFE7FFFF
#define   S_008F0C_INDEX_STRIDE(x)                                    (((unsigned)(x) & 0x03) << 21)
#define   G_008F0C_INDEX_STRIDE(x)                                    (((x) >> 21) & 0x03)
#define   C_008F0C_INDEX_STRIDE                                       0xFF9FFFFF
#define   S_008F0C_ADD_TID_ENABLE(x)                                  (((unsigned)(x) & 0x1) << 23)
#define   G_008F0C_ADD_TID_ENABLE(x)                                  (((x) >> 23) & 0x1)
#define   C_008F0C_ADD_TID_ENABLE                                     0xFF7FFFFF
/* CIK */
#define   S_008F0C_ATC(x)                                             (((unsigned)(x) & 0x1) << 24)
#define   G_008F0C_ATC(x)                                             (((x) >> 24) & 0x1)
#define   C_008F0C_ATC                                                0xFEFFFFFF
/*     */
#define   S_008F0C_HASH_ENABLE(x)                                     (((unsigned)(x) & 0x1) << 25)
#define   G_008F0C_HASH_ENABLE(x)                                     (((x) >> 25) & 0x1)
#define   C_008F0C_HASH_ENABLE                                        0xFDFFFFFF
#define   S_008F0C_HEAP(x)                                            (((unsigned)(x) & 0x1) << 26)
#define   G_008F0C_HEAP(x)                                            (((x) >> 26) & 0x1)
#define   C_008F0C_HEAP                                               0xFBFFFFFF
/* CIK */
#define   S_008F0C_MTYPE(x)                                           (((unsigned)(x) & 0x07) << 27)
#define   G_008F0C_MTYPE(x)                                           (((x) >> 27) & 0x07)
#define   C_008F0C_MTYPE                                              0xC7FFFFFF
/*     */
#define   S_008F0C_TYPE(x)                                            (((unsigned)(x) & 0x03) << 30)
#define   G_008F0C_TYPE(x)                                            (((x) >> 30) & 0x03)
#define   C_008F0C_TYPE                                               0x3FFFFFFF
#define     V_008F0C_SQ_RSRC_BUF                                    0x00
#define     V_008F0C_SQ_RSRC_BUF_RSVD_1                             0x01
#define     V_008F0C_SQ_RSRC_BUF_RSVD_2                             0x02
#define     V_008F0C_SQ_RSRC_BUF_RSVD_3                             0x03
#define R_030F10_DB_OCCLUSION_COUNT2_LOW                                0x030F10
#define R_008F10_SQ_IMG_RSRC_WORD0                                      0x008F10
#define R_030F14_DB_OCCLUSION_COUNT2_HI                                 0x030F14
#define   S_030F14_COUNT_HI(x)                                        (((unsigned)(x) & 0x7FFFFFFF) << 0)
#define   G_030F14_COUNT_HI(x)                                        (((x) >> 0) & 0x7FFFFFFF)
#define   C_030F14_COUNT_HI                                           0x80000000
#define R_008F14_SQ_IMG_RSRC_WORD1                                      0x008F14
#define   S_008F14_BASE_ADDRESS_HI(x)                                 (((unsigned)(x) & 0xFF) << 0)
#define   G_008F14_BASE_ADDRESS_HI(x)                                 (((x) >> 0) & 0xFF)
#define   C_008F14_BASE_ADDRESS_HI                                    0xFFFFFF00
#define   S_008F14_MIN_LOD(x)                                         (((unsigned)(x) & 0xFFF) << 8)
#define   G_008F14_MIN_LOD(x)                                         (((x) >> 8) & 0xFFF)
#define   C_008F14_MIN_LOD                                            0xFFF000FF
#define   S_008F14_DATA_FORMAT(x)                                     (((unsigned)(x) & 0x3F) << 20)
#define   G_008F14_DATA_FORMAT(x)                                     (((x) >> 20) & 0x3F)
#define   C_008F14_DATA_FORMAT                                        0xFC0FFFFF
#define     V_008F14_IMG_DATA_FORMAT_INVALID                        0x00
#define     V_008F14_IMG_DATA_FORMAT_8                              0x01
#define     V_008F14_IMG_DATA_FORMAT_16                             0x02
#define     V_008F14_IMG_DATA_FORMAT_8_8                            0x03
#define     V_008F14_IMG_DATA_FORMAT_32                             0x04
#define     V_008F14_IMG_DATA_FORMAT_16_16                          0x05
#define     V_008F14_IMG_DATA_FORMAT_10_11_11                       0x06
#define     V_008F14_IMG_DATA_FORMAT_11_11_10                       0x07
#define     V_008F14_IMG_DATA_FORMAT_10_10_10_2                     0x08
#define     V_008F14_IMG_DATA_FORMAT_2_10_10_10                     0x09
#define     V_008F14_IMG_DATA_FORMAT_8_8_8_8                        0x0A
#define     V_008F14_IMG_DATA_FORMAT_32_32                          0x0B
#define     V_008F14_IMG_DATA_FORMAT_16_16_16_16                    0x0C
#define     V_008F14_IMG_DATA_FORMAT_32_32_32                       0x0D
#define     V_008F14_IMG_DATA_FORMAT_32_32_32_32                    0x0E
#define     V_008F14_IMG_DATA_FORMAT_RESERVED_15                    0x0F
#define     V_008F14_IMG_DATA_FORMAT_5_6_5                          0x10
#define     V_008F14_IMG_DATA_FORMAT_1_5_5_5                        0x11
#define     V_008F14_IMG_DATA_FORMAT_5_5_5_1                        0x12
#define     V_008F14_IMG_DATA_FORMAT_4_4_4_4                        0x13
#define     V_008F14_IMG_DATA_FORMAT_8_24                           0x14
#define     V_008F14_IMG_DATA_FORMAT_24_8                           0x15
#define     V_008F14_IMG_DATA_FORMAT_X24_8_32                       0x16
#define     V_008F14_IMG_DATA_FORMAT_8_AS_8_8_8_8                   0x17 /* stoney+ */
#define     V_008F14_IMG_DATA_FORMAT_ETC2_RGB                       0x18 /* stoney+ */
#define     V_008F14_IMG_DATA_FORMAT_ETC2_RGBA                      0x19 /* stoney+ */
#define     V_008F14_IMG_DATA_FORMAT_ETC2_R                         0x1A /* stoney+ */
#define     V_008F14_IMG_DATA_FORMAT_ETC2_RG                        0x1B /* stoney+ */
#define     V_008F14_IMG_DATA_FORMAT_ETC2_RGBA1                     0x1C /* stoney+ */
#define     V_008F14_IMG_DATA_FORMAT_RESERVED_29                    0x1D
#define     V_008F14_IMG_DATA_FORMAT_RESERVED_30                    0x1E
#define     V_008F14_IMG_DATA_FORMAT_RESERVED_31                    0x1F
#define     V_008F14_IMG_DATA_FORMAT_GB_GR                          0x20
#define     V_008F14_IMG_DATA_FORMAT_BG_RG                          0x21
#define     V_008F14_IMG_DATA_FORMAT_5_9_9_9                        0x22
#define     V_008F14_IMG_DATA_FORMAT_BC1                            0x23
#define     V_008F14_IMG_DATA_FORMAT_BC2                            0x24
#define     V_008F14_IMG_DATA_FORMAT_BC3                            0x25
#define     V_008F14_IMG_DATA_FORMAT_BC4                            0x26
#define     V_008F14_IMG_DATA_FORMAT_BC5                            0x27
#define     V_008F14_IMG_DATA_FORMAT_BC6                            0x28
#define     V_008F14_IMG_DATA_FORMAT_BC7                            0x29
#define     V_008F14_IMG_DATA_FORMAT_16_AS_16_16_16_16              0x2A /* stoney+ */
#define     V_008F14_IMG_DATA_FORMAT_16_AS_32_32_32_32              0x2B /* stoney+ */
#define     V_008F14_IMG_DATA_FORMAT_FMASK8_S2_F1                   0x2C
#define     V_008F14_IMG_DATA_FORMAT_FMASK8_S4_F1                   0x2D
#define     V_008F14_IMG_DATA_FORMAT_FMASK8_S8_F1                   0x2E
#define     V_008F14_IMG_DATA_FORMAT_FMASK8_S2_F2                   0x2F
#define     V_008F14_IMG_DATA_FORMAT_FMASK8_S4_F2                   0x30
#define     V_008F14_IMG_DATA_FORMAT_FMASK8_S4_F4                   0x31
#define     V_008F14_IMG_DATA_FORMAT_FMASK16_S16_F1                 0x32
#define     V_008F14_IMG_DATA_FORMAT_FMASK16_S8_F2                  0x33
#define     V_008F14_IMG_DATA_FORMAT_FMASK32_S16_F2                 0x34
#define     V_008F14_IMG_DATA_FORMAT_FMASK32_S8_F4                  0x35
#define     V_008F14_IMG_DATA_FORMAT_FMASK32_S8_F8                  0x36
#define     V_008F14_IMG_DATA_FORMAT_FMASK64_S16_F4                 0x37
#define     V_008F14_IMG_DATA_FORMAT_FMASK64_S16_F8                 0x38
#define     V_008F14_IMG_DATA_FORMAT_4_4                            0x39
#define     V_008F14_IMG_DATA_FORMAT_6_5_5                          0x3A
#define     V_008F14_IMG_DATA_FORMAT_1                              0x3B
#define     V_008F14_IMG_DATA_FORMAT_1_REVERSED                     0x3C
#define     V_008F14_IMG_DATA_FORMAT_32_AS_8                        0x3D /* not on stoney */
#define     V_008F14_IMG_DATA_FORMAT_32_AS_8_8                      0x3E /* not on stoney */
#define     V_008F14_IMG_DATA_FORMAT_32_AS_32_32_32_32              0x3F
#define   S_008F14_NUM_FORMAT(x)                                      (((unsigned)(x) & 0x0F) << 26)
#define   G_008F14_NUM_FORMAT(x)                                      (((x) >> 26) & 0x0F)
#define   C_008F14_NUM_FORMAT                                         0xC3FFFFFF
#define     V_008F14_IMG_NUM_FORMAT_UNORM                           0x00
#define     V_008F14_IMG_NUM_FORMAT_SNORM                           0x01
#define     V_008F14_IMG_NUM_FORMAT_USCALED                         0x02
#define     V_008F14_IMG_NUM_FORMAT_SSCALED                         0x03
#define     V_008F14_IMG_NUM_FORMAT_UINT                            0x04
#define     V_008F14_IMG_NUM_FORMAT_SINT                            0x05
#define     V_008F14_IMG_NUM_FORMAT_SNORM_OGL                       0x06
#define     V_008F14_IMG_NUM_FORMAT_FLOAT                           0x07
#define     V_008F14_IMG_NUM_FORMAT_RESERVED_8                      0x08
#define     V_008F14_IMG_NUM_FORMAT_SRGB                            0x09
#define     V_008F14_IMG_NUM_FORMAT_UBNORM                          0x0A
#define     V_008F14_IMG_NUM_FORMAT_UBNORM_OGL                      0x0B
#define     V_008F14_IMG_NUM_FORMAT_UBINT                           0x0C
#define     V_008F14_IMG_NUM_FORMAT_UBSCALED                        0x0D
#define     V_008F14_IMG_NUM_FORMAT_RESERVED_14                     0x0E
#define     V_008F14_IMG_NUM_FORMAT_RESERVED_15                     0x0F
/* CIK */
#define   S_008F14_MTYPE(x)                                           (((unsigned)(x) & 0x03) << 30)
#define   G_008F14_MTYPE(x)                                           (((x) >> 30) & 0x03)
#define   C_008F14_MTYPE                                              0x3FFFFFFF
/*     */
#define R_030F18_DB_OCCLUSION_COUNT3_LOW                                0x030F18
#define R_008F18_SQ_IMG_RSRC_WORD2                                      0x008F18
#define   S_008F18_WIDTH(x)                                           (((unsigned)(x) & 0x3FFF) << 0)
#define   G_008F18_WIDTH(x)                                           (((x) >> 0) & 0x3FFF)
#define   C_008F18_WIDTH                                              0xFFFFC000
#define   S_008F18_HEIGHT(x)                                          (((unsigned)(x) & 0x3FFF) << 14)
#define   G_008F18_HEIGHT(x)                                          (((x) >> 14) & 0x3FFF)
#define   C_008F18_HEIGHT                                             0xF0003FFF
#define   S_008F18_PERF_MOD(x)                                        (((unsigned)(x) & 0x07) << 28)
#define   G_008F18_PERF_MOD(x)                                        (((x) >> 28) & 0x07)
#define   C_008F18_PERF_MOD                                           0x8FFFFFFF
#define   S_008F18_INTERLACED(x)                                      (((unsigned)(x) & 0x1) << 31)
#define   G_008F18_INTERLACED(x)                                      (((x) >> 31) & 0x1)
#define   C_008F18_INTERLACED                                         0x7FFFFFFF
#define R_030F1C_DB_OCCLUSION_COUNT3_HI                                 0x030F1C
#define   S_030F1C_COUNT_HI(x)                                        (((unsigned)(x) & 0x7FFFFFFF) << 0)
#define   G_030F1C_COUNT_HI(x)                                        (((x) >> 0) & 0x7FFFFFFF)
#define   C_030F1C_COUNT_HI                                           0x80000000
#define R_008F1C_SQ_IMG_RSRC_WORD3                                      0x008F1C
#define   S_008F1C_DST_SEL_X(x)                                       (((unsigned)(x) & 0x07) << 0)
#define   G_008F1C_DST_SEL_X(x)                                       (((x) >> 0) & 0x07)
#define   C_008F1C_DST_SEL_X                                          0xFFFFFFF8
#define     V_008F1C_SQ_SEL_0                                       0x00
#define     V_008F1C_SQ_SEL_1                                       0x01
#define     V_008F1C_SQ_SEL_RESERVED_0                              0x02
#define     V_008F1C_SQ_SEL_RESERVED_1                              0x03
#define     V_008F1C_SQ_SEL_X                                       0x04
#define     V_008F1C_SQ_SEL_Y                                       0x05
#define     V_008F1C_SQ_SEL_Z                                       0x06
#define     V_008F1C_SQ_SEL_W                                       0x07
#define   S_008F1C_DST_SEL_Y(x)                                       (((unsigned)(x) & 0x07) << 3)
#define   G_008F1C_DST_SEL_Y(x)                                       (((x) >> 3) & 0x07)
#define   C_008F1C_DST_SEL_Y                                          0xFFFFFFC7
#define     V_008F1C_SQ_SEL_0                                       0x00
#define     V_008F1C_SQ_SEL_1                                       0x01
#define     V_008F1C_SQ_SEL_RESERVED_0                              0x02
#define     V_008F1C_SQ_SEL_RESERVED_1                              0x03
#define     V_008F1C_SQ_SEL_X                                       0x04
#define     V_008F1C_SQ_SEL_Y                                       0x05
#define     V_008F1C_SQ_SEL_Z                                       0x06
#define     V_008F1C_SQ_SEL_W                                       0x07
#define   S_008F1C_DST_SEL_Z(x)                                       (((unsigned)(x) & 0x07) << 6)
#define   G_008F1C_DST_SEL_Z(x)                                       (((x) >> 6) & 0x07)
#define   C_008F1C_DST_SEL_Z                                          0xFFFFFE3F
#define     V_008F1C_SQ_SEL_0                                       0x00
#define     V_008F1C_SQ_SEL_1                                       0x01
#define     V_008F1C_SQ_SEL_RESERVED_0                              0x02
#define     V_008F1C_SQ_SEL_RESERVED_1                              0x03
#define     V_008F1C_SQ_SEL_X                                       0x04
#define     V_008F1C_SQ_SEL_Y                                       0x05
#define     V_008F1C_SQ_SEL_Z                                       0x06
#define     V_008F1C_SQ_SEL_W                                       0x07
#define   S_008F1C_DST_SEL_W(x)                                       (((unsigned)(x) & 0x07) << 9)
#define   G_008F1C_DST_SEL_W(x)                                       (((x) >> 9) & 0x07)
#define   C_008F1C_DST_SEL_W                                          0xFFFFF1FF
#define     V_008F1C_SQ_SEL_0                                       0x00
#define     V_008F1C_SQ_SEL_1                                       0x01
#define     V_008F1C_SQ_SEL_RESERVED_0                              0x02
#define     V_008F1C_SQ_SEL_RESERVED_1                              0x03
#define     V_008F1C_SQ_SEL_X                                       0x04
#define     V_008F1C_SQ_SEL_Y                                       0x05
#define     V_008F1C_SQ_SEL_Z                                       0x06
#define     V_008F1C_SQ_SEL_W                                       0x07
#define   S_008F1C_BASE_LEVEL(x)                                      (((unsigned)(x) & 0x0F) << 12)
#define   G_008F1C_BASE_LEVEL(x)                                      (((x) >> 12) & 0x0F)
#define   C_008F1C_BASE_LEVEL                                         0xFFFF0FFF
#define   S_008F1C_LAST_LEVEL(x)                                      (((unsigned)(x) & 0x0F) << 16)
#define   G_008F1C_LAST_LEVEL(x)                                      (((x) >> 16) & 0x0F)
#define   C_008F1C_LAST_LEVEL                                         0xFFF0FFFF
#define   S_008F1C_TILING_INDEX(x)                                    (((unsigned)(x) & 0x1F) << 20)
#define   G_008F1C_TILING_INDEX(x)                                    (((x) >> 20) & 0x1F)
#define   C_008F1C_TILING_INDEX                                       0xFE0FFFFF
#define   S_008F1C_POW2_PAD(x)                                        (((unsigned)(x) & 0x1) << 25)
#define   G_008F1C_POW2_PAD(x)                                        (((x) >> 25) & 0x1)
#define   C_008F1C_POW2_PAD                                           0xFDFFFFFF
/* CIK */
#define   S_008F1C_MTYPE(x)                                           (((unsigned)(x) & 0x1) << 26)
#define   G_008F1C_MTYPE(x)                                           (((x) >> 26) & 0x1)
#define   C_008F1C_MTYPE                                              0xFBFFFFFF
#define   S_008F1C_ATC(x)                                             (((unsigned)(x) & 0x1) << 27)
#define   G_008F1C_ATC(x)                                             (((x) >> 27) & 0x1)
#define   C_008F1C_ATC                                                0xF7FFFFFF
/*     */
#define   S_008F1C_TYPE(x)                                            (((unsigned)(x) & 0x0F) << 28)
#define   G_008F1C_TYPE(x)                                            (((x) >> 28) & 0x0F)
#define   C_008F1C_TYPE                                               0x0FFFFFFF
#define     V_008F1C_SQ_RSRC_IMG_RSVD_0                             0x00
#define     V_008F1C_SQ_RSRC_IMG_RSVD_1                             0x01
#define     V_008F1C_SQ_RSRC_IMG_RSVD_2                             0x02
#define     V_008F1C_SQ_RSRC_IMG_RSVD_3                             0x03
#define     V_008F1C_SQ_RSRC_IMG_RSVD_4                             0x04
#define     V_008F1C_SQ_RSRC_IMG_RSVD_5                             0x05
#define     V_008F1C_SQ_RSRC_IMG_RSVD_6                             0x06
#define     V_008F1C_SQ_RSRC_IMG_RSVD_7                             0x07
#define     V_008F1C_SQ_RSRC_IMG_1D                                 0x08
#define     V_008F1C_SQ_RSRC_IMG_2D                                 0x09
#define     V_008F1C_SQ_RSRC_IMG_3D                                 0x0A
#define     V_008F1C_SQ_RSRC_IMG_CUBE                               0x0B
#define     V_008F1C_SQ_RSRC_IMG_1D_ARRAY                           0x0C
#define     V_008F1C_SQ_RSRC_IMG_2D_ARRAY                           0x0D
#define     V_008F1C_SQ_RSRC_IMG_2D_MSAA                            0x0E
#define     V_008F1C_SQ_RSRC_IMG_2D_MSAA_ARRAY                      0x0F
#define R_008F20_SQ_IMG_RSRC_WORD4                                      0x008F20
#define   S_008F20_DEPTH(x)                                           (((unsigned)(x) & 0x1FFF) << 0)
#define   G_008F20_DEPTH(x)                                           (((x) >> 0) & 0x1FFF)
#define   C_008F20_DEPTH                                              0xFFFFE000
#define   S_008F20_PITCH(x)                                           (((unsigned)(x) & 0x3FFF) << 13)
#define   G_008F20_PITCH(x)                                           (((x) >> 13) & 0x3FFF)
#define   C_008F20_PITCH                                              0xF8001FFF
#define R_008F24_SQ_IMG_RSRC_WORD5                                      0x008F24
#define   S_008F24_BASE_ARRAY(x)                                      (((unsigned)(x) & 0x1FFF) << 0)
#define   G_008F24_BASE_ARRAY(x)                                      (((x) >> 0) & 0x1FFF)
#define   C_008F24_BASE_ARRAY                                         0xFFFFE000
#define   S_008F24_LAST_ARRAY(x)                                      (((unsigned)(x) & 0x1FFF) << 13)
#define   G_008F24_LAST_ARRAY(x)                                      (((x) >> 13) & 0x1FFF)
#define   C_008F24_LAST_ARRAY                                         0xFC001FFF
#define R_008F28_SQ_IMG_RSRC_WORD6                                      0x008F28
#define   S_008F28_MIN_LOD_WARN(x)                                    (((unsigned)(x) & 0xFFF) << 0)
#define   G_008F28_MIN_LOD_WARN(x)                                    (((x) >> 0) & 0xFFF)
#define   C_008F28_MIN_LOD_WARN                                       0xFFFFF000
/* CIK */
#define   S_008F28_COUNTER_BANK_ID(x)                                 (((unsigned)(x) & 0xFF) << 12)
#define   G_008F28_COUNTER_BANK_ID(x)                                 (((x) >> 12) & 0xFF)
#define   C_008F28_COUNTER_BANK_ID                                    0xFFF00FFF
#define   S_008F28_LOD_HDW_CNT_EN(x)                                  (((unsigned)(x) & 0x1) << 20)
#define   G_008F28_LOD_HDW_CNT_EN(x)                                  (((x) >> 20) & 0x1)
#define   C_008F28_LOD_HDW_CNT_EN                                     0xFFEFFFFF
/*     */
/* VI */
#define   S_008F28_COMPRESSION_EN(x)                                  (((unsigned)(x) & 0x1) << 21)
#define   G_008F28_COMPRESSION_EN(x)                                  (((x) >> 21) & 0x1)
#define   C_008F28_COMPRESSION_EN                                     0xFFDFFFFF
#define   S_008F28_ALPHA_IS_ON_MSB(x)                                 (((unsigned)(x) & 0x1) << 22)
#define   G_008F28_ALPHA_IS_ON_MSB(x)                                 (((x) >> 22) & 0x1)
#define   C_008F28_ALPHA_IS_ON_MSB                                    0xFFBFFFFF
#define   S_008F28_COLOR_TRANSFORM(x)                                 (((unsigned)(x) & 0x1) << 23)
#define   G_008F28_COLOR_TRANSFORM(x)                                 (((x) >> 23) & 0x1)
#define   C_008F28_COLOR_TRANSFORM                                    0xFF7FFFFF
#define   S_008F28_LOST_ALPHA_BITS(x)                                 (((unsigned)(x) & 0x0F) << 24)
#define   G_008F28_LOST_ALPHA_BITS(x)                                 (((x) >> 24) & 0x0F)
#define   C_008F28_LOST_ALPHA_BITS                                    0xF0FFFFFF
#define   S_008F28_LOST_COLOR_BITS(x)                                 (((unsigned)(x) & 0x0F) << 28)
#define   G_008F28_LOST_COLOR_BITS(x)                                 (((x) >> 28) & 0x0F)
#define   C_008F28_LOST_COLOR_BITS                                    0x0FFFFFFF
/*    */
#define R_008F2C_SQ_IMG_RSRC_WORD7                                      0x008F2C
#define R_008F30_SQ_IMG_SAMP_WORD0                                      0x008F30
#define   S_008F30_CLAMP_X(x)                                         (((unsigned)(x) & 0x07) << 0)
#define   G_008F30_CLAMP_X(x)                                         (((x) >> 0) & 0x07)
#define   C_008F30_CLAMP_X                                            0xFFFFFFF8
#define     V_008F30_SQ_TEX_WRAP                                    0x00
#define     V_008F30_SQ_TEX_MIRROR                                  0x01
#define     V_008F30_SQ_TEX_CLAMP_LAST_TEXEL                        0x02
#define     V_008F30_SQ_TEX_MIRROR_ONCE_LAST_TEXEL                  0x03
#define     V_008F30_SQ_TEX_CLAMP_HALF_BORDER                       0x04
#define     V_008F30_SQ_TEX_MIRROR_ONCE_HALF_BORDER                 0x05
#define     V_008F30_SQ_TEX_CLAMP_BORDER                            0x06
#define     V_008F30_SQ_TEX_MIRROR_ONCE_BORDER                      0x07
#define   S_008F30_CLAMP_Y(x)                                         (((unsigned)(x) & 0x07) << 3)
#define   G_008F30_CLAMP_Y(x)                                         (((x) >> 3) & 0x07)
#define   C_008F30_CLAMP_Y                                            0xFFFFFFC7
#define     V_008F30_SQ_TEX_WRAP                                    0x00
#define     V_008F30_SQ_TEX_MIRROR                                  0x01
#define     V_008F30_SQ_TEX_CLAMP_LAST_TEXEL                        0x02
#define     V_008F30_SQ_TEX_MIRROR_ONCE_LAST_TEXEL                  0x03
#define     V_008F30_SQ_TEX_CLAMP_HALF_BORDER                       0x04
#define     V_008F30_SQ_TEX_MIRROR_ONCE_HALF_BORDER                 0x05
#define     V_008F30_SQ_TEX_CLAMP_BORDER                            0x06
#define     V_008F30_SQ_TEX_MIRROR_ONCE_BORDER                      0x07
#define   S_008F30_CLAMP_Z(x)                                         (((unsigned)(x) & 0x07) << 6)
#define   G_008F30_CLAMP_Z(x)                                         (((x) >> 6) & 0x07)
#define   C_008F30_CLAMP_Z                                            0xFFFFFE3F
#define     V_008F30_SQ_TEX_WRAP                                    0x00
#define     V_008F30_SQ_TEX_MIRROR                                  0x01
#define     V_008F30_SQ_TEX_CLAMP_LAST_TEXEL                        0x02
#define     V_008F30_SQ_TEX_MIRROR_ONCE_LAST_TEXEL                  0x03
#define     V_008F30_SQ_TEX_CLAMP_HALF_BORDER                       0x04
#define     V_008F30_SQ_TEX_MIRROR_ONCE_HALF_BORDER                 0x05
#define     V_008F30_SQ_TEX_CLAMP_BORDER                            0x06
#define     V_008F30_SQ_TEX_MIRROR_ONCE_BORDER                      0x07
#define   S_008F30_MAX_ANISO_RATIO(x)                                 (((unsigned)(x) & 0x07) << 9)
#define   G_008F30_MAX_ANISO_RATIO(x)                                 (((x) >> 9) & 0x07)
#define   C_008F30_MAX_ANISO_RATIO                                    0xFFFFF1FF
#define   S_008F30_DEPTH_COMPARE_FUNC(x)                              (((unsigned)(x) & 0x07) << 12)
#define   G_008F30_DEPTH_COMPARE_FUNC(x)                              (((x) >> 12) & 0x07)
#define   C_008F30_DEPTH_COMPARE_FUNC                                 0xFFFF8FFF
#define     V_008F30_SQ_TEX_DEPTH_COMPARE_NEVER                     0x00
#define     V_008F30_SQ_TEX_DEPTH_COMPARE_LESS                      0x01
#define     V_008F30_SQ_TEX_DEPTH_COMPARE_EQUAL                     0x02
#define     V_008F30_SQ_TEX_DEPTH_COMPARE_LESSEQUAL                 0x03
#define     V_008F30_SQ_TEX_DEPTH_COMPARE_GREATER                   0x04
#define     V_008F30_SQ_TEX_DEPTH_COMPARE_NOTEQUAL                  0x05
#define     V_008F30_SQ_TEX_DEPTH_COMPARE_GREATEREQUAL              0x06
#define     V_008F30_SQ_TEX_DEPTH_COMPARE_ALWAYS                    0x07
#define   S_008F30_FORCE_UNNORMALIZED(x)                              (((unsigned)(x) & 0x1) << 15)
#define   G_008F30_FORCE_UNNORMALIZED(x)                              (((x) >> 15) & 0x1)
#define   C_008F30_FORCE_UNNORMALIZED                                 0xFFFF7FFF
#define   S_008F30_ANISO_THRESHOLD(x)                                 (((unsigned)(x) & 0x07) << 16)
#define   G_008F30_ANISO_THRESHOLD(x)                                 (((x) >> 16) & 0x07)
#define   C_008F30_ANISO_THRESHOLD                                    0xFFF8FFFF
#define   S_008F30_MC_COORD_TRUNC(x)                                  (((unsigned)(x) & 0x1) << 19)
#define   G_008F30_MC_COORD_TRUNC(x)                                  (((x) >> 19) & 0x1)
#define   C_008F30_MC_COORD_TRUNC                                     0xFFF7FFFF
#define   S_008F30_FORCE_DEGAMMA(x)                                   (((unsigned)(x) & 0x1) << 20)
#define   G_008F30_FORCE_DEGAMMA(x)                                   (((x) >> 20) & 0x1)
#define   C_008F30_FORCE_DEGAMMA                                      0xFFEFFFFF
#define   S_008F30_ANISO_BIAS(x)                                      (((unsigned)(x) & 0x3F) << 21)
#define   G_008F30_ANISO_BIAS(x)                                      (((x) >> 21) & 0x3F)
#define   C_008F30_ANISO_BIAS                                         0xF81FFFFF
#define   S_008F30_TRUNC_COORD(x)                                     (((unsigned)(x) & 0x1) << 27)
#define   G_008F30_TRUNC_COORD(x)                                     (((x) >> 27) & 0x1)
#define   C_008F30_TRUNC_COORD                                        0xF7FFFFFF
#define   S_008F30_DISABLE_CUBE_WRAP(x)                               (((unsigned)(x) & 0x1) << 28)
#define   G_008F30_DISABLE_CUBE_WRAP(x)                               (((x) >> 28) & 0x1)
#define   C_008F30_DISABLE_CUBE_WRAP                                  0xEFFFFFFF
#define   S_008F30_FILTER_MODE(x)                                     (((unsigned)(x) & 0x03) << 29)
#define   G_008F30_FILTER_MODE(x)                                     (((x) >> 29) & 0x03)
#define   C_008F30_FILTER_MODE                                        0x9FFFFFFF
/* VI */
#define   S_008F30_COMPAT_MODE(x)                                     (((unsigned)(x) & 0x1) << 31)
#define   G_008F30_COMPAT_MODE(x)                                     (((x) >> 31) & 0x1)
#define   C_008F30_COMPAT_MODE                                        0x7FFFFFFF
/*    */
#define R_008F34_SQ_IMG_SAMP_WORD1                                      0x008F34
#define   S_008F34_MIN_LOD(x)                                         (((unsigned)(x) & 0xFFF) << 0)
#define   G_008F34_MIN_LOD(x)                                         (((x) >> 0) & 0xFFF)
#define   C_008F34_MIN_LOD                                            0xFFFFF000
#define   S_008F34_MAX_LOD(x)                                         (((unsigned)(x) & 0xFFF) << 12)
#define   G_008F34_MAX_LOD(x)                                         (((x) >> 12) & 0xFFF)
#define   C_008F34_MAX_LOD                                            0xFF000FFF
#define   S_008F34_PERF_MIP(x)                                        (((unsigned)(x) & 0x0F) << 24)
#define   G_008F34_PERF_MIP(x)                                        (((x) >> 24) & 0x0F)
#define   C_008F34_PERF_MIP                                           0xF0FFFFFF
#define   S_008F34_PERF_Z(x)                                          (((unsigned)(x) & 0x0F) << 28)
#define   G_008F34_PERF_Z(x)                                          (((x) >> 28) & 0x0F)
#define   C_008F34_PERF_Z                                             0x0FFFFFFF
#define R_008F38_SQ_IMG_SAMP_WORD2                                      0x008F38
#define   S_008F38_LOD_BIAS(x)                                        (((unsigned)(x) & 0x3FFF) << 0)
#define   G_008F38_LOD_BIAS(x)                                        (((x) >> 0) & 0x3FFF)
#define   C_008F38_LOD_BIAS                                           0xFFFFC000
#define   S_008F38_LOD_BIAS_SEC(x)                                    (((unsigned)(x) & 0x3F) << 14)
#define   G_008F38_LOD_BIAS_SEC(x)                                    (((x) >> 14) & 0x3F)
#define   C_008F38_LOD_BIAS_SEC                                       0xFFF03FFF
#define   S_008F38_XY_MAG_FILTER(x)                                   (((unsigned)(x) & 0x03) << 20)
#define   G_008F38_XY_MAG_FILTER(x)                                   (((x) >> 20) & 0x03)
#define   C_008F38_XY_MAG_FILTER                                      0xFFCFFFFF
#define     V_008F38_SQ_TEX_XY_FILTER_POINT                         0x00
#define     V_008F38_SQ_TEX_XY_FILTER_BILINEAR                      0x01
#define   S_008F38_XY_MIN_FILTER(x)                                   (((unsigned)(x) & 0x03) << 22)
#define   G_008F38_XY_MIN_FILTER(x)                                   (((x) >> 22) & 0x03)
#define   C_008F38_XY_MIN_FILTER                                      0xFF3FFFFF
#define     V_008F38_SQ_TEX_XY_FILTER_POINT                         0x00
#define     V_008F38_SQ_TEX_XY_FILTER_BILINEAR                      0x01
#define     V_008F38_SQ_TEX_XY_FILTER_ANISO_POINT                   0x02
#define     V_008F38_SQ_TEX_XY_FILTER_ANISO_BILINEAR                0x03
#define   S_008F38_Z_FILTER(x)                                        (((unsigned)(x) & 0x03) << 24)
#define   G_008F38_Z_FILTER(x)                                        (((x) >> 24) & 0x03)
#define   C_008F38_Z_FILTER                                           0xFCFFFFFF
#define     V_008F38_SQ_TEX_Z_FILTER_NONE                           0x00
#define     V_008F38_SQ_TEX_Z_FILTER_POINT                          0x01
#define     V_008F38_SQ_TEX_Z_FILTER_LINEAR                         0x02
#define   S_008F38_MIP_FILTER(x)                                      (((unsigned)(x) & 0x03) << 26)
#define   G_008F38_MIP_FILTER(x)                                      (((x) >> 26) & 0x03)
#define   C_008F38_MIP_FILTER                                         0xF3FFFFFF
#define     V_008F38_SQ_TEX_Z_FILTER_NONE                           0x00
#define     V_008F38_SQ_TEX_Z_FILTER_POINT                          0x01
#define     V_008F38_SQ_TEX_Z_FILTER_LINEAR                         0x02
#define   S_008F38_MIP_POINT_PRECLAMP(x)                              (((unsigned)(x) & 0x1) << 28)
#define   G_008F38_MIP_POINT_PRECLAMP(x)                              (((x) >> 28) & 0x1)
#define   C_008F38_MIP_POINT_PRECLAMP                                 0xEFFFFFFF
#define   S_008F38_DISABLE_LSB_CEIL(x)                                (((unsigned)(x) & 0x1) << 29)
#define   G_008F38_DISABLE_LSB_CEIL(x)                                (((x) >> 29) & 0x1)
#define   C_008F38_DISABLE_LSB_CEIL                                   0xDFFFFFFF
#define   S_008F38_FILTER_PREC_FIX(x)                                 (((unsigned)(x) & 0x1) << 30)
#define   G_008F38_FILTER_PREC_FIX(x)                                 (((x) >> 30) & 0x1)
#define   C_008F38_FILTER_PREC_FIX                                    0xBFFFFFFF
#define   S_008F38_ANISO_OVERRIDE(x)                                  (((unsigned)(x) & 0x1) << 31)
#define   G_008F38_ANISO_OVERRIDE(x)                                  (((x) >> 31) & 0x1)
#define   C_008F38_ANISO_OVERRIDE                                     0x7FFFFFFF
#define R_008F3C_SQ_IMG_SAMP_WORD3                                      0x008F3C
#define   S_008F3C_BORDER_COLOR_PTR(x)                                (((unsigned)(x) & 0xFFF) << 0)
#define   G_008F3C_BORDER_COLOR_PTR(x)                                (((x) >> 0) & 0xFFF)
#define   C_008F3C_BORDER_COLOR_PTR                                   0xFFFFF000
#define   S_008F3C_BORDER_COLOR_TYPE(x)                               (((unsigned)(x) & 0x03) << 30)
#define   G_008F3C_BORDER_COLOR_TYPE(x)                               (((x) >> 30) & 0x03)
#define   C_008F3C_BORDER_COLOR_TYPE                                  0x3FFFFFFF
#define     V_008F3C_SQ_TEX_BORDER_COLOR_TRANS_BLACK                0x00
#define     V_008F3C_SQ_TEX_BORDER_COLOR_OPAQUE_BLACK               0x01
#define     V_008F3C_SQ_TEX_BORDER_COLOR_OPAQUE_WHITE               0x02
#define     V_008F3C_SQ_TEX_BORDER_COLOR_REGISTER                   0x03
#define R_0090DC_SPI_DYN_GPR_LOCK_EN                                    0x0090DC /* not on CIK */
#define   S_0090DC_VS_LOW_THRESHOLD(x)                                (((unsigned)(x) & 0x0F) << 0)
#define   G_0090DC_VS_LOW_THRESHOLD(x)                                (((x) >> 0) & 0x0F)
#define   C_0090DC_VS_LOW_THRESHOLD                                   0xFFFFFFF0
#define   S_0090DC_GS_LOW_THRESHOLD(x)                                (((unsigned)(x) & 0x0F) << 4)
#define   G_0090DC_GS_LOW_THRESHOLD(x)                                (((x) >> 4) & 0x0F)
#define   C_0090DC_GS_LOW_THRESHOLD                                   0xFFFFFF0F
#define   S_0090DC_ES_LOW_THRESHOLD(x)                                (((unsigned)(x) & 0x0F) << 8)
#define   G_0090DC_ES_LOW_THRESHOLD(x)                                (((x) >> 8) & 0x0F)
#define   C_0090DC_ES_LOW_THRESHOLD                                   0xFFFFF0FF
#define   S_0090DC_HS_LOW_THRESHOLD(x)                                (((unsigned)(x) & 0x0F) << 12)
#define   G_0090DC_HS_LOW_THRESHOLD(x)                                (((x) >> 12) & 0x0F)
#define   C_0090DC_HS_LOW_THRESHOLD                                   0xFFFF0FFF
#define   S_0090DC_LS_LOW_THRESHOLD(x)                                (((unsigned)(x) & 0x0F) << 16)
#define   G_0090DC_LS_LOW_THRESHOLD(x)                                (((x) >> 16) & 0x0F)
#define   C_0090DC_LS_LOW_THRESHOLD                                   0xFFF0FFFF
#define R_0090E0_SPI_STATIC_THREAD_MGMT_1                               0x0090E0 /* not on CIK */
#define   S_0090E0_PS_CU_EN(x)                                        (((unsigned)(x) & 0xFFFF) << 0)
#define   G_0090E0_PS_CU_EN(x)                                        (((x) >> 0) & 0xFFFF)
#define   C_0090E0_PS_CU_EN                                           0xFFFF0000
#define   S_0090E0_VS_CU_EN(x)                                        (((unsigned)(x) & 0xFFFF) << 16)
#define   G_0090E0_VS_CU_EN(x)                                        (((x) >> 16) & 0xFFFF)
#define   C_0090E0_VS_CU_EN                                           0x0000FFFF
#define R_0090E4_SPI_STATIC_THREAD_MGMT_2                               0x0090E4 /* not on CIK */
#define   S_0090E4_GS_CU_EN(x)                                        (((unsigned)(x) & 0xFFFF) << 0)
#define   G_0090E4_GS_CU_EN(x)                                        (((x) >> 0) & 0xFFFF)
#define   C_0090E4_GS_CU_EN                                           0xFFFF0000
#define   S_0090E4_ES_CU_EN(x)                                        (((unsigned)(x) & 0xFFFF) << 16)
#define   G_0090E4_ES_CU_EN(x)                                        (((x) >> 16) & 0xFFFF)
#define   C_0090E4_ES_CU_EN                                           0x0000FFFF
#define R_0090E8_SPI_STATIC_THREAD_MGMT_3                               0x0090E8 /* not on CIK */
#define   S_0090E8_LSHS_CU_EN(x)                                      (((unsigned)(x) & 0xFFFF) << 0)
#define   G_0090E8_LSHS_CU_EN(x)                                      (((x) >> 0) & 0xFFFF)
#define   C_0090E8_LSHS_CU_EN                                         0xFFFF0000
#define R_0090EC_SPI_PS_MAX_WAVE_ID                                     0x0090EC
#define   S_0090EC_MAX_WAVE_ID(x)                                     (((unsigned)(x) & 0xFFF) << 0)
#define   G_0090EC_MAX_WAVE_ID(x)                                     (((x) >> 0) & 0xFFF)
#define   C_0090EC_MAX_WAVE_ID                                        0xFFFFF000
/* CIK */
#define R_0090E8_SPI_PS_MAX_WAVE_ID                                     0x0090E8
#define   S_0090E8_MAX_WAVE_ID(x)                                     (((unsigned)(x) & 0xFFF) << 0)
#define   G_0090E8_MAX_WAVE_ID(x)                                     (((x) >> 0) & 0xFFF)
#define   C_0090E8_MAX_WAVE_ID                                        0xFFFFF000
/*     */
#define R_0090F0_SPI_ARB_PRIORITY                                       0x0090F0
#define   S_0090F0_RING_ORDER_TS0(x)                                  (((unsigned)(x) & 0x07) << 0)
#define   G_0090F0_RING_ORDER_TS0(x)                                  (((x) >> 0) & 0x07)
#define   C_0090F0_RING_ORDER_TS0                                     0xFFFFFFF8
#define     V_0090F0_X_R0                                           0x00
#define   S_0090F0_RING_ORDER_TS1(x)                                  (((unsigned)(x) & 0x07) << 3)
#define   G_0090F0_RING_ORDER_TS1(x)                                  (((x) >> 3) & 0x07)
#define   C_0090F0_RING_ORDER_TS1                                     0xFFFFFFC7
#define   S_0090F0_RING_ORDER_TS2(x)                                  (((unsigned)(x) & 0x07) << 6)
#define   G_0090F0_RING_ORDER_TS2(x)                                  (((x) >> 6) & 0x07)
#define   C_0090F0_RING_ORDER_TS2                                     0xFFFFFE3F
/* CIK */
#define R_00C700_SPI_ARB_PRIORITY                                       0x00C700
#define   S_00C700_PIPE_ORDER_TS0(x)                                  (((unsigned)(x) & 0x07) << 0)
#define   G_00C700_PIPE_ORDER_TS0(x)                                  (((x) >> 0) & 0x07)
#define   C_00C700_PIPE_ORDER_TS0                                     0xFFFFFFF8
#define   S_00C700_PIPE_ORDER_TS1(x)                                  (((unsigned)(x) & 0x07) << 3)
#define   G_00C700_PIPE_ORDER_TS1(x)                                  (((x) >> 3) & 0x07)
#define   C_00C700_PIPE_ORDER_TS1                                     0xFFFFFFC7
#define   S_00C700_PIPE_ORDER_TS2(x)                                  (((unsigned)(x) & 0x07) << 6)
#define   G_00C700_PIPE_ORDER_TS2(x)                                  (((x) >> 6) & 0x07)
#define   C_00C700_PIPE_ORDER_TS2                                     0xFFFFFE3F
#define   S_00C700_PIPE_ORDER_TS3(x)                                  (((unsigned)(x) & 0x07) << 9)
#define   G_00C700_PIPE_ORDER_TS3(x)                                  (((x) >> 9) & 0x07)
#define   C_00C700_PIPE_ORDER_TS3                                     0xFFFFF1FF
#define   S_00C700_TS0_DUR_MULT(x)                                    (((unsigned)(x) & 0x03) << 12)
#define   G_00C700_TS0_DUR_MULT(x)                                    (((x) >> 12) & 0x03)
#define   C_00C700_TS0_DUR_MULT                                       0xFFFFCFFF
#define   S_00C700_TS1_DUR_MULT(x)                                    (((unsigned)(x) & 0x03) << 14)
#define   G_00C700_TS1_DUR_MULT(x)                                    (((x) >> 14) & 0x03)
#define   C_00C700_TS1_DUR_MULT                                       0xFFFF3FFF
#define   S_00C700_TS2_DUR_MULT(x)                                    (((unsigned)(x) & 0x03) << 16)
#define   G_00C700_TS2_DUR_MULT(x)                                    (((x) >> 16) & 0x03)
#define   C_00C700_TS2_DUR_MULT                                       0xFFFCFFFF
#define   S_00C700_TS3_DUR_MULT(x)                                    (((unsigned)(x) & 0x03) << 18)
#define   G_00C700_TS3_DUR_MULT(x)                                    (((x) >> 18) & 0x03)
#define   C_00C700_TS3_DUR_MULT                                       0xFFF3FFFF
/*     */
#define R_0090F4_SPI_ARB_CYCLES_0                                       0x0090F4 /* moved to 0xC704 on CIK */
#define   S_0090F4_TS0_DURATION(x)                                    (((unsigned)(x) & 0xFFFF) << 0)
#define   G_0090F4_TS0_DURATION(x)                                    (((x) >> 0) & 0xFFFF)
#define   C_0090F4_TS0_DURATION                                       0xFFFF0000
#define   S_0090F4_TS1_DURATION(x)                                    (((unsigned)(x) & 0xFFFF) << 16)
#define   G_0090F4_TS1_DURATION(x)                                    (((x) >> 16) & 0xFFFF)
#define   C_0090F4_TS1_DURATION                                       0x0000FFFF
#define R_0090F8_SPI_ARB_CYCLES_1                                       0x0090F8 /* moved to 0xC708 on CIK */
#define   S_0090F8_TS2_DURATION(x)                                    (((unsigned)(x) & 0xFFFF) << 0)
#define   G_0090F8_TS2_DURATION(x)                                    (((x) >> 0) & 0xFFFF)
#define   C_0090F8_TS2_DURATION                                       0xFFFF0000
/* CIK */
#define R_008F40_SQ_FLAT_SCRATCH_WORD0                                  0x008F40
#define   S_008F40_SIZE(x)                                            (((unsigned)(x) & 0x7FFFF) << 0)
#define   G_008F40_SIZE(x)                                            (((x) >> 0) & 0x7FFFF)
#define   C_008F40_SIZE                                               0xFFF80000
#define R_008F44_SQ_FLAT_SCRATCH_WORD1                                  0x008F44
#define   S_008F44_OFFSET(x)                                          (((unsigned)(x) & 0xFFFFFF) << 0)
#define   G_008F44_OFFSET(x)                                          (((x) >> 0) & 0xFFFFFF)
#define   C_008F44_OFFSET                                             0xFF000000
/*     */
#define R_030FF8_DB_ZPASS_COUNT_LOW                                     0x030FF8
#define R_030FFC_DB_ZPASS_COUNT_HI                                      0x030FFC
#define   S_030FFC_COUNT_HI(x)                                        (((unsigned)(x) & 0x7FFFFFFF) << 0)
#define   G_030FFC_COUNT_HI(x)                                        (((x) >> 0) & 0x7FFFFFFF)
#define   C_030FFC_COUNT_HI                                           0x80000000
#define R_009100_SPI_CONFIG_CNTL                                        0x009100
#define   S_009100_GPR_WRITE_PRIORITY(x)                              (((unsigned)(x) & 0x1FFFFF) << 0)
#define   G_009100_GPR_WRITE_PRIORITY(x)                              (((x) >> 0) & 0x1FFFFF)
#define   C_009100_GPR_WRITE_PRIORITY                                 0xFFE00000
#define   S_009100_EXP_PRIORITY_ORDER(x)                              (((unsigned)(x) & 0x07) << 21)
#define   G_009100_EXP_PRIORITY_ORDER(x)                              (((x) >> 21) & 0x07)
#define   C_009100_EXP_PRIORITY_ORDER                                 0xFF1FFFFF
#define   S_009100_ENABLE_SQG_TOP_EVENTS(x)                           (((unsigned)(x) & 0x1) << 24)
#define   G_009100_ENABLE_SQG_TOP_EVENTS(x)                           (((x) >> 24) & 0x1)
#define   C_009100_ENABLE_SQG_TOP_EVENTS                              0xFEFFFFFF
#define   S_009100_ENABLE_SQG_BOP_EVENTS(x)                           (((unsigned)(x) & 0x1) << 25)
#define   G_009100_ENABLE_SQG_BOP_EVENTS(x)                           (((x) >> 25) & 0x1)
#define   C_009100_ENABLE_SQG_BOP_EVENTS                              0xFDFFFFFF
#define   S_009100_RSRC_MGMT_RESET(x)                                 (((unsigned)(x) & 0x1) << 26)
#define   G_009100_RSRC_MGMT_RESET(x)                                 (((x) >> 26) & 0x1)
#define   C_009100_RSRC_MGMT_RESET                                    0xFBFFFFFF
#define R_00913C_SPI_CONFIG_CNTL_1                                      0x00913C
#define   S_00913C_VTX_DONE_DELAY(x)                                  (((unsigned)(x) & 0x0F) << 0)
#define   G_00913C_VTX_DONE_DELAY(x)                                  (((x) >> 0) & 0x0F)
#define   C_00913C_VTX_DONE_DELAY                                     0xFFFFFFF0
#define     V_00913C_X_DELAY_14_CLKS                                0x00
#define     V_00913C_X_DELAY_16_CLKS                                0x01
#define     V_00913C_X_DELAY_18_CLKS                                0x02
#define     V_00913C_X_DELAY_20_CLKS                                0x03
#define     V_00913C_X_DELAY_22_CLKS                                0x04
#define     V_00913C_X_DELAY_24_CLKS                                0x05
#define     V_00913C_X_DELAY_26_CLKS                                0x06
#define     V_00913C_X_DELAY_28_CLKS                                0x07
#define     V_00913C_X_DELAY_30_CLKS                                0x08
#define     V_00913C_X_DELAY_32_CLKS                                0x09
#define     V_00913C_X_DELAY_34_CLKS                                0x0A
#define     V_00913C_X_DELAY_4_CLKS                                 0x0B
#define     V_00913C_X_DELAY_6_CLKS                                 0x0C
#define     V_00913C_X_DELAY_8_CLKS                                 0x0D
#define     V_00913C_X_DELAY_10_CLKS                                0x0E
#define     V_00913C_X_DELAY_12_CLKS                                0x0F
#define   S_00913C_INTERP_ONE_PRIM_PER_ROW(x)                         (((unsigned)(x) & 0x1) << 4)
#define   G_00913C_INTERP_ONE_PRIM_PER_ROW(x)                         (((x) >> 4) & 0x1)
#define   C_00913C_INTERP_ONE_PRIM_PER_ROW                            0xFFFFFFEF
#define   S_00913C_PC_LIMIT_ENABLE(x)                                 (((unsigned)(x) & 0x1) << 6)
#define   G_00913C_PC_LIMIT_ENABLE(x)                                 (((x) >> 6) & 0x1)
#define   C_00913C_PC_LIMIT_ENABLE                                    0xFFFFFFBF
#define   S_00913C_PC_LIMIT_STRICT(x)                                 (((unsigned)(x) & 0x1) << 7)
#define   G_00913C_PC_LIMIT_STRICT(x)                                 (((x) >> 7) & 0x1)
#define   C_00913C_PC_LIMIT_STRICT                                    0xFFFFFF7F
#define   S_00913C_PC_LIMIT_SIZE(x)                                   (((unsigned)(x) & 0xFFFF) << 16)
#define   G_00913C_PC_LIMIT_SIZE(x)                                   (((x) >> 16) & 0xFFFF)
#define   C_00913C_PC_LIMIT_SIZE                                      0x0000FFFF
#define R_00936C_SPI_RESOURCE_RESERVE_CU_AB_0                           0x00936C
#define   S_00936C_TYPE_A(x)                                          (((unsigned)(x) & 0x0F) << 0)
#define   G_00936C_TYPE_A(x)                                          (((x) >> 0) & 0x0F)
#define   C_00936C_TYPE_A                                             0xFFFFFFF0
#define   S_00936C_VGPR_A(x)                                          (((unsigned)(x) & 0x07) << 4)
#define   G_00936C_VGPR_A(x)                                          (((x) >> 4) & 0x07)
#define   C_00936C_VGPR_A                                             0xFFFFFF8F
#define   S_00936C_SGPR_A(x)                                          (((unsigned)(x) & 0x07) << 7)
#define   G_00936C_SGPR_A(x)                                          (((x) >> 7) & 0x07)
#define   C_00936C_SGPR_A                                             0xFFFFFC7F
#define   S_00936C_LDS_A(x)                                           (((unsigned)(x) & 0x07) << 10)
#define   G_00936C_LDS_A(x)                                           (((x) >> 10) & 0x07)
#define   C_00936C_LDS_A                                              0xFFFFE3FF
#define   S_00936C_WAVES_A(x)                                         (((unsigned)(x) & 0x03) << 13)
#define   G_00936C_WAVES_A(x)                                         (((x) >> 13) & 0x03)
#define   C_00936C_WAVES_A                                            0xFFFF9FFF
#define   S_00936C_EN_A(x)                                            (((unsigned)(x) & 0x1) << 15)
#define   G_00936C_EN_A(x)                                            (((x) >> 15) & 0x1)
#define   C_00936C_EN_A                                               0xFFFF7FFF
#define   S_00936C_TYPE_B(x)                                          (((unsigned)(x) & 0x0F) << 16)
#define   G_00936C_TYPE_B(x)                                          (((x) >> 16) & 0x0F)
#define   C_00936C_TYPE_B                                             0xFFF0FFFF
#define   S_00936C_VGPR_B(x)                                          (((unsigned)(x) & 0x07) << 20)
#define   G_00936C_VGPR_B(x)                                          (((x) >> 20) & 0x07)
#define   C_00936C_VGPR_B                                             0xFF8FFFFF
#define   S_00936C_SGPR_B(x)                                          (((unsigned)(x) & 0x07) << 23)
#define   G_00936C_SGPR_B(x)                                          (((x) >> 23) & 0x07)
#define   C_00936C_SGPR_B                                             0xFC7FFFFF
#define   S_00936C_LDS_B(x)                                           (((unsigned)(x) & 0x07) << 26)
#define   G_00936C_LDS_B(x)                                           (((x) >> 26) & 0x07)
#define   C_00936C_LDS_B                                              0xE3FFFFFF
#define   S_00936C_WAVES_B(x)                                         (((unsigned)(x) & 0x03) << 29)
#define   G_00936C_WAVES_B(x)                                         (((x) >> 29) & 0x03)
#define   C_00936C_WAVES_B                                            0x9FFFFFFF
#define   S_00936C_EN_B(x)                                            (((unsigned)(x) & 0x1) << 31)
#define   G_00936C_EN_B(x)                                            (((x) >> 31) & 0x1)
#define   C_00936C_EN_B                                               0x7FFFFFFF
#define R_00950C_TA_CS_BC_BASE_ADDR                                     0x00950C
#define R_009858_DB_SUBTILE_CONTROL                                     0x009858
#define   S_009858_MSAA1_X(x)                                         (((unsigned)(x) & 0x03) << 0)
#define   G_009858_MSAA1_X(x)                                         (((x) >> 0) & 0x03)
#define   C_009858_MSAA1_X                                            0xFFFFFFFC
#define   S_009858_MSAA1_Y(x)                                         (((unsigned)(x) & 0x03) << 2)
#define   G_009858_MSAA1_Y(x)                                         (((x) >> 2) & 0x03)
#define   C_009858_MSAA1_Y                                            0xFFFFFFF3
#define   S_009858_MSAA2_X(x)                                         (((unsigned)(x) & 0x03) << 4)
#define   G_009858_MSAA2_X(x)                                         (((x) >> 4) & 0x03)
#define   C_009858_MSAA2_X                                            0xFFFFFFCF
#define   S_009858_MSAA2_Y(x)                                         (((unsigned)(x) & 0x03) << 6)
#define   G_009858_MSAA2_Y(x)                                         (((x) >> 6) & 0x03)
#define   C_009858_MSAA2_Y                                            0xFFFFFF3F
#define   S_009858_MSAA4_X(x)                                         (((unsigned)(x) & 0x03) << 8)
#define   G_009858_MSAA4_X(x)                                         (((x) >> 8) & 0x03)
#define   C_009858_MSAA4_X                                            0xFFFFFCFF
#define   S_009858_MSAA4_Y(x)                                         (((unsigned)(x) & 0x03) << 10)
#define   G_009858_MSAA4_Y(x)                                         (((x) >> 10) & 0x03)
#define   C_009858_MSAA4_Y                                            0xFFFFF3FF
#define   S_009858_MSAA8_X(x)                                         (((unsigned)(x) & 0x03) << 12)
#define   G_009858_MSAA8_X(x)                                         (((x) >> 12) & 0x03)
#define   C_009858_MSAA8_X                                            0xFFFFCFFF
#define   S_009858_MSAA8_Y(x)                                         (((unsigned)(x) & 0x03) << 14)
#define   G_009858_MSAA8_Y(x)                                         (((x) >> 14) & 0x03)
#define   C_009858_MSAA8_Y                                            0xFFFF3FFF
#define   S_009858_MSAA16_X(x)                                        (((unsigned)(x) & 0x03) << 16)
#define   G_009858_MSAA16_X(x)                                        (((x) >> 16) & 0x03)
#define   C_009858_MSAA16_X                                           0xFFFCFFFF
#define   S_009858_MSAA16_Y(x)                                        (((unsigned)(x) & 0x03) << 18)
#define   G_009858_MSAA16_Y(x)                                        (((x) >> 18) & 0x03)
#define   C_009858_MSAA16_Y                                           0xFFF3FFFF
#define R_0098F8_GB_ADDR_CONFIG                                         0x0098F8
#define   S_0098F8_NUM_PIPES(x)                                       (((unsigned)(x) & 0x07) << 0)
#define   G_0098F8_NUM_PIPES(x)                                       (((x) >> 0) & 0x07)
#define   C_0098F8_NUM_PIPES                                          0xFFFFFFF8
#define   S_0098F8_PIPE_INTERLEAVE_SIZE(x)                            (((unsigned)(x) & 0x07) << 4)
#define   G_0098F8_PIPE_INTERLEAVE_SIZE(x)                            (((x) >> 4) & 0x07)
#define   C_0098F8_PIPE_INTERLEAVE_SIZE                               0xFFFFFF8F
#define   S_0098F8_BANK_INTERLEAVE_SIZE(x)                            (((unsigned)(x) & 0x07) << 8)
#define   G_0098F8_BANK_INTERLEAVE_SIZE(x)                            (((x) >> 8) & 0x07)
#define   C_0098F8_BANK_INTERLEAVE_SIZE                               0xFFFFF8FF
#define   S_0098F8_NUM_SHADER_ENGINES(x)                              (((unsigned)(x) & 0x03) << 12)
#define   G_0098F8_NUM_SHADER_ENGINES(x)                              (((x) >> 12) & 0x03)
#define   C_0098F8_NUM_SHADER_ENGINES                                 0xFFFFCFFF
#define   S_0098F8_SHADER_ENGINE_TILE_SIZE(x)                         (((unsigned)(x) & 0x07) << 16)
#define   G_0098F8_SHADER_ENGINE_TILE_SIZE(x)                         (((x) >> 16) & 0x07)
#define   C_0098F8_SHADER_ENGINE_TILE_SIZE                            0xFFF8FFFF
#define   S_0098F8_NUM_GPUS(x)                                        (((unsigned)(x) & 0x07) << 20)
#define   G_0098F8_NUM_GPUS(x)                                        (((x) >> 20) & 0x07)
#define   C_0098F8_NUM_GPUS                                           0xFF8FFFFF
#define   S_0098F8_MULTI_GPU_TILE_SIZE(x)                             (((unsigned)(x) & 0x03) << 24)
#define   G_0098F8_MULTI_GPU_TILE_SIZE(x)                             (((x) >> 24) & 0x03)
#define   C_0098F8_MULTI_GPU_TILE_SIZE                                0xFCFFFFFF
#define   S_0098F8_ROW_SIZE(x)                                        (((unsigned)(x) & 0x03) << 28)
#define   G_0098F8_ROW_SIZE(x)                                        (((x) >> 28) & 0x03)
#define   C_0098F8_ROW_SIZE                                           0xCFFFFFFF
#define   S_0098F8_NUM_LOWER_PIPES(x)                                 (((unsigned)(x) & 0x1) << 30)
#define   G_0098F8_NUM_LOWER_PIPES(x)                                 (((x) >> 30) & 0x1)
#define   C_0098F8_NUM_LOWER_PIPES                                    0xBFFFFFFF
#define R_009910_GB_TILE_MODE0                                          0x009910
#define   S_009910_MICRO_TILE_MODE(x)                                 (((unsigned)(x) & 0x03) << 0)
#define   G_009910_MICRO_TILE_MODE(x)                                 (((x) >> 0) & 0x03)
#define   C_009910_MICRO_TILE_MODE                                    0xFFFFFFFC
#define     V_009910_ADDR_SURF_DISPLAY_MICRO_TILING                 0x00
#define     V_009910_ADDR_SURF_THIN_MICRO_TILING                    0x01
#define     V_009910_ADDR_SURF_DEPTH_MICRO_TILING                   0x02
#define     V_009910_ADDR_SURF_THICK_MICRO_TILING                   0x03
#define   S_009910_ARRAY_MODE(x)                                      (((unsigned)(x) & 0x0F) << 2)
#define   G_009910_ARRAY_MODE(x)                                      (((x) >> 2) & 0x0F)
#define   C_009910_ARRAY_MODE                                         0xFFFFFFC3
#define     V_009910_ARRAY_LINEAR_GENERAL                           0x00
#define     V_009910_ARRAY_LINEAR_ALIGNED                           0x01
#define     V_009910_ARRAY_1D_TILED_THIN1                           0x02
#define     V_009910_ARRAY_1D_TILED_THICK                           0x03
#define     V_009910_ARRAY_2D_TILED_THIN1                           0x04
#define     V_009910_ARRAY_2D_TILED_THICK                           0x07
#define     V_009910_ARRAY_2D_TILED_XTHICK                          0x08
#define     V_009910_ARRAY_3D_TILED_THIN1                           0x0C
#define     V_009910_ARRAY_3D_TILED_THICK                           0x0D
#define     V_009910_ARRAY_3D_TILED_XTHICK                          0x0E
#define     V_009910_ARRAY_POWER_SAVE                               0x0F
#define   S_009910_PIPE_CONFIG(x)                                     (((unsigned)(x) & 0x1F) << 6)
#define   G_009910_PIPE_CONFIG(x)                                     (((x) >> 6) & 0x1F)
#define   C_009910_PIPE_CONFIG                                        0xFFFFF83F
#define     V_009910_ADDR_SURF_P2                                   0x00
#define     V_009910_ADDR_SURF_P2_RESERVED0                         0x01
#define     V_009910_ADDR_SURF_P2_RESERVED1                         0x02
#define     V_009910_ADDR_SURF_P2_RESERVED2                         0x03
#define     V_009910_X_ADDR_SURF_P4_8X16                            0x04
#define     V_009910_X_ADDR_SURF_P4_16X16                           0x05
#define     V_009910_X_ADDR_SURF_P4_16X32                           0x06
#define     V_009910_X_ADDR_SURF_P4_32X32                           0x07
#define     V_009910_X_ADDR_SURF_P8_16X16_8X16                      0x08
#define     V_009910_X_ADDR_SURF_P8_16X32_8X16                      0x09
#define     V_009910_X_ADDR_SURF_P8_32X32_8X16                      0x0A
#define     V_009910_X_ADDR_SURF_P8_16X32_16X16                     0x0B
#define     V_009910_X_ADDR_SURF_P8_32X32_16X16                     0x0C
#define     V_009910_X_ADDR_SURF_P8_32X32_16X32                     0x0D
#define     V_009910_X_ADDR_SURF_P8_32X64_32X32                     0x0E
#define   S_009910_TILE_SPLIT(x)                                      (((unsigned)(x) & 0x07) << 11)
#define   G_009910_TILE_SPLIT(x)                                      (((x) >> 11) & 0x07)
#define   C_009910_TILE_SPLIT                                         0xFFFFC7FF
#define     V_009910_ADDR_SURF_TILE_SPLIT_64B                       0x00
#define     V_009910_ADDR_SURF_TILE_SPLIT_128B                      0x01
#define     V_009910_ADDR_SURF_TILE_SPLIT_256B                      0x02
#define     V_009910_ADDR_SURF_TILE_SPLIT_512B                      0x03
#define     V_009910_ADDR_SURF_TILE_SPLIT_1KB                       0x04
#define     V_009910_ADDR_SURF_TILE_SPLIT_2KB                       0x05
#define     V_009910_ADDR_SURF_TILE_SPLIT_4KB                       0x06
#define   S_009910_BANK_WIDTH(x)                                      (((unsigned)(x) & 0x03) << 14)
#define   G_009910_BANK_WIDTH(x)                                      (((x) >> 14) & 0x03)
#define   C_009910_BANK_WIDTH                                         0xFFFF3FFF
#define     V_009910_ADDR_SURF_BANK_WIDTH_1                         0x00
#define     V_009910_ADDR_SURF_BANK_WIDTH_2                         0x01
#define     V_009910_ADDR_SURF_BANK_WIDTH_4                         0x02
#define     V_009910_ADDR_SURF_BANK_WIDTH_8                         0x03
#define   S_009910_BANK_HEIGHT(x)                                     (((unsigned)(x) & 0x03) << 16)
#define   G_009910_BANK_HEIGHT(x)                                     (((x) >> 16) & 0x03)
#define   C_009910_BANK_HEIGHT                                        0xFFFCFFFF
#define     V_009910_ADDR_SURF_BANK_HEIGHT_1                        0x00
#define     V_009910_ADDR_SURF_BANK_HEIGHT_2                        0x01
#define     V_009910_ADDR_SURF_BANK_HEIGHT_4                        0x02
#define     V_009910_ADDR_SURF_BANK_HEIGHT_8                        0x03
#define   S_009910_MACRO_TILE_ASPECT(x)                               (((unsigned)(x) & 0x03) << 18)
#define   G_009910_MACRO_TILE_ASPECT(x)                               (((x) >> 18) & 0x03)
#define   C_009910_MACRO_TILE_ASPECT                                  0xFFF3FFFF
#define     V_009910_ADDR_SURF_MACRO_ASPECT_1                       0x00
#define     V_009910_ADDR_SURF_MACRO_ASPECT_2                       0x01
#define     V_009910_ADDR_SURF_MACRO_ASPECT_4                       0x02
#define     V_009910_ADDR_SURF_MACRO_ASPECT_8                       0x03
#define   S_009910_NUM_BANKS(x)                                       (((unsigned)(x) & 0x03) << 20)
#define   G_009910_NUM_BANKS(x)                                       (((x) >> 20) & 0x03)
#define   C_009910_NUM_BANKS                                          0xFFCFFFFF
#define     V_009910_ADDR_SURF_2_BANK                               0x00
#define     V_009910_ADDR_SURF_4_BANK                               0x01
#define     V_009910_ADDR_SURF_8_BANK                               0x02
#define     V_009910_ADDR_SURF_16_BANK                              0x03
#define   S_009910_MICRO_TILE_MODE_NEW(x)                             (((unsigned)(x) & 0x07) << 22)
#define   G_009910_MICRO_TILE_MODE_NEW(x)                             (((x) >> 22) & 0x07)
#define   C_009910_MICRO_TILE_MODE_NEW                                0xFE3FFFFF
#define     V_009910_ADDR_SURF_DISPLAY_MICRO_TILING                 0x00
#define     V_009910_ADDR_SURF_THIN_MICRO_TILING                    0x01
#define     V_009910_ADDR_SURF_DEPTH_MICRO_TILING                   0x02
#define     V_009910_ADDR_SURF_ROTATED_MICRO_TILING                 0x03
#define   S_009910_SAMPLE_SPLIT(x)                                    (((unsigned)(x) & 0x03) << 25)
#define   G_009910_SAMPLE_SPLIT(x)                                    (((x) >> 25) & 0x03)
#define   C_009910_SAMPLE_SPLIT                                       0xF9FFFFFF
#define R_009914_GB_TILE_MODE1                                          0x009914
#define R_009918_GB_TILE_MODE2                                          0x009918
#define R_00991C_GB_TILE_MODE3                                          0x00991C
#define R_009920_GB_TILE_MODE4                                          0x009920
#define R_009924_GB_TILE_MODE5                                          0x009924
#define R_009928_GB_TILE_MODE6                                          0x009928
#define R_00992C_GB_TILE_MODE7                                          0x00992C
#define R_009930_GB_TILE_MODE8                                          0x009930
#define R_009934_GB_TILE_MODE9                                          0x009934
#define R_009938_GB_TILE_MODE10                                         0x009938
#define R_00993C_GB_TILE_MODE11                                         0x00993C
#define R_009940_GB_TILE_MODE12                                         0x009940
#define R_009944_GB_TILE_MODE13                                         0x009944
#define R_009948_GB_TILE_MODE14                                         0x009948
#define R_00994C_GB_TILE_MODE15                                         0x00994C
#define R_009950_GB_TILE_MODE16                                         0x009950
#define R_009954_GB_TILE_MODE17                                         0x009954
#define R_009958_GB_TILE_MODE18                                         0x009958
#define R_00995C_GB_TILE_MODE19                                         0x00995C
#define R_009960_GB_TILE_MODE20                                         0x009960
#define R_009964_GB_TILE_MODE21                                         0x009964
#define R_009968_GB_TILE_MODE22                                         0x009968
#define R_00996C_GB_TILE_MODE23                                         0x00996C
#define R_009970_GB_TILE_MODE24                                         0x009970
#define R_009974_GB_TILE_MODE25                                         0x009974
#define R_009978_GB_TILE_MODE26                                         0x009978
#define R_00997C_GB_TILE_MODE27                                         0x00997C
#define R_009980_GB_TILE_MODE28                                         0x009980
#define R_009984_GB_TILE_MODE29                                         0x009984
#define R_009988_GB_TILE_MODE30                                         0x009988
#define R_00998C_GB_TILE_MODE31                                         0x00998C
/* CIK */
#define R_009990_GB_MACROTILE_MODE0                                     0x009990
#define   S_009990_BANK_WIDTH(x)                                      (((unsigned)(x) & 0x03) << 0)
#define   G_009990_BANK_WIDTH(x)                                      (((x) >> 0) & 0x03)
#define   C_009990_BANK_WIDTH                                         0xFFFFFFFC
#define   S_009990_BANK_HEIGHT(x)                                     (((unsigned)(x) & 0x03) << 2)
#define   G_009990_BANK_HEIGHT(x)                                     (((x) >> 2) & 0x03)
#define   C_009990_BANK_HEIGHT                                        0xFFFFFFF3
#define   S_009990_MACRO_TILE_ASPECT(x)                               (((unsigned)(x) & 0x03) << 4)
#define   G_009990_MACRO_TILE_ASPECT(x)                               (((x) >> 4) & 0x03)
#define   C_009990_MACRO_TILE_ASPECT                                  0xFFFFFFCF
#define   S_009990_NUM_BANKS(x)                                       (((unsigned)(x) & 0x03) << 6)
#define   G_009990_NUM_BANKS(x)                                       (((x) >> 6) & 0x03)
#define   C_009990_NUM_BANKS                                          0xFFFFFF3F
#define R_009994_GB_MACROTILE_MODE1                                     0x009994
#define R_009998_GB_MACROTILE_MODE2                                     0x009998
#define R_00999C_GB_MACROTILE_MODE3                                     0x00999C
#define R_0099A0_GB_MACROTILE_MODE4                                     0x0099A0
#define R_0099A4_GB_MACROTILE_MODE5                                     0x0099A4
#define R_0099A8_GB_MACROTILE_MODE6                                     0x0099A8
#define R_0099AC_GB_MACROTILE_MODE7                                     0x0099AC
#define R_0099B0_GB_MACROTILE_MODE8                                     0x0099B0
#define R_0099B4_GB_MACROTILE_MODE9                                     0x0099B4
#define R_0099B8_GB_MACROTILE_MODE10                                    0x0099B8
#define R_0099BC_GB_MACROTILE_MODE11                                    0x0099BC
#define R_0099C0_GB_MACROTILE_MODE12                                    0x0099C0
#define R_0099C4_GB_MACROTILE_MODE13                                    0x0099C4
#define R_0099C8_GB_MACROTILE_MODE14                                    0x0099C8
#define R_0099CC_GB_MACROTILE_MODE15                                    0x0099CC
/*     */
#define R_00B000_SPI_SHADER_TBA_LO_PS                                   0x00B000
#define R_00B004_SPI_SHADER_TBA_HI_PS                                   0x00B004
#define   S_00B004_MEM_BASE(x)                                        (((unsigned)(x) & 0xFF) << 0)
#define   G_00B004_MEM_BASE(x)                                        (((x) >> 0) & 0xFF)
#define   C_00B004_MEM_BASE                                           0xFFFFFF00
#define R_00B008_SPI_SHADER_TMA_LO_PS                                   0x00B008
#define R_00B00C_SPI_SHADER_TMA_HI_PS                                   0x00B00C
#define   S_00B00C_MEM_BASE(x)                                        (((unsigned)(x) & 0xFF) << 0)
#define   G_00B00C_MEM_BASE(x)                                        (((x) >> 0) & 0xFF)
#define   C_00B00C_MEM_BASE                                           0xFFFFFF00
/* CIK */
#define R_00B01C_SPI_SHADER_PGM_RSRC3_PS                                0x00B01C
#define   S_00B01C_CU_EN(x)                                           (((unsigned)(x) & 0xFFFF) << 0)
#define   G_00B01C_CU_EN(x)                                           (((x) >> 0) & 0xFFFF)
#define   C_00B01C_CU_EN                                              0xFFFF0000
#define   S_00B01C_WAVE_LIMIT(x)                                      (((unsigned)(x) & 0x3F) << 16)
#define   G_00B01C_WAVE_LIMIT(x)                                      (((x) >> 16) & 0x3F)
#define   C_00B01C_WAVE_LIMIT                                         0xFFC0FFFF
#define   S_00B01C_LOCK_LOW_THRESHOLD(x)                              (((unsigned)(x) & 0x0F) << 22)
#define   G_00B01C_LOCK_LOW_THRESHOLD(x)                              (((x) >> 22) & 0x0F)
#define   C_00B01C_LOCK_LOW_THRESHOLD                                 0xFC3FFFFF
/*     */
#define R_00B020_SPI_SHADER_PGM_LO_PS                                   0x00B020
#define R_00B024_SPI_SHADER_PGM_HI_PS                                   0x00B024
#define   S_00B024_MEM_BASE(x)                                        (((unsigned)(x) & 0xFF) << 0)
#define   G_00B024_MEM_BASE(x)                                        (((x) >> 0) & 0xFF)
#define   C_00B024_MEM_BASE                                           0xFFFFFF00
#define R_00B028_SPI_SHADER_PGM_RSRC1_PS                                0x00B028
#define   S_00B028_VGPRS(x)                                           (((unsigned)(x) & 0x3F) << 0)
#define   G_00B028_VGPRS(x)                                           (((x) >> 0) & 0x3F)
#define   C_00B028_VGPRS                                              0xFFFFFFC0
#define   S_00B028_SGPRS(x)                                           (((unsigned)(x) & 0x0F) << 6)
#define   G_00B028_SGPRS(x)                                           (((x) >> 6) & 0x0F)
#define   C_00B028_SGPRS                                              0xFFFFFC3F
#define   S_00B028_PRIORITY(x)                                        (((unsigned)(x) & 0x03) << 10)
#define   G_00B028_PRIORITY(x)                                        (((x) >> 10) & 0x03)
#define   C_00B028_PRIORITY                                           0xFFFFF3FF
#define   S_00B028_FLOAT_MODE(x)                                      (((unsigned)(x) & 0xFF) << 12)
#define   G_00B028_FLOAT_MODE(x)                                      (((x) >> 12) & 0xFF)
#define   C_00B028_FLOAT_MODE                                         0xFFF00FFF
#define     V_00B028_FP_32_DENORMS					0x30
#define     V_00B028_FP_64_DENORMS					0xc0
#define     V_00B028_FP_ALL_DENORMS					0xf0
#define   S_00B028_PRIV(x)                                            (((unsigned)(x) & 0x1) << 20)
#define   G_00B028_PRIV(x)                                            (((x) >> 20) & 0x1)
#define   C_00B028_PRIV                                               0xFFEFFFFF
#define   S_00B028_DX10_CLAMP(x)                                      (((unsigned)(x) & 0x1) << 21)
#define   G_00B028_DX10_CLAMP(x)                                      (((x) >> 21) & 0x1)
#define   C_00B028_DX10_CLAMP                                         0xFFDFFFFF
#define   S_00B028_DEBUG_MODE(x)                                      (((unsigned)(x) & 0x1) << 22)
#define   G_00B028_DEBUG_MODE(x)                                      (((x) >> 22) & 0x1)
#define   C_00B028_DEBUG_MODE                                         0xFFBFFFFF
#define   S_00B028_IEEE_MODE(x)                                       (((unsigned)(x) & 0x1) << 23)
#define   G_00B028_IEEE_MODE(x)                                       (((x) >> 23) & 0x1)
#define   C_00B028_IEEE_MODE                                          0xFF7FFFFF
#define   S_00B028_CU_GROUP_DISABLE(x)                                (((unsigned)(x) & 0x1) << 24)
#define   G_00B028_CU_GROUP_DISABLE(x)                                (((x) >> 24) & 0x1)
#define   C_00B028_CU_GROUP_DISABLE                                   0xFEFFFFFF
/* CIK */
#define   S_00B028_CACHE_CTL(x)                                       (((unsigned)(x) & 0x07) << 25)
#define   G_00B028_CACHE_CTL(x)                                       (((x) >> 25) & 0x07)
#define   C_00B028_CACHE_CTL                                          0xF1FFFFFF
#define   S_00B028_CDBG_USER(x)                                       (((unsigned)(x) & 0x1) << 28)
#define   G_00B028_CDBG_USER(x)                                       (((x) >> 28) & 0x1)
#define   C_00B028_CDBG_USER                                          0xEFFFFFFF
/*    */
#define R_00B02C_SPI_SHADER_PGM_RSRC2_PS                                0x00B02C
#define   S_00B02C_SCRATCH_EN(x)                                      (((unsigned)(x) & 0x1) << 0)
#define   G_00B02C_SCRATCH_EN(x)                                      (((x) >> 0) & 0x1)
#define   C_00B02C_SCRATCH_EN                                         0xFFFFFFFE
#define   S_00B02C_USER_SGPR(x)                                       (((unsigned)(x) & 0x1F) << 1)
#define   G_00B02C_USER_SGPR(x)                                       (((x) >> 1) & 0x1F)
#define   C_00B02C_USER_SGPR                                          0xFFFFFFC1
#define   S_00B02C_TRAP_PRESENT(x)                                    (((unsigned)(x) & 0x1) << 6)
#define   G_00B02C_TRAP_PRESENT(x)                                    (((x) >> 6) & 0x1)
#define   C_00B02C_TRAP_PRESENT                                       0xFFFFFFBF
#define   S_00B02C_WAVE_CNT_EN(x)                                     (((unsigned)(x) & 0x1) << 7)
#define   G_00B02C_WAVE_CNT_EN(x)                                     (((x) >> 7) & 0x1)
#define   C_00B02C_WAVE_CNT_EN                                        0xFFFFFF7F
#define   S_00B02C_EXTRA_LDS_SIZE(x)                                  (((unsigned)(x) & 0xFF) << 8)
#define   G_00B02C_EXTRA_LDS_SIZE(x)                                  (((x) >> 8) & 0xFF)
#define   C_00B02C_EXTRA_LDS_SIZE                                     0xFFFF00FF
#define   S_00B02C_EXCP_EN(x)                                         (((unsigned)(x) & 0x7F) << 16) /* mask is 0x1FF on CIK */
#define   G_00B02C_EXCP_EN(x)                                         (((x) >> 16) & 0x7F) /* mask is 0x1FF on CIK */
#define   C_00B02C_EXCP_EN                                            0xFF80FFFF /* mask is 0x1FF on CIK */
#define   S_00B02C_EXCP_EN_CIK(x)                                     (((unsigned)(x) & 0x1FF) << 16)
#define   G_00B02C_EXCP_EN_CIK(x)                                     (((x) >> 16) & 0x1FF)
#define   C_00B02C_EXCP_EN_CIK                                        0xFE00FFFF
#define R_00B030_SPI_SHADER_USER_DATA_PS_0                              0x00B030
#define R_00B034_SPI_SHADER_USER_DATA_PS_1                              0x00B034
#define R_00B038_SPI_SHADER_USER_DATA_PS_2                              0x00B038
#define R_00B03C_SPI_SHADER_USER_DATA_PS_3                              0x00B03C
#define R_00B040_SPI_SHADER_USER_DATA_PS_4                              0x00B040
#define R_00B044_SPI_SHADER_USER_DATA_PS_5                              0x00B044
#define R_00B048_SPI_SHADER_USER_DATA_PS_6                              0x00B048
#define R_00B04C_SPI_SHADER_USER_DATA_PS_7                              0x00B04C
#define R_00B050_SPI_SHADER_USER_DATA_PS_8                              0x00B050
#define R_00B054_SPI_SHADER_USER_DATA_PS_9                              0x00B054
#define R_00B058_SPI_SHADER_USER_DATA_PS_10                             0x00B058
#define R_00B05C_SPI_SHADER_USER_DATA_PS_11                             0x00B05C
#define R_00B060_SPI_SHADER_USER_DATA_PS_12                             0x00B060
#define R_00B064_SPI_SHADER_USER_DATA_PS_13                             0x00B064
#define R_00B068_SPI_SHADER_USER_DATA_PS_14                             0x00B068
#define R_00B06C_SPI_SHADER_USER_DATA_PS_15                             0x00B06C
#define R_00B100_SPI_SHADER_TBA_LO_VS                                   0x00B100
#define R_00B104_SPI_SHADER_TBA_HI_VS                                   0x00B104
#define   S_00B104_MEM_BASE(x)                                        (((unsigned)(x) & 0xFF) << 0)
#define   G_00B104_MEM_BASE(x)                                        (((x) >> 0) & 0xFF)
#define   C_00B104_MEM_BASE                                           0xFFFFFF00
#define R_00B108_SPI_SHADER_TMA_LO_VS                                   0x00B108
#define R_00B10C_SPI_SHADER_TMA_HI_VS                                   0x00B10C
#define   S_00B10C_MEM_BASE(x)                                        (((unsigned)(x) & 0xFF) << 0)
#define   G_00B10C_MEM_BASE(x)                                        (((x) >> 0) & 0xFF)
#define   C_00B10C_MEM_BASE                                           0xFFFFFF00
/* CIK */
#define R_00B118_SPI_SHADER_PGM_RSRC3_VS                                0x00B118
#define   S_00B118_CU_EN(x)                                           (((unsigned)(x) & 0xFFFF) << 0)
#define   G_00B118_CU_EN(x)                                           (((x) >> 0) & 0xFFFF)
#define   C_00B118_CU_EN                                              0xFFFF0000
#define   S_00B118_WAVE_LIMIT(x)                                      (((unsigned)(x) & 0x3F) << 16)
#define   G_00B118_WAVE_LIMIT(x)                                      (((x) >> 16) & 0x3F)
#define   C_00B118_WAVE_LIMIT                                         0xFFC0FFFF
#define   S_00B118_LOCK_LOW_THRESHOLD(x)                              (((unsigned)(x) & 0x0F) << 22)
#define   G_00B118_LOCK_LOW_THRESHOLD(x)                              (((x) >> 22) & 0x0F)
#define   C_00B118_LOCK_LOW_THRESHOLD                                 0xFC3FFFFF
#define R_00B11C_SPI_SHADER_LATE_ALLOC_VS                               0x00B11C
#define   S_00B11C_LIMIT(x)                                           (((unsigned)(x) & 0x3F) << 0)
#define   G_00B11C_LIMIT(x)                                           (((x) >> 0) & 0x3F)
#define   C_00B11C_LIMIT                                              0xFFFFFFC0
/*     */
#define R_00B120_SPI_SHADER_PGM_LO_VS                                   0x00B120
#define R_00B124_SPI_SHADER_PGM_HI_VS                                   0x00B124
#define   S_00B124_MEM_BASE(x)                                        (((unsigned)(x) & 0xFF) << 0)
#define   G_00B124_MEM_BASE(x)                                        (((x) >> 0) & 0xFF)
#define   C_00B124_MEM_BASE                                           0xFFFFFF00
#define R_00B128_SPI_SHADER_PGM_RSRC1_VS                                0x00B128
#define   S_00B128_VGPRS(x)                                           (((unsigned)(x) & 0x3F) << 0)
#define   G_00B128_VGPRS(x)                                           (((x) >> 0) & 0x3F)
#define   C_00B128_VGPRS                                              0xFFFFFFC0
#define   S_00B128_SGPRS(x)                                           (((unsigned)(x) & 0x0F) << 6)
#define   G_00B128_SGPRS(x)                                           (((x) >> 6) & 0x0F)
#define   C_00B128_SGPRS                                              0xFFFFFC3F
#define   S_00B128_PRIORITY(x)                                        (((unsigned)(x) & 0x03) << 10)
#define   G_00B128_PRIORITY(x)                                        (((x) >> 10) & 0x03)
#define   C_00B128_PRIORITY                                           0xFFFFF3FF
#define   S_00B128_FLOAT_MODE(x)                                      (((unsigned)(x) & 0xFF) << 12)
#define   G_00B128_FLOAT_MODE(x)                                      (((x) >> 12) & 0xFF)
#define   C_00B128_FLOAT_MODE                                         0xFFF00FFF
#define   S_00B128_PRIV(x)                                            (((unsigned)(x) & 0x1) << 20)
#define   G_00B128_PRIV(x)                                            (((x) >> 20) & 0x1)
#define   C_00B128_PRIV                                               0xFFEFFFFF
#define   S_00B128_DX10_CLAMP(x)                                      (((unsigned)(x) & 0x1) << 21)
#define   G_00B128_DX10_CLAMP(x)                                      (((x) >> 21) & 0x1)
#define   C_00B128_DX10_CLAMP                                         0xFFDFFFFF
#define   S_00B128_DEBUG_MODE(x)                                      (((unsigned)(x) & 0x1) << 22)
#define   G_00B128_DEBUG_MODE(x)                                      (((x) >> 22) & 0x1)
#define   C_00B128_DEBUG_MODE                                         0xFFBFFFFF
#define   S_00B128_IEEE_MODE(x)                                       (((unsigned)(x) & 0x1) << 23)
#define   G_00B128_IEEE_MODE(x)                                       (((x) >> 23) & 0x1)
#define   C_00B128_IEEE_MODE                                          0xFF7FFFFF
#define   S_00B128_VGPR_COMP_CNT(x)                                   (((unsigned)(x) & 0x03) << 24)
#define   G_00B128_VGPR_COMP_CNT(x)                                   (((x) >> 24) & 0x03)
#define   C_00B128_VGPR_COMP_CNT                                      0xFCFFFFFF
#define   S_00B128_CU_GROUP_ENABLE(x)                                 (((unsigned)(x) & 0x1) << 26)
#define   G_00B128_CU_GROUP_ENABLE(x)                                 (((x) >> 26) & 0x1)
#define   C_00B128_CU_GROUP_ENABLE                                    0xFBFFFFFF
/* CIK */
#define   S_00B128_CACHE_CTL(x)                                       (((unsigned)(x) & 0x07) << 27)
#define   G_00B128_CACHE_CTL(x)                                       (((x) >> 27) & 0x07)
#define   C_00B128_CACHE_CTL                                          0xC7FFFFFF
#define   S_00B128_CDBG_USER(x)                                       (((unsigned)(x) & 0x1) << 30)
#define   G_00B128_CDBG_USER(x)                                       (((x) >> 30) & 0x1)
#define   C_00B128_CDBG_USER                                          0xBFFFFFFF
/*    */
#define R_00B12C_SPI_SHADER_PGM_RSRC2_VS                                0x00B12C
#define   S_00B12C_SCRATCH_EN(x)                                      (((unsigned)(x) & 0x1) << 0)
#define   G_00B12C_SCRATCH_EN(x)                                      (((x) >> 0) & 0x1)
#define   C_00B12C_SCRATCH_EN                                         0xFFFFFFFE
#define   S_00B12C_USER_SGPR(x)                                       (((unsigned)(x) & 0x1F) << 1)
#define   G_00B12C_USER_SGPR(x)                                       (((x) >> 1) & 0x1F)
#define   C_00B12C_USER_SGPR                                          0xFFFFFFC1
#define   S_00B12C_TRAP_PRESENT(x)                                    (((unsigned)(x) & 0x1) << 6)
#define   G_00B12C_TRAP_PRESENT(x)                                    (((x) >> 6) & 0x1)
#define   C_00B12C_TRAP_PRESENT                                       0xFFFFFFBF
#define   S_00B12C_OC_LDS_EN(x)                                       (((unsigned)(x) & 0x1) << 7)
#define   G_00B12C_OC_LDS_EN(x)                                       (((x) >> 7) & 0x1)
#define   C_00B12C_OC_LDS_EN                                          0xFFFFFF7F
#define   S_00B12C_SO_BASE0_EN(x)                                     (((unsigned)(x) & 0x1) << 8)
#define   G_00B12C_SO_BASE0_EN(x)                                     (((x) >> 8) & 0x1)
#define   C_00B12C_SO_BASE0_EN                                        0xFFFFFEFF
#define   S_00B12C_SO_BASE1_EN(x)                                     (((unsigned)(x) & 0x1) << 9)
#define   G_00B12C_SO_BASE1_EN(x)                                     (((x) >> 9) & 0x1)
#define   C_00B12C_SO_BASE1_EN                                        0xFFFFFDFF
#define   S_00B12C_SO_BASE2_EN(x)                                     (((unsigned)(x) & 0x1) << 10)
#define   G_00B12C_SO_BASE2_EN(x)                                     (((x) >> 10) & 0x1)
#define   C_00B12C_SO_BASE2_EN                                        0xFFFFFBFF
#define   S_00B12C_SO_BASE3_EN(x)                                     (((unsigned)(x) & 0x1) << 11)
#define   G_00B12C_SO_BASE3_EN(x)                                     (((x) >> 11) & 0x1)
#define   C_00B12C_SO_BASE3_EN                                        0xFFFFF7FF
#define   S_00B12C_SO_EN(x)                                           (((unsigned)(x) & 0x1) << 12)
#define   G_00B12C_SO_EN(x)                                           (((x) >> 12) & 0x1)
#define   C_00B12C_SO_EN                                              0xFFFFEFFF
#define   S_00B12C_EXCP_EN(x)                                         (((unsigned)(x) & 0x7F) << 13) /* mask is 0x1FF on CIK */
#define   G_00B12C_EXCP_EN(x)                                         (((x) >> 13) & 0x7F) /* mask is 0x1FF on CIK */
#define   C_00B12C_EXCP_EN                                            0xFFF01FFF /* mask is 0x1FF on CIK */
#define   S_00B12C_EXCP_EN_CIK(x)                                     (((unsigned)(x) & 0x1FF) << 13)
#define   G_00B12C_EXCP_EN_CIK(x)                                     (((x) >> 13) & 0x1FF)
#define   C_00B12C_EXCP_EN_CIK                                        0xFFC01FFF
/* VI */
#define   S_00B12C_DISPATCH_DRAW_EN(x)                                (((unsigned)(x) & 0x1) << 24)
#define   G_00B12C_DISPATCH_DRAW_EN(x)                                (((x) >> 24) & 0x1)
#define   C_00B12C_DISPATCH_DRAW_EN                                   0xFEFFFFFF
/*    */
#define R_00B130_SPI_SHADER_USER_DATA_VS_0                              0x00B130
#define R_00B134_SPI_SHADER_USER_DATA_VS_1                              0x00B134
#define R_00B138_SPI_SHADER_USER_DATA_VS_2                              0x00B138
#define R_00B13C_SPI_SHADER_USER_DATA_VS_3                              0x00B13C
#define R_00B140_SPI_SHADER_USER_DATA_VS_4                              0x00B140
#define R_00B144_SPI_SHADER_USER_DATA_VS_5                              0x00B144
#define R_00B148_SPI_SHADER_USER_DATA_VS_6                              0x00B148
#define R_00B14C_SPI_SHADER_USER_DATA_VS_7                              0x00B14C
#define R_00B150_SPI_SHADER_USER_DATA_VS_8                              0x00B150
#define R_00B154_SPI_SHADER_USER_DATA_VS_9                              0x00B154
#define R_00B158_SPI_SHADER_USER_DATA_VS_10                             0x00B158
#define R_00B15C_SPI_SHADER_USER_DATA_VS_11                             0x00B15C
#define R_00B160_SPI_SHADER_USER_DATA_VS_12                             0x00B160
#define R_00B164_SPI_SHADER_USER_DATA_VS_13                             0x00B164
#define R_00B168_SPI_SHADER_USER_DATA_VS_14                             0x00B168
#define R_00B16C_SPI_SHADER_USER_DATA_VS_15                             0x00B16C
#define R_00B200_SPI_SHADER_TBA_LO_GS                                   0x00B200
#define R_00B204_SPI_SHADER_TBA_HI_GS                                   0x00B204
#define   S_00B204_MEM_BASE(x)                                        (((unsigned)(x) & 0xFF) << 0)
#define   G_00B204_MEM_BASE(x)                                        (((x) >> 0) & 0xFF)
#define   C_00B204_MEM_BASE                                           0xFFFFFF00
#define R_00B208_SPI_SHADER_TMA_LO_GS                                   0x00B208
#define R_00B20C_SPI_SHADER_TMA_HI_GS                                   0x00B20C
#define   S_00B20C_MEM_BASE(x)                                        (((unsigned)(x) & 0xFF) << 0)
#define   G_00B20C_MEM_BASE(x)                                        (((x) >> 0) & 0xFF)
#define   C_00B20C_MEM_BASE                                           0xFFFFFF00
/* CIK */
#define R_00B21C_SPI_SHADER_PGM_RSRC3_GS                                0x00B21C
#define   S_00B21C_CU_EN(x)                                           (((unsigned)(x) & 0xFFFF) << 0)
#define   G_00B21C_CU_EN(x)                                           (((x) >> 0) & 0xFFFF)
#define   C_00B21C_CU_EN                                              0xFFFF0000
#define   S_00B21C_WAVE_LIMIT(x)                                      (((unsigned)(x) & 0x3F) << 16)
#define   G_00B21C_WAVE_LIMIT(x)                                      (((x) >> 16) & 0x3F)
#define   C_00B21C_WAVE_LIMIT                                         0xFFC0FFFF
#define   S_00B21C_LOCK_LOW_THRESHOLD(x)                              (((unsigned)(x) & 0x0F) << 22)
#define   G_00B21C_LOCK_LOW_THRESHOLD(x)                              (((x) >> 22) & 0x0F)
#define   C_00B21C_LOCK_LOW_THRESHOLD                                 0xFC3FFFFF
/*     */
/* VI */
#define   S_00B21C_GROUP_FIFO_DEPTH(x)                                (((unsigned)(x) & 0x3F) << 26)
#define   G_00B21C_GROUP_FIFO_DEPTH(x)                                (((x) >> 26) & 0x3F)
#define   C_00B21C_GROUP_FIFO_DEPTH                                   0x03FFFFFF
/*    */
#define R_00B220_SPI_SHADER_PGM_LO_GS                                   0x00B220
#define R_00B224_SPI_SHADER_PGM_HI_GS                                   0x00B224
#define   S_00B224_MEM_BASE(x)                                        (((unsigned)(x) & 0xFF) << 0)
#define   G_00B224_MEM_BASE(x)                                        (((x) >> 0) & 0xFF)
#define   C_00B224_MEM_BASE                                           0xFFFFFF00
#define R_00B228_SPI_SHADER_PGM_RSRC1_GS                                0x00B228
#define   S_00B228_VGPRS(x)                                           (((unsigned)(x) & 0x3F) << 0)
#define   G_00B228_VGPRS(x)                                           (((x) >> 0) & 0x3F)
#define   C_00B228_VGPRS                                              0xFFFFFFC0
#define   S_00B228_SGPRS(x)                                           (((unsigned)(x) & 0x0F) << 6)
#define   G_00B228_SGPRS(x)                                           (((x) >> 6) & 0x0F)
#define   C_00B228_SGPRS                                              0xFFFFFC3F
#define   S_00B228_PRIORITY(x)                                        (((unsigned)(x) & 0x03) << 10)
#define   G_00B228_PRIORITY(x)                                        (((x) >> 10) & 0x03)
#define   C_00B228_PRIORITY                                           0xFFFFF3FF
#define   S_00B228_FLOAT_MODE(x)                                      (((unsigned)(x) & 0xFF) << 12)
#define   G_00B228_FLOAT_MODE(x)                                      (((x) >> 12) & 0xFF)
#define   C_00B228_FLOAT_MODE                                         0xFFF00FFF
#define   S_00B228_PRIV(x)                                            (((unsigned)(x) & 0x1) << 20)
#define   G_00B228_PRIV(x)                                            (((x) >> 20) & 0x1)
#define   C_00B228_PRIV                                               0xFFEFFFFF
#define   S_00B228_DX10_CLAMP(x)                                      (((unsigned)(x) & 0x1) << 21)
#define   G_00B228_DX10_CLAMP(x)                                      (((x) >> 21) & 0x1)
#define   C_00B228_DX10_CLAMP                                         0xFFDFFFFF
#define   S_00B228_DEBUG_MODE(x)                                      (((unsigned)(x) & 0x1) << 22)
#define   G_00B228_DEBUG_MODE(x)                                      (((x) >> 22) & 0x1)
#define   C_00B228_DEBUG_MODE                                         0xFFBFFFFF
#define   S_00B228_IEEE_MODE(x)                                       (((unsigned)(x) & 0x1) << 23)
#define   G_00B228_IEEE_MODE(x)                                       (((x) >> 23) & 0x1)
#define   C_00B228_IEEE_MODE                                          0xFF7FFFFF
#define   S_00B228_CU_GROUP_ENABLE(x)                                 (((unsigned)(x) & 0x1) << 24)
#define   G_00B228_CU_GROUP_ENABLE(x)                                 (((x) >> 24) & 0x1)
#define   C_00B228_CU_GROUP_ENABLE                                    0xFEFFFFFF
/* CIK */
#define   S_00B228_CACHE_CTL(x)                                       (((unsigned)(x) & 0x07) << 25)
#define   G_00B228_CACHE_CTL(x)                                       (((x) >> 25) & 0x07)
#define   C_00B228_CACHE_CTL                                          0xF1FFFFFF
#define   S_00B228_CDBG_USER(x)                                       (((unsigned)(x) & 0x1) << 28)
#define   G_00B228_CDBG_USER(x)                                       (((x) >> 28) & 0x1)
#define   C_00B228_CDBG_USER                                          0xEFFFFFFF
/*     */
#define R_00B22C_SPI_SHADER_PGM_RSRC2_GS                                0x00B22C
#define   S_00B22C_SCRATCH_EN(x)                                      (((unsigned)(x) & 0x1) << 0)
#define   G_00B22C_SCRATCH_EN(x)                                      (((x) >> 0) & 0x1)
#define   C_00B22C_SCRATCH_EN                                         0xFFFFFFFE
#define   S_00B22C_USER_SGPR(x)                                       (((unsigned)(x) & 0x1F) << 1)
#define   G_00B22C_USER_SGPR(x)                                       (((x) >> 1) & 0x1F)
#define   C_00B22C_USER_SGPR                                          0xFFFFFFC1
#define   S_00B22C_TRAP_PRESENT(x)                                    (((unsigned)(x) & 0x1) << 6)
#define   G_00B22C_TRAP_PRESENT(x)                                    (((x) >> 6) & 0x1)
#define   C_00B22C_TRAP_PRESENT                                       0xFFFFFFBF
#define   S_00B22C_EXCP_EN(x)                                         (((unsigned)(x) & 0x7F) << 7) /* mask is 0x1FF on CIK */
#define   G_00B22C_EXCP_EN(x)                                         (((x) >> 7) & 0x7F) /* mask is 0x1FF on CIK */
#define   C_00B22C_EXCP_EN                                            0xFFFFC07F /* mask is 0x1FF on CIK */
#define   S_00B22C_EXCP_EN_CIK(x)                                     (((unsigned)(x) & 0x1FF) << 7)
#define   G_00B22C_EXCP_EN_CIK(x)                                     (((x) >> 7) & 0x1FF)
#define   C_00B22C_EXCP_EN_CIK                                        0xFFFF007F
#define R_00B230_SPI_SHADER_USER_DATA_GS_0                              0x00B230
#define R_00B234_SPI_SHADER_USER_DATA_GS_1                              0x00B234
#define R_00B238_SPI_SHADER_USER_DATA_GS_2                              0x00B238
#define R_00B23C_SPI_SHADER_USER_DATA_GS_3                              0x00B23C
#define R_00B240_SPI_SHADER_USER_DATA_GS_4                              0x00B240
#define R_00B244_SPI_SHADER_USER_DATA_GS_5                              0x00B244
#define R_00B248_SPI_SHADER_USER_DATA_GS_6                              0x00B248
#define R_00B24C_SPI_SHADER_USER_DATA_GS_7                              0x00B24C
#define R_00B250_SPI_SHADER_USER_DATA_GS_8                              0x00B250
#define R_00B254_SPI_SHADER_USER_DATA_GS_9                              0x00B254
#define R_00B258_SPI_SHADER_USER_DATA_GS_10                             0x00B258
#define R_00B25C_SPI_SHADER_USER_DATA_GS_11                             0x00B25C
#define R_00B260_SPI_SHADER_USER_DATA_GS_12                             0x00B260
#define R_00B264_SPI_SHADER_USER_DATA_GS_13                             0x00B264
#define R_00B268_SPI_SHADER_USER_DATA_GS_14                             0x00B268
#define R_00B26C_SPI_SHADER_USER_DATA_GS_15                             0x00B26C
#define R_00B300_SPI_SHADER_TBA_LO_ES                                   0x00B300
#define R_00B304_SPI_SHADER_TBA_HI_ES                                   0x00B304
#define   S_00B304_MEM_BASE(x)                                        (((unsigned)(x) & 0xFF) << 0)
#define   G_00B304_MEM_BASE(x)                                        (((x) >> 0) & 0xFF)
#define   C_00B304_MEM_BASE                                           0xFFFFFF00
#define R_00B308_SPI_SHADER_TMA_LO_ES                                   0x00B308
#define R_00B30C_SPI_SHADER_TMA_HI_ES                                   0x00B30C
#define   S_00B30C_MEM_BASE(x)                                        (((unsigned)(x) & 0xFF) << 0)
#define   G_00B30C_MEM_BASE(x)                                        (((x) >> 0) & 0xFF)
#define   C_00B30C_MEM_BASE                                           0xFFFFFF00
/* CIK */
#define R_00B31C_SPI_SHADER_PGM_RSRC3_ES                                0x00B31C
#define   S_00B31C_CU_EN(x)                                           (((unsigned)(x) & 0xFFFF) << 0)
#define   G_00B31C_CU_EN(x)                                           (((x) >> 0) & 0xFFFF)
#define   C_00B31C_CU_EN                                              0xFFFF0000
#define   S_00B31C_WAVE_LIMIT(x)                                      (((unsigned)(x) & 0x3F) << 16)
#define   G_00B31C_WAVE_LIMIT(x)                                      (((x) >> 16) & 0x3F)
#define   C_00B31C_WAVE_LIMIT                                         0xFFC0FFFF
#define   S_00B31C_LOCK_LOW_THRESHOLD(x)                              (((unsigned)(x) & 0x0F) << 22)
#define   G_00B31C_LOCK_LOW_THRESHOLD(x)                              (((x) >> 22) & 0x0F)
#define   C_00B31C_LOCK_LOW_THRESHOLD                                 0xFC3FFFFF
/*     */
/* VI */
#define   S_00B31C_GROUP_FIFO_DEPTH(x)                                (((unsigned)(x) & 0x3F) << 26)
#define   G_00B31C_GROUP_FIFO_DEPTH(x)                                (((x) >> 26) & 0x3F)
#define   C_00B31C_GROUP_FIFO_DEPTH                                   0x03FFFFFF
/*    */
#define R_00B320_SPI_SHADER_PGM_LO_ES                                   0x00B320
#define R_00B324_SPI_SHADER_PGM_HI_ES                                   0x00B324
#define   S_00B324_MEM_BASE(x)                                        (((unsigned)(x) & 0xFF) << 0)
#define   G_00B324_MEM_BASE(x)                                        (((x) >> 0) & 0xFF)
#define   C_00B324_MEM_BASE                                           0xFFFFFF00
#define R_00B328_SPI_SHADER_PGM_RSRC1_ES                                0x00B328
#define   S_00B328_VGPRS(x)                                           (((unsigned)(x) & 0x3F) << 0)
#define   G_00B328_VGPRS(x)                                           (((x) >> 0) & 0x3F)
#define   C_00B328_VGPRS                                              0xFFFFFFC0
#define   S_00B328_SGPRS(x)                                           (((unsigned)(x) & 0x0F) << 6)
#define   G_00B328_SGPRS(x)                                           (((x) >> 6) & 0x0F)
#define   C_00B328_SGPRS                                              0xFFFFFC3F
#define   S_00B328_PRIORITY(x)                                        (((unsigned)(x) & 0x03) << 10)
#define   G_00B328_PRIORITY(x)                                        (((x) >> 10) & 0x03)
#define   C_00B328_PRIORITY                                           0xFFFFF3FF
#define   S_00B328_FLOAT_MODE(x)                                      (((unsigned)(x) & 0xFF) << 12)
#define   G_00B328_FLOAT_MODE(x)                                      (((x) >> 12) & 0xFF)
#define   C_00B328_FLOAT_MODE                                         0xFFF00FFF
#define   S_00B328_PRIV(x)                                            (((unsigned)(x) & 0x1) << 20)
#define   G_00B328_PRIV(x)                                            (((x) >> 20) & 0x1)
#define   C_00B328_PRIV                                               0xFFEFFFFF
#define   S_00B328_DX10_CLAMP(x)                                      (((unsigned)(x) & 0x1) << 21)
#define   G_00B328_DX10_CLAMP(x)                                      (((x) >> 21) & 0x1)
#define   C_00B328_DX10_CLAMP                                         0xFFDFFFFF
#define   S_00B328_DEBUG_MODE(x)                                      (((unsigned)(x) & 0x1) << 22)
#define   G_00B328_DEBUG_MODE(x)                                      (((x) >> 22) & 0x1)
#define   C_00B328_DEBUG_MODE                                         0xFFBFFFFF
#define   S_00B328_IEEE_MODE(x)                                       (((unsigned)(x) & 0x1) << 23)
#define   G_00B328_IEEE_MODE(x)                                       (((x) >> 23) & 0x1)
#define   C_00B328_IEEE_MODE                                          0xFF7FFFFF
#define   S_00B328_VGPR_COMP_CNT(x)                                   (((unsigned)(x) & 0x03) << 24)
#define   G_00B328_VGPR_COMP_CNT(x)                                   (((x) >> 24) & 0x03)
#define   C_00B328_VGPR_COMP_CNT                                      0xFCFFFFFF
#define   S_00B328_CU_GROUP_ENABLE(x)                                 (((unsigned)(x) & 0x1) << 26)
#define   G_00B328_CU_GROUP_ENABLE(x)                                 (((x) >> 26) & 0x1)
#define   C_00B328_CU_GROUP_ENABLE                                    0xFBFFFFFF
/* CIK */
#define   S_00B328_CACHE_CTL(x)                                       (((unsigned)(x) & 0x07) << 27)
#define   G_00B328_CACHE_CTL(x)                                       (((x) >> 27) & 0x07)
#define   C_00B328_CACHE_CTL                                          0xC7FFFFFF
#define   S_00B328_CDBG_USER(x)                                       (((unsigned)(x) & 0x1) << 30)
#define   G_00B328_CDBG_USER(x)                                       (((x) >> 30) & 0x1)
#define   C_00B328_CDBG_USER                                          0xBFFFFFFF
/*     */
#define R_00B32C_SPI_SHADER_PGM_RSRC2_ES                                0x00B32C
#define   S_00B32C_SCRATCH_EN(x)                                      (((unsigned)(x) & 0x1) << 0)
#define   G_00B32C_SCRATCH_EN(x)                                      (((x) >> 0) & 0x1)
#define   C_00B32C_SCRATCH_EN                                         0xFFFFFFFE
#define   S_00B32C_USER_SGPR(x)                                       (((unsigned)(x) & 0x1F) << 1)
#define   G_00B32C_USER_SGPR(x)                                       (((x) >> 1) & 0x1F)
#define   C_00B32C_USER_SGPR                                          0xFFFFFFC1
#define   S_00B32C_TRAP_PRESENT(x)                                    (((unsigned)(x) & 0x1) << 6)
#define   G_00B32C_TRAP_PRESENT(x)                                    (((x) >> 6) & 0x1)
#define   C_00B32C_TRAP_PRESENT                                       0xFFFFFFBF
#define   S_00B32C_OC_LDS_EN(x)                                       (((unsigned)(x) & 0x1) << 7)
#define   G_00B32C_OC_LDS_EN(x)                                       (((x) >> 7) & 0x1)
#define   C_00B32C_OC_LDS_EN                                          0xFFFFFF7F
#define   S_00B32C_EXCP_EN(x)                                         (((unsigned)(x) & 0x7F) << 8) /* mask is 0x1FF on CIK */
#define   G_00B32C_EXCP_EN(x)                                         (((x) >> 8) & 0x7F) /* mask is 0x1FF on CIK */
#define   C_00B32C_EXCP_EN                                            0xFFFF80FF /* mask is 0x1FF on CIK */
#define   S_00B32C_LDS_SIZE(x)                                        (((unsigned)(x) & 0x1FF) << 20) /* CIK, for on-chip GS */
#define   G_00B32C_LDS_SIZE(x)                                        (((x) >> 20) & 0x1FF) /* CIK, for on-chip GS */
#define   C_00B32C_LDS_SIZE                                           0xE00FFFFF /* CIK, for on-chip GS */
#define R_00B330_SPI_SHADER_USER_DATA_ES_0                              0x00B330
#define R_00B334_SPI_SHADER_USER_DATA_ES_1                              0x00B334
#define R_00B338_SPI_SHADER_USER_DATA_ES_2                              0x00B338
#define R_00B33C_SPI_SHADER_USER_DATA_ES_3                              0x00B33C
#define R_00B340_SPI_SHADER_USER_DATA_ES_4                              0x00B340
#define R_00B344_SPI_SHADER_USER_DATA_ES_5                              0x00B344
#define R_00B348_SPI_SHADER_USER_DATA_ES_6                              0x00B348
#define R_00B34C_SPI_SHADER_USER_DATA_ES_7                              0x00B34C
#define R_00B350_SPI_SHADER_USER_DATA_ES_8                              0x00B350
#define R_00B354_SPI_SHADER_USER_DATA_ES_9                              0x00B354
#define R_00B358_SPI_SHADER_USER_DATA_ES_10                             0x00B358
#define R_00B35C_SPI_SHADER_USER_DATA_ES_11                             0x00B35C
#define R_00B360_SPI_SHADER_USER_DATA_ES_12                             0x00B360
#define R_00B364_SPI_SHADER_USER_DATA_ES_13                             0x00B364
#define R_00B368_SPI_SHADER_USER_DATA_ES_14                             0x00B368
#define R_00B36C_SPI_SHADER_USER_DATA_ES_15                             0x00B36C
#define R_00B400_SPI_SHADER_TBA_LO_HS                                   0x00B400
#define R_00B404_SPI_SHADER_TBA_HI_HS                                   0x00B404
#define   S_00B404_MEM_BASE(x)                                        (((unsigned)(x) & 0xFF) << 0)
#define   G_00B404_MEM_BASE(x)                                        (((x) >> 0) & 0xFF)
#define   C_00B404_MEM_BASE                                           0xFFFFFF00
#define R_00B408_SPI_SHADER_TMA_LO_HS                                   0x00B408
#define R_00B40C_SPI_SHADER_TMA_HI_HS                                   0x00B40C
#define   S_00B40C_MEM_BASE(x)                                        (((unsigned)(x) & 0xFF) << 0)
#define   G_00B40C_MEM_BASE(x)                                        (((x) >> 0) & 0xFF)
#define   C_00B40C_MEM_BASE                                           0xFFFFFF00
/* CIK */
#define R_00B41C_SPI_SHADER_PGM_RSRC3_HS                                0x00B41C
#define   S_00B41C_WAVE_LIMIT(x)                                      (((unsigned)(x) & 0x3F) << 0)
#define   G_00B41C_WAVE_LIMIT(x)                                      (((x) >> 0) & 0x3F)
#define   C_00B41C_WAVE_LIMIT                                         0xFFFFFFC0
#define   S_00B41C_LOCK_LOW_THRESHOLD(x)                              (((unsigned)(x) & 0x0F) << 6)
#define   G_00B41C_LOCK_LOW_THRESHOLD(x)                              (((x) >> 6) & 0x0F)
#define   C_00B41C_LOCK_LOW_THRESHOLD                                 0xFFFFFC3F
/*     */
/* VI */
#define   S_00B41C_GROUP_FIFO_DEPTH(x)                                (((unsigned)(x) & 0x3F) << 10)
#define   G_00B41C_GROUP_FIFO_DEPTH(x)                                (((x) >> 10) & 0x3F)
#define   C_00B41C_GROUP_FIFO_DEPTH                                   0xFFFF03FF
/*    */
#define R_00B420_SPI_SHADER_PGM_LO_HS                                   0x00B420
#define R_00B424_SPI_SHADER_PGM_HI_HS                                   0x00B424
#define   S_00B424_MEM_BASE(x)                                        (((unsigned)(x) & 0xFF) << 0)
#define   G_00B424_MEM_BASE(x)                                        (((x) >> 0) & 0xFF)
#define   C_00B424_MEM_BASE                                           0xFFFFFF00
#define R_00B428_SPI_SHADER_PGM_RSRC1_HS                                0x00B428
#define   S_00B428_VGPRS(x)                                           (((unsigned)(x) & 0x3F) << 0)
#define   G_00B428_VGPRS(x)                                           (((x) >> 0) & 0x3F)
#define   C_00B428_VGPRS                                              0xFFFFFFC0
#define   S_00B428_SGPRS(x)                                           (((unsigned)(x) & 0x0F) << 6)
#define   G_00B428_SGPRS(x)                                           (((x) >> 6) & 0x0F)
#define   C_00B428_SGPRS                                              0xFFFFFC3F
#define   S_00B428_PRIORITY(x)                                        (((unsigned)(x) & 0x03) << 10)
#define   G_00B428_PRIORITY(x)                                        (((x) >> 10) & 0x03)
#define   C_00B428_PRIORITY                                           0xFFFFF3FF
#define   S_00B428_FLOAT_MODE(x)                                      (((unsigned)(x) & 0xFF) << 12)
#define   G_00B428_FLOAT_MODE(x)                                      (((x) >> 12) & 0xFF)
#define   C_00B428_FLOAT_MODE                                         0xFFF00FFF
#define   S_00B428_PRIV(x)                                            (((unsigned)(x) & 0x1) << 20)
#define   G_00B428_PRIV(x)                                            (((x) >> 20) & 0x1)
#define   C_00B428_PRIV                                               0xFFEFFFFF
#define   S_00B428_DX10_CLAMP(x)                                      (((unsigned)(x) & 0x1) << 21)
#define   G_00B428_DX10_CLAMP(x)                                      (((x) >> 21) & 0x1)
#define   C_00B428_DX10_CLAMP                                         0xFFDFFFFF
#define   S_00B428_DEBUG_MODE(x)                                      (((unsigned)(x) & 0x1) << 22)
#define   G_00B428_DEBUG_MODE(x)                                      (((x) >> 22) & 0x1)
#define   C_00B428_DEBUG_MODE                                         0xFFBFFFFF
#define   S_00B428_IEEE_MODE(x)                                       (((unsigned)(x) & 0x1) << 23)
#define   G_00B428_IEEE_MODE(x)                                       (((x) >> 23) & 0x1)
#define   C_00B428_IEEE_MODE                                          0xFF7FFFFF
/* CIK */
#define   S_00B428_CACHE_CTL(x)                                       (((unsigned)(x) & 0x07) << 24)
#define   G_00B428_CACHE_CTL(x)                                       (((x) >> 24) & 0x07)
#define   C_00B428_CACHE_CTL                                          0xF8FFFFFF
#define   S_00B428_CDBG_USER(x)                                       (((unsigned)(x) & 0x1) << 27)
#define   G_00B428_CDBG_USER(x)                                       (((x) >> 27) & 0x1)
#define   C_00B428_CDBG_USER                                          0xF7FFFFFF
/*     */
#define R_00B42C_SPI_SHADER_PGM_RSRC2_HS                                0x00B42C
#define   S_00B42C_SCRATCH_EN(x)                                      (((unsigned)(x) & 0x1) << 0)
#define   G_00B42C_SCRATCH_EN(x)                                      (((x) >> 0) & 0x1)
#define   C_00B42C_SCRATCH_EN                                         0xFFFFFFFE
#define   S_00B42C_USER_SGPR(x)                                       (((unsigned)(x) & 0x1F) << 1)
#define   G_00B42C_USER_SGPR(x)                                       (((x) >> 1) & 0x1F)
#define   C_00B42C_USER_SGPR                                          0xFFFFFFC1
#define   S_00B42C_TRAP_PRESENT(x)                                    (((unsigned)(x) & 0x1) << 6)
#define   G_00B42C_TRAP_PRESENT(x)                                    (((x) >> 6) & 0x1)
#define   C_00B42C_TRAP_PRESENT                                       0xFFFFFFBF
#define   S_00B42C_OC_LDS_EN(x)                                       (((unsigned)(x) & 0x1) << 7)
#define   G_00B42C_OC_LDS_EN(x)                                       (((x) >> 7) & 0x1)
#define   C_00B42C_OC_LDS_EN                                          0xFFFFFF7F
#define   S_00B42C_TG_SIZE_EN(x)                                      (((unsigned)(x) & 0x1) << 8)
#define   G_00B42C_TG_SIZE_EN(x)                                      (((x) >> 8) & 0x1)
#define   C_00B42C_TG_SIZE_EN                                         0xFFFFFEFF
#define   S_00B42C_EXCP_EN(x)                                         (((unsigned)(x) & 0x7F) << 9) /* mask is 0x1FF on CIK */
#define   G_00B42C_EXCP_EN(x)                                         (((x) >> 9) & 0x7F) /* mask is 0x1FF on CIK */
#define   C_00B42C_EXCP_EN                                            0xFFFF01FF /* mask is 0x1FF on CIK */
#define R_00B430_SPI_SHADER_USER_DATA_HS_0                              0x00B430
#define R_00B434_SPI_SHADER_USER_DATA_HS_1                              0x00B434
#define R_00B438_SPI_SHADER_USER_DATA_HS_2                              0x00B438
#define R_00B43C_SPI_SHADER_USER_DATA_HS_3                              0x00B43C
#define R_00B440_SPI_SHADER_USER_DATA_HS_4                              0x00B440
#define R_00B444_SPI_SHADER_USER_DATA_HS_5                              0x00B444
#define R_00B448_SPI_SHADER_USER_DATA_HS_6                              0x00B448
#define R_00B44C_SPI_SHADER_USER_DATA_HS_7                              0x00B44C
#define R_00B450_SPI_SHADER_USER_DATA_HS_8                              0x00B450
#define R_00B454_SPI_SHADER_USER_DATA_HS_9                              0x00B454
#define R_00B458_SPI_SHADER_USER_DATA_HS_10                             0x00B458
#define R_00B45C_SPI_SHADER_USER_DATA_HS_11                             0x00B45C
#define R_00B460_SPI_SHADER_USER_DATA_HS_12                             0x00B460
#define R_00B464_SPI_SHADER_USER_DATA_HS_13                             0x00B464
#define R_00B468_SPI_SHADER_USER_DATA_HS_14                             0x00B468
#define R_00B46C_SPI_SHADER_USER_DATA_HS_15                             0x00B46C
#define R_00B500_SPI_SHADER_TBA_LO_LS                                   0x00B500
#define R_00B504_SPI_SHADER_TBA_HI_LS                                   0x00B504
#define   S_00B504_MEM_BASE(x)                                        (((unsigned)(x) & 0xFF) << 0)
#define   G_00B504_MEM_BASE(x)                                        (((x) >> 0) & 0xFF)
#define   C_00B504_MEM_BASE                                           0xFFFFFF00
#define R_00B508_SPI_SHADER_TMA_LO_LS                                   0x00B508
#define R_00B50C_SPI_SHADER_TMA_HI_LS                                   0x00B50C
#define   S_00B50C_MEM_BASE(x)                                        (((unsigned)(x) & 0xFF) << 0)
#define   G_00B50C_MEM_BASE(x)                                        (((x) >> 0) & 0xFF)
#define   C_00B50C_MEM_BASE                                           0xFFFFFF00
/* CIK */
#define R_00B51C_SPI_SHADER_PGM_RSRC3_LS                                0x00B51C
#define   S_00B51C_CU_EN(x)                                           (((unsigned)(x) & 0xFFFF) << 0)
#define   G_00B51C_CU_EN(x)                                           (((x) >> 0) & 0xFFFF)
#define   C_00B51C_CU_EN                                              0xFFFF0000
#define   S_00B51C_WAVE_LIMIT(x)                                      (((unsigned)(x) & 0x3F) << 16)
#define   G_00B51C_WAVE_LIMIT(x)                                      (((x) >> 16) & 0x3F)
#define   C_00B51C_WAVE_LIMIT                                         0xFFC0FFFF
#define   S_00B51C_LOCK_LOW_THRESHOLD(x)                              (((unsigned)(x) & 0x0F) << 22)
#define   G_00B51C_LOCK_LOW_THRESHOLD(x)                              (((x) >> 22) & 0x0F)
#define   C_00B51C_LOCK_LOW_THRESHOLD                                 0xFC3FFFFF
/*     */
/* VI */
#define   S_00B51C_GROUP_FIFO_DEPTH(x)                                (((unsigned)(x) & 0x3F) << 26)
#define   G_00B51C_GROUP_FIFO_DEPTH(x)                                (((x) >> 26) & 0x3F)
#define   C_00B51C_GROUP_FIFO_DEPTH                                   0x03FFFFFF
/*    */
#define R_00B520_SPI_SHADER_PGM_LO_LS                                   0x00B520
#define R_00B524_SPI_SHADER_PGM_HI_LS                                   0x00B524
#define   S_00B524_MEM_BASE(x)                                        (((unsigned)(x) & 0xFF) << 0)
#define   G_00B524_MEM_BASE(x)                                        (((x) >> 0) & 0xFF)
#define   C_00B524_MEM_BASE                                           0xFFFFFF00
#define R_00B528_SPI_SHADER_PGM_RSRC1_LS                                0x00B528
#define   S_00B528_VGPRS(x)                                           (((unsigned)(x) & 0x3F) << 0)
#define   G_00B528_VGPRS(x)                                           (((x) >> 0) & 0x3F)
#define   C_00B528_VGPRS                                              0xFFFFFFC0
#define   S_00B528_SGPRS(x)                                           (((unsigned)(x) & 0x0F) << 6)
#define   G_00B528_SGPRS(x)                                           (((x) >> 6) & 0x0F)
#define   C_00B528_SGPRS                                              0xFFFFFC3F
#define   S_00B528_PRIORITY(x)                                        (((unsigned)(x) & 0x03) << 10)
#define   G_00B528_PRIORITY(x)                                        (((x) >> 10) & 0x03)
#define   C_00B528_PRIORITY                                           0xFFFFF3FF
#define   S_00B528_FLOAT_MODE(x)                                      (((unsigned)(x) & 0xFF) << 12)
#define   G_00B528_FLOAT_MODE(x)                                      (((x) >> 12) & 0xFF)
#define   C_00B528_FLOAT_MODE                                         0xFFF00FFF
#define   S_00B528_PRIV(x)                                            (((unsigned)(x) & 0x1) << 20)
#define   G_00B528_PRIV(x)                                            (((x) >> 20) & 0x1)
#define   C_00B528_PRIV                                               0xFFEFFFFF
#define   S_00B528_DX10_CLAMP(x)                                      (((unsigned)(x) & 0x1) << 21)
#define   G_00B528_DX10_CLAMP(x)                                      (((x) >> 21) & 0x1)
#define   C_00B528_DX10_CLAMP                                         0xFFDFFFFF
#define   S_00B528_DEBUG_MODE(x)                                      (((unsigned)(x) & 0x1) << 22)
#define   G_00B528_DEBUG_MODE(x)                                      (((x) >> 22) & 0x1)
#define   C_00B528_DEBUG_MODE                                         0xFFBFFFFF
#define   S_00B528_IEEE_MODE(x)                                       (((unsigned)(x) & 0x1) << 23)
#define   G_00B528_IEEE_MODE(x)                                       (((x) >> 23) & 0x1)
#define   C_00B528_IEEE_MODE                                          0xFF7FFFFF
#define   S_00B528_VGPR_COMP_CNT(x)                                   (((unsigned)(x) & 0x03) << 24)
#define   G_00B528_VGPR_COMP_CNT(x)                                   (((x) >> 24) & 0x03)
#define   C_00B528_VGPR_COMP_CNT                                      0xFCFFFFFF
/* CIK */
#define   S_00B528_CACHE_CTL(x)                                       (((unsigned)(x) & 0x07) << 26)
#define   G_00B528_CACHE_CTL(x)                                       (((x) >> 26) & 0x07)
#define   C_00B528_CACHE_CTL                                          0xE3FFFFFF
#define   S_00B528_CDBG_USER(x)                                       (((unsigned)(x) & 0x1) << 29)
#define   G_00B528_CDBG_USER(x)                                       (((x) >> 29) & 0x1)
#define   C_00B528_CDBG_USER                                          0xDFFFFFFF
/*     */
#define R_00B52C_SPI_SHADER_PGM_RSRC2_LS                                0x00B52C
#define   S_00B52C_SCRATCH_EN(x)                                      (((unsigned)(x) & 0x1) << 0)
#define   G_00B52C_SCRATCH_EN(x)                                      (((x) >> 0) & 0x1)
#define   C_00B52C_SCRATCH_EN                                         0xFFFFFFFE
#define   S_00B52C_USER_SGPR(x)                                       (((unsigned)(x) & 0x1F) << 1)
#define   G_00B52C_USER_SGPR(x)                                       (((x) >> 1) & 0x1F)
#define   C_00B52C_USER_SGPR                                          0xFFFFFFC1
#define   S_00B52C_TRAP_PRESENT(x)                                    (((unsigned)(x) & 0x1) << 6)
#define   G_00B52C_TRAP_PRESENT(x)                                    (((x) >> 6) & 0x1)
#define   C_00B52C_TRAP_PRESENT                                       0xFFFFFFBF
#define   S_00B52C_LDS_SIZE(x)                                        (((unsigned)(x) & 0x1FF) << 7)
#define   G_00B52C_LDS_SIZE(x)                                        (((x) >> 7) & 0x1FF)
#define   C_00B52C_LDS_SIZE                                           0xFFFF007F
#define   S_00B52C_EXCP_EN(x)                                         (((unsigned)(x) & 0x7F) << 16) /* mask is 0x1FF on CIK */
#define   G_00B52C_EXCP_EN(x)                                         (((x) >> 16) & 0x7F) /* mask is 0x1FF on CIK */
#define   C_00B52C_EXCP_EN                                            0xFF80FFFF /* mask is 0x1FF on CIK */
#define R_00B530_SPI_SHADER_USER_DATA_LS_0                              0x00B530
#define R_00B534_SPI_SHADER_USER_DATA_LS_1                              0x00B534
#define R_00B538_SPI_SHADER_USER_DATA_LS_2                              0x00B538
#define R_00B53C_SPI_SHADER_USER_DATA_LS_3                              0x00B53C
#define R_00B540_SPI_SHADER_USER_DATA_LS_4                              0x00B540
#define R_00B544_SPI_SHADER_USER_DATA_LS_5                              0x00B544
#define R_00B548_SPI_SHADER_USER_DATA_LS_6                              0x00B548
#define R_00B54C_SPI_SHADER_USER_DATA_LS_7                              0x00B54C
#define R_00B550_SPI_SHADER_USER_DATA_LS_8                              0x00B550
#define R_00B554_SPI_SHADER_USER_DATA_LS_9                              0x00B554
#define R_00B558_SPI_SHADER_USER_DATA_LS_10                             0x00B558
#define R_00B55C_SPI_SHADER_USER_DATA_LS_11                             0x00B55C
#define R_00B560_SPI_SHADER_USER_DATA_LS_12                             0x00B560
#define R_00B564_SPI_SHADER_USER_DATA_LS_13                             0x00B564
#define R_00B568_SPI_SHADER_USER_DATA_LS_14                             0x00B568
#define R_00B56C_SPI_SHADER_USER_DATA_LS_15                             0x00B56C
#define R_00B800_COMPUTE_DISPATCH_INITIATOR                             0x00B800
#define   S_00B800_COMPUTE_SHADER_EN(x)                               (((unsigned)(x) & 0x1) << 0)
#define   G_00B800_COMPUTE_SHADER_EN(x)                               (((x) >> 0) & 0x1)
#define   C_00B800_COMPUTE_SHADER_EN                                  0xFFFFFFFE
#define   S_00B800_PARTIAL_TG_EN(x)                                   (((unsigned)(x) & 0x1) << 1)
#define   G_00B800_PARTIAL_TG_EN(x)                                   (((x) >> 1) & 0x1)
#define   C_00B800_PARTIAL_TG_EN                                      0xFFFFFFFD
#define   S_00B800_FORCE_START_AT_000(x)                              (((unsigned)(x) & 0x1) << 2)
#define   G_00B800_FORCE_START_AT_000(x)                              (((x) >> 2) & 0x1)
#define   C_00B800_FORCE_START_AT_000                                 0xFFFFFFFB
#define   S_00B800_ORDERED_APPEND_ENBL(x)                             (((unsigned)(x) & 0x1) << 3)
#define   G_00B800_ORDERED_APPEND_ENBL(x)                             (((x) >> 3) & 0x1)
#define   C_00B800_ORDERED_APPEND_ENBL                                0xFFFFFFF7
/* CIK */
#define   S_00B800_ORDERED_APPEND_MODE(x)                             (((unsigned)(x) & 0x1) << 4)
#define   G_00B800_ORDERED_APPEND_MODE(x)                             (((x) >> 4) & 0x1)
#define   C_00B800_ORDERED_APPEND_MODE                                0xFFFFFFEF
#define   S_00B800_USE_THREAD_DIMENSIONS(x)                           (((unsigned)(x) & 0x1) << 5)
#define   G_00B800_USE_THREAD_DIMENSIONS(x)                           (((x) >> 5) & 0x1)
#define   C_00B800_USE_THREAD_DIMENSIONS                              0xFFFFFFDF
#define   S_00B800_ORDER_MODE(x)                                      (((unsigned)(x) & 0x1) << 6)
#define   G_00B800_ORDER_MODE(x)                                      (((x) >> 6) & 0x1)
#define   C_00B800_ORDER_MODE                                         0xFFFFFFBF
#define   S_00B800_DISPATCH_CACHE_CNTL(x)                             (((unsigned)(x) & 0x07) << 7)
#define   G_00B800_DISPATCH_CACHE_CNTL(x)                             (((x) >> 7) & 0x07)
#define   C_00B800_DISPATCH_CACHE_CNTL                                0xFFFFFC7F
#define   S_00B800_SCALAR_L1_INV_VOL(x)                               (((unsigned)(x) & 0x1) << 10)
#define   G_00B800_SCALAR_L1_INV_VOL(x)                               (((x) >> 10) & 0x1)
#define   C_00B800_SCALAR_L1_INV_VOL                                  0xFFFFFBFF
#define   S_00B800_VECTOR_L1_INV_VOL(x)                               (((unsigned)(x) & 0x1) << 11)
#define   G_00B800_VECTOR_L1_INV_VOL(x)                               (((x) >> 11) & 0x1)
#define   C_00B800_VECTOR_L1_INV_VOL                                  0xFFFFF7FF
#define   S_00B800_DATA_ATC(x)                                        (((unsigned)(x) & 0x1) << 12)
#define   G_00B800_DATA_ATC(x)                                        (((x) >> 12) & 0x1)
#define   C_00B800_DATA_ATC                                           0xFFFFEFFF
#define   S_00B800_RESTORE(x)                                         (((unsigned)(x) & 0x1) << 14)
#define   G_00B800_RESTORE(x)                                         (((x) >> 14) & 0x1)
#define   C_00B800_RESTORE                                            0xFFFFBFFF
/*     */
#define R_00B804_COMPUTE_DIM_X                                          0x00B804
#define R_00B808_COMPUTE_DIM_Y                                          0x00B808
#define R_00B80C_COMPUTE_DIM_Z                                          0x00B80C
#define R_00B810_COMPUTE_START_X                                        0x00B810
#define R_00B814_COMPUTE_START_Y                                        0x00B814
#define R_00B818_COMPUTE_START_Z                                        0x00B818
#define R_00B81C_COMPUTE_NUM_THREAD_X                                   0x00B81C
#define   S_00B81C_NUM_THREAD_FULL(x)                                 (((unsigned)(x) & 0xFFFF) << 0)
#define   G_00B81C_NUM_THREAD_FULL(x)                                 (((x) >> 0) & 0xFFFF)
#define   C_00B81C_NUM_THREAD_FULL                                    0xFFFF0000
#define   S_00B81C_NUM_THREAD_PARTIAL(x)                              (((unsigned)(x) & 0xFFFF) << 16)
#define   G_00B81C_NUM_THREAD_PARTIAL(x)                              (((x) >> 16) & 0xFFFF)
#define   C_00B81C_NUM_THREAD_PARTIAL                                 0x0000FFFF
#define R_00B820_COMPUTE_NUM_THREAD_Y                                   0x00B820
#define   S_00B820_NUM_THREAD_FULL(x)                                 (((unsigned)(x) & 0xFFFF) << 0)
#define   G_00B820_NUM_THREAD_FULL(x)                                 (((x) >> 0) & 0xFFFF)
#define   C_00B820_NUM_THREAD_FULL                                    0xFFFF0000
#define   S_00B820_NUM_THREAD_PARTIAL(x)                              (((unsigned)(x) & 0xFFFF) << 16)
#define   G_00B820_NUM_THREAD_PARTIAL(x)                              (((x) >> 16) & 0xFFFF)
#define   C_00B820_NUM_THREAD_PARTIAL                                 0x0000FFFF
#define R_00B824_COMPUTE_NUM_THREAD_Z                                   0x00B824
#define   S_00B824_NUM_THREAD_FULL(x)                                 (((unsigned)(x) & 0xFFFF) << 0)
#define   G_00B824_NUM_THREAD_FULL(x)                                 (((x) >> 0) & 0xFFFF)
#define   C_00B824_NUM_THREAD_FULL                                    0xFFFF0000
#define   S_00B824_NUM_THREAD_PARTIAL(x)                              (((unsigned)(x) & 0xFFFF) << 16)
#define   G_00B824_NUM_THREAD_PARTIAL(x)                              (((x) >> 16) & 0xFFFF)
#define   C_00B824_NUM_THREAD_PARTIAL                                 0x0000FFFF
#define R_00B82C_COMPUTE_MAX_WAVE_ID                                    0x00B82C /* moved to 0xCD20 on CIK */
#define   S_00B82C_MAX_WAVE_ID(x)                                     (((unsigned)(x) & 0xFFF) << 0)
#define   G_00B82C_MAX_WAVE_ID(x)                                     (((x) >> 0) & 0xFFF)
#define   C_00B82C_MAX_WAVE_ID                                        0xFFFFF000
/* CIK */
#define R_00B828_COMPUTE_PIPELINESTAT_ENABLE                            0x00B828
#define   S_00B828_PIPELINESTAT_ENABLE(x)                             (((unsigned)(x) & 0x1) << 0)
#define   G_00B828_PIPELINESTAT_ENABLE(x)                             (((x) >> 0) & 0x1)
#define   C_00B828_PIPELINESTAT_ENABLE                                0xFFFFFFFE
#define R_00B82C_COMPUTE_PERFCOUNT_ENABLE                               0x00B82C
#define   S_00B82C_PERFCOUNT_ENABLE(x)                                (((unsigned)(x) & 0x1) << 0)
#define   G_00B82C_PERFCOUNT_ENABLE(x)                                (((x) >> 0) & 0x1)
#define   C_00B82C_PERFCOUNT_ENABLE                                   0xFFFFFFFE
/*     */
#define R_00B830_COMPUTE_PGM_LO                                         0x00B830
#define R_00B834_COMPUTE_PGM_HI                                         0x00B834
#define   S_00B834_DATA(x)                                            (((unsigned)(x) & 0xFF) << 0)
#define   G_00B834_DATA(x)                                            (((x) >> 0) & 0xFF)
#define   C_00B834_DATA                                               0xFFFFFF00
/* CIK */
#define   S_00B834_INST_ATC(x)                                        (((unsigned)(x) & 0x1) << 8)
#define   G_00B834_INST_ATC(x)                                        (((x) >> 8) & 0x1)
#define   C_00B834_INST_ATC                                           0xFFFFFEFF
/*     */
#define R_00B838_COMPUTE_TBA_LO                                         0x00B838
#define R_00B83C_COMPUTE_TBA_HI                                         0x00B83C
#define   S_00B83C_DATA(x)                                            (((unsigned)(x) & 0xFF) << 0)
#define   G_00B83C_DATA(x)                                            (((x) >> 0) & 0xFF)
#define   C_00B83C_DATA                                               0xFFFFFF00
#define R_00B840_COMPUTE_TMA_LO                                         0x00B840
#define R_00B844_COMPUTE_TMA_HI                                         0x00B844
#define   S_00B844_DATA(x)                                            (((unsigned)(x) & 0xFF) << 0)
#define   G_00B844_DATA(x)                                            (((x) >> 0) & 0xFF)
#define   C_00B844_DATA                                               0xFFFFFF00
#define R_00B848_COMPUTE_PGM_RSRC1                                      0x00B848
#define   S_00B848_VGPRS(x)                                           (((unsigned)(x) & 0x3F) << 0)
#define   G_00B848_VGPRS(x)                                           (((x) >> 0) & 0x3F)
#define   C_00B848_VGPRS                                              0xFFFFFFC0
#define   S_00B848_SGPRS(x)                                           (((unsigned)(x) & 0x0F) << 6)
#define   G_00B848_SGPRS(x)                                           (((x) >> 6) & 0x0F)
#define   C_00B848_SGPRS                                              0xFFFFFC3F
#define   S_00B848_PRIORITY(x)                                        (((unsigned)(x) & 0x03) << 10)
#define   G_00B848_PRIORITY(x)                                        (((x) >> 10) & 0x03)
#define   C_00B848_PRIORITY                                           0xFFFFF3FF
#define   S_00B848_FLOAT_MODE(x)                                      (((unsigned)(x) & 0xFF) << 12)
#define   G_00B848_FLOAT_MODE(x)                                      (((x) >> 12) & 0xFF)
#define   C_00B848_FLOAT_MODE                                         0xFFF00FFF
#define   S_00B848_PRIV(x)                                            (((unsigned)(x) & 0x1) << 20)
#define   G_00B848_PRIV(x)                                            (((x) >> 20) & 0x1)
#define   C_00B848_PRIV                                               0xFFEFFFFF
#define   S_00B848_DX10_CLAMP(x)                                      (((unsigned)(x) & 0x1) << 21)
#define   G_00B848_DX10_CLAMP(x)                                      (((x) >> 21) & 0x1)
#define   C_00B848_DX10_CLAMP                                         0xFFDFFFFF
#define   S_00B848_DEBUG_MODE(x)                                      (((unsigned)(x) & 0x1) << 22)
#define   G_00B848_DEBUG_MODE(x)                                      (((x) >> 22) & 0x1)
#define   C_00B848_DEBUG_MODE                                         0xFFBFFFFF
#define   S_00B848_IEEE_MODE(x)                                       (((unsigned)(x) & 0x1) << 23)
#define   G_00B848_IEEE_MODE(x)                                       (((x) >> 23) & 0x1)
#define   C_00B848_IEEE_MODE                                          0xFF7FFFFF
/* CIK */
#define   S_00B848_BULKY(x)                                           (((unsigned)(x) & 0x1) << 24)
#define   G_00B848_BULKY(x)                                           (((x) >> 24) & 0x1)
#define   C_00B848_BULKY                                              0xFEFFFFFF
#define   S_00B848_CDBG_USER(x)                                       (((unsigned)(x) & 0x1) << 25)
#define   G_00B848_CDBG_USER(x)                                       (((x) >> 25) & 0x1)
#define   C_00B848_CDBG_USER                                          0xFDFFFFFF
/*     */
#define R_00B84C_COMPUTE_PGM_RSRC2                                      0x00B84C
#define   S_00B84C_SCRATCH_EN(x)                                      (((unsigned)(x) & 0x1) << 0)
#define   G_00B84C_SCRATCH_EN(x)                                      (((x) >> 0) & 0x1)
#define   C_00B84C_SCRATCH_EN                                         0xFFFFFFFE
#define   S_00B84C_USER_SGPR(x)                                       (((unsigned)(x) & 0x1F) << 1)
#define   G_00B84C_USER_SGPR(x)                                       (((x) >> 1) & 0x1F)
#define   C_00B84C_USER_SGPR                                          0xFFFFFFC1
#define   S_00B84C_TRAP_PRESENT(x)                                    (((unsigned)(x) & 0x1) << 6)
#define   G_00B84C_TRAP_PRESENT(x)                                    (((x) >> 6) & 0x1)
#define   C_00B84C_TRAP_PRESENT                                       0xFFFFFFBF
#define   S_00B84C_TGID_X_EN(x)                                       (((unsigned)(x) & 0x1) << 7)
#define   G_00B84C_TGID_X_EN(x)                                       (((x) >> 7) & 0x1)
#define   C_00B84C_TGID_X_EN                                          0xFFFFFF7F
#define   S_00B84C_TGID_Y_EN(x)                                       (((unsigned)(x) & 0x1) << 8)
#define   G_00B84C_TGID_Y_EN(x)                                       (((x) >> 8) & 0x1)
#define   C_00B84C_TGID_Y_EN                                          0xFFFFFEFF
#define   S_00B84C_TGID_Z_EN(x)                                       (((unsigned)(x) & 0x1) << 9)
#define   G_00B84C_TGID_Z_EN(x)                                       (((x) >> 9) & 0x1)
#define   C_00B84C_TGID_Z_EN                                          0xFFFFFDFF
#define   S_00B84C_TG_SIZE_EN(x)                                      (((unsigned)(x) & 0x1) << 10)
#define   G_00B84C_TG_SIZE_EN(x)                                      (((x) >> 10) & 0x1)
#define   C_00B84C_TG_SIZE_EN                                         0xFFFFFBFF
#define   S_00B84C_TIDIG_COMP_CNT(x)                                  (((unsigned)(x) & 0x03) << 11)
#define   G_00B84C_TIDIG_COMP_CNT(x)                                  (((x) >> 11) & 0x03)
#define   C_00B84C_TIDIG_COMP_CNT                                     0xFFFFE7FF
/* CIK */
#define   S_00B84C_EXCP_EN_MSB(x)                                     (((unsigned)(x) & 0x03) << 13)
#define   G_00B84C_EXCP_EN_MSB(x)                                     (((x) >> 13) & 0x03)
#define   C_00B84C_EXCP_EN_MSB                                        0xFFFF9FFF
/*     */
#define   S_00B84C_LDS_SIZE(x)                                        (((unsigned)(x) & 0x1FF) << 15)
#define   G_00B84C_LDS_SIZE(x)                                        (((x) >> 15) & 0x1FF)
#define   C_00B84C_LDS_SIZE                                           0xFF007FFF
#define   S_00B84C_EXCP_EN(x)                                         (((unsigned)(x) & 0x7F) << 24)
#define   G_00B84C_EXCP_EN(x)                                         (((x) >> 24) & 0x7F)
#define   C_00B84C_EXCP_EN                                            0x80FFFFFF
#define R_00B850_COMPUTE_VMID                                           0x00B850
#define   S_00B850_DATA(x)                                            (((unsigned)(x) & 0x0F) << 0)
#define   G_00B850_DATA(x)                                            (((x) >> 0) & 0x0F)
#define   C_00B850_DATA                                               0xFFFFFFF0
#define R_00B854_COMPUTE_RESOURCE_LIMITS                                0x00B854
#define   S_00B854_WAVES_PER_SH(x)                                    (((unsigned)(x) & 0x3F) << 0) /* mask is 0x3FF on CIK */
#define   G_00B854_WAVES_PER_SH(x)                                    (((x) >> 0) & 0x3F) /* mask is 0x3FF on CIK */
#define   C_00B854_WAVES_PER_SH                                       0xFFFFFFC0 /* mask is 0x3FF on CIK */
#define   S_00B854_WAVES_PER_SH_CIK(x)                                (((unsigned)(x) & 0x3FF) << 0)
#define   G_00B854_WAVES_PER_SH_CIK(x)                                (((x) >> 0) & 0x3FF)
#define   C_00B854_WAVES_PER_SH_CIK                                   0xFFFFFC00
#define   S_00B854_TG_PER_CU(x)                                       (((unsigned)(x) & 0x0F) << 12)
#define   G_00B854_TG_PER_CU(x)                                       (((x) >> 12) & 0x0F)
#define   C_00B854_TG_PER_CU                                          0xFFFF0FFF
#define   S_00B854_LOCK_THRESHOLD(x)                                  (((unsigned)(x) & 0x3F) << 16)
#define   G_00B854_LOCK_THRESHOLD(x)                                  (((x) >> 16) & 0x3F)
#define   C_00B854_LOCK_THRESHOLD                                     0xFFC0FFFF
#define   S_00B854_SIMD_DEST_CNTL(x)                                  (((unsigned)(x) & 0x1) << 22)
#define   G_00B854_SIMD_DEST_CNTL(x)                                  (((x) >> 22) & 0x1)
#define   C_00B854_SIMD_DEST_CNTL                                     0xFFBFFFFF
/* CIK */
#define   S_00B854_FORCE_SIMD_DIST(x)                                 (((unsigned)(x) & 0x1) << 23)
#define   G_00B854_FORCE_SIMD_DIST(x)                                 (((x) >> 23) & 0x1)
#define   C_00B854_FORCE_SIMD_DIST                                    0xFF7FFFFF
#define   S_00B854_CU_GROUP_COUNT(x)                                  (((unsigned)(x) & 0x07) << 24)
#define   G_00B854_CU_GROUP_COUNT(x)                                  (((x) >> 24) & 0x07)
#define   C_00B854_CU_GROUP_COUNT                                     0xF8FFFFFF
/*     */
#define R_00B858_COMPUTE_STATIC_THREAD_MGMT_SE0                         0x00B858
#define   S_00B858_SH0_CU_EN(x)                                       (((unsigned)(x) & 0xFFFF) << 0)
#define   G_00B858_SH0_CU_EN(x)                                       (((x) >> 0) & 0xFFFF)
#define   C_00B858_SH0_CU_EN                                          0xFFFF0000
#define   S_00B858_SH1_CU_EN(x)                                       (((unsigned)(x) & 0xFFFF) << 16)
#define   G_00B858_SH1_CU_EN(x)                                       (((x) >> 16) & 0xFFFF)
#define   C_00B858_SH1_CU_EN                                          0x0000FFFF
#define R_00B85C_COMPUTE_STATIC_THREAD_MGMT_SE1                         0x00B85C
#define   S_00B85C_SH0_CU_EN(x)                                       (((unsigned)(x) & 0xFFFF) << 0)
#define   G_00B85C_SH0_CU_EN(x)                                       (((x) >> 0) & 0xFFFF)
#define   C_00B85C_SH0_CU_EN                                          0xFFFF0000
#define   S_00B85C_SH1_CU_EN(x)                                       (((unsigned)(x) & 0xFFFF) << 16)
#define   G_00B85C_SH1_CU_EN(x)                                       (((x) >> 16) & 0xFFFF)
#define   C_00B85C_SH1_CU_EN                                          0x0000FFFF
#define R_00B860_COMPUTE_TMPRING_SIZE                                   0x00B860
#define   S_00B860_WAVES(x)                                           (((unsigned)(x) & 0xFFF) << 0)
#define   G_00B860_WAVES(x)                                           (((x) >> 0) & 0xFFF)
#define   C_00B860_WAVES                                              0xFFFFF000
#define   S_00B860_WAVESIZE(x)                                        (((unsigned)(x) & 0x1FFF) << 12)
#define   G_00B860_WAVESIZE(x)                                        (((x) >> 12) & 0x1FFF)
#define   C_00B860_WAVESIZE                                           0xFE000FFF
/* CIK */
#define R_00B864_COMPUTE_STATIC_THREAD_MGMT_SE2                         0x00B864
#define   S_00B864_SH0_CU_EN(x)                                       (((unsigned)(x) & 0xFFFF) << 0)
#define   G_00B864_SH0_CU_EN(x)                                       (((x) >> 0) & 0xFFFF)
#define   C_00B864_SH0_CU_EN                                          0xFFFF0000
#define   S_00B864_SH1_CU_EN(x)                                       (((unsigned)(x) & 0xFFFF) << 16)
#define   G_00B864_SH1_CU_EN(x)                                       (((x) >> 16) & 0xFFFF)
#define   C_00B864_SH1_CU_EN                                          0x0000FFFF
#define R_00B868_COMPUTE_STATIC_THREAD_MGMT_SE3                         0x00B868
#define   S_00B868_SH0_CU_EN(x)                                       (((unsigned)(x) & 0xFFFF) << 0)
#define   G_00B868_SH0_CU_EN(x)                                       (((x) >> 0) & 0xFFFF)
#define   C_00B868_SH0_CU_EN                                          0xFFFF0000
#define   S_00B868_SH1_CU_EN(x)                                       (((unsigned)(x) & 0xFFFF) << 16)
#define   G_00B868_SH1_CU_EN(x)                                       (((x) >> 16) & 0xFFFF)
#define   C_00B868_SH1_CU_EN                                          0x0000FFFF
#define R_00B86C_COMPUTE_RESTART_X                                      0x00B86C
#define R_00B870_COMPUTE_RESTART_Y                                      0x00B870
#define R_00B874_COMPUTE_RESTART_Z                                      0x00B874
#define R_00B87C_COMPUTE_MISC_RESERVED                                  0x00B87C
#define   S_00B87C_SEND_SEID(x)                                       (((unsigned)(x) & 0x03) << 0)
#define   G_00B87C_SEND_SEID(x)                                       (((x) >> 0) & 0x03)
#define   C_00B87C_SEND_SEID                                          0xFFFFFFFC
#define   S_00B87C_RESERVED2(x)                                       (((unsigned)(x) & 0x1) << 2)
#define   G_00B87C_RESERVED2(x)                                       (((x) >> 2) & 0x1)
#define   C_00B87C_RESERVED2                                          0xFFFFFFFB
#define   S_00B87C_RESERVED3(x)                                       (((unsigned)(x) & 0x1) << 3)
#define   G_00B87C_RESERVED3(x)                                       (((x) >> 3) & 0x1)
#define   C_00B87C_RESERVED3                                          0xFFFFFFF7
#define   S_00B87C_RESERVED4(x)                                       (((unsigned)(x) & 0x1) << 4)
#define   G_00B87C_RESERVED4(x)                                       (((x) >> 4) & 0x1)
#define   C_00B87C_RESERVED4                                          0xFFFFFFEF
/* VI */
#define   S_00B87C_WAVE_ID_BASE(x)                                    (((unsigned)(x) & 0xFFF) << 5)
#define   G_00B87C_WAVE_ID_BASE(x)                                    (((x) >> 5) & 0xFFF)
#define   C_00B87C_WAVE_ID_BASE                                       0xFFFE001F
#define R_00B880_COMPUTE_DISPATCH_ID                                    0x00B880
#define R_00B884_COMPUTE_THREADGROUP_ID                                 0x00B884
#define R_00B888_COMPUTE_RELAUNCH                                       0x00B888
#define   S_00B888_PAYLOAD(x)                                         (((unsigned)(x) & 0x3FFFFFFF) << 0)
#define   G_00B888_PAYLOAD(x)                                         (((x) >> 0) & 0x3FFFFFFF)
#define   C_00B888_PAYLOAD                                            0xC0000000
#define   S_00B888_IS_EVENT(x)                                        (((unsigned)(x) & 0x1) << 30)
#define   G_00B888_IS_EVENT(x)                                        (((x) >> 30) & 0x1)
#define   C_00B888_IS_EVENT                                           0xBFFFFFFF
#define   S_00B888_IS_STATE(x)                                        (((unsigned)(x) & 0x1) << 31)
#define   G_00B888_IS_STATE(x)                                        (((x) >> 31) & 0x1)
#define   C_00B888_IS_STATE                                           0x7FFFFFFF
#define R_00B88C_COMPUTE_WAVE_RESTORE_ADDR_LO                           0x00B88C
#define R_00B890_COMPUTE_WAVE_RESTORE_ADDR_HI                           0x00B890
#define   S_00B890_ADDR(x)                                            (((unsigned)(x) & 0xFFFF) << 0)
#define   G_00B890_ADDR(x)                                            (((x) >> 0) & 0xFFFF)
#define   C_00B890_ADDR                                               0xFFFF0000
#define R_00B894_COMPUTE_WAVE_RESTORE_CONTROL                           0x00B894
#define   S_00B894_ATC(x)                                             (((unsigned)(x) & 0x1) << 0)
#define   G_00B894_ATC(x)                                             (((x) >> 0) & 0x1)
#define   C_00B894_ATC                                                0xFFFFFFFE
#define   S_00B894_MTYPE(x)                                           (((unsigned)(x) & 0x03) << 1)
#define   G_00B894_MTYPE(x)                                           (((x) >> 1) & 0x03)
#define   C_00B894_MTYPE                                              0xFFFFFFF9
/*    */
/*     */
#define R_00B900_COMPUTE_USER_DATA_0                                    0x00B900
#define R_00B904_COMPUTE_USER_DATA_1                                    0x00B904
#define R_00B908_COMPUTE_USER_DATA_2                                    0x00B908
#define R_00B90C_COMPUTE_USER_DATA_3                                    0x00B90C
#define R_00B910_COMPUTE_USER_DATA_4                                    0x00B910
#define R_00B914_COMPUTE_USER_DATA_5                                    0x00B914
#define R_00B918_COMPUTE_USER_DATA_6                                    0x00B918
#define R_00B91C_COMPUTE_USER_DATA_7                                    0x00B91C
#define R_00B920_COMPUTE_USER_DATA_8                                    0x00B920
#define R_00B924_COMPUTE_USER_DATA_9                                    0x00B924
#define R_00B928_COMPUTE_USER_DATA_10                                   0x00B928
#define R_00B92C_COMPUTE_USER_DATA_11                                   0x00B92C
#define R_00B930_COMPUTE_USER_DATA_12                                   0x00B930
#define R_00B934_COMPUTE_USER_DATA_13                                   0x00B934
#define R_00B938_COMPUTE_USER_DATA_14                                   0x00B938
#define R_00B93C_COMPUTE_USER_DATA_15                                   0x00B93C
#define R_00B9FC_COMPUTE_NOWHERE                                        0x00B9FC
#define R_034000_CPG_PERFCOUNTER1_LO                                    0x034000
#define R_034004_CPG_PERFCOUNTER1_HI                                    0x034004
#define R_034008_CPG_PERFCOUNTER0_LO                                    0x034008
#define R_03400C_CPG_PERFCOUNTER0_HI                                    0x03400C
#define R_034010_CPC_PERFCOUNTER1_LO                                    0x034010
#define R_034014_CPC_PERFCOUNTER1_HI                                    0x034014
#define R_034018_CPC_PERFCOUNTER0_LO                                    0x034018
#define R_03401C_CPC_PERFCOUNTER0_HI                                    0x03401C
#define R_034020_CPF_PERFCOUNTER1_LO                                    0x034020
#define R_034024_CPF_PERFCOUNTER1_HI                                    0x034024
#define R_034028_CPF_PERFCOUNTER0_LO                                    0x034028
#define R_03402C_CPF_PERFCOUNTER0_HI                                    0x03402C
#define R_034100_GRBM_PERFCOUNTER0_LO                                   0x034100
#define R_034104_GRBM_PERFCOUNTER0_HI                                   0x034104
#define R_03410C_GRBM_PERFCOUNTER1_LO                                   0x03410C
#define R_034110_GRBM_PERFCOUNTER1_HI                                   0x034110
#define R_034114_GRBM_SE0_PERFCOUNTER_LO                                0x034114
#define R_034118_GRBM_SE0_PERFCOUNTER_HI                                0x034118
#define R_03411C_GRBM_SE1_PERFCOUNTER_LO                                0x03411C
#define R_034120_GRBM_SE1_PERFCOUNTER_HI                                0x034120
#define R_034124_GRBM_SE2_PERFCOUNTER_LO                                0x034124
#define R_034128_GRBM_SE2_PERFCOUNTER_HI                                0x034128
#define R_03412C_GRBM_SE3_PERFCOUNTER_LO                                0x03412C
#define R_034130_GRBM_SE3_PERFCOUNTER_HI                                0x034130
#define R_034200_WD_PERFCOUNTER0_LO                                     0x034200
#define R_034204_WD_PERFCOUNTER0_HI                                     0x034204
#define R_034208_WD_PERFCOUNTER1_LO                                     0x034208
#define R_03420C_WD_PERFCOUNTER1_HI                                     0x03420C
#define R_034210_WD_PERFCOUNTER2_LO                                     0x034210
#define R_034214_WD_PERFCOUNTER2_HI                                     0x034214
#define R_034218_WD_PERFCOUNTER3_LO                                     0x034218
#define R_03421C_WD_PERFCOUNTER3_HI                                     0x03421C
#define R_034220_IA_PERFCOUNTER0_LO                                     0x034220
#define R_034224_IA_PERFCOUNTER0_HI                                     0x034224
#define R_034228_IA_PERFCOUNTER1_LO                                     0x034228
#define R_03422C_IA_PERFCOUNTER1_HI                                     0x03422C
#define R_034230_IA_PERFCOUNTER2_LO                                     0x034230
#define R_034234_IA_PERFCOUNTER2_HI                                     0x034234
#define R_034238_IA_PERFCOUNTER3_LO                                     0x034238
#define R_03423C_IA_PERFCOUNTER3_HI                                     0x03423C
#define R_034240_VGT_PERFCOUNTER0_LO                                    0x034240
#define R_034244_VGT_PERFCOUNTER0_HI                                    0x034244
#define R_034248_VGT_PERFCOUNTER1_LO                                    0x034248
#define R_03424C_VGT_PERFCOUNTER1_HI                                    0x03424C
#define R_034250_VGT_PERFCOUNTER2_LO                                    0x034250
#define R_034254_VGT_PERFCOUNTER2_HI                                    0x034254
#define R_034258_VGT_PERFCOUNTER3_LO                                    0x034258
#define R_03425C_VGT_PERFCOUNTER3_HI                                    0x03425C
#define R_034400_PA_SU_PERFCOUNTER0_LO                                  0x034400
#define R_034404_PA_SU_PERFCOUNTER0_HI                                  0x034404
#define   S_034404_PERFCOUNTER_HI(x)                                  (((unsigned)(x) & 0xFFFF) << 0)
#define   G_034404_PERFCOUNTER_HI(x)                                  (((x) >> 0) & 0xFFFF)
#define   C_034404_PERFCOUNTER_HI                                     0xFFFF0000
#define R_034408_PA_SU_PERFCOUNTER1_LO                                  0x034408
#define R_03440C_PA_SU_PERFCOUNTER1_HI                                  0x03440C
#define R_034410_PA_SU_PERFCOUNTER2_LO                                  0x034410
#define R_034414_PA_SU_PERFCOUNTER2_HI                                  0x034414
#define R_034418_PA_SU_PERFCOUNTER3_LO                                  0x034418
#define R_03441C_PA_SU_PERFCOUNTER3_HI                                  0x03441C
#define R_034500_PA_SC_PERFCOUNTER0_LO                                  0x034500
#define R_034504_PA_SC_PERFCOUNTER0_HI                                  0x034504
#define R_034508_PA_SC_PERFCOUNTER1_LO                                  0x034508
#define R_03450C_PA_SC_PERFCOUNTER1_HI                                  0x03450C
#define R_034510_PA_SC_PERFCOUNTER2_LO                                  0x034510
#define R_034514_PA_SC_PERFCOUNTER2_HI                                  0x034514
#define R_034518_PA_SC_PERFCOUNTER3_LO                                  0x034518
#define R_03451C_PA_SC_PERFCOUNTER3_HI                                  0x03451C
#define R_034520_PA_SC_PERFCOUNTER4_LO                                  0x034520
#define R_034524_PA_SC_PERFCOUNTER4_HI                                  0x034524
#define R_034528_PA_SC_PERFCOUNTER5_LO                                  0x034528
#define R_03452C_PA_SC_PERFCOUNTER5_HI                                  0x03452C
#define R_034530_PA_SC_PERFCOUNTER6_LO                                  0x034530
#define R_034534_PA_SC_PERFCOUNTER6_HI                                  0x034534
#define R_034538_PA_SC_PERFCOUNTER7_LO                                  0x034538
#define R_03453C_PA_SC_PERFCOUNTER7_HI                                  0x03453C
#define R_034600_SPI_PERFCOUNTER0_HI                                    0x034600
#define R_034604_SPI_PERFCOUNTER0_LO                                    0x034604
#define R_034608_SPI_PERFCOUNTER1_HI                                    0x034608
#define R_03460C_SPI_PERFCOUNTER1_LO                                    0x03460C
#define R_034610_SPI_PERFCOUNTER2_HI                                    0x034610
#define R_034614_SPI_PERFCOUNTER2_LO                                    0x034614
#define R_034618_SPI_PERFCOUNTER3_HI                                    0x034618
#define R_03461C_SPI_PERFCOUNTER3_LO                                    0x03461C
#define R_034620_SPI_PERFCOUNTER4_HI                                    0x034620
#define R_034624_SPI_PERFCOUNTER4_LO                                    0x034624
#define R_034628_SPI_PERFCOUNTER5_HI                                    0x034628
#define R_03462C_SPI_PERFCOUNTER5_LO                                    0x03462C
#define R_034700_SQ_PERFCOUNTER0_LO                                     0x034700
#define R_034704_SQ_PERFCOUNTER0_HI                                     0x034704
#define R_034708_SQ_PERFCOUNTER1_LO                                     0x034708
#define R_03470C_SQ_PERFCOUNTER1_HI                                     0x03470C
#define R_034710_SQ_PERFCOUNTER2_LO                                     0x034710
#define R_034714_SQ_PERFCOUNTER2_HI                                     0x034714
#define R_034718_SQ_PERFCOUNTER3_LO                                     0x034718
#define R_03471C_SQ_PERFCOUNTER3_HI                                     0x03471C
#define R_034720_SQ_PERFCOUNTER4_LO                                     0x034720
#define R_034724_SQ_PERFCOUNTER4_HI                                     0x034724
#define R_034728_SQ_PERFCOUNTER5_LO                                     0x034728
#define R_03472C_SQ_PERFCOUNTER5_HI                                     0x03472C
#define R_034730_SQ_PERFCOUNTER6_LO                                     0x034730
#define R_034734_SQ_PERFCOUNTER6_HI                                     0x034734
#define R_034738_SQ_PERFCOUNTER7_LO                                     0x034738
#define R_03473C_SQ_PERFCOUNTER7_HI                                     0x03473C
#define R_034740_SQ_PERFCOUNTER8_LO                                     0x034740
#define R_034744_SQ_PERFCOUNTER8_HI                                     0x034744
#define R_034748_SQ_PERFCOUNTER9_LO                                     0x034748
#define R_03474C_SQ_PERFCOUNTER9_HI                                     0x03474C
#define R_034750_SQ_PERFCOUNTER10_LO                                    0x034750
#define R_034754_SQ_PERFCOUNTER10_HI                                    0x034754
#define R_034758_SQ_PERFCOUNTER11_LO                                    0x034758
#define R_03475C_SQ_PERFCOUNTER11_HI                                    0x03475C
#define R_034760_SQ_PERFCOUNTER12_LO                                    0x034760
#define R_034764_SQ_PERFCOUNTER12_HI                                    0x034764
#define R_034768_SQ_PERFCOUNTER13_LO                                    0x034768
#define R_03476C_SQ_PERFCOUNTER13_HI                                    0x03476C
#define R_034770_SQ_PERFCOUNTER14_LO                                    0x034770
#define R_034774_SQ_PERFCOUNTER14_HI                                    0x034774
#define R_034778_SQ_PERFCOUNTER15_LO                                    0x034778
#define R_03477C_SQ_PERFCOUNTER15_HI                                    0x03477C
#define R_034900_SX_PERFCOUNTER0_LO                                     0x034900
#define R_034904_SX_PERFCOUNTER0_HI                                     0x034904
#define R_034908_SX_PERFCOUNTER1_LO                                     0x034908
#define R_03490C_SX_PERFCOUNTER1_HI                                     0x03490C
#define R_034910_SX_PERFCOUNTER2_LO                                     0x034910
#define R_034914_SX_PERFCOUNTER2_HI                                     0x034914
#define R_034918_SX_PERFCOUNTER3_LO                                     0x034918
#define R_03491C_SX_PERFCOUNTER3_HI                                     0x03491C
#define R_034A00_GDS_PERFCOUNTER0_LO                                    0x034A00
#define R_034A04_GDS_PERFCOUNTER0_HI                                    0x034A04
#define R_034A08_GDS_PERFCOUNTER1_LO                                    0x034A08
#define R_034A0C_GDS_PERFCOUNTER1_HI                                    0x034A0C
#define R_034A10_GDS_PERFCOUNTER2_LO                                    0x034A10
#define R_034A14_GDS_PERFCOUNTER2_HI                                    0x034A14
#define R_034A18_GDS_PERFCOUNTER3_LO                                    0x034A18
#define R_034A1C_GDS_PERFCOUNTER3_HI                                    0x034A1C
#define R_034B00_TA_PERFCOUNTER0_LO                                     0x034B00
#define R_034B04_TA_PERFCOUNTER0_HI                                     0x034B04
#define R_034B08_TA_PERFCOUNTER1_LO                                     0x034B08
#define R_034B0C_TA_PERFCOUNTER1_HI                                     0x034B0C
#define R_034C00_TD_PERFCOUNTER0_LO                                     0x034C00
#define R_034C04_TD_PERFCOUNTER0_HI                                     0x034C04
#define R_034C08_TD_PERFCOUNTER1_LO                                     0x034C08
#define R_034C0C_TD_PERFCOUNTER1_HI                                     0x034C0C
#define R_034D00_TCP_PERFCOUNTER0_LO                                    0x034D00
#define R_034D04_TCP_PERFCOUNTER0_HI                                    0x034D04
#define R_034D08_TCP_PERFCOUNTER1_LO                                    0x034D08
#define R_034D0C_TCP_PERFCOUNTER1_HI                                    0x034D0C
#define R_034D10_TCP_PERFCOUNTER2_LO                                    0x034D10
#define R_034D14_TCP_PERFCOUNTER2_HI                                    0x034D14
#define R_034D18_TCP_PERFCOUNTER3_LO                                    0x034D18
#define R_034D1C_TCP_PERFCOUNTER3_HI                                    0x034D1C
#define R_034E00_TCC_PERFCOUNTER0_LO                                    0x034E00
#define R_034E04_TCC_PERFCOUNTER0_HI                                    0x034E04
#define R_034E08_TCC_PERFCOUNTER1_LO                                    0x034E08
#define R_034E0C_TCC_PERFCOUNTER1_HI                                    0x034E0C
#define R_034E10_TCC_PERFCOUNTER2_LO                                    0x034E10
#define R_034E14_TCC_PERFCOUNTER2_HI                                    0x034E14
#define R_034E18_TCC_PERFCOUNTER3_LO                                    0x034E18
#define R_034E1C_TCC_PERFCOUNTER3_HI                                    0x034E1C
#define R_034E40_TCA_PERFCOUNTER0_LO                                    0x034E40
#define R_034E44_TCA_PERFCOUNTER0_HI                                    0x034E44
#define R_034E48_TCA_PERFCOUNTER1_LO                                    0x034E48
#define R_034E4C_TCA_PERFCOUNTER1_HI                                    0x034E4C
#define R_034E50_TCA_PERFCOUNTER2_LO                                    0x034E50
#define R_034E54_TCA_PERFCOUNTER2_HI                                    0x034E54
#define R_034E58_TCA_PERFCOUNTER3_LO                                    0x034E58
#define R_034E5C_TCA_PERFCOUNTER3_HI                                    0x034E5C
#define R_035018_CB_PERFCOUNTER0_LO                                     0x035018
#define R_03501C_CB_PERFCOUNTER0_HI                                     0x03501C
#define R_035020_CB_PERFCOUNTER1_LO                                     0x035020
#define R_035024_CB_PERFCOUNTER1_HI                                     0x035024
#define R_035028_CB_PERFCOUNTER2_LO                                     0x035028
#define R_03502C_CB_PERFCOUNTER2_HI                                     0x03502C
#define R_035030_CB_PERFCOUNTER3_LO                                     0x035030
#define R_035034_CB_PERFCOUNTER3_HI                                     0x035034
#define R_035100_DB_PERFCOUNTER0_LO                                     0x035100
#define R_035104_DB_PERFCOUNTER0_HI                                     0x035104
#define R_035108_DB_PERFCOUNTER1_LO                                     0x035108
#define R_03510C_DB_PERFCOUNTER1_HI                                     0x03510C
#define R_035110_DB_PERFCOUNTER2_LO                                     0x035110
#define R_035114_DB_PERFCOUNTER2_HI                                     0x035114
#define R_035118_DB_PERFCOUNTER3_LO                                     0x035118
#define R_03511C_DB_PERFCOUNTER3_HI                                     0x03511C
#define R_035200_RLC_PERFCOUNTER0_LO                                    0x035200
#define R_035204_RLC_PERFCOUNTER0_HI                                    0x035204
#define R_035208_RLC_PERFCOUNTER1_LO                                    0x035208
#define R_03520C_RLC_PERFCOUNTER1_HI                                    0x03520C
#define R_036000_CPG_PERFCOUNTER1_SELECT                                0x036000
#define R_036004_CPG_PERFCOUNTER0_SELECT1                               0x036004
#define   S_036004_PERF_SEL2(x)                                       (((unsigned)(x) & 0x3F) << 0)
#define   G_036004_PERF_SEL2(x)                                       (((x) >> 0) & 0x3F)
#define   C_036004_PERF_SEL2                                          0xFFFFFFC0
#define   S_036004_PERF_SEL3(x)                                       (((unsigned)(x) & 0x3F) << 10)
#define   G_036004_PERF_SEL3(x)                                       (((x) >> 10) & 0x3F)
#define   C_036004_PERF_SEL3                                          0xFFFF03FF
#define R_036008_CPG_PERFCOUNTER0_SELECT                                0x036008
#define   S_036008_PERF_SEL(x)                                        (((unsigned)(x) & 0x3F) << 0)
#define   G_036008_PERF_SEL(x)                                        (((x) >> 0) & 0x3F)
#define   C_036008_PERF_SEL                                           0xFFFFFFC0
#define   S_036008_PERF_SEL1(x)                                       (((unsigned)(x) & 0x3F) << 10)
#define   G_036008_PERF_SEL1(x)                                       (((x) >> 10) & 0x3F)
#define   C_036008_PERF_SEL1                                          0xFFFF03FF
#define   S_036008_CNTR_MODE(x)                                       (((unsigned)(x) & 0x0F) << 20)
#define   G_036008_CNTR_MODE(x)                                       (((x) >> 20) & 0x0F)
#define   C_036008_CNTR_MODE                                          0xFF0FFFFF
#define R_03600C_CPC_PERFCOUNTER1_SELECT                                0x03600C
#define R_036010_CPC_PERFCOUNTER0_SELECT1                               0x036010
#define   S_036010_PERF_SEL2(x)                                       (((unsigned)(x) & 0x3F) << 0)
#define   G_036010_PERF_SEL2(x)                                       (((x) >> 0) & 0x3F)
#define   C_036010_PERF_SEL2                                          0xFFFFFFC0
#define   S_036010_PERF_SEL3(x)                                       (((unsigned)(x) & 0x3F) << 10)
#define   G_036010_PERF_SEL3(x)                                       (((x) >> 10) & 0x3F)
#define   C_036010_PERF_SEL3                                          0xFFFF03FF
#define R_036014_CPF_PERFCOUNTER1_SELECT                                0x036014
#define R_036018_CPF_PERFCOUNTER0_SELECT1                               0x036018
#define   S_036018_PERF_SEL2(x)                                       (((unsigned)(x) & 0x3F) << 0)
#define   G_036018_PERF_SEL2(x)                                       (((x) >> 0) & 0x3F)
#define   C_036018_PERF_SEL2                                          0xFFFFFFC0
#define   S_036018_PERF_SEL3(x)                                       (((unsigned)(x) & 0x3F) << 10)
#define   G_036018_PERF_SEL3(x)                                       (((x) >> 10) & 0x3F)
#define   C_036018_PERF_SEL3                                          0xFFFF03FF
#define R_03601C_CPF_PERFCOUNTER0_SELECT                                0x03601C
#define   S_03601C_PERF_SEL(x)                                        (((unsigned)(x) & 0x3F) << 0)
#define   G_03601C_PERF_SEL(x)                                        (((x) >> 0) & 0x3F)
#define   C_03601C_PERF_SEL                                           0xFFFFFFC0
#define   S_03601C_PERF_SEL1(x)                                       (((unsigned)(x) & 0x3F) << 10)
#define   G_03601C_PERF_SEL1(x)                                       (((x) >> 10) & 0x3F)
#define   C_03601C_PERF_SEL1                                          0xFFFF03FF
#define   S_03601C_CNTR_MODE(x)                                       (((unsigned)(x) & 0x0F) << 20)
#define   G_03601C_CNTR_MODE(x)                                       (((x) >> 20) & 0x0F)
#define   C_03601C_CNTR_MODE                                          0xFF0FFFFF
#define R_036020_CP_PERFMON_CNTL                                        0x036020
#define   S_036020_PERFMON_STATE(x)                                   (((unsigned)(x) & 0x0F) << 0)
#define   G_036020_PERFMON_STATE(x)                                   (((x) >> 0) & 0x0F)
#define   C_036020_PERFMON_STATE                                      0xFFFFFFF0
#define     V_036020_DISABLE_AND_RESET                              0x00
#define     V_036020_START_COUNTING                                 0x01
#define     V_036020_STOP_COUNTING                                  0x02
#define   S_036020_SPM_PERFMON_STATE(x)                               (((unsigned)(x) & 0x0F) << 4)
#define   G_036020_SPM_PERFMON_STATE(x)                               (((x) >> 4) & 0x0F)
#define   C_036020_SPM_PERFMON_STATE                                  0xFFFFFF0F
#define   S_036020_PERFMON_ENABLE_MODE(x)                             (((unsigned)(x) & 0x03) << 8)
#define   G_036020_PERFMON_ENABLE_MODE(x)                             (((x) >> 8) & 0x03)
#define   C_036020_PERFMON_ENABLE_MODE                                0xFFFFFCFF
#define   S_036020_PERFMON_SAMPLE_ENABLE(x)                           (((unsigned)(x) & 0x1) << 10)
#define   G_036020_PERFMON_SAMPLE_ENABLE(x)                           (((x) >> 10) & 0x1)
#define   C_036020_PERFMON_SAMPLE_ENABLE                              0xFFFFFBFF
#define R_036024_CPC_PERFCOUNTER0_SELECT                                0x036024
#define   S_036024_PERF_SEL(x)                                        (((unsigned)(x) & 0x3F) << 0)
#define   G_036024_PERF_SEL(x)                                        (((x) >> 0) & 0x3F)
#define   C_036024_PERF_SEL                                           0xFFFFFFC0
#define   S_036024_PERF_SEL1(x)                                       (((unsigned)(x) & 0x3F) << 10)
#define   G_036024_PERF_SEL1(x)                                       (((x) >> 10) & 0x3F)
#define   C_036024_PERF_SEL1                                          0xFFFF03FF
#define   S_036024_CNTR_MODE(x)                                       (((unsigned)(x) & 0x0F) << 20)
#define   G_036024_CNTR_MODE(x)                                       (((x) >> 20) & 0x0F)
#define   C_036024_CNTR_MODE                                          0xFF0FFFFF
#define R_036100_GRBM_PERFCOUNTER0_SELECT                               0x036100
#define   S_036100_PERF_SEL(x)                                        (((unsigned)(x) & 0x3F) << 0)
#define   G_036100_PERF_SEL(x)                                        (((x) >> 0) & 0x3F)
#define   C_036100_PERF_SEL                                           0xFFFFFFC0
#define   S_036100_DB_CLEAN_USER_DEFINED_MASK(x)                      (((unsigned)(x) & 0x1) << 10)
#define   G_036100_DB_CLEAN_USER_DEFINED_MASK(x)                      (((x) >> 10) & 0x1)
#define   C_036100_DB_CLEAN_USER_DEFINED_MASK                         0xFFFFFBFF
#define   S_036100_CB_CLEAN_USER_DEFINED_MASK(x)                      (((unsigned)(x) & 0x1) << 11)
#define   G_036100_CB_CLEAN_USER_DEFINED_MASK(x)                      (((x) >> 11) & 0x1)
#define   C_036100_CB_CLEAN_USER_DEFINED_MASK                         0xFFFFF7FF
#define   S_036100_VGT_BUSY_USER_DEFINED_MASK(x)                      (((unsigned)(x) & 0x1) << 12)
#define   G_036100_VGT_BUSY_USER_DEFINED_MASK(x)                      (((x) >> 12) & 0x1)
#define   C_036100_VGT_BUSY_USER_DEFINED_MASK                         0xFFFFEFFF
#define   S_036100_TA_BUSY_USER_DEFINED_MASK(x)                       (((unsigned)(x) & 0x1) << 13)
#define   G_036100_TA_BUSY_USER_DEFINED_MASK(x)                       (((x) >> 13) & 0x1)
#define   C_036100_TA_BUSY_USER_DEFINED_MASK                          0xFFFFDFFF
#define   S_036100_SX_BUSY_USER_DEFINED_MASK(x)                       (((unsigned)(x) & 0x1) << 14)
#define   G_036100_SX_BUSY_USER_DEFINED_MASK(x)                       (((x) >> 14) & 0x1)
#define   C_036100_SX_BUSY_USER_DEFINED_MASK                          0xFFFFBFFF
#define   S_036100_SPI_BUSY_USER_DEFINED_MASK(x)                      (((unsigned)(x) & 0x1) << 16)
#define   G_036100_SPI_BUSY_USER_DEFINED_MASK(x)                      (((x) >> 16) & 0x1)
#define   C_036100_SPI_BUSY_USER_DEFINED_MASK                         0xFFFEFFFF
#define   S_036100_SC_BUSY_USER_DEFINED_MASK(x)                       (((unsigned)(x) & 0x1) << 17)
#define   G_036100_SC_BUSY_USER_DEFINED_MASK(x)                       (((x) >> 17) & 0x1)
#define   C_036100_SC_BUSY_USER_DEFINED_MASK                          0xFFFDFFFF
#define   S_036100_PA_BUSY_USER_DEFINED_MASK(x)                       (((unsigned)(x) & 0x1) << 18)
#define   G_036100_PA_BUSY_USER_DEFINED_MASK(x)                       (((x) >> 18) & 0x1)
#define   C_036100_PA_BUSY_USER_DEFINED_MASK                          0xFFFBFFFF
#define   S_036100_GRBM_BUSY_USER_DEFINED_MASK(x)                     (((unsigned)(x) & 0x1) << 19)
#define   G_036100_GRBM_BUSY_USER_DEFINED_MASK(x)                     (((x) >> 19) & 0x1)
#define   C_036100_GRBM_BUSY_USER_DEFINED_MASK                        0xFFF7FFFF
#define   S_036100_DB_BUSY_USER_DEFINED_MASK(x)                       (((unsigned)(x) & 0x1) << 20)
#define   G_036100_DB_BUSY_USER_DEFINED_MASK(x)                       (((x) >> 20) & 0x1)
#define   C_036100_DB_BUSY_USER_DEFINED_MASK                          0xFFEFFFFF
#define   S_036100_CB_BUSY_USER_DEFINED_MASK(x)                       (((unsigned)(x) & 0x1) << 21)
#define   G_036100_CB_BUSY_USER_DEFINED_MASK(x)                       (((x) >> 21) & 0x1)
#define   C_036100_CB_BUSY_USER_DEFINED_MASK                          0xFFDFFFFF
#define   S_036100_CP_BUSY_USER_DEFINED_MASK(x)                       (((unsigned)(x) & 0x1) << 22)
#define   G_036100_CP_BUSY_USER_DEFINED_MASK(x)                       (((x) >> 22) & 0x1)
#define   C_036100_CP_BUSY_USER_DEFINED_MASK                          0xFFBFFFFF
#define   S_036100_IA_BUSY_USER_DEFINED_MASK(x)                       (((unsigned)(x) & 0x1) << 23)
#define   G_036100_IA_BUSY_USER_DEFINED_MASK(x)                       (((x) >> 23) & 0x1)
#define   C_036100_IA_BUSY_USER_DEFINED_MASK                          0xFF7FFFFF
#define   S_036100_GDS_BUSY_USER_DEFINED_MASK(x)                      (((unsigned)(x) & 0x1) << 24)
#define   G_036100_GDS_BUSY_USER_DEFINED_MASK(x)                      (((x) >> 24) & 0x1)
#define   C_036100_GDS_BUSY_USER_DEFINED_MASK                         0xFEFFFFFF
#define   S_036100_BCI_BUSY_USER_DEFINED_MASK(x)                      (((unsigned)(x) & 0x1) << 25)
#define   G_036100_BCI_BUSY_USER_DEFINED_MASK(x)                      (((x) >> 25) & 0x1)
#define   C_036100_BCI_BUSY_USER_DEFINED_MASK                         0xFDFFFFFF
#define   S_036100_RLC_BUSY_USER_DEFINED_MASK(x)                      (((unsigned)(x) & 0x1) << 26)
#define   G_036100_RLC_BUSY_USER_DEFINED_MASK(x)                      (((x) >> 26) & 0x1)
#define   C_036100_RLC_BUSY_USER_DEFINED_MASK                         0xFBFFFFFF
#define   S_036100_TC_BUSY_USER_DEFINED_MASK(x)                       (((unsigned)(x) & 0x1) << 27)
#define   G_036100_TC_BUSY_USER_DEFINED_MASK(x)                       (((x) >> 27) & 0x1)
#define   C_036100_TC_BUSY_USER_DEFINED_MASK                          0xF7FFFFFF
#define   S_036100_WD_BUSY_USER_DEFINED_MASK(x)                       (((unsigned)(x) & 0x1) << 28)
#define   G_036100_WD_BUSY_USER_DEFINED_MASK(x)                       (((x) >> 28) & 0x1)
#define   C_036100_WD_BUSY_USER_DEFINED_MASK                          0xEFFFFFFF
#define R_036104_GRBM_PERFCOUNTER1_SELECT                               0x036104
#define R_036108_GRBM_SE0_PERFCOUNTER_SELECT                            0x036108
#define   S_036108_PERF_SEL(x)                                        (((unsigned)(x) & 0x3F) << 0)
#define   G_036108_PERF_SEL(x)                                        (((x) >> 0) & 0x3F)
#define   C_036108_PERF_SEL                                           0xFFFFFFC0
#define   S_036108_DB_CLEAN_USER_DEFINED_MASK(x)                      (((unsigned)(x) & 0x1) << 10)
#define   G_036108_DB_CLEAN_USER_DEFINED_MASK(x)                      (((x) >> 10) & 0x1)
#define   C_036108_DB_CLEAN_USER_DEFINED_MASK                         0xFFFFFBFF
#define   S_036108_CB_CLEAN_USER_DEFINED_MASK(x)                      (((unsigned)(x) & 0x1) << 11)
#define   G_036108_CB_CLEAN_USER_DEFINED_MASK(x)                      (((x) >> 11) & 0x1)
#define   C_036108_CB_CLEAN_USER_DEFINED_MASK                         0xFFFFF7FF
#define   S_036108_TA_BUSY_USER_DEFINED_MASK(x)                       (((unsigned)(x) & 0x1) << 12)
#define   G_036108_TA_BUSY_USER_DEFINED_MASK(x)                       (((x) >> 12) & 0x1)
#define   C_036108_TA_BUSY_USER_DEFINED_MASK                          0xFFFFEFFF
#define   S_036108_SX_BUSY_USER_DEFINED_MASK(x)                       (((unsigned)(x) & 0x1) << 13)
#define   G_036108_SX_BUSY_USER_DEFINED_MASK(x)                       (((x) >> 13) & 0x1)
#define   C_036108_SX_BUSY_USER_DEFINED_MASK                          0xFFFFDFFF
#define   S_036108_SPI_BUSY_USER_DEFINED_MASK(x)                      (((unsigned)(x) & 0x1) << 15)
#define   G_036108_SPI_BUSY_USER_DEFINED_MASK(x)                      (((x) >> 15) & 0x1)
#define   C_036108_SPI_BUSY_USER_DEFINED_MASK                         0xFFFF7FFF
#define   S_036108_SC_BUSY_USER_DEFINED_MASK(x)                       (((unsigned)(x) & 0x1) << 16)
#define   G_036108_SC_BUSY_USER_DEFINED_MASK(x)                       (((x) >> 16) & 0x1)
#define   C_036108_SC_BUSY_USER_DEFINED_MASK                          0xFFFEFFFF
#define   S_036108_DB_BUSY_USER_DEFINED_MASK(x)                       (((unsigned)(x) & 0x1) << 17)
#define   G_036108_DB_BUSY_USER_DEFINED_MASK(x)                       (((x) >> 17) & 0x1)
#define   C_036108_DB_BUSY_USER_DEFINED_MASK                          0xFFFDFFFF
#define   S_036108_CB_BUSY_USER_DEFINED_MASK(x)                       (((unsigned)(x) & 0x1) << 18)
#define   G_036108_CB_BUSY_USER_DEFINED_MASK(x)                       (((x) >> 18) & 0x1)
#define   C_036108_CB_BUSY_USER_DEFINED_MASK                          0xFFFBFFFF
#define   S_036108_VGT_BUSY_USER_DEFINED_MASK(x)                      (((unsigned)(x) & 0x1) << 19)
#define   G_036108_VGT_BUSY_USER_DEFINED_MASK(x)                      (((x) >> 19) & 0x1)
#define   C_036108_VGT_BUSY_USER_DEFINED_MASK                         0xFFF7FFFF
#define   S_036108_PA_BUSY_USER_DEFINED_MASK(x)                       (((unsigned)(x) & 0x1) << 20)
#define   G_036108_PA_BUSY_USER_DEFINED_MASK(x)                       (((x) >> 20) & 0x1)
#define   C_036108_PA_BUSY_USER_DEFINED_MASK                          0xFFEFFFFF
#define   S_036108_BCI_BUSY_USER_DEFINED_MASK(x)                      (((unsigned)(x) & 0x1) << 21)
#define   G_036108_BCI_BUSY_USER_DEFINED_MASK(x)                      (((x) >> 21) & 0x1)
#define   C_036108_BCI_BUSY_USER_DEFINED_MASK                         0xFFDFFFFF
#define R_03610C_GRBM_SE1_PERFCOUNTER_SELECT                            0x03610C
#define   S_03610C_PERF_SEL(x)                                        (((unsigned)(x) & 0x3F) << 0)
#define   G_03610C_PERF_SEL(x)                                        (((x) >> 0) & 0x3F)
#define   C_03610C_PERF_SEL                                           0xFFFFFFC0
#define   S_03610C_DB_CLEAN_USER_DEFINED_MASK(x)                      (((unsigned)(x) & 0x1) << 10)
#define   G_03610C_DB_CLEAN_USER_DEFINED_MASK(x)                      (((x) >> 10) & 0x1)
#define   C_03610C_DB_CLEAN_USER_DEFINED_MASK                         0xFFFFFBFF
#define   S_03610C_CB_CLEAN_USER_DEFINED_MASK(x)                      (((unsigned)(x) & 0x1) << 11)
#define   G_03610C_CB_CLEAN_USER_DEFINED_MASK(x)                      (((x) >> 11) & 0x1)
#define   C_03610C_CB_CLEAN_USER_DEFINED_MASK                         0xFFFFF7FF
#define   S_03610C_TA_BUSY_USER_DEFINED_MASK(x)                       (((unsigned)(x) & 0x1) << 12)
#define   G_03610C_TA_BUSY_USER_DEFINED_MASK(x)                       (((x) >> 12) & 0x1)
#define   C_03610C_TA_BUSY_USER_DEFINED_MASK                          0xFFFFEFFF
#define   S_03610C_SX_BUSY_USER_DEFINED_MASK(x)                       (((unsigned)(x) & 0x1) << 13)
#define   G_03610C_SX_BUSY_USER_DEFINED_MASK(x)                       (((x) >> 13) & 0x1)
#define   C_03610C_SX_BUSY_USER_DEFINED_MASK                          0xFFFFDFFF
#define   S_03610C_SPI_BUSY_USER_DEFINED_MASK(x)                      (((unsigned)(x) & 0x1) << 15)
#define   G_03610C_SPI_BUSY_USER_DEFINED_MASK(x)                      (((x) >> 15) & 0x1)
#define   C_03610C_SPI_BUSY_USER_DEFINED_MASK                         0xFFFF7FFF
#define   S_03610C_SC_BUSY_USER_DEFINED_MASK(x)                       (((unsigned)(x) & 0x1) << 16)
#define   G_03610C_SC_BUSY_USER_DEFINED_MASK(x)                       (((x) >> 16) & 0x1)
#define   C_03610C_SC_BUSY_USER_DEFINED_MASK                          0xFFFEFFFF
#define   S_03610C_DB_BUSY_USER_DEFINED_MASK(x)                       (((unsigned)(x) & 0x1) << 17)
#define   G_03610C_DB_BUSY_USER_DEFINED_MASK(x)                       (((x) >> 17) & 0x1)
#define   C_03610C_DB_BUSY_USER_DEFINED_MASK                          0xFFFDFFFF
#define   S_03610C_CB_BUSY_USER_DEFINED_MASK(x)                       (((unsigned)(x) & 0x1) << 18)
#define   G_03610C_CB_BUSY_USER_DEFINED_MASK(x)                       (((x) >> 18) & 0x1)
#define   C_03610C_CB_BUSY_USER_DEFINED_MASK                          0xFFFBFFFF
#define   S_03610C_VGT_BUSY_USER_DEFINED_MASK(x)                      (((unsigned)(x) & 0x1) << 19)
#define   G_03610C_VGT_BUSY_USER_DEFINED_MASK(x)                      (((x) >> 19) & 0x1)
#define   C_03610C_VGT_BUSY_USER_DEFINED_MASK                         0xFFF7FFFF
#define   S_03610C_PA_BUSY_USER_DEFINED_MASK(x)                       (((unsigned)(x) & 0x1) << 20)
#define   G_03610C_PA_BUSY_USER_DEFINED_MASK(x)                       (((x) >> 20) & 0x1)
#define   C_03610C_PA_BUSY_USER_DEFINED_MASK                          0xFFEFFFFF
#define   S_03610C_BCI_BUSY_USER_DEFINED_MASK(x)                      (((unsigned)(x) & 0x1) << 21)
#define   G_03610C_BCI_BUSY_USER_DEFINED_MASK(x)                      (((x) >> 21) & 0x1)
#define   C_03610C_BCI_BUSY_USER_DEFINED_MASK                         0xFFDFFFFF
#define R_036110_GRBM_SE2_PERFCOUNTER_SELECT                            0x036110
#define   S_036110_PERF_SEL(x)                                        (((unsigned)(x) & 0x3F) << 0)
#define   G_036110_PERF_SEL(x)                                        (((x) >> 0) & 0x3F)
#define   C_036110_PERF_SEL                                           0xFFFFFFC0
#define   S_036110_DB_CLEAN_USER_DEFINED_MASK(x)                      (((unsigned)(x) & 0x1) << 10)
#define   G_036110_DB_CLEAN_USER_DEFINED_MASK(x)                      (((x) >> 10) & 0x1)
#define   C_036110_DB_CLEAN_USER_DEFINED_MASK                         0xFFFFFBFF
#define   S_036110_CB_CLEAN_USER_DEFINED_MASK(x)                      (((unsigned)(x) & 0x1) << 11)
#define   G_036110_CB_CLEAN_USER_DEFINED_MASK(x)                      (((x) >> 11) & 0x1)
#define   C_036110_CB_CLEAN_USER_DEFINED_MASK                         0xFFFFF7FF
#define   S_036110_TA_BUSY_USER_DEFINED_MASK(x)                       (((unsigned)(x) & 0x1) << 12)
#define   G_036110_TA_BUSY_USER_DEFINED_MASK(x)                       (((x) >> 12) & 0x1)
#define   C_036110_TA_BUSY_USER_DEFINED_MASK                          0xFFFFEFFF
#define   S_036110_SX_BUSY_USER_DEFINED_MASK(x)                       (((unsigned)(x) & 0x1) << 13)
#define   G_036110_SX_BUSY_USER_DEFINED_MASK(x)                       (((x) >> 13) & 0x1)
#define   C_036110_SX_BUSY_USER_DEFINED_MASK                          0xFFFFDFFF
#define   S_036110_SPI_BUSY_USER_DEFINED_MASK(x)                      (((unsigned)(x) & 0x1) << 15)
#define   G_036110_SPI_BUSY_USER_DEFINED_MASK(x)                      (((x) >> 15) & 0x1)
#define   C_036110_SPI_BUSY_USER_DEFINED_MASK                         0xFFFF7FFF
#define   S_036110_SC_BUSY_USER_DEFINED_MASK(x)                       (((unsigned)(x) & 0x1) << 16)
#define   G_036110_SC_BUSY_USER_DEFINED_MASK(x)                       (((x) >> 16) & 0x1)
#define   C_036110_SC_BUSY_USER_DEFINED_MASK                          0xFFFEFFFF
#define   S_036110_DB_BUSY_USER_DEFINED_MASK(x)                       (((unsigned)(x) & 0x1) << 17)
#define   G_036110_DB_BUSY_USER_DEFINED_MASK(x)                       (((x) >> 17) & 0x1)
#define   C_036110_DB_BUSY_USER_DEFINED_MASK                          0xFFFDFFFF
#define   S_036110_CB_BUSY_USER_DEFINED_MASK(x)                       (((unsigned)(x) & 0x1) << 18)
#define   G_036110_CB_BUSY_USER_DEFINED_MASK(x)                       (((x) >> 18) & 0x1)
#define   C_036110_CB_BUSY_USER_DEFINED_MASK                          0xFFFBFFFF
#define   S_036110_VGT_BUSY_USER_DEFINED_MASK(x)                      (((unsigned)(x) & 0x1) << 19)
#define   G_036110_VGT_BUSY_USER_DEFINED_MASK(x)                      (((x) >> 19) & 0x1)
#define   C_036110_VGT_BUSY_USER_DEFINED_MASK                         0xFFF7FFFF
#define   S_036110_PA_BUSY_USER_DEFINED_MASK(x)                       (((unsigned)(x) & 0x1) << 20)
#define   G_036110_PA_BUSY_USER_DEFINED_MASK(x)                       (((x) >> 20) & 0x1)
#define   C_036110_PA_BUSY_USER_DEFINED_MASK                          0xFFEFFFFF
#define   S_036110_BCI_BUSY_USER_DEFINED_MASK(x)                      (((unsigned)(x) & 0x1) << 21)
#define   G_036110_BCI_BUSY_USER_DEFINED_MASK(x)                      (((x) >> 21) & 0x1)
#define   C_036110_BCI_BUSY_USER_DEFINED_MASK                         0xFFDFFFFF
#define R_036114_GRBM_SE3_PERFCOUNTER_SELECT                            0x036114
#define   S_036114_PERF_SEL(x)                                        (((unsigned)(x) & 0x3F) << 0)
#define   G_036114_PERF_SEL(x)                                        (((x) >> 0) & 0x3F)
#define   C_036114_PERF_SEL                                           0xFFFFFFC0
#define   S_036114_DB_CLEAN_USER_DEFINED_MASK(x)                      (((unsigned)(x) & 0x1) << 10)
#define   G_036114_DB_CLEAN_USER_DEFINED_MASK(x)                      (((x) >> 10) & 0x1)
#define   C_036114_DB_CLEAN_USER_DEFINED_MASK                         0xFFFFFBFF
#define   S_036114_CB_CLEAN_USER_DEFINED_MASK(x)                      (((unsigned)(x) & 0x1) << 11)
#define   G_036114_CB_CLEAN_USER_DEFINED_MASK(x)                      (((x) >> 11) & 0x1)
#define   C_036114_CB_CLEAN_USER_DEFINED_MASK                         0xFFFFF7FF
#define   S_036114_TA_BUSY_USER_DEFINED_MASK(x)                       (((unsigned)(x) & 0x1) << 12)
#define   G_036114_TA_BUSY_USER_DEFINED_MASK(x)                       (((x) >> 12) & 0x1)
#define   C_036114_TA_BUSY_USER_DEFINED_MASK                          0xFFFFEFFF
#define   S_036114_SX_BUSY_USER_DEFINED_MASK(x)                       (((unsigned)(x) & 0x1) << 13)
#define   G_036114_SX_BUSY_USER_DEFINED_MASK(x)                       (((x) >> 13) & 0x1)
#define   C_036114_SX_BUSY_USER_DEFINED_MASK                          0xFFFFDFFF
#define   S_036114_SPI_BUSY_USER_DEFINED_MASK(x)                      (((unsigned)(x) & 0x1) << 15)
#define   G_036114_SPI_BUSY_USER_DEFINED_MASK(x)                      (((x) >> 15) & 0x1)
#define   C_036114_SPI_BUSY_USER_DEFINED_MASK                         0xFFFF7FFF
#define   S_036114_SC_BUSY_USER_DEFINED_MASK(x)                       (((unsigned)(x) & 0x1) << 16)
#define   G_036114_SC_BUSY_USER_DEFINED_MASK(x)                       (((x) >> 16) & 0x1)
#define   C_036114_SC_BUSY_USER_DEFINED_MASK                          0xFFFEFFFF
#define   S_036114_DB_BUSY_USER_DEFINED_MASK(x)                       (((unsigned)(x) & 0x1) << 17)
#define   G_036114_DB_BUSY_USER_DEFINED_MASK(x)                       (((x) >> 17) & 0x1)
#define   C_036114_DB_BUSY_USER_DEFINED_MASK                          0xFFFDFFFF
#define   S_036114_CB_BUSY_USER_DEFINED_MASK(x)                       (((unsigned)(x) & 0x1) << 18)
#define   G_036114_CB_BUSY_USER_DEFINED_MASK(x)                       (((x) >> 18) & 0x1)
#define   C_036114_CB_BUSY_USER_DEFINED_MASK                          0xFFFBFFFF
#define   S_036114_VGT_BUSY_USER_DEFINED_MASK(x)                      (((unsigned)(x) & 0x1) << 19)
#define   G_036114_VGT_BUSY_USER_DEFINED_MASK(x)                      (((x) >> 19) & 0x1)
#define   C_036114_VGT_BUSY_USER_DEFINED_MASK                         0xFFF7FFFF
#define   S_036114_PA_BUSY_USER_DEFINED_MASK(x)                       (((unsigned)(x) & 0x1) << 20)
#define   G_036114_PA_BUSY_USER_DEFINED_MASK(x)                       (((x) >> 20) & 0x1)
#define   C_036114_PA_BUSY_USER_DEFINED_MASK                          0xFFEFFFFF
#define   S_036114_BCI_BUSY_USER_DEFINED_MASK(x)                      (((unsigned)(x) & 0x1) << 21)
#define   G_036114_BCI_BUSY_USER_DEFINED_MASK(x)                      (((x) >> 21) & 0x1)
#define   C_036114_BCI_BUSY_USER_DEFINED_MASK                         0xFFDFFFFF
#define R_036200_WD_PERFCOUNTER0_SELECT                                 0x036200
#define   S_036200_PERF_SEL(x)                                        (((unsigned)(x) & 0xFF) << 0)
#define   G_036200_PERF_SEL(x)                                        (((x) >> 0) & 0xFF)
#define   C_036200_PERF_SEL                                           0xFFFFFF00
#define   S_036200_PERF_MODE(x)                                       (((unsigned)(x) & 0x0F) << 28)
#define   G_036200_PERF_MODE(x)                                       (((x) >> 28) & 0x0F)
#define   C_036200_PERF_MODE                                          0x0FFFFFFF
#define R_036204_WD_PERFCOUNTER1_SELECT                                 0x036204
#define R_036208_WD_PERFCOUNTER2_SELECT                                 0x036208
#define R_03620C_WD_PERFCOUNTER3_SELECT                                 0x03620C
#define R_036210_IA_PERFCOUNTER0_SELECT                                 0x036210
#define   S_036210_PERF_SEL(x)                                        (((unsigned)(x) & 0x3FF) << 0)
#define   G_036210_PERF_SEL(x)                                        (((x) >> 0) & 0x3FF)
#define   C_036210_PERF_SEL                                           0xFFFFFC00
#define   S_036210_PERF_SEL1(x)                                       (((unsigned)(x) & 0x3FF) << 10)
#define   G_036210_PERF_SEL1(x)                                       (((x) >> 10) & 0x3FF)
#define   C_036210_PERF_SEL1                                          0xFFF003FF
#define   S_036210_CNTR_MODE(x)                                       (((unsigned)(x) & 0x0F) << 20)
#define   G_036210_CNTR_MODE(x)                                       (((x) >> 20) & 0x0F)
#define   C_036210_CNTR_MODE                                          0xFF0FFFFF
#define   S_036210_PERF_MODE1(x)                                      (((unsigned)(x) & 0x0F) << 24)
#define   G_036210_PERF_MODE1(x)                                      (((x) >> 24) & 0x0F)
#define   C_036210_PERF_MODE1                                         0xF0FFFFFF
#define   S_036210_PERF_MODE(x)                                       (((unsigned)(x) & 0x0F) << 28)
#define   G_036210_PERF_MODE(x)                                       (((x) >> 28) & 0x0F)
#define   C_036210_PERF_MODE                                          0x0FFFFFFF
#define R_036214_IA_PERFCOUNTER1_SELECT                                 0x036214
#define R_036218_IA_PERFCOUNTER2_SELECT                                 0x036218
#define R_03621C_IA_PERFCOUNTER3_SELECT                                 0x03621C
#define R_036220_IA_PERFCOUNTER0_SELECT1                                0x036220
#define   S_036220_PERF_SEL2(x)                                       (((unsigned)(x) & 0x3FF) << 0)
#define   G_036220_PERF_SEL2(x)                                       (((x) >> 0) & 0x3FF)
#define   C_036220_PERF_SEL2                                          0xFFFFFC00
#define   S_036220_PERF_SEL3(x)                                       (((unsigned)(x) & 0x3FF) << 10)
#define   G_036220_PERF_SEL3(x)                                       (((x) >> 10) & 0x3FF)
#define   C_036220_PERF_SEL3                                          0xFFF003FF
#define   S_036220_PERF_MODE3(x)                                      (((unsigned)(x) & 0x0F) << 24)
#define   G_036220_PERF_MODE3(x)                                      (((x) >> 24) & 0x0F)
#define   C_036220_PERF_MODE3                                         0xF0FFFFFF
#define   S_036220_PERF_MODE2(x)                                      (((unsigned)(x) & 0x0F) << 28)
#define   G_036220_PERF_MODE2(x)                                      (((x) >> 28) & 0x0F)
#define   C_036220_PERF_MODE2                                         0x0FFFFFFF
#define R_036230_VGT_PERFCOUNTER0_SELECT                                0x036230
#define   S_036230_PERF_SEL(x)                                        (((unsigned)(x) & 0x3FF) << 0)
#define   G_036230_PERF_SEL(x)                                        (((x) >> 0) & 0x3FF)
#define   C_036230_PERF_SEL                                           0xFFFFFC00
#define   S_036230_PERF_SEL1(x)                                       (((unsigned)(x) & 0x3FF) << 10)
#define   G_036230_PERF_SEL1(x)                                       (((x) >> 10) & 0x3FF)
#define   C_036230_PERF_SEL1                                          0xFFF003FF
#define   S_036230_CNTR_MODE(x)                                       (((unsigned)(x) & 0x0F) << 20)
#define   G_036230_CNTR_MODE(x)                                       (((x) >> 20) & 0x0F)
#define   C_036230_CNTR_MODE                                          0xFF0FFFFF
#define   S_036230_PERF_MODE1(x)                                      (((unsigned)(x) & 0x0F) << 24)
#define   G_036230_PERF_MODE1(x)                                      (((x) >> 24) & 0x0F)
#define   C_036230_PERF_MODE1                                         0xF0FFFFFF
#define   S_036230_PERF_MODE(x)                                       (((unsigned)(x) & 0x0F) << 28)
#define   G_036230_PERF_MODE(x)                                       (((x) >> 28) & 0x0F)
#define   C_036230_PERF_MODE                                          0x0FFFFFFF
#define R_036234_VGT_PERFCOUNTER1_SELECT                                0x036234
#define R_036238_VGT_PERFCOUNTER2_SELECT                                0x036238
#define R_03623C_VGT_PERFCOUNTER3_SELECT                                0x03623C
#define R_036240_VGT_PERFCOUNTER0_SELECT1                               0x036240
#define   S_036240_PERF_SEL2(x)                                       (((unsigned)(x) & 0x3FF) << 0)
#define   G_036240_PERF_SEL2(x)                                       (((x) >> 0) & 0x3FF)
#define   C_036240_PERF_SEL2                                          0xFFFFFC00
#define   S_036240_PERF_SEL3(x)                                       (((unsigned)(x) & 0x3FF) << 10)
#define   G_036240_PERF_SEL3(x)                                       (((x) >> 10) & 0x3FF)
#define   C_036240_PERF_SEL3                                          0xFFF003FF
#define   S_036240_PERF_MODE3(x)                                      (((unsigned)(x) & 0x0F) << 24)
#define   G_036240_PERF_MODE3(x)                                      (((x) >> 24) & 0x0F)
#define   C_036240_PERF_MODE3                                         0xF0FFFFFF
#define   S_036240_PERF_MODE2(x)                                      (((unsigned)(x) & 0x0F) << 28)
#define   G_036240_PERF_MODE2(x)                                      (((x) >> 28) & 0x0F)
#define   C_036240_PERF_MODE2                                         0x0FFFFFFF
#define R_036244_VGT_PERFCOUNTER1_SELECT1                               0x036244
#define R_036250_VGT_PERFCOUNTER_SEID_MASK                              0x036250
#define   S_036250_PERF_SEID_IGNORE_MASK(x)                           (((unsigned)(x) & 0xFF) << 0)
#define   G_036250_PERF_SEID_IGNORE_MASK(x)                           (((x) >> 0) & 0xFF)
#define   C_036250_PERF_SEID_IGNORE_MASK                              0xFFFFFF00
#define R_036400_PA_SU_PERFCOUNTER0_SELECT                              0x036400
#define   S_036400_PERF_SEL(x)                                        (((unsigned)(x) & 0x3FF) << 0)
#define   G_036400_PERF_SEL(x)                                        (((x) >> 0) & 0x3FF)
#define   C_036400_PERF_SEL                                           0xFFFFFC00
#define   S_036400_PERF_SEL1(x)                                       (((unsigned)(x) & 0x3FF) << 10)
#define   G_036400_PERF_SEL1(x)                                       (((x) >> 10) & 0x3FF)
#define   C_036400_PERF_SEL1                                          0xFFF003FF
#define   S_036400_CNTR_MODE(x)                                       (((unsigned)(x) & 0x0F) << 20)
#define   G_036400_CNTR_MODE(x)                                       (((x) >> 20) & 0x0F)
#define   C_036400_CNTR_MODE                                          0xFF0FFFFF
#define R_036404_PA_SU_PERFCOUNTER0_SELECT1                             0x036404
#define   S_036404_PERF_SEL2(x)                                       (((unsigned)(x) & 0x3FF) << 0)
#define   G_036404_PERF_SEL2(x)                                       (((x) >> 0) & 0x3FF)
#define   C_036404_PERF_SEL2                                          0xFFFFFC00
#define   S_036404_PERF_SEL3(x)                                       (((unsigned)(x) & 0x3FF) << 10)
#define   G_036404_PERF_SEL3(x)                                       (((x) >> 10) & 0x3FF)
#define   C_036404_PERF_SEL3                                          0xFFF003FF
#define R_036408_PA_SU_PERFCOUNTER1_SELECT                              0x036408
#define R_03640C_PA_SU_PERFCOUNTER1_SELECT1                             0x03640C
#define R_036410_PA_SU_PERFCOUNTER2_SELECT                              0x036410
#define R_036414_PA_SU_PERFCOUNTER3_SELECT                              0x036414
#define R_036500_PA_SC_PERFCOUNTER0_SELECT                              0x036500
#define   S_036500_PERF_SEL(x)                                        (((unsigned)(x) & 0x3FF) << 0)
#define   G_036500_PERF_SEL(x)                                        (((x) >> 0) & 0x3FF)
#define   C_036500_PERF_SEL                                           0xFFFFFC00
#define   S_036500_PERF_SEL1(x)                                       (((unsigned)(x) & 0x3FF) << 10)
#define   G_036500_PERF_SEL1(x)                                       (((x) >> 10) & 0x3FF)
#define   C_036500_PERF_SEL1                                          0xFFF003FF
#define   S_036500_CNTR_MODE(x)                                       (((unsigned)(x) & 0x0F) << 20)
#define   G_036500_CNTR_MODE(x)                                       (((x) >> 20) & 0x0F)
#define   C_036500_CNTR_MODE                                          0xFF0FFFFF
#define R_036504_PA_SC_PERFCOUNTER0_SELECT1                             0x036504
#define   S_036504_PERF_SEL2(x)                                       (((unsigned)(x) & 0x3FF) << 0)
#define   G_036504_PERF_SEL2(x)                                       (((x) >> 0) & 0x3FF)
#define   C_036504_PERF_SEL2                                          0xFFFFFC00
#define   S_036504_PERF_SEL3(x)                                       (((unsigned)(x) & 0x3FF) << 10)
#define   G_036504_PERF_SEL3(x)                                       (((x) >> 10) & 0x3FF)
#define   C_036504_PERF_SEL3                                          0xFFF003FF
#define R_036508_PA_SC_PERFCOUNTER1_SELECT                              0x036508
#define R_03650C_PA_SC_PERFCOUNTER2_SELECT                              0x03650C
#define R_036510_PA_SC_PERFCOUNTER3_SELECT                              0x036510
#define R_036514_PA_SC_PERFCOUNTER4_SELECT                              0x036514
#define R_036518_PA_SC_PERFCOUNTER5_SELECT                              0x036518
#define R_03651C_PA_SC_PERFCOUNTER6_SELECT                              0x03651C
#define R_036520_PA_SC_PERFCOUNTER7_SELECT                              0x036520
#define R_036600_SPI_PERFCOUNTER0_SELECT                                0x036600
#define   S_036600_PERF_SEL(x)                                        (((unsigned)(x) & 0x3FF) << 0)
#define   G_036600_PERF_SEL(x)                                        (((x) >> 0) & 0x3FF)
#define   C_036600_PERF_SEL                                           0xFFFFFC00
#define   S_036600_PERF_SEL1(x)                                       (((unsigned)(x) & 0x3FF) << 10)
#define   G_036600_PERF_SEL1(x)                                       (((x) >> 10) & 0x3FF)
#define   C_036600_PERF_SEL1                                          0xFFF003FF
#define   S_036600_CNTR_MODE(x)                                       (((unsigned)(x) & 0x0F) << 20)
#define   G_036600_CNTR_MODE(x)                                       (((x) >> 20) & 0x0F)
#define   C_036600_CNTR_MODE                                          0xFF0FFFFF
#define R_036604_SPI_PERFCOUNTER1_SELECT                                0x036604
#define R_036608_SPI_PERFCOUNTER2_SELECT                                0x036608
#define R_03660C_SPI_PERFCOUNTER3_SELECT                                0x03660C
#define R_036610_SPI_PERFCOUNTER0_SELECT1                               0x036610
#define   S_036610_PERF_SEL2(x)                                       (((unsigned)(x) & 0x3FF) << 0)
#define   G_036610_PERF_SEL2(x)                                       (((x) >> 0) & 0x3FF)
#define   C_036610_PERF_SEL2                                          0xFFFFFC00
#define   S_036610_PERF_SEL3(x)                                       (((unsigned)(x) & 0x3FF) << 10)
#define   G_036610_PERF_SEL3(x)                                       (((x) >> 10) & 0x3FF)
#define   C_036610_PERF_SEL3                                          0xFFF003FF
#define R_036614_SPI_PERFCOUNTER1_SELECT1                               0x036614
#define R_036618_SPI_PERFCOUNTER2_SELECT1                               0x036618
#define R_03661C_SPI_PERFCOUNTER3_SELECT1                               0x03661C
#define R_036620_SPI_PERFCOUNTER4_SELECT                                0x036620
#define R_036624_SPI_PERFCOUNTER5_SELECT                                0x036624
#define R_036628_SPI_PERFCOUNTER_BINS                                   0x036628
#define   S_036628_BIN0_MIN(x)                                        (((unsigned)(x) & 0x0F) << 0)
#define   G_036628_BIN0_MIN(x)                                        (((x) >> 0) & 0x0F)
#define   C_036628_BIN0_MIN                                           0xFFFFFFF0
#define   S_036628_BIN0_MAX(x)                                        (((unsigned)(x) & 0x0F) << 4)
#define   G_036628_BIN0_MAX(x)                                        (((x) >> 4) & 0x0F)
#define   C_036628_BIN0_MAX                                           0xFFFFFF0F
#define   S_036628_BIN1_MIN(x)                                        (((unsigned)(x) & 0x0F) << 8)
#define   G_036628_BIN1_MIN(x)                                        (((x) >> 8) & 0x0F)
#define   C_036628_BIN1_MIN                                           0xFFFFF0FF
#define   S_036628_BIN1_MAX(x)                                        (((unsigned)(x) & 0x0F) << 12)
#define   G_036628_BIN1_MAX(x)                                        (((x) >> 12) & 0x0F)
#define   C_036628_BIN1_MAX                                           0xFFFF0FFF
#define   S_036628_BIN2_MIN(x)                                        (((unsigned)(x) & 0x0F) << 16)
#define   G_036628_BIN2_MIN(x)                                        (((x) >> 16) & 0x0F)
#define   C_036628_BIN2_MIN                                           0xFFF0FFFF
#define   S_036628_BIN2_MAX(x)                                        (((unsigned)(x) & 0x0F) << 20)
#define   G_036628_BIN2_MAX(x)                                        (((x) >> 20) & 0x0F)
#define   C_036628_BIN2_MAX                                           0xFF0FFFFF
#define   S_036628_BIN3_MIN(x)                                        (((unsigned)(x) & 0x0F) << 24)
#define   G_036628_BIN3_MIN(x)                                        (((x) >> 24) & 0x0F)
#define   C_036628_BIN3_MIN                                           0xF0FFFFFF
#define   S_036628_BIN3_MAX(x)                                        (((unsigned)(x) & 0x0F) << 28)
#define   G_036628_BIN3_MAX(x)                                        (((x) >> 28) & 0x0F)
#define   C_036628_BIN3_MAX                                           0x0FFFFFFF
#define R_036700_SQ_PERFCOUNTER0_SELECT                                 0x036700
#define   S_036700_PERF_SEL(x)                                        (((unsigned)(x) & 0x1FF) << 0)
#define   G_036700_PERF_SEL(x)                                        (((x) >> 0) & 0x1FF)
#define   C_036700_PERF_SEL                                           0xFFFFFE00
#define   S_036700_SQC_BANK_MASK(x)                                   (((unsigned)(x) & 0x0F) << 12)
#define   G_036700_SQC_BANK_MASK(x)                                   (((x) >> 12) & 0x0F)
#define   C_036700_SQC_BANK_MASK                                      0xFFFF0FFF
#define   S_036700_SQC_CLIENT_MASK(x)                                 (((unsigned)(x) & 0x0F) << 16)
#define   G_036700_SQC_CLIENT_MASK(x)                                 (((x) >> 16) & 0x0F)
#define   C_036700_SQC_CLIENT_MASK                                    0xFFF0FFFF
#define   S_036700_SPM_MODE(x)                                        (((unsigned)(x) & 0x0F) << 20)
#define   G_036700_SPM_MODE(x)                                        (((x) >> 20) & 0x0F)
#define   C_036700_SPM_MODE                                           0xFF0FFFFF
#define   S_036700_SIMD_MASK(x)                                       (((unsigned)(x) & 0x0F) << 24)
#define   G_036700_SIMD_MASK(x)                                       (((x) >> 24) & 0x0F)
#define   C_036700_SIMD_MASK                                          0xF0FFFFFF
#define   S_036700_PERF_MODE(x)                                       (((unsigned)(x) & 0x0F) << 28)
#define   G_036700_PERF_MODE(x)                                       (((x) >> 28) & 0x0F)
#define   C_036700_PERF_MODE                                          0x0FFFFFFF
#define R_036704_SQ_PERFCOUNTER1_SELECT                                 0x036704
#define R_036708_SQ_PERFCOUNTER2_SELECT                                 0x036708
#define R_03670C_SQ_PERFCOUNTER3_SELECT                                 0x03670C
#define R_036710_SQ_PERFCOUNTER4_SELECT                                 0x036710
#define R_036714_SQ_PERFCOUNTER5_SELECT                                 0x036714
#define R_036718_SQ_PERFCOUNTER6_SELECT                                 0x036718
#define R_03671C_SQ_PERFCOUNTER7_SELECT                                 0x03671C
#define R_036720_SQ_PERFCOUNTER8_SELECT                                 0x036720
#define R_036724_SQ_PERFCOUNTER9_SELECT                                 0x036724
#define R_036728_SQ_PERFCOUNTER10_SELECT                                0x036728
#define R_03672C_SQ_PERFCOUNTER11_SELECT                                0x03672C
#define R_036730_SQ_PERFCOUNTER12_SELECT                                0x036730
#define R_036734_SQ_PERFCOUNTER13_SELECT                                0x036734
#define R_036738_SQ_PERFCOUNTER14_SELECT                                0x036738
#define R_03673C_SQ_PERFCOUNTER15_SELECT                                0x03673C
#define R_036780_SQ_PERFCOUNTER_CTRL                                    0x036780
#define   S_036780_PS_EN(x)                                           (((unsigned)(x) & 0x1) << 0)
#define   G_036780_PS_EN(x)                                           (((x) >> 0) & 0x1)
#define   C_036780_PS_EN                                              0xFFFFFFFE
#define   S_036780_VS_EN(x)                                           (((unsigned)(x) & 0x1) << 1)
#define   G_036780_VS_EN(x)                                           (((x) >> 1) & 0x1)
#define   C_036780_VS_EN                                              0xFFFFFFFD
#define   S_036780_GS_EN(x)                                           (((unsigned)(x) & 0x1) << 2)
#define   G_036780_GS_EN(x)                                           (((x) >> 2) & 0x1)
#define   C_036780_GS_EN                                              0xFFFFFFFB
#define   S_036780_ES_EN(x)                                           (((unsigned)(x) & 0x1) << 3)
#define   G_036780_ES_EN(x)                                           (((x) >> 3) & 0x1)
#define   C_036780_ES_EN                                              0xFFFFFFF7
#define   S_036780_HS_EN(x)                                           (((unsigned)(x) & 0x1) << 4)
#define   G_036780_HS_EN(x)                                           (((x) >> 4) & 0x1)
#define   C_036780_HS_EN                                              0xFFFFFFEF
#define   S_036780_LS_EN(x)                                           (((unsigned)(x) & 0x1) << 5)
#define   G_036780_LS_EN(x)                                           (((x) >> 5) & 0x1)
#define   C_036780_LS_EN                                              0xFFFFFFDF
#define   S_036780_CS_EN(x)                                           (((unsigned)(x) & 0x1) << 6)
#define   G_036780_CS_EN(x)                                           (((x) >> 6) & 0x1)
#define   C_036780_CS_EN                                              0xFFFFFFBF
#define   S_036780_CNTR_RATE(x)                                       (((unsigned)(x) & 0x1F) << 8)
#define   G_036780_CNTR_RATE(x)                                       (((x) >> 8) & 0x1F)
#define   C_036780_CNTR_RATE                                          0xFFFFE0FF
#define   S_036780_DISABLE_FLUSH(x)                                   (((unsigned)(x) & 0x1) << 13)
#define   G_036780_DISABLE_FLUSH(x)                                   (((x) >> 13) & 0x1)
#define   C_036780_DISABLE_FLUSH                                      0xFFFFDFFF
#define R_036784_SQ_PERFCOUNTER_MASK                                    0x036784
#define   S_036784_SH0_MASK(x)                                        (((unsigned)(x) & 0xFFFF) << 0)
#define   G_036784_SH0_MASK(x)                                        (((x) >> 0) & 0xFFFF)
#define   C_036784_SH0_MASK                                           0xFFFF0000
#define   S_036784_SH1_MASK(x)                                        (((unsigned)(x) & 0xFFFF) << 16)
#define   G_036784_SH1_MASK(x)                                        (((x) >> 16) & 0xFFFF)
#define   C_036784_SH1_MASK                                           0x0000FFFF
#define R_036788_SQ_PERFCOUNTER_CTRL2                                   0x036788
#define   S_036788_FORCE_EN(x)                                        (((unsigned)(x) & 0x1) << 0)
#define   G_036788_FORCE_EN(x)                                        (((x) >> 0) & 0x1)
#define   C_036788_FORCE_EN                                           0xFFFFFFFE
#define R_036900_SX_PERFCOUNTER0_SELECT                                 0x036900
#define   S_036900_PERFCOUNTER_SELECT(x)                              (((unsigned)(x) & 0x3FF) << 0)
#define   G_036900_PERFCOUNTER_SELECT(x)                              (((x) >> 0) & 0x3FF)
#define   C_036900_PERFCOUNTER_SELECT                                 0xFFFFFC00
#define   S_036900_PERFCOUNTER_SELECT1(x)                             (((unsigned)(x) & 0x3FF) << 10)
#define   G_036900_PERFCOUNTER_SELECT1(x)                             (((x) >> 10) & 0x3FF)
#define   C_036900_PERFCOUNTER_SELECT1                                0xFFF003FF
#define   S_036900_CNTR_MODE(x)                                       (((unsigned)(x) & 0x0F) << 20)
#define   G_036900_CNTR_MODE(x)                                       (((x) >> 20) & 0x0F)
#define   C_036900_CNTR_MODE                                          0xFF0FFFFF
#define R_036904_SX_PERFCOUNTER1_SELECT                                 0x036904
#define R_036908_SX_PERFCOUNTER2_SELECT                                 0x036908
#define R_03690C_SX_PERFCOUNTER3_SELECT                                 0x03690C
#define R_036910_SX_PERFCOUNTER0_SELECT1                                0x036910
#define   S_036910_PERFCOUNTER_SELECT2(x)                             (((unsigned)(x) & 0x3FF) << 0)
#define   G_036910_PERFCOUNTER_SELECT2(x)                             (((x) >> 0) & 0x3FF)
#define   C_036910_PERFCOUNTER_SELECT2                                0xFFFFFC00
#define   S_036910_PERFCOUNTER_SELECT3(x)                             (((unsigned)(x) & 0x3FF) << 10)
#define   G_036910_PERFCOUNTER_SELECT3(x)                             (((x) >> 10) & 0x3FF)
#define   C_036910_PERFCOUNTER_SELECT3                                0xFFF003FF
#define R_036914_SX_PERFCOUNTER1_SELECT1                                0x036914
#define R_036A00_GDS_PERFCOUNTER0_SELECT                                0x036A00
#define   S_036A00_PERFCOUNTER_SELECT(x)                              (((unsigned)(x) & 0x3FF) << 0)
#define   G_036A00_PERFCOUNTER_SELECT(x)                              (((x) >> 0) & 0x3FF)
#define   C_036A00_PERFCOUNTER_SELECT                                 0xFFFFFC00
#define   S_036A00_PERFCOUNTER_SELECT1(x)                             (((unsigned)(x) & 0x3FF) << 10)
#define   G_036A00_PERFCOUNTER_SELECT1(x)                             (((x) >> 10) & 0x3FF)
#define   C_036A00_PERFCOUNTER_SELECT1                                0xFFF003FF
#define   S_036A00_CNTR_MODE(x)                                       (((unsigned)(x) & 0x0F) << 20)
#define   G_036A00_CNTR_MODE(x)                                       (((x) >> 20) & 0x0F)
#define   C_036A00_CNTR_MODE                                          0xFF0FFFFF
#define R_036A04_GDS_PERFCOUNTER1_SELECT                                0x036A04
#define R_036A08_GDS_PERFCOUNTER2_SELECT                                0x036A08
#define R_036A0C_GDS_PERFCOUNTER3_SELECT                                0x036A0C
#define R_036A10_GDS_PERFCOUNTER0_SELECT1                               0x036A10
#define   S_036A10_PERFCOUNTER_SELECT2(x)                             (((unsigned)(x) & 0x3FF) << 0)
#define   G_036A10_PERFCOUNTER_SELECT2(x)                             (((x) >> 0) & 0x3FF)
#define   C_036A10_PERFCOUNTER_SELECT2                                0xFFFFFC00
#define   S_036A10_PERFCOUNTER_SELECT3(x)                             (((unsigned)(x) & 0x3FF) << 10)
#define   G_036A10_PERFCOUNTER_SELECT3(x)                             (((x) >> 10) & 0x3FF)
#define   C_036A10_PERFCOUNTER_SELECT3                                0xFFF003FF
#define R_036B00_TA_PERFCOUNTER0_SELECT                                 0x036B00
#define   S_036B00_PERF_SEL(x)                                        (((unsigned)(x) & 0xFF) << 0)
#define   G_036B00_PERF_SEL(x)                                        (((x) >> 0) & 0xFF)
#define   C_036B00_PERF_SEL                                           0xFFFFFF00
#define   S_036B00_PERF_SEL1(x)                                       (((unsigned)(x) & 0xFF) << 10)
#define   G_036B00_PERF_SEL1(x)                                       (((x) >> 10) & 0xFF)
#define   C_036B00_PERF_SEL1                                          0xFFFC03FF
#define   S_036B00_CNTR_MODE(x)                                       (((unsigned)(x) & 0x0F) << 20)
#define   G_036B00_CNTR_MODE(x)                                       (((x) >> 20) & 0x0F)
#define   C_036B00_CNTR_MODE                                          0xFF0FFFFF
#define   S_036B00_PERF_MODE1(x)                                      (((unsigned)(x) & 0x0F) << 24)
#define   G_036B00_PERF_MODE1(x)                                      (((x) >> 24) & 0x0F)
#define   C_036B00_PERF_MODE1                                         0xF0FFFFFF
#define   S_036B00_PERF_MODE(x)                                       (((unsigned)(x) & 0x0F) << 28)
#define   G_036B00_PERF_MODE(x)                                       (((x) >> 28) & 0x0F)
#define   C_036B00_PERF_MODE                                          0x0FFFFFFF
#define R_036B04_TA_PERFCOUNTER0_SELECT1                                0x036B04
#define   S_036B04_PERF_SEL2(x)                                       (((unsigned)(x) & 0xFF) << 0)
#define   G_036B04_PERF_SEL2(x)                                       (((x) >> 0) & 0xFF)
#define   C_036B04_PERF_SEL2                                          0xFFFFFF00
#define   S_036B04_PERF_SEL3(x)                                       (((unsigned)(x) & 0xFF) << 10)
#define   G_036B04_PERF_SEL3(x)                                       (((x) >> 10) & 0xFF)
#define   C_036B04_PERF_SEL3                                          0xFFFC03FF
#define   S_036B04_PERF_MODE3(x)                                      (((unsigned)(x) & 0x0F) << 24)
#define   G_036B04_PERF_MODE3(x)                                      (((x) >> 24) & 0x0F)
#define   C_036B04_PERF_MODE3                                         0xF0FFFFFF
#define   S_036B04_PERF_MODE2(x)                                      (((unsigned)(x) & 0x0F) << 28)
#define   G_036B04_PERF_MODE2(x)                                      (((x) >> 28) & 0x0F)
#define   C_036B04_PERF_MODE2                                         0x0FFFFFFF
#define R_036B08_TA_PERFCOUNTER1_SELECT                                 0x036B08
#define R_036C00_TD_PERFCOUNTER0_SELECT                                 0x036C00
#define   S_036C00_PERF_SEL(x)                                        (((unsigned)(x) & 0xFF) << 0)
#define   G_036C00_PERF_SEL(x)                                        (((x) >> 0) & 0xFF)
#define   C_036C00_PERF_SEL                                           0xFFFFFF00
#define   S_036C00_PERF_SEL1(x)                                       (((unsigned)(x) & 0xFF) << 10)
#define   G_036C00_PERF_SEL1(x)                                       (((x) >> 10) & 0xFF)
#define   C_036C00_PERF_SEL1                                          0xFFFC03FF
#define   S_036C00_CNTR_MODE(x)                                       (((unsigned)(x) & 0x0F) << 20)
#define   G_036C00_CNTR_MODE(x)                                       (((x) >> 20) & 0x0F)
#define   C_036C00_CNTR_MODE                                          0xFF0FFFFF
#define   S_036C00_PERF_MODE1(x)                                      (((unsigned)(x) & 0x0F) << 24)
#define   G_036C00_PERF_MODE1(x)                                      (((x) >> 24) & 0x0F)
#define   C_036C00_PERF_MODE1                                         0xF0FFFFFF
#define   S_036C00_PERF_MODE(x)                                       (((unsigned)(x) & 0x0F) << 28)
#define   G_036C00_PERF_MODE(x)                                       (((x) >> 28) & 0x0F)
#define   C_036C00_PERF_MODE                                          0x0FFFFFFF
#define R_036C04_TD_PERFCOUNTER0_SELECT1                                0x036C04
#define   S_036C04_PERF_SEL2(x)                                       (((unsigned)(x) & 0xFF) << 0)
#define   G_036C04_PERF_SEL2(x)                                       (((x) >> 0) & 0xFF)
#define   C_036C04_PERF_SEL2                                          0xFFFFFF00
#define   S_036C04_PERF_SEL3(x)                                       (((unsigned)(x) & 0xFF) << 10)
#define   G_036C04_PERF_SEL3(x)                                       (((x) >> 10) & 0xFF)
#define   C_036C04_PERF_SEL3                                          0xFFFC03FF
#define   S_036C04_PERF_MODE3(x)                                      (((unsigned)(x) & 0x0F) << 24)
#define   G_036C04_PERF_MODE3(x)                                      (((x) >> 24) & 0x0F)
#define   C_036C04_PERF_MODE3                                         0xF0FFFFFF
#define   S_036C04_PERF_MODE2(x)                                      (((unsigned)(x) & 0x0F) << 28)
#define   G_036C04_PERF_MODE2(x)                                      (((x) >> 28) & 0x0F)
#define   C_036C04_PERF_MODE2                                         0x0FFFFFFF
#define R_036C08_TD_PERFCOUNTER1_SELECT                                 0x036C08
#define R_036D00_TCP_PERFCOUNTER0_SELECT                                0x036D00
#define   S_036D00_PERF_SEL(x)                                        (((unsigned)(x) & 0x3FF) << 0)
#define   G_036D00_PERF_SEL(x)                                        (((x) >> 0) & 0x3FF)
#define   C_036D00_PERF_SEL                                           0xFFFFFC00
#define   S_036D00_PERF_SEL1(x)                                       (((unsigned)(x) & 0x3FF) << 10)
#define   G_036D00_PERF_SEL1(x)                                       (((x) >> 10) & 0x3FF)
#define   C_036D00_PERF_SEL1                                          0xFFF003FF
#define   S_036D00_CNTR_MODE(x)                                       (((unsigned)(x) & 0x0F) << 20)
#define   G_036D00_CNTR_MODE(x)                                       (((x) >> 20) & 0x0F)
#define   C_036D00_CNTR_MODE                                          0xFF0FFFFF
#define   S_036D00_PERF_MODE1(x)                                      (((unsigned)(x) & 0x0F) << 24)
#define   G_036D00_PERF_MODE1(x)                                      (((x) >> 24) & 0x0F)
#define   C_036D00_PERF_MODE1                                         0xF0FFFFFF
#define   S_036D00_PERF_MODE(x)                                       (((unsigned)(x) & 0x0F) << 28)
#define   G_036D00_PERF_MODE(x)                                       (((x) >> 28) & 0x0F)
#define   C_036D00_PERF_MODE                                          0x0FFFFFFF
#define R_036D04_TCP_PERFCOUNTER0_SELECT1                               0x036D04
#define   S_036D04_PERF_SEL2(x)                                       (((unsigned)(x) & 0x3FF) << 0)
#define   G_036D04_PERF_SEL2(x)                                       (((x) >> 0) & 0x3FF)
#define   C_036D04_PERF_SEL2                                          0xFFFFFC00
#define   S_036D04_PERF_SEL3(x)                                       (((unsigned)(x) & 0x3FF) << 10)
#define   G_036D04_PERF_SEL3(x)                                       (((x) >> 10) & 0x3FF)
#define   C_036D04_PERF_SEL3                                          0xFFF003FF
#define   S_036D04_PERF_MODE3(x)                                      (((unsigned)(x) & 0x0F) << 24)
#define   G_036D04_PERF_MODE3(x)                                      (((x) >> 24) & 0x0F)
#define   C_036D04_PERF_MODE3                                         0xF0FFFFFF
#define   S_036D04_PERF_MODE2(x)                                      (((unsigned)(x) & 0x0F) << 28)
#define   G_036D04_PERF_MODE2(x)                                      (((x) >> 28) & 0x0F)
#define   C_036D04_PERF_MODE2                                         0x0FFFFFFF
#define R_036D08_TCP_PERFCOUNTER1_SELECT                                0x036D08
#define R_036D0C_TCP_PERFCOUNTER1_SELECT1                               0x036D0C
#define R_036D10_TCP_PERFCOUNTER2_SELECT                                0x036D10
#define R_036D14_TCP_PERFCOUNTER3_SELECT                                0x036D14
#define R_036E00_TCC_PERFCOUNTER0_SELECT                                0x036E00
#define   S_036E00_PERF_SEL(x)                                        (((unsigned)(x) & 0x3FF) << 0)
#define   G_036E00_PERF_SEL(x)                                        (((x) >> 0) & 0x3FF)
#define   C_036E00_PERF_SEL                                           0xFFFFFC00
#define   S_036E00_PERF_SEL1(x)                                       (((unsigned)(x) & 0x3FF) << 10)
#define   G_036E00_PERF_SEL1(x)                                       (((x) >> 10) & 0x3FF)
#define   C_036E00_PERF_SEL1                                          0xFFF003FF
#define   S_036E00_CNTR_MODE(x)                                       (((unsigned)(x) & 0x0F) << 20)
#define   G_036E00_CNTR_MODE(x)                                       (((x) >> 20) & 0x0F)
#define   C_036E00_CNTR_MODE                                          0xFF0FFFFF
#define   S_036E00_PERF_MODE1(x)                                      (((unsigned)(x) & 0x0F) << 24)
#define   G_036E00_PERF_MODE1(x)                                      (((x) >> 24) & 0x0F)
#define   C_036E00_PERF_MODE1                                         0xF0FFFFFF
#define   S_036E00_PERF_MODE(x)                                       (((unsigned)(x) & 0x0F) << 28)
#define   G_036E00_PERF_MODE(x)                                       (((x) >> 28) & 0x0F)
#define   C_036E00_PERF_MODE                                          0x0FFFFFFF
#define R_036E04_TCC_PERFCOUNTER0_SELECT1                               0x036E04
#define   S_036E04_PERF_SEL2(x)                                       (((unsigned)(x) & 0x3FF) << 0)
#define   G_036E04_PERF_SEL2(x)                                       (((x) >> 0) & 0x3FF)
#define   C_036E04_PERF_SEL2                                          0xFFFFFC00
#define   S_036E04_PERF_SEL3(x)                                       (((unsigned)(x) & 0x3FF) << 10)
#define   G_036E04_PERF_SEL3(x)                                       (((x) >> 10) & 0x3FF)
#define   C_036E04_PERF_SEL3                                          0xFFF003FF
#define   S_036E04_PERF_MODE2(x)                                      (((unsigned)(x) & 0x0F) << 24)
#define   G_036E04_PERF_MODE2(x)                                      (((x) >> 24) & 0x0F)
#define   C_036E04_PERF_MODE2                                         0xF0FFFFFF
#define   S_036E04_PERF_MODE3(x)                                      (((unsigned)(x) & 0x0F) << 28)
#define   G_036E04_PERF_MODE3(x)                                      (((x) >> 28) & 0x0F)
#define   C_036E04_PERF_MODE3                                         0x0FFFFFFF
#define R_036E08_TCC_PERFCOUNTER1_SELECT                                0x036E08
#define R_036E0C_TCC_PERFCOUNTER1_SELECT1                               0x036E0C
#define R_036E10_TCC_PERFCOUNTER2_SELECT                                0x036E10
#define R_036E14_TCC_PERFCOUNTER3_SELECT                                0x036E14
#define R_036E40_TCA_PERFCOUNTER0_SELECT                                0x036E40
#define   S_036E40_PERF_SEL(x)                                        (((unsigned)(x) & 0x3FF) << 0)
#define   G_036E40_PERF_SEL(x)                                        (((x) >> 0) & 0x3FF)
#define   C_036E40_PERF_SEL                                           0xFFFFFC00
#define   S_036E40_PERF_SEL1(x)                                       (((unsigned)(x) & 0x3FF) << 10)
#define   G_036E40_PERF_SEL1(x)                                       (((x) >> 10) & 0x3FF)
#define   C_036E40_PERF_SEL1                                          0xFFF003FF
#define   S_036E40_CNTR_MODE(x)                                       (((unsigned)(x) & 0x0F) << 20)
#define   G_036E40_CNTR_MODE(x)                                       (((x) >> 20) & 0x0F)
#define   C_036E40_CNTR_MODE                                          0xFF0FFFFF
#define   S_036E40_PERF_MODE1(x)                                      (((unsigned)(x) & 0x0F) << 24)
#define   G_036E40_PERF_MODE1(x)                                      (((x) >> 24) & 0x0F)
#define   C_036E40_PERF_MODE1                                         0xF0FFFFFF
#define   S_036E40_PERF_MODE(x)                                       (((unsigned)(x) & 0x0F) << 28)
#define   G_036E40_PERF_MODE(x)                                       (((x) >> 28) & 0x0F)
#define   C_036E40_PERF_MODE                                          0x0FFFFFFF
#define R_036E44_TCA_PERFCOUNTER0_SELECT1                               0x036E44
#define   S_036E44_PERF_SEL2(x)                                       (((unsigned)(x) & 0x3FF) << 0)
#define   G_036E44_PERF_SEL2(x)                                       (((x) >> 0) & 0x3FF)
#define   C_036E44_PERF_SEL2                                          0xFFFFFC00
#define   S_036E44_PERF_SEL3(x)                                       (((unsigned)(x) & 0x3FF) << 10)
#define   G_036E44_PERF_SEL3(x)                                       (((x) >> 10) & 0x3FF)
#define   C_036E44_PERF_SEL3                                          0xFFF003FF
#define   S_036E44_PERF_MODE2(x)                                      (((unsigned)(x) & 0x0F) << 24)
#define   G_036E44_PERF_MODE2(x)                                      (((x) >> 24) & 0x0F)
#define   C_036E44_PERF_MODE2                                         0xF0FFFFFF
#define   S_036E44_PERF_MODE3(x)                                      (((unsigned)(x) & 0x0F) << 28)
#define   G_036E44_PERF_MODE3(x)                                      (((x) >> 28) & 0x0F)
#define   C_036E44_PERF_MODE3                                         0x0FFFFFFF
#define R_036E48_TCA_PERFCOUNTER1_SELECT                                0x036E48
#define R_036E4C_TCA_PERFCOUNTER1_SELECT1                               0x036E4C
#define R_036E50_TCA_PERFCOUNTER2_SELECT                                0x036E50
#define R_036E54_TCA_PERFCOUNTER3_SELECT                                0x036E54
#define R_037000_CB_PERFCOUNTER_FILTER                                  0x037000
#define   S_037000_OP_FILTER_ENABLE(x)                                (((unsigned)(x) & 0x1) << 0)
#define   G_037000_OP_FILTER_ENABLE(x)                                (((x) >> 0) & 0x1)
#define   C_037000_OP_FILTER_ENABLE                                   0xFFFFFFFE
#define   S_037000_OP_FILTER_SEL(x)                                   (((unsigned)(x) & 0x07) << 1)
#define   G_037000_OP_FILTER_SEL(x)                                   (((x) >> 1) & 0x07)
#define   C_037000_OP_FILTER_SEL                                      0xFFFFFFF1
#define   S_037000_FORMAT_FILTER_ENABLE(x)                            (((unsigned)(x) & 0x1) << 4)
#define   G_037000_FORMAT_FILTER_ENABLE(x)                            (((x) >> 4) & 0x1)
#define   C_037000_FORMAT_FILTER_ENABLE                               0xFFFFFFEF
#define   S_037000_FORMAT_FILTER_SEL(x)                               (((unsigned)(x) & 0x1F) << 5)
#define   G_037000_FORMAT_FILTER_SEL(x)                               (((x) >> 5) & 0x1F)
#define   C_037000_FORMAT_FILTER_SEL                                  0xFFFFFC1F
#define   S_037000_CLEAR_FILTER_ENABLE(x)                             (((unsigned)(x) & 0x1) << 10)
#define   G_037000_CLEAR_FILTER_ENABLE(x)                             (((x) >> 10) & 0x1)
#define   C_037000_CLEAR_FILTER_ENABLE                                0xFFFFFBFF
#define   S_037000_CLEAR_FILTER_SEL(x)                                (((unsigned)(x) & 0x1) << 11)
#define   G_037000_CLEAR_FILTER_SEL(x)                                (((x) >> 11) & 0x1)
#define   C_037000_CLEAR_FILTER_SEL                                   0xFFFFF7FF
#define   S_037000_MRT_FILTER_ENABLE(x)                               (((unsigned)(x) & 0x1) << 12)
#define   G_037000_MRT_FILTER_ENABLE(x)                               (((x) >> 12) & 0x1)
#define   C_037000_MRT_FILTER_ENABLE                                  0xFFFFEFFF
#define   S_037000_MRT_FILTER_SEL(x)                                  (((unsigned)(x) & 0x07) << 13)
#define   G_037000_MRT_FILTER_SEL(x)                                  (((x) >> 13) & 0x07)
#define   C_037000_MRT_FILTER_SEL                                     0xFFFF1FFF
#define   S_037000_NUM_SAMPLES_FILTER_ENABLE(x)                       (((unsigned)(x) & 0x1) << 17)
#define   G_037000_NUM_SAMPLES_FILTER_ENABLE(x)                       (((x) >> 17) & 0x1)
#define   C_037000_NUM_SAMPLES_FILTER_ENABLE                          0xFFFDFFFF
#define   S_037000_NUM_SAMPLES_FILTER_SEL(x)                          (((unsigned)(x) & 0x07) << 18)
#define   G_037000_NUM_SAMPLES_FILTER_SEL(x)                          (((x) >> 18) & 0x07)
#define   C_037000_NUM_SAMPLES_FILTER_SEL                             0xFFE3FFFF
#define   S_037000_NUM_FRAGMENTS_FILTER_ENABLE(x)                     (((unsigned)(x) & 0x1) << 21)
#define   G_037000_NUM_FRAGMENTS_FILTER_ENABLE(x)                     (((x) >> 21) & 0x1)
#define   C_037000_NUM_FRAGMENTS_FILTER_ENABLE                        0xFFDFFFFF
#define   S_037000_NUM_FRAGMENTS_FILTER_SEL(x)                        (((unsigned)(x) & 0x03) << 22)
#define   G_037000_NUM_FRAGMENTS_FILTER_SEL(x)                        (((x) >> 22) & 0x03)
#define   C_037000_NUM_FRAGMENTS_FILTER_SEL                           0xFF3FFFFF
#define R_037004_CB_PERFCOUNTER0_SELECT                                 0x037004
#define   S_037004_PERF_SEL(x)                                        (((unsigned)(x) & 0x1FF) << 0)
#define   G_037004_PERF_SEL(x)                                        (((x) >> 0) & 0x1FF)
#define   C_037004_PERF_SEL                                           0xFFFFFE00
#define   S_037004_PERF_SEL1(x)                                       (((unsigned)(x) & 0x1FF) << 10)
#define   G_037004_PERF_SEL1(x)                                       (((x) >> 10) & 0x1FF)
#define   C_037004_PERF_SEL1                                          0xFFF803FF
#define   S_037004_CNTR_MODE(x)                                       (((unsigned)(x) & 0x0F) << 20)
#define   G_037004_CNTR_MODE(x)                                       (((x) >> 20) & 0x0F)
#define   C_037004_CNTR_MODE                                          0xFF0FFFFF
#define   S_037004_PERF_MODE1(x)                                      (((unsigned)(x) & 0x0F) << 24)
#define   G_037004_PERF_MODE1(x)                                      (((x) >> 24) & 0x0F)
#define   C_037004_PERF_MODE1                                         0xF0FFFFFF
#define   S_037004_PERF_MODE(x)                                       (((unsigned)(x) & 0x0F) << 28)
#define   G_037004_PERF_MODE(x)                                       (((x) >> 28) & 0x0F)
#define   C_037004_PERF_MODE                                          0x0FFFFFFF
#define R_037008_CB_PERFCOUNTER0_SELECT1                                0x037008
#define   S_037008_PERF_SEL2(x)                                       (((unsigned)(x) & 0x1FF) << 0)
#define   G_037008_PERF_SEL2(x)                                       (((x) >> 0) & 0x1FF)
#define   C_037008_PERF_SEL2                                          0xFFFFFE00
#define   S_037008_PERF_SEL3(x)                                       (((unsigned)(x) & 0x1FF) << 10)
#define   G_037008_PERF_SEL3(x)                                       (((x) >> 10) & 0x1FF)
#define   C_037008_PERF_SEL3                                          0xFFF803FF
#define   S_037008_PERF_MODE3(x)                                      (((unsigned)(x) & 0x0F) << 24)
#define   G_037008_PERF_MODE3(x)                                      (((x) >> 24) & 0x0F)
#define   C_037008_PERF_MODE3                                         0xF0FFFFFF
#define   S_037008_PERF_MODE2(x)                                      (((unsigned)(x) & 0x0F) << 28)
#define   G_037008_PERF_MODE2(x)                                      (((x) >> 28) & 0x0F)
#define   C_037008_PERF_MODE2                                         0x0FFFFFFF
#define R_03700C_CB_PERFCOUNTER1_SELECT                                 0x03700C
#define R_037010_CB_PERFCOUNTER2_SELECT                                 0x037010
#define R_037014_CB_PERFCOUNTER3_SELECT                                 0x037014
#define R_037100_DB_PERFCOUNTER0_SELECT                                 0x037100
#define   S_037100_PERF_SEL(x)                                        (((unsigned)(x) & 0x3FF) << 0)
#define   G_037100_PERF_SEL(x)                                        (((x) >> 0) & 0x3FF)
#define   C_037100_PERF_SEL                                           0xFFFFFC00
#define   S_037100_PERF_SEL1(x)                                       (((unsigned)(x) & 0x3FF) << 10)
#define   G_037100_PERF_SEL1(x)                                       (((x) >> 10) & 0x3FF)
#define   C_037100_PERF_SEL1                                          0xFFF003FF
#define   S_037100_CNTR_MODE(x)                                       (((unsigned)(x) & 0x0F) << 20)
#define   G_037100_CNTR_MODE(x)                                       (((x) >> 20) & 0x0F)
#define   C_037100_CNTR_MODE                                          0xFF0FFFFF
#define   S_037100_PERF_MODE1(x)                                      (((unsigned)(x) & 0x0F) << 24)
#define   G_037100_PERF_MODE1(x)                                      (((x) >> 24) & 0x0F)
#define   C_037100_PERF_MODE1                                         0xF0FFFFFF
#define   S_037100_PERF_MODE(x)                                       (((unsigned)(x) & 0x0F) << 28)
#define   G_037100_PERF_MODE(x)                                       (((x) >> 28) & 0x0F)
#define   C_037100_PERF_MODE                                          0x0FFFFFFF
#define R_037104_DB_PERFCOUNTER0_SELECT1                                0x037104
#define   S_037104_PERF_SEL2(x)                                       (((unsigned)(x) & 0x3FF) << 0)
#define   G_037104_PERF_SEL2(x)                                       (((x) >> 0) & 0x3FF)
#define   C_037104_PERF_SEL2                                          0xFFFFFC00
#define   S_037104_PERF_SEL3(x)                                       (((unsigned)(x) & 0x3FF) << 10)
#define   G_037104_PERF_SEL3(x)                                       (((x) >> 10) & 0x3FF)
#define   C_037104_PERF_SEL3                                          0xFFF003FF
#define   S_037104_PERF_MODE3(x)                                      (((unsigned)(x) & 0x0F) << 24)
#define   G_037104_PERF_MODE3(x)                                      (((x) >> 24) & 0x0F)
#define   C_037104_PERF_MODE3                                         0xF0FFFFFF
#define   S_037104_PERF_MODE2(x)                                      (((unsigned)(x) & 0x0F) << 28)
#define   G_037104_PERF_MODE2(x)                                      (((x) >> 28) & 0x0F)
#define   C_037104_PERF_MODE2                                         0x0FFFFFFF
#define R_037108_DB_PERFCOUNTER1_SELECT                                 0x037108
#define R_03710C_DB_PERFCOUNTER1_SELECT1                                0x03710C
#define R_037110_DB_PERFCOUNTER2_SELECT                                 0x037110
#define R_037118_DB_PERFCOUNTER3_SELECT                                 0x037118
#define R_028000_DB_RENDER_CONTROL                                      0x028000
#define   S_028000_DEPTH_CLEAR_ENABLE(x)                              (((unsigned)(x) & 0x1) << 0)
#define   G_028000_DEPTH_CLEAR_ENABLE(x)                              (((x) >> 0) & 0x1)
#define   C_028000_DEPTH_CLEAR_ENABLE                                 0xFFFFFFFE
#define   S_028000_STENCIL_CLEAR_ENABLE(x)                            (((unsigned)(x) & 0x1) << 1)
#define   G_028000_STENCIL_CLEAR_ENABLE(x)                            (((x) >> 1) & 0x1)
#define   C_028000_STENCIL_CLEAR_ENABLE                               0xFFFFFFFD
#define   S_028000_DEPTH_COPY(x)                                      (((unsigned)(x) & 0x1) << 2)
#define   G_028000_DEPTH_COPY(x)                                      (((x) >> 2) & 0x1)
#define   C_028000_DEPTH_COPY                                         0xFFFFFFFB
#define   S_028000_STENCIL_COPY(x)                                    (((unsigned)(x) & 0x1) << 3)
#define   G_028000_STENCIL_COPY(x)                                    (((x) >> 3) & 0x1)
#define   C_028000_STENCIL_COPY                                       0xFFFFFFF7
#define   S_028000_RESUMMARIZE_ENABLE(x)                              (((unsigned)(x) & 0x1) << 4)
#define   G_028000_RESUMMARIZE_ENABLE(x)                              (((x) >> 4) & 0x1)
#define   C_028000_RESUMMARIZE_ENABLE                                 0xFFFFFFEF
#define   S_028000_STENCIL_COMPRESS_DISABLE(x)                        (((unsigned)(x) & 0x1) << 5)
#define   G_028000_STENCIL_COMPRESS_DISABLE(x)                        (((x) >> 5) & 0x1)
#define   C_028000_STENCIL_COMPRESS_DISABLE                           0xFFFFFFDF
#define   S_028000_DEPTH_COMPRESS_DISABLE(x)                          (((unsigned)(x) & 0x1) << 6)
#define   G_028000_DEPTH_COMPRESS_DISABLE(x)                          (((x) >> 6) & 0x1)
#define   C_028000_DEPTH_COMPRESS_DISABLE                             0xFFFFFFBF
#define   S_028000_COPY_CENTROID(x)                                   (((unsigned)(x) & 0x1) << 7)
#define   G_028000_COPY_CENTROID(x)                                   (((x) >> 7) & 0x1)
#define   C_028000_COPY_CENTROID                                      0xFFFFFF7F
#define   S_028000_COPY_SAMPLE(x)                                     (((unsigned)(x) & 0x0F) << 8)
#define   G_028000_COPY_SAMPLE(x)                                     (((x) >> 8) & 0x0F)
#define   C_028000_COPY_SAMPLE                                        0xFFFFF0FF
/* VI */
#define   S_028000_DECOMPRESS_ENABLE(x)                               (((unsigned)(x) & 0x1) << 12)
#define   G_028000_DECOMPRESS_ENABLE(x)                               (((x) >> 12) & 0x1)
#define   C_028000_DECOMPRESS_ENABLE                                  0xFFFFEFFF
/*    */
#define R_028004_DB_COUNT_CONTROL                                       0x028004
#define   S_028004_ZPASS_INCREMENT_DISABLE(x)                         (((unsigned)(x) & 0x1) << 0)
#define   G_028004_ZPASS_INCREMENT_DISABLE(x)                         (((x) >> 0) & 0x1)
#define   C_028004_ZPASS_INCREMENT_DISABLE                            0xFFFFFFFE
#define   S_028004_PERFECT_ZPASS_COUNTS(x)                            (((unsigned)(x) & 0x1) << 1)
#define   G_028004_PERFECT_ZPASS_COUNTS(x)                            (((x) >> 1) & 0x1)
#define   C_028004_PERFECT_ZPASS_COUNTS                               0xFFFFFFFD
#define   S_028004_SAMPLE_RATE(x)                                     (((unsigned)(x) & 0x07) << 4)
#define   G_028004_SAMPLE_RATE(x)                                     (((x) >> 4) & 0x07)
#define   C_028004_SAMPLE_RATE                                        0xFFFFFF8F
/* CIK */
#define   S_028004_ZPASS_ENABLE(x)                                    (((unsigned)(x) & 0x0F) << 8)
#define   G_028004_ZPASS_ENABLE(x)                                    (((x) >> 8) & 0x0F)
#define   C_028004_ZPASS_ENABLE                                       0xFFFFF0FF
#define   S_028004_ZFAIL_ENABLE(x)                                    (((unsigned)(x) & 0x0F) << 12)
#define   G_028004_ZFAIL_ENABLE(x)                                    (((x) >> 12) & 0x0F)
#define   C_028004_ZFAIL_ENABLE                                       0xFFFF0FFF
#define   S_028004_SFAIL_ENABLE(x)                                    (((unsigned)(x) & 0x0F) << 16)
#define   G_028004_SFAIL_ENABLE(x)                                    (((x) >> 16) & 0x0F)
#define   C_028004_SFAIL_ENABLE                                       0xFFF0FFFF
#define   S_028004_DBFAIL_ENABLE(x)                                   (((unsigned)(x) & 0x0F) << 20)
#define   G_028004_DBFAIL_ENABLE(x)                                   (((x) >> 20) & 0x0F)
#define   C_028004_DBFAIL_ENABLE                                      0xFF0FFFFF
#define   S_028004_SLICE_EVEN_ENABLE(x)                               (((unsigned)(x) & 0x0F) << 24)
#define   G_028004_SLICE_EVEN_ENABLE(x)                               (((x) >> 24) & 0x0F)
#define   C_028004_SLICE_EVEN_ENABLE                                  0xF0FFFFFF
#define   S_028004_SLICE_ODD_ENABLE(x)                                (((unsigned)(x) & 0x0F) << 28)
#define   G_028004_SLICE_ODD_ENABLE(x)                                (((x) >> 28) & 0x0F)
#define   C_028004_SLICE_ODD_ENABLE                                   0x0FFFFFFF
/*     */
#define R_028008_DB_DEPTH_VIEW                                          0x028008
#define   S_028008_SLICE_START(x)                                     (((unsigned)(x) & 0x7FF) << 0)
#define   G_028008_SLICE_START(x)                                     (((x) >> 0) & 0x7FF)
#define   C_028008_SLICE_START                                        0xFFFFF800
#define   S_028008_SLICE_MAX(x)                                       (((unsigned)(x) & 0x7FF) << 13)
#define   G_028008_SLICE_MAX(x)                                       (((x) >> 13) & 0x7FF)
#define   C_028008_SLICE_MAX                                          0xFF001FFF
#define   S_028008_Z_READ_ONLY(x)                                     (((unsigned)(x) & 0x1) << 24)
#define   G_028008_Z_READ_ONLY(x)                                     (((x) >> 24) & 0x1)
#define   C_028008_Z_READ_ONLY                                        0xFEFFFFFF
#define   S_028008_STENCIL_READ_ONLY(x)                               (((unsigned)(x) & 0x1) << 25)
#define   G_028008_STENCIL_READ_ONLY(x)                               (((x) >> 25) & 0x1)
#define   C_028008_STENCIL_READ_ONLY                                  0xFDFFFFFF
#define R_02800C_DB_RENDER_OVERRIDE                                     0x02800C
#define   S_02800C_FORCE_HIZ_ENABLE(x)                                (((unsigned)(x) & 0x03) << 0)
#define   G_02800C_FORCE_HIZ_ENABLE(x)                                (((x) >> 0) & 0x03)
#define   C_02800C_FORCE_HIZ_ENABLE                                   0xFFFFFFFC
#define     V_02800C_FORCE_OFF                                      0x00
#define     V_02800C_FORCE_ENABLE                                   0x01
#define     V_02800C_FORCE_DISABLE                                  0x02
#define     V_02800C_FORCE_RESERVED                                 0x03
#define   S_02800C_FORCE_HIS_ENABLE0(x)                               (((unsigned)(x) & 0x03) << 2)
#define   G_02800C_FORCE_HIS_ENABLE0(x)                               (((x) >> 2) & 0x03)
#define   C_02800C_FORCE_HIS_ENABLE0                                  0xFFFFFFF3
#define     V_02800C_FORCE_OFF                                      0x00
#define     V_02800C_FORCE_ENABLE                                   0x01
#define     V_02800C_FORCE_DISABLE                                  0x02
#define     V_02800C_FORCE_RESERVED                                 0x03
#define   S_02800C_FORCE_HIS_ENABLE1(x)                               (((unsigned)(x) & 0x03) << 4)
#define   G_02800C_FORCE_HIS_ENABLE1(x)                               (((x) >> 4) & 0x03)
#define   C_02800C_FORCE_HIS_ENABLE1                                  0xFFFFFFCF
#define     V_02800C_FORCE_OFF                                      0x00
#define     V_02800C_FORCE_ENABLE                                   0x01
#define     V_02800C_FORCE_DISABLE                                  0x02
#define     V_02800C_FORCE_RESERVED                                 0x03
#define   S_02800C_FORCE_SHADER_Z_ORDER(x)                            (((unsigned)(x) & 0x1) << 6)
#define   G_02800C_FORCE_SHADER_Z_ORDER(x)                            (((x) >> 6) & 0x1)
#define   C_02800C_FORCE_SHADER_Z_ORDER                               0xFFFFFFBF
#define   S_02800C_FAST_Z_DISABLE(x)                                  (((unsigned)(x) & 0x1) << 7)
#define   G_02800C_FAST_Z_DISABLE(x)                                  (((x) >> 7) & 0x1)
#define   C_02800C_FAST_Z_DISABLE                                     0xFFFFFF7F
#define   S_02800C_FAST_STENCIL_DISABLE(x)                            (((unsigned)(x) & 0x1) << 8)
#define   G_02800C_FAST_STENCIL_DISABLE(x)                            (((x) >> 8) & 0x1)
#define   C_02800C_FAST_STENCIL_DISABLE                               0xFFFFFEFF
#define   S_02800C_NOOP_CULL_DISABLE(x)                               (((unsigned)(x) & 0x1) << 9)
#define   G_02800C_NOOP_CULL_DISABLE(x)                               (((x) >> 9) & 0x1)
#define   C_02800C_NOOP_CULL_DISABLE                                  0xFFFFFDFF
#define   S_02800C_FORCE_COLOR_KILL(x)                                (((unsigned)(x) & 0x1) << 10)
#define   G_02800C_FORCE_COLOR_KILL(x)                                (((x) >> 10) & 0x1)
#define   C_02800C_FORCE_COLOR_KILL                                   0xFFFFFBFF
#define   S_02800C_FORCE_Z_READ(x)                                    (((unsigned)(x) & 0x1) << 11)
#define   G_02800C_FORCE_Z_READ(x)                                    (((x) >> 11) & 0x1)
#define   C_02800C_FORCE_Z_READ                                       0xFFFFF7FF
#define   S_02800C_FORCE_STENCIL_READ(x)                              (((unsigned)(x) & 0x1) << 12)
#define   G_02800C_FORCE_STENCIL_READ(x)                              (((x) >> 12) & 0x1)
#define   C_02800C_FORCE_STENCIL_READ                                 0xFFFFEFFF
#define   S_02800C_FORCE_FULL_Z_RANGE(x)                              (((unsigned)(x) & 0x03) << 13)
#define   G_02800C_FORCE_FULL_Z_RANGE(x)                              (((x) >> 13) & 0x03)
#define   C_02800C_FORCE_FULL_Z_RANGE                                 0xFFFF9FFF
#define     V_02800C_FORCE_OFF                                      0x00
#define     V_02800C_FORCE_ENABLE                                   0x01
#define     V_02800C_FORCE_DISABLE                                  0x02
#define     V_02800C_FORCE_RESERVED                                 0x03
#define   S_02800C_FORCE_QC_SMASK_CONFLICT(x)                         (((unsigned)(x) & 0x1) << 15)
#define   G_02800C_FORCE_QC_SMASK_CONFLICT(x)                         (((x) >> 15) & 0x1)
#define   C_02800C_FORCE_QC_SMASK_CONFLICT                            0xFFFF7FFF
#define   S_02800C_DISABLE_VIEWPORT_CLAMP(x)                          (((unsigned)(x) & 0x1) << 16)
#define   G_02800C_DISABLE_VIEWPORT_CLAMP(x)                          (((x) >> 16) & 0x1)
#define   C_02800C_DISABLE_VIEWPORT_CLAMP                             0xFFFEFFFF
#define   S_02800C_IGNORE_SC_ZRANGE(x)                                (((unsigned)(x) & 0x1) << 17)
#define   G_02800C_IGNORE_SC_ZRANGE(x)                                (((x) >> 17) & 0x1)
#define   C_02800C_IGNORE_SC_ZRANGE                                   0xFFFDFFFF
#define   S_02800C_DISABLE_FULLY_COVERED(x)                           (((unsigned)(x) & 0x1) << 18)
#define   G_02800C_DISABLE_FULLY_COVERED(x)                           (((x) >> 18) & 0x1)
#define   C_02800C_DISABLE_FULLY_COVERED                              0xFFFBFFFF
#define   S_02800C_FORCE_Z_LIMIT_SUMM(x)                              (((unsigned)(x) & 0x03) << 19)
#define   G_02800C_FORCE_Z_LIMIT_SUMM(x)                              (((x) >> 19) & 0x03)
#define   C_02800C_FORCE_Z_LIMIT_SUMM                                 0xFFE7FFFF
#define     V_02800C_FORCE_SUMM_OFF                                 0x00
#define     V_02800C_FORCE_SUMM_MINZ                                0x01
#define     V_02800C_FORCE_SUMM_MAXZ                                0x02
#define     V_02800C_FORCE_SUMM_BOTH                                0x03
#define   S_02800C_MAX_TILES_IN_DTT(x)                                (((unsigned)(x) & 0x1F) << 21)
#define   G_02800C_MAX_TILES_IN_DTT(x)                                (((x) >> 21) & 0x1F)
#define   C_02800C_MAX_TILES_IN_DTT                                   0xFC1FFFFF
#define   S_02800C_DISABLE_TILE_RATE_TILES(x)                         (((unsigned)(x) & 0x1) << 26)
#define   G_02800C_DISABLE_TILE_RATE_TILES(x)                         (((x) >> 26) & 0x1)
#define   C_02800C_DISABLE_TILE_RATE_TILES                            0xFBFFFFFF
#define   S_02800C_FORCE_Z_DIRTY(x)                                   (((unsigned)(x) & 0x1) << 27)
#define   G_02800C_FORCE_Z_DIRTY(x)                                   (((x) >> 27) & 0x1)
#define   C_02800C_FORCE_Z_DIRTY                                      0xF7FFFFFF
#define   S_02800C_FORCE_STENCIL_DIRTY(x)                             (((unsigned)(x) & 0x1) << 28)
#define   G_02800C_FORCE_STENCIL_DIRTY(x)                             (((x) >> 28) & 0x1)
#define   C_02800C_FORCE_STENCIL_DIRTY                                0xEFFFFFFF
#define   S_02800C_FORCE_Z_VALID(x)                                   (((unsigned)(x) & 0x1) << 29)
#define   G_02800C_FORCE_Z_VALID(x)                                   (((x) >> 29) & 0x1)
#define   C_02800C_FORCE_Z_VALID                                      0xDFFFFFFF
#define   S_02800C_FORCE_STENCIL_VALID(x)                             (((unsigned)(x) & 0x1) << 30)
#define   G_02800C_FORCE_STENCIL_VALID(x)                             (((x) >> 30) & 0x1)
#define   C_02800C_FORCE_STENCIL_VALID                                0xBFFFFFFF
#define   S_02800C_PRESERVE_COMPRESSION(x)                            (((unsigned)(x) & 0x1) << 31)
#define   G_02800C_PRESERVE_COMPRESSION(x)                            (((x) >> 31) & 0x1)
#define   C_02800C_PRESERVE_COMPRESSION                               0x7FFFFFFF
#define R_028010_DB_RENDER_OVERRIDE2                                    0x028010
#define   S_028010_PARTIAL_SQUAD_LAUNCH_CONTROL(x)                    (((unsigned)(x) & 0x03) << 0)
#define   G_028010_PARTIAL_SQUAD_LAUNCH_CONTROL(x)                    (((x) >> 0) & 0x03)
#define   C_028010_PARTIAL_SQUAD_LAUNCH_CONTROL                       0xFFFFFFFC
#define     V_028010_PSLC_AUTO                                      0x00
#define     V_028010_PSLC_ON_HANG_ONLY                              0x01
#define     V_028010_PSLC_ASAP                                      0x02
#define     V_028010_PSLC_COUNTDOWN                                 0x03
#define   S_028010_PARTIAL_SQUAD_LAUNCH_COUNTDOWN(x)                  (((unsigned)(x) & 0x07) << 2)
#define   G_028010_PARTIAL_SQUAD_LAUNCH_COUNTDOWN(x)                  (((x) >> 2) & 0x07)
#define   C_028010_PARTIAL_SQUAD_LAUNCH_COUNTDOWN                     0xFFFFFFE3
#define   S_028010_DISABLE_ZMASK_EXPCLEAR_OPTIMIZATION(x)             (((unsigned)(x) & 0x1) << 5)
#define   G_028010_DISABLE_ZMASK_EXPCLEAR_OPTIMIZATION(x)             (((x) >> 5) & 0x1)
#define   C_028010_DISABLE_ZMASK_EXPCLEAR_OPTIMIZATION                0xFFFFFFDF
#define   S_028010_DISABLE_SMEM_EXPCLEAR_OPTIMIZATION(x)              (((unsigned)(x) & 0x1) << 6)
#define   G_028010_DISABLE_SMEM_EXPCLEAR_OPTIMIZATION(x)              (((x) >> 6) & 0x1)
#define   C_028010_DISABLE_SMEM_EXPCLEAR_OPTIMIZATION                 0xFFFFFFBF
#define   S_028010_DISABLE_COLOR_ON_VALIDATION(x)                     (((unsigned)(x) & 0x1) << 7)
#define   G_028010_DISABLE_COLOR_ON_VALIDATION(x)                     (((x) >> 7) & 0x1)
#define   C_028010_DISABLE_COLOR_ON_VALIDATION                        0xFFFFFF7F
#define   S_028010_DECOMPRESS_Z_ON_FLUSH(x)                           (((unsigned)(x) & 0x1) << 8)
#define   G_028010_DECOMPRESS_Z_ON_FLUSH(x)                           (((x) >> 8) & 0x1)
#define   C_028010_DECOMPRESS_Z_ON_FLUSH                              0xFFFFFEFF
#define   S_028010_DISABLE_REG_SNOOP(x)                               (((unsigned)(x) & 0x1) << 9)
#define   G_028010_DISABLE_REG_SNOOP(x)                               (((x) >> 9) & 0x1)
#define   C_028010_DISABLE_REG_SNOOP                                  0xFFFFFDFF
#define   S_028010_DEPTH_BOUNDS_HIER_DEPTH_DISABLE(x)                 (((unsigned)(x) & 0x1) << 10)
#define   G_028010_DEPTH_BOUNDS_HIER_DEPTH_DISABLE(x)                 (((x) >> 10) & 0x1)
#define   C_028010_DEPTH_BOUNDS_HIER_DEPTH_DISABLE                    0xFFFFFBFF
/* CIK */
#define   S_028010_SEPARATE_HIZS_FUNC_ENABLE(x)                       (((unsigned)(x) & 0x1) << 11)
#define   G_028010_SEPARATE_HIZS_FUNC_ENABLE(x)                       (((x) >> 11) & 0x1)
#define   C_028010_SEPARATE_HIZS_FUNC_ENABLE                          0xFFFFF7FF
#define   S_028010_HIZ_ZFUNC(x)                                       (((unsigned)(x) & 0x07) << 12)
#define   G_028010_HIZ_ZFUNC(x)                                       (((x) >> 12) & 0x07)
#define   C_028010_HIZ_ZFUNC                                          0xFFFF8FFF
#define   S_028010_HIS_SFUNC_FF(x)                                    (((unsigned)(x) & 0x07) << 15)
#define   G_028010_HIS_SFUNC_FF(x)                                    (((x) >> 15) & 0x07)
#define   C_028010_HIS_SFUNC_FF                                       0xFFFC7FFF
#define   S_028010_HIS_SFUNC_BF(x)                                    (((unsigned)(x) & 0x07) << 18)
#define   G_028010_HIS_SFUNC_BF(x)                                    (((x) >> 18) & 0x07)
#define   C_028010_HIS_SFUNC_BF                                       0xFFE3FFFF
#define   S_028010_PRESERVE_ZRANGE(x)                                 (((unsigned)(x) & 0x1) << 21)
#define   G_028010_PRESERVE_ZRANGE(x)                                 (((x) >> 21) & 0x1)
#define   C_028010_PRESERVE_ZRANGE                                    0xFFDFFFFF
#define   S_028010_PRESERVE_SRESULTS(x)                               (((unsigned)(x) & 0x1) << 22)
#define   G_028010_PRESERVE_SRESULTS(x)                               (((x) >> 22) & 0x1)
#define   C_028010_PRESERVE_SRESULTS                                  0xFFBFFFFF
#define   S_028010_DISABLE_FAST_PASS(x)                               (((unsigned)(x) & 0x1) << 23)
#define   G_028010_DISABLE_FAST_PASS(x)                               (((x) >> 23) & 0x1)
#define   C_028010_DISABLE_FAST_PASS                                  0xFF7FFFFF
/*     */
#define R_028014_DB_HTILE_DATA_BASE                                     0x028014
#define R_028020_DB_DEPTH_BOUNDS_MIN                                    0x028020
#define R_028024_DB_DEPTH_BOUNDS_MAX                                    0x028024
#define R_028028_DB_STENCIL_CLEAR                                       0x028028
#define   S_028028_CLEAR(x)                                           (((unsigned)(x) & 0xFF) << 0)
#define   G_028028_CLEAR(x)                                           (((x) >> 0) & 0xFF)
#define   C_028028_CLEAR                                              0xFFFFFF00
#define R_02802C_DB_DEPTH_CLEAR                                         0x02802C
#define R_028030_PA_SC_SCREEN_SCISSOR_TL                                0x028030
#define   S_028030_TL_X(x)                                            (((unsigned)(x) & 0xFFFF) << 0)
#define   G_028030_TL_X(x)                                            (((x) >> 0) & 0xFFFF)
#define   C_028030_TL_X                                               0xFFFF0000
#define   S_028030_TL_Y(x)                                            (((unsigned)(x) & 0xFFFF) << 16)
#define   G_028030_TL_Y(x)                                            (((x) >> 16) & 0xFFFF)
#define   C_028030_TL_Y                                               0x0000FFFF
#define R_028034_PA_SC_SCREEN_SCISSOR_BR                                0x028034
#define   S_028034_BR_X(x)                                            (((unsigned)(x) & 0xFFFF) << 0)
#define   G_028034_BR_X(x)                                            (((x) >> 0) & 0xFFFF)
#define   C_028034_BR_X                                               0xFFFF0000
#define   S_028034_BR_Y(x)                                            (((unsigned)(x) & 0xFFFF) << 16)
#define   G_028034_BR_Y(x)                                            (((x) >> 16) & 0xFFFF)
#define   C_028034_BR_Y                                               0x0000FFFF
#define R_02803C_DB_DEPTH_INFO                                          0x02803C
#define   S_02803C_ADDR5_SWIZZLE_MASK(x)                              (((unsigned)(x) & 0x0F) << 0)
#define   G_02803C_ADDR5_SWIZZLE_MASK(x)                              (((x) >> 0) & 0x0F)
#define   C_02803C_ADDR5_SWIZZLE_MASK                                 0xFFFFFFF0
/* CIK */
#define   S_02803C_ARRAY_MODE(x)                                      (((unsigned)(x) & 0x0F) << 4)
#define   G_02803C_ARRAY_MODE(x)                                      (((x) >> 4) & 0x0F)
#define   C_02803C_ARRAY_MODE                                         0xFFFFFF0F
#define     V_02803C_ARRAY_LINEAR_GENERAL                           0x00
#define     V_02803C_ARRAY_LINEAR_ALIGNED                           0x01
#define     V_02803C_ARRAY_1D_TILED_THIN1                           0x02
#define     V_02803C_ARRAY_2D_TILED_THIN1                           0x04
#define     V_02803C_ARRAY_PRT_TILED_THIN1                          0x05
#define     V_02803C_ARRAY_PRT_2D_TILED_THIN1                       0x06
#define   S_02803C_PIPE_CONFIG(x)                                     (((unsigned)(x) & 0x1F) << 8)
#define   G_02803C_PIPE_CONFIG(x)                                     (((x) >> 8) & 0x1F)
#define   C_02803C_PIPE_CONFIG                                        0xFFFFE0FF
#define     V_02803C_ADDR_SURF_P2                                   0x00
#define     V_02803C_X_ADDR_SURF_P4_8X16                            0x04
#define     V_02803C_X_ADDR_SURF_P4_16X16                           0x05
#define     V_02803C_X_ADDR_SURF_P4_16X32                           0x06
#define     V_02803C_X_ADDR_SURF_P4_32X32                           0x07
#define     V_02803C_X_ADDR_SURF_P8_16X16_8X16                      0x08
#define     V_02803C_X_ADDR_SURF_P8_16X32_8X16                      0x09
#define     V_02803C_X_ADDR_SURF_P8_32X32_8X16                      0x0A
#define     V_02803C_X_ADDR_SURF_P8_16X32_16X16                     0x0B
#define     V_02803C_X_ADDR_SURF_P8_32X32_16X16                     0x0C
#define     V_02803C_X_ADDR_SURF_P8_32X32_16X32                     0x0D
#define     V_02803C_X_ADDR_SURF_P8_32X64_32X32                     0x0E
#define     V_02803C_X_ADDR_SURF_P16_32X32_8X16                     0x10
#define     V_02803C_X_ADDR_SURF_P16_32X32_16X16                    0x11
#define   S_02803C_BANK_WIDTH(x)                                      (((unsigned)(x) & 0x03) << 13)
#define   G_02803C_BANK_WIDTH(x)                                      (((x) >> 13) & 0x03)
#define   C_02803C_BANK_WIDTH                                         0xFFFF9FFF
#define     V_02803C_ADDR_SURF_BANK_WIDTH_1                         0x00
#define     V_02803C_ADDR_SURF_BANK_WIDTH_2                         0x01
#define     V_02803C_ADDR_SURF_BANK_WIDTH_4                         0x02
#define     V_02803C_ADDR_SURF_BANK_WIDTH_8                         0x03
#define   S_02803C_BANK_HEIGHT(x)                                     (((unsigned)(x) & 0x03) << 15)
#define   G_02803C_BANK_HEIGHT(x)                                     (((x) >> 15) & 0x03)
#define   C_02803C_BANK_HEIGHT                                        0xFFFE7FFF
#define     V_02803C_ADDR_SURF_BANK_HEIGHT_1                        0x00
#define     V_02803C_ADDR_SURF_BANK_HEIGHT_2                        0x01
#define     V_02803C_ADDR_SURF_BANK_HEIGHT_4                        0x02
#define     V_02803C_ADDR_SURF_BANK_HEIGHT_8                        0x03
#define   S_02803C_MACRO_TILE_ASPECT(x)                               (((unsigned)(x) & 0x03) << 17)
#define   G_02803C_MACRO_TILE_ASPECT(x)                               (((x) >> 17) & 0x03)
#define   C_02803C_MACRO_TILE_ASPECT                                  0xFFF9FFFF
#define     V_02803C_ADDR_SURF_MACRO_ASPECT_1                       0x00
#define     V_02803C_ADDR_SURF_MACRO_ASPECT_2                       0x01
#define     V_02803C_ADDR_SURF_MACRO_ASPECT_4                       0x02
#define     V_02803C_ADDR_SURF_MACRO_ASPECT_8                       0x03
#define   S_02803C_NUM_BANKS(x)                                       (((unsigned)(x) & 0x03) << 19)
#define   G_02803C_NUM_BANKS(x)                                       (((x) >> 19) & 0x03)
#define   C_02803C_NUM_BANKS                                          0xFFE7FFFF
#define     V_02803C_ADDR_SURF_2_BANK                               0x00
#define     V_02803C_ADDR_SURF_4_BANK                               0x01
#define     V_02803C_ADDR_SURF_8_BANK                               0x02
#define     V_02803C_ADDR_SURF_16_BANK                              0x03
/*     */
#define R_028040_DB_Z_INFO                                              0x028040
#define   S_028040_FORMAT(x)                                          (((unsigned)(x) & 0x03) << 0)
#define   G_028040_FORMAT(x)                                          (((x) >> 0) & 0x03)
#define   C_028040_FORMAT                                             0xFFFFFFFC
#define     V_028040_Z_INVALID                                      0x00
#define     V_028040_Z_16                                           0x01
#define     V_028040_Z_24                                           0x02 /* deprecated */
#define     V_028040_Z_32_FLOAT                                     0x03
#define   S_028040_NUM_SAMPLES(x)                                     (((unsigned)(x) & 0x03) << 2)
#define   G_028040_NUM_SAMPLES(x)                                     (((x) >> 2) & 0x03)
#define   C_028040_NUM_SAMPLES                                        0xFFFFFFF3
/* CIK */
#define   S_028040_TILE_SPLIT(x)                                      (((unsigned)(x) & 0x07) << 13)
#define   G_028040_TILE_SPLIT(x)                                      (((x) >> 13) & 0x07)
#define   C_028040_TILE_SPLIT                                         0xFFFF1FFF
#define     V_028040_ADDR_SURF_TILE_SPLIT_64B                       0x00
#define     V_028040_ADDR_SURF_TILE_SPLIT_128B                      0x01
#define     V_028040_ADDR_SURF_TILE_SPLIT_256B                      0x02
#define     V_028040_ADDR_SURF_TILE_SPLIT_512B                      0x03
#define     V_028040_ADDR_SURF_TILE_SPLIT_1KB                       0x04
#define     V_028040_ADDR_SURF_TILE_SPLIT_2KB                       0x05
#define     V_028040_ADDR_SURF_TILE_SPLIT_4KB                       0x06
/*     */
#define   S_028040_TILE_MODE_INDEX(x)                                 (((unsigned)(x) & 0x07) << 20) /* not on CIK */
#define   G_028040_TILE_MODE_INDEX(x)                                 (((x) >> 20) & 0x07) /* not on CIK */
#define   C_028040_TILE_MODE_INDEX                                    0xFF8FFFFF /* not on CIK */
/* VI */
#define   S_028040_DECOMPRESS_ON_N_ZPLANES(x)                         (((unsigned)(x) & 0x0F) << 23)
#define   G_028040_DECOMPRESS_ON_N_ZPLANES(x)                         (((x) >> 23) & 0x0F)
#define   C_028040_DECOMPRESS_ON_N_ZPLANES                            0xF87FFFFF
/*    */
#define   S_028040_ALLOW_EXPCLEAR(x)                                  (((unsigned)(x) & 0x1) << 27)
#define   G_028040_ALLOW_EXPCLEAR(x)                                  (((x) >> 27) & 0x1)
#define   C_028040_ALLOW_EXPCLEAR                                     0xF7FFFFFF
#define   S_028040_READ_SIZE(x)                                       (((unsigned)(x) & 0x1) << 28)
#define   G_028040_READ_SIZE(x)                                       (((x) >> 28) & 0x1)
#define   C_028040_READ_SIZE                                          0xEFFFFFFF
#define   S_028040_TILE_SURFACE_ENABLE(x)                             (((unsigned)(x) & 0x1) << 29)
#define   G_028040_TILE_SURFACE_ENABLE(x)                             (((x) >> 29) & 0x1)
#define   C_028040_TILE_SURFACE_ENABLE                                0xDFFFFFFF
/* VI */
#define   S_028040_CLEAR_DISALLOWED(x)                                (((unsigned)(x) & 0x1) << 30)
#define   G_028040_CLEAR_DISALLOWED(x)                                (((x) >> 30) & 0x1)
#define   C_028040_CLEAR_DISALLOWED                                   0xBFFFFFFF
/*    */
#define   S_028040_ZRANGE_PRECISION(x)                                (((unsigned)(x) & 0x1) << 31)
#define   G_028040_ZRANGE_PRECISION(x)                                (((x) >> 31) & 0x1)
#define   C_028040_ZRANGE_PRECISION                                   0x7FFFFFFF
#define R_028044_DB_STENCIL_INFO                                        0x028044
#define   S_028044_FORMAT(x)                                          (((unsigned)(x) & 0x1) << 0)
#define   G_028044_FORMAT(x)                                          (((x) >> 0) & 0x1)
#define   C_028044_FORMAT                                             0xFFFFFFFE
#define     V_028044_STENCIL_INVALID                                0x00
#define     V_028044_STENCIL_8                                      0x01
/* CIK */
#define   S_028044_TILE_SPLIT(x)                                      (((unsigned)(x) & 0x07) << 13)
#define   G_028044_TILE_SPLIT(x)                                      (((x) >> 13) & 0x07)
#define   C_028044_TILE_SPLIT                                         0xFFFF1FFF
#define     V_028044_ADDR_SURF_TILE_SPLIT_64B                       0x00
#define     V_028044_ADDR_SURF_TILE_SPLIT_128B                      0x01
#define     V_028044_ADDR_SURF_TILE_SPLIT_256B                      0x02
#define     V_028044_ADDR_SURF_TILE_SPLIT_512B                      0x03
#define     V_028044_ADDR_SURF_TILE_SPLIT_1KB                       0x04
#define     V_028044_ADDR_SURF_TILE_SPLIT_2KB                       0x05
#define     V_028044_ADDR_SURF_TILE_SPLIT_4KB                       0x06
/*     */
#define   S_028044_TILE_MODE_INDEX(x)                                 (((unsigned)(x) & 0x07) << 20) /* not on CIK */
#define   G_028044_TILE_MODE_INDEX(x)                                 (((x) >> 20) & 0x07) /* not on CIK */
#define   C_028044_TILE_MODE_INDEX                                    0xFF8FFFFF /* not on CIK */
#define   S_028044_ALLOW_EXPCLEAR(x)                                  (((unsigned)(x) & 0x1) << 27)
#define   G_028044_ALLOW_EXPCLEAR(x)                                  (((x) >> 27) & 0x1)
#define   C_028044_ALLOW_EXPCLEAR                                     0xF7FFFFFF
#define   S_028044_TILE_STENCIL_DISABLE(x)                            (((unsigned)(x) & 0x1) << 29)
#define   G_028044_TILE_STENCIL_DISABLE(x)                            (((x) >> 29) & 0x1)
#define   C_028044_TILE_STENCIL_DISABLE                               0xDFFFFFFF
/* VI */
#define   S_028044_CLEAR_DISALLOWED(x)                                (((unsigned)(x) & 0x1) << 30)
#define   G_028044_CLEAR_DISALLOWED(x)                                (((x) >> 30) & 0x1)
#define   C_028044_CLEAR_DISALLOWED                                   0xBFFFFFFF
/*    */
#define R_028048_DB_Z_READ_BASE                                         0x028048
#define R_02804C_DB_STENCIL_READ_BASE                                   0x02804C
#define R_028050_DB_Z_WRITE_BASE                                        0x028050
#define R_028054_DB_STENCIL_WRITE_BASE                                  0x028054
#define R_028058_DB_DEPTH_SIZE                                          0x028058
#define   S_028058_PITCH_TILE_MAX(x)                                  (((unsigned)(x) & 0x7FF) << 0)
#define   G_028058_PITCH_TILE_MAX(x)                                  (((x) >> 0) & 0x7FF)
#define   C_028058_PITCH_TILE_MAX                                     0xFFFFF800
#define   S_028058_HEIGHT_TILE_MAX(x)                                 (((unsigned)(x) & 0x7FF) << 11)
#define   G_028058_HEIGHT_TILE_MAX(x)                                 (((x) >> 11) & 0x7FF)
#define   C_028058_HEIGHT_TILE_MAX                                    0xFFC007FF
#define R_02805C_DB_DEPTH_SLICE                                         0x02805C
#define   S_02805C_SLICE_TILE_MAX(x)                                  (((unsigned)(x) & 0x3FFFFF) << 0)
#define   G_02805C_SLICE_TILE_MAX(x)                                  (((x) >> 0) & 0x3FFFFF)
#define   C_02805C_SLICE_TILE_MAX                                     0xFFC00000
#define R_028080_TA_BC_BASE_ADDR                                        0x028080
/* CIK */
#define R_028084_TA_BC_BASE_ADDR_HI                                     0x028084
#define   S_028084_ADDRESS(x)                                         (((unsigned)(x) & 0xFF) << 0)
#define   G_028084_ADDRESS(x)                                         (((x) >> 0) & 0xFF)
#define   C_028084_ADDRESS                                            0xFFFFFF00
#define R_0281E8_COHER_DEST_BASE_HI_0                                   0x0281E8
#define R_0281EC_COHER_DEST_BASE_HI_1                                   0x0281EC
#define R_0281F0_COHER_DEST_BASE_HI_2                                   0x0281F0
#define R_0281F4_COHER_DEST_BASE_HI_3                                   0x0281F4
/*     */
#define R_0281F8_COHER_DEST_BASE_2                                      0x0281F8
#define R_0281FC_COHER_DEST_BASE_3                                      0x0281FC
#define R_028200_PA_SC_WINDOW_OFFSET                                    0x028200
#define   S_028200_WINDOW_X_OFFSET(x)                                 (((unsigned)(x) & 0xFFFF) << 0)
#define   G_028200_WINDOW_X_OFFSET(x)                                 (((x) >> 0) & 0xFFFF)
#define   C_028200_WINDOW_X_OFFSET                                    0xFFFF0000
#define   S_028200_WINDOW_Y_OFFSET(x)                                 (((unsigned)(x) & 0xFFFF) << 16)
#define   G_028200_WINDOW_Y_OFFSET(x)                                 (((x) >> 16) & 0xFFFF)
#define   C_028200_WINDOW_Y_OFFSET                                    0x0000FFFF
#define R_028204_PA_SC_WINDOW_SCISSOR_TL                                0x028204
#define   S_028204_TL_X(x)                                            (((unsigned)(x) & 0x7FFF) << 0)
#define   G_028204_TL_X(x)                                            (((x) >> 0) & 0x7FFF)
#define   C_028204_TL_X                                               0xFFFF8000
#define   S_028204_TL_Y(x)                                            (((unsigned)(x) & 0x7FFF) << 16)
#define   G_028204_TL_Y(x)                                            (((x) >> 16) & 0x7FFF)
#define   C_028204_TL_Y                                               0x8000FFFF
#define   S_028204_WINDOW_OFFSET_DISABLE(x)                           (((unsigned)(x) & 0x1) << 31)
#define   G_028204_WINDOW_OFFSET_DISABLE(x)                           (((x) >> 31) & 0x1)
#define   C_028204_WINDOW_OFFSET_DISABLE                              0x7FFFFFFF
#define R_028208_PA_SC_WINDOW_SCISSOR_BR                                0x028208
#define   S_028208_BR_X(x)                                            (((unsigned)(x) & 0x7FFF) << 0)
#define   G_028208_BR_X(x)                                            (((x) >> 0) & 0x7FFF)
#define   C_028208_BR_X                                               0xFFFF8000
#define   S_028208_BR_Y(x)                                            (((unsigned)(x) & 0x7FFF) << 16)
#define   G_028208_BR_Y(x)                                            (((x) >> 16) & 0x7FFF)
#define   C_028208_BR_Y                                               0x8000FFFF
#define R_02820C_PA_SC_CLIPRECT_RULE                                    0x02820C
#define   S_02820C_CLIP_RULE(x)                                       (((unsigned)(x) & 0xFFFF) << 0)
#define   G_02820C_CLIP_RULE(x)                                       (((x) >> 0) & 0xFFFF)
#define   C_02820C_CLIP_RULE                                          0xFFFF0000
#define R_028210_PA_SC_CLIPRECT_0_TL                                    0x028210
#define   S_028210_TL_X(x)                                            (((unsigned)(x) & 0x7FFF) << 0)
#define   G_028210_TL_X(x)                                            (((x) >> 0) & 0x7FFF)
#define   C_028210_TL_X                                               0xFFFF8000
#define   S_028210_TL_Y(x)                                            (((unsigned)(x) & 0x7FFF) << 16)
#define   G_028210_TL_Y(x)                                            (((x) >> 16) & 0x7FFF)
#define   C_028210_TL_Y                                               0x8000FFFF
#define R_028214_PA_SC_CLIPRECT_0_BR                                    0x028214
#define   S_028214_BR_X(x)                                            (((unsigned)(x) & 0x7FFF) << 0)
#define   G_028214_BR_X(x)                                            (((x) >> 0) & 0x7FFF)
#define   C_028214_BR_X                                               0xFFFF8000
#define   S_028214_BR_Y(x)                                            (((unsigned)(x) & 0x7FFF) << 16)
#define   G_028214_BR_Y(x)                                            (((x) >> 16) & 0x7FFF)
#define   C_028214_BR_Y                                               0x8000FFFF
#define R_028218_PA_SC_CLIPRECT_1_TL                                    0x028218
#define R_02821C_PA_SC_CLIPRECT_1_BR                                    0x02821C
#define R_028220_PA_SC_CLIPRECT_2_TL                                    0x028220
#define R_028224_PA_SC_CLIPRECT_2_BR                                    0x028224
#define R_028228_PA_SC_CLIPRECT_3_TL                                    0x028228
#define R_02822C_PA_SC_CLIPRECT_3_BR                                    0x02822C
#define R_028230_PA_SC_EDGERULE                                         0x028230
#define   S_028230_ER_TRI(x)                                          (((unsigned)(x) & 0x0F) << 0)
#define   G_028230_ER_TRI(x)                                          (((x) >> 0) & 0x0F)
#define   C_028230_ER_TRI                                             0xFFFFFFF0
#define   S_028230_ER_POINT(x)                                        (((unsigned)(x) & 0x0F) << 4)
#define   G_028230_ER_POINT(x)                                        (((x) >> 4) & 0x0F)
#define   C_028230_ER_POINT                                           0xFFFFFF0F
#define   S_028230_ER_RECT(x)                                         (((unsigned)(x) & 0x0F) << 8)
#define   G_028230_ER_RECT(x)                                         (((x) >> 8) & 0x0F)
#define   C_028230_ER_RECT                                            0xFFFFF0FF
#define   S_028230_ER_LINE_LR(x)                                      (((unsigned)(x) & 0x3F) << 12)
#define   G_028230_ER_LINE_LR(x)                                      (((x) >> 12) & 0x3F)
#define   C_028230_ER_LINE_LR                                         0xFFFC0FFF
#define   S_028230_ER_LINE_RL(x)                                      (((unsigned)(x) & 0x3F) << 18)
#define   G_028230_ER_LINE_RL(x)                                      (((x) >> 18) & 0x3F)
#define   C_028230_ER_LINE_RL                                         0xFF03FFFF
#define   S_028230_ER_LINE_TB(x)                                      (((unsigned)(x) & 0x0F) << 24)
#define   G_028230_ER_LINE_TB(x)                                      (((x) >> 24) & 0x0F)
#define   C_028230_ER_LINE_TB                                         0xF0FFFFFF
#define   S_028230_ER_LINE_BT(x)                                      (((unsigned)(x) & 0x0F) << 28)
#define   G_028230_ER_LINE_BT(x)                                      (((x) >> 28) & 0x0F)
#define   C_028230_ER_LINE_BT                                         0x0FFFFFFF
#define R_028234_PA_SU_HARDWARE_SCREEN_OFFSET                           0x028234
#define   S_028234_HW_SCREEN_OFFSET_X(x)                              (((unsigned)(x) & 0x1FF) << 0)
#define   G_028234_HW_SCREEN_OFFSET_X(x)                              (((x) >> 0) & 0x1FF)
#define   C_028234_HW_SCREEN_OFFSET_X                                 0xFFFFFE00
#define   S_028234_HW_SCREEN_OFFSET_Y(x)                              (((unsigned)(x) & 0x1FF) << 16)
#define   G_028234_HW_SCREEN_OFFSET_Y(x)                              (((x) >> 16) & 0x1FF)
#define   C_028234_HW_SCREEN_OFFSET_Y                                 0xFE00FFFF
#define R_028238_CB_TARGET_MASK                                         0x028238
#define   S_028238_TARGET0_ENABLE(x)                                  (((unsigned)(x) & 0x0F) << 0)
#define   G_028238_TARGET0_ENABLE(x)                                  (((x) >> 0) & 0x0F)
#define   C_028238_TARGET0_ENABLE                                     0xFFFFFFF0
#define   S_028238_TARGET1_ENABLE(x)                                  (((unsigned)(x) & 0x0F) << 4)
#define   G_028238_TARGET1_ENABLE(x)                                  (((x) >> 4) & 0x0F)
#define   C_028238_TARGET1_ENABLE                                     0xFFFFFF0F
#define   S_028238_TARGET2_ENABLE(x)                                  (((unsigned)(x) & 0x0F) << 8)
#define   G_028238_TARGET2_ENABLE(x)                                  (((x) >> 8) & 0x0F)
#define   C_028238_TARGET2_ENABLE                                     0xFFFFF0FF
#define   S_028238_TARGET3_ENABLE(x)                                  (((unsigned)(x) & 0x0F) << 12)
#define   G_028238_TARGET3_ENABLE(x)                                  (((x) >> 12) & 0x0F)
#define   C_028238_TARGET3_ENABLE                                     0xFFFF0FFF
#define   S_028238_TARGET4_ENABLE(x)                                  (((unsigned)(x) & 0x0F) << 16)
#define   G_028238_TARGET4_ENABLE(x)                                  (((x) >> 16) & 0x0F)
#define   C_028238_TARGET4_ENABLE                                     0xFFF0FFFF
#define   S_028238_TARGET5_ENABLE(x)                                  (((unsigned)(x) & 0x0F) << 20)
#define   G_028238_TARGET5_ENABLE(x)                                  (((x) >> 20) & 0x0F)
#define   C_028238_TARGET5_ENABLE                                     0xFF0FFFFF
#define   S_028238_TARGET6_ENABLE(x)                                  (((unsigned)(x) & 0x0F) << 24)
#define   G_028238_TARGET6_ENABLE(x)                                  (((x) >> 24) & 0x0F)
#define   C_028238_TARGET6_ENABLE                                     0xF0FFFFFF
#define   S_028238_TARGET7_ENABLE(x)                                  (((unsigned)(x) & 0x0F) << 28)
#define   G_028238_TARGET7_ENABLE(x)                                  (((x) >> 28) & 0x0F)
#define   C_028238_TARGET7_ENABLE                                     0x0FFFFFFF
#define R_02823C_CB_SHADER_MASK                                         0x02823C
#define   S_02823C_OUTPUT0_ENABLE(x)                                  (((unsigned)(x) & 0x0F) << 0)
#define   G_02823C_OUTPUT0_ENABLE(x)                                  (((x) >> 0) & 0x0F)
#define   C_02823C_OUTPUT0_ENABLE                                     0xFFFFFFF0
#define   S_02823C_OUTPUT1_ENABLE(x)                                  (((unsigned)(x) & 0x0F) << 4)
#define   G_02823C_OUTPUT1_ENABLE(x)                                  (((x) >> 4) & 0x0F)
#define   C_02823C_OUTPUT1_ENABLE                                     0xFFFFFF0F
#define   S_02823C_OUTPUT2_ENABLE(x)                                  (((unsigned)(x) & 0x0F) << 8)
#define   G_02823C_OUTPUT2_ENABLE(x)                                  (((x) >> 8) & 0x0F)
#define   C_02823C_OUTPUT2_ENABLE                                     0xFFFFF0FF
#define   S_02823C_OUTPUT3_ENABLE(x)                                  (((unsigned)(x) & 0x0F) << 12)
#define   G_02823C_OUTPUT3_ENABLE(x)                                  (((x) >> 12) & 0x0F)
#define   C_02823C_OUTPUT3_ENABLE                                     0xFFFF0FFF
#define   S_02823C_OUTPUT4_ENABLE(x)                                  (((unsigned)(x) & 0x0F) << 16)
#define   G_02823C_OUTPUT4_ENABLE(x)                                  (((x) >> 16) & 0x0F)
#define   C_02823C_OUTPUT4_ENABLE                                     0xFFF0FFFF
#define   S_02823C_OUTPUT5_ENABLE(x)                                  (((unsigned)(x) & 0x0F) << 20)
#define   G_02823C_OUTPUT5_ENABLE(x)                                  (((x) >> 20) & 0x0F)
#define   C_02823C_OUTPUT5_ENABLE                                     0xFF0FFFFF
#define   S_02823C_OUTPUT6_ENABLE(x)                                  (((unsigned)(x) & 0x0F) << 24)
#define   G_02823C_OUTPUT6_ENABLE(x)                                  (((x) >> 24) & 0x0F)
#define   C_02823C_OUTPUT6_ENABLE                                     0xF0FFFFFF
#define   S_02823C_OUTPUT7_ENABLE(x)                                  (((unsigned)(x) & 0x0F) << 28)
#define   G_02823C_OUTPUT7_ENABLE(x)                                  (((x) >> 28) & 0x0F)
#define   C_02823C_OUTPUT7_ENABLE                                     0x0FFFFFFF
#define R_028240_PA_SC_GENERIC_SCISSOR_TL                               0x028240
#define   S_028240_TL_X(x)                                            (((unsigned)(x) & 0x7FFF) << 0)
#define   G_028240_TL_X(x)                                            (((x) >> 0) & 0x7FFF)
#define   C_028240_TL_X                                               0xFFFF8000
#define   S_028240_TL_Y(x)                                            (((unsigned)(x) & 0x7FFF) << 16)
#define   G_028240_TL_Y(x)                                            (((x) >> 16) & 0x7FFF)
#define   C_028240_TL_Y                                               0x8000FFFF
#define   S_028240_WINDOW_OFFSET_DISABLE(x)                           (((unsigned)(x) & 0x1) << 31)
#define   G_028240_WINDOW_OFFSET_DISABLE(x)                           (((x) >> 31) & 0x1)
#define   C_028240_WINDOW_OFFSET_DISABLE                              0x7FFFFFFF
#define R_028244_PA_SC_GENERIC_SCISSOR_BR                               0x028244
#define   S_028244_BR_X(x)                                            (((unsigned)(x) & 0x7FFF) << 0)
#define   G_028244_BR_X(x)                                            (((x) >> 0) & 0x7FFF)
#define   C_028244_BR_X                                               0xFFFF8000
#define   S_028244_BR_Y(x)                                            (((unsigned)(x) & 0x7FFF) << 16)
#define   G_028244_BR_Y(x)                                            (((x) >> 16) & 0x7FFF)
#define   C_028244_BR_Y                                               0x8000FFFF
#define R_028248_COHER_DEST_BASE_0                                      0x028248
#define R_02824C_COHER_DEST_BASE_1                                      0x02824C
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
#define R_028254_PA_SC_VPORT_SCISSOR_0_BR                               0x028254
#define   S_028254_BR_X(x)                                            (((unsigned)(x) & 0x7FFF) << 0)
#define   G_028254_BR_X(x)                                            (((x) >> 0) & 0x7FFF)
#define   C_028254_BR_X                                               0xFFFF8000
#define   S_028254_BR_Y(x)                                            (((unsigned)(x) & 0x7FFF) << 16)
#define   G_028254_BR_Y(x)                                            (((x) >> 16) & 0x7FFF)
#define   C_028254_BR_Y                                               0x8000FFFF
#define R_028258_PA_SC_VPORT_SCISSOR_1_TL                               0x028258
#define R_02825C_PA_SC_VPORT_SCISSOR_1_BR                               0x02825C
#define R_028260_PA_SC_VPORT_SCISSOR_2_TL                               0x028260
#define R_028264_PA_SC_VPORT_SCISSOR_2_BR                               0x028264
#define R_028268_PA_SC_VPORT_SCISSOR_3_TL                               0x028268
#define R_02826C_PA_SC_VPORT_SCISSOR_3_BR                               0x02826C
#define R_028270_PA_SC_VPORT_SCISSOR_4_TL                               0x028270
#define R_028274_PA_SC_VPORT_SCISSOR_4_BR                               0x028274
#define R_028278_PA_SC_VPORT_SCISSOR_5_TL                               0x028278
#define R_02827C_PA_SC_VPORT_SCISSOR_5_BR                               0x02827C
#define R_028280_PA_SC_VPORT_SCISSOR_6_TL                               0x028280
#define R_028284_PA_SC_VPORT_SCISSOR_6_BR                               0x028284
#define R_028288_PA_SC_VPORT_SCISSOR_7_TL                               0x028288
#define R_02828C_PA_SC_VPORT_SCISSOR_7_BR                               0x02828C
#define R_028290_PA_SC_VPORT_SCISSOR_8_TL                               0x028290
#define R_028294_PA_SC_VPORT_SCISSOR_8_BR                               0x028294
#define R_028298_PA_SC_VPORT_SCISSOR_9_TL                               0x028298
#define R_02829C_PA_SC_VPORT_SCISSOR_9_BR                               0x02829C
#define R_0282A0_PA_SC_VPORT_SCISSOR_10_TL                              0x0282A0
#define R_0282A4_PA_SC_VPORT_SCISSOR_10_BR                              0x0282A4
#define R_0282A8_PA_SC_VPORT_SCISSOR_11_TL                              0x0282A8
#define R_0282AC_PA_SC_VPORT_SCISSOR_11_BR                              0x0282AC
#define R_0282B0_PA_SC_VPORT_SCISSOR_12_TL                              0x0282B0
#define R_0282B4_PA_SC_VPORT_SCISSOR_12_BR                              0x0282B4
#define R_0282B8_PA_SC_VPORT_SCISSOR_13_TL                              0x0282B8
#define R_0282BC_PA_SC_VPORT_SCISSOR_13_BR                              0x0282BC
#define R_0282C0_PA_SC_VPORT_SCISSOR_14_TL                              0x0282C0
#define R_0282C4_PA_SC_VPORT_SCISSOR_14_BR                              0x0282C4
#define R_0282C8_PA_SC_VPORT_SCISSOR_15_TL                              0x0282C8
#define R_0282CC_PA_SC_VPORT_SCISSOR_15_BR                              0x0282CC
#define R_0282D0_PA_SC_VPORT_ZMIN_0                                     0x0282D0
#define R_0282D4_PA_SC_VPORT_ZMAX_0                                     0x0282D4
#define R_0282D8_PA_SC_VPORT_ZMIN_1                                     0x0282D8
#define R_0282DC_PA_SC_VPORT_ZMAX_1                                     0x0282DC
#define R_0282E0_PA_SC_VPORT_ZMIN_2                                     0x0282E0
#define R_0282E4_PA_SC_VPORT_ZMAX_2                                     0x0282E4
#define R_0282E8_PA_SC_VPORT_ZMIN_3                                     0x0282E8
#define R_0282EC_PA_SC_VPORT_ZMAX_3                                     0x0282EC
#define R_0282F0_PA_SC_VPORT_ZMIN_4                                     0x0282F0
#define R_0282F4_PA_SC_VPORT_ZMAX_4                                     0x0282F4
#define R_0282F8_PA_SC_VPORT_ZMIN_5                                     0x0282F8
#define R_0282FC_PA_SC_VPORT_ZMAX_5                                     0x0282FC
#define R_028300_PA_SC_VPORT_ZMIN_6                                     0x028300
#define R_028304_PA_SC_VPORT_ZMAX_6                                     0x028304
#define R_028308_PA_SC_VPORT_ZMIN_7                                     0x028308
#define R_02830C_PA_SC_VPORT_ZMAX_7                                     0x02830C
#define R_028310_PA_SC_VPORT_ZMIN_8                                     0x028310
#define R_028314_PA_SC_VPORT_ZMAX_8                                     0x028314
#define R_028318_PA_SC_VPORT_ZMIN_9                                     0x028318
#define R_02831C_PA_SC_VPORT_ZMAX_9                                     0x02831C
#define R_028320_PA_SC_VPORT_ZMIN_10                                    0x028320
#define R_028324_PA_SC_VPORT_ZMAX_10                                    0x028324
#define R_028328_PA_SC_VPORT_ZMIN_11                                    0x028328
#define R_02832C_PA_SC_VPORT_ZMAX_11                                    0x02832C
#define R_028330_PA_SC_VPORT_ZMIN_12                                    0x028330
#define R_028334_PA_SC_VPORT_ZMAX_12                                    0x028334
#define R_028338_PA_SC_VPORT_ZMIN_13                                    0x028338
#define R_02833C_PA_SC_VPORT_ZMAX_13                                    0x02833C
#define R_028340_PA_SC_VPORT_ZMIN_14                                    0x028340
#define R_028344_PA_SC_VPORT_ZMAX_14                                    0x028344
#define R_028348_PA_SC_VPORT_ZMIN_15                                    0x028348
#define R_02834C_PA_SC_VPORT_ZMAX_15                                    0x02834C
#define R_028350_PA_SC_RASTER_CONFIG                                    0x028350
#define   S_028350_RB_MAP_PKR0(x)                                     (((unsigned)(x) & 0x03) << 0)
#define   G_028350_RB_MAP_PKR0(x)                                     (((x) >> 0) & 0x03)
#define   C_028350_RB_MAP_PKR0                                        0xFFFFFFFC
#define     V_028350_RASTER_CONFIG_RB_MAP_0                         0x00
#define     V_028350_RASTER_CONFIG_RB_MAP_1                         0x01
#define     V_028350_RASTER_CONFIG_RB_MAP_2                         0x02
#define     V_028350_RASTER_CONFIG_RB_MAP_3                         0x03
#define   S_028350_RB_MAP_PKR1(x)                                     (((unsigned)(x) & 0x03) << 2)
#define   G_028350_RB_MAP_PKR1(x)                                     (((x) >> 2) & 0x03)
#define   C_028350_RB_MAP_PKR1                                        0xFFFFFFF3
#define     V_028350_RASTER_CONFIG_RB_MAP_0                         0x00
#define     V_028350_RASTER_CONFIG_RB_MAP_1                         0x01
#define     V_028350_RASTER_CONFIG_RB_MAP_2                         0x02
#define     V_028350_RASTER_CONFIG_RB_MAP_3                         0x03
#define   S_028350_RB_XSEL2(x)                                        (((unsigned)(x) & 0x03) << 4)
#define   G_028350_RB_XSEL2(x)                                        (((x) >> 4) & 0x03)
#define   C_028350_RB_XSEL2                                           0xFFFFFFCF
#define     V_028350_RASTER_CONFIG_RB_XSEL2_0                       0x00
#define     V_028350_RASTER_CONFIG_RB_XSEL2_1                       0x01
#define     V_028350_RASTER_CONFIG_RB_XSEL2_2                       0x02
#define     V_028350_RASTER_CONFIG_RB_XSEL2_3                       0x03
#define   S_028350_RB_XSEL(x)                                         (((unsigned)(x) & 0x1) << 6)
#define   G_028350_RB_XSEL(x)                                         (((x) >> 6) & 0x1)
#define   C_028350_RB_XSEL                                            0xFFFFFFBF
#define   S_028350_RB_YSEL(x)                                         (((unsigned)(x) & 0x1) << 7)
#define   G_028350_RB_YSEL(x)                                         (((x) >> 7) & 0x1)
#define   C_028350_RB_YSEL                                            0xFFFFFF7F
#define   S_028350_PKR_MAP(x)                                         (((unsigned)(x) & 0x03) << 8)
#define   G_028350_PKR_MAP(x)                                         (((x) >> 8) & 0x03)
#define   C_028350_PKR_MAP                                            0xFFFFFCFF
#define     V_028350_RASTER_CONFIG_PKR_MAP_0                        0x00
#define     V_028350_RASTER_CONFIG_PKR_MAP_1                        0x01
#define     V_028350_RASTER_CONFIG_PKR_MAP_2                        0x02
#define     V_028350_RASTER_CONFIG_PKR_MAP_3                        0x03
#define   S_028350_PKR_XSEL(x)                                        (((unsigned)(x) & 0x03) << 10)
#define   G_028350_PKR_XSEL(x)                                        (((x) >> 10) & 0x03)
#define   C_028350_PKR_XSEL                                           0xFFFFF3FF
#define     V_028350_RASTER_CONFIG_PKR_XSEL_0                       0x00
#define     V_028350_RASTER_CONFIG_PKR_XSEL_1                       0x01
#define     V_028350_RASTER_CONFIG_PKR_XSEL_2                       0x02
#define     V_028350_RASTER_CONFIG_PKR_XSEL_3                       0x03
#define   S_028350_PKR_YSEL(x)                                        (((unsigned)(x) & 0x03) << 12)
#define   G_028350_PKR_YSEL(x)                                        (((x) >> 12) & 0x03)
#define   C_028350_PKR_YSEL                                           0xFFFFCFFF
#define     V_028350_RASTER_CONFIG_PKR_YSEL_0                       0x00
#define     V_028350_RASTER_CONFIG_PKR_YSEL_1                       0x01
#define     V_028350_RASTER_CONFIG_PKR_YSEL_2                       0x02
#define     V_028350_RASTER_CONFIG_PKR_YSEL_3                       0x03
#define   S_028350_PKR_XSEL2(x)                                       (((unsigned)(x) & 0x03) << 14)
#define   G_028350_PKR_XSEL2(x)                                       (((x) >> 14) & 0x03)
#define   C_028350_PKR_XSEL2                                          0xFFFF3FFF
#define     V_028350_RASTER_CONFIG_PKR_XSEL2_0                      0x00
#define     V_028350_RASTER_CONFIG_PKR_XSEL2_1                      0x01
#define     V_028350_RASTER_CONFIG_PKR_XSEL2_2                      0x02
#define     V_028350_RASTER_CONFIG_PKR_XSEL2_3                      0x03
#define   S_028350_SC_MAP(x)                                          (((unsigned)(x) & 0x03) << 16)
#define   G_028350_SC_MAP(x)                                          (((x) >> 16) & 0x03)
#define   C_028350_SC_MAP                                             0xFFFCFFFF
#define     V_028350_RASTER_CONFIG_SC_MAP_0                         0x00
#define     V_028350_RASTER_CONFIG_SC_MAP_1                         0x01
#define     V_028350_RASTER_CONFIG_SC_MAP_2                         0x02
#define     V_028350_RASTER_CONFIG_SC_MAP_3                         0x03
#define   S_028350_SC_XSEL(x)                                         (((unsigned)(x) & 0x03) << 18)
#define   G_028350_SC_XSEL(x)                                         (((x) >> 18) & 0x03)
#define   C_028350_SC_XSEL                                            0xFFF3FFFF
#define     V_028350_RASTER_CONFIG_SC_XSEL_8_WIDE_TILE              0x00
#define     V_028350_RASTER_CONFIG_SC_XSEL_16_WIDE_TILE             0x01
#define     V_028350_RASTER_CONFIG_SC_XSEL_32_WIDE_TILE             0x02
#define     V_028350_RASTER_CONFIG_SC_XSEL_64_WIDE_TILE             0x03
#define   S_028350_SC_YSEL(x)                                         (((unsigned)(x) & 0x03) << 20)
#define   G_028350_SC_YSEL(x)                                         (((x) >> 20) & 0x03)
#define   C_028350_SC_YSEL                                            0xFFCFFFFF
#define     V_028350_RASTER_CONFIG_SC_YSEL_8_WIDE_TILE              0x00
#define     V_028350_RASTER_CONFIG_SC_YSEL_16_WIDE_TILE             0x01
#define     V_028350_RASTER_CONFIG_SC_YSEL_32_WIDE_TILE             0x02
#define     V_028350_RASTER_CONFIG_SC_YSEL_64_WIDE_TILE             0x03
#define   S_028350_SE_MAP(x)                                          (((unsigned)(x) & 0x03) << 24)
#define   G_028350_SE_MAP(x)                                          (((x) >> 24) & 0x03)
#define   C_028350_SE_MAP                                             0xFCFFFFFF
#define     V_028350_RASTER_CONFIG_SE_MAP_0                         0x00
#define     V_028350_RASTER_CONFIG_SE_MAP_1                         0x01
#define     V_028350_RASTER_CONFIG_SE_MAP_2                         0x02
#define     V_028350_RASTER_CONFIG_SE_MAP_3                         0x03
#define   S_028350_SE_XSEL(x)                                         (((unsigned)(x) & 0x03) << 26)
#define   G_028350_SE_XSEL(x)                                         (((x) >> 26) & 0x03)
#define   C_028350_SE_XSEL                                            0xF3FFFFFF
#define     V_028350_RASTER_CONFIG_SE_XSEL_8_WIDE_TILE              0x00
#define     V_028350_RASTER_CONFIG_SE_XSEL_16_WIDE_TILE             0x01
#define     V_028350_RASTER_CONFIG_SE_XSEL_32_WIDE_TILE             0x02
#define     V_028350_RASTER_CONFIG_SE_XSEL_64_WIDE_TILE             0x03
#define   S_028350_SE_YSEL(x)                                         (((unsigned)(x) & 0x03) << 28)
#define   G_028350_SE_YSEL(x)                                         (((x) >> 28) & 0x03)
#define   C_028350_SE_YSEL                                            0xCFFFFFFF
#define     V_028350_RASTER_CONFIG_SE_YSEL_8_WIDE_TILE              0x00
#define     V_028350_RASTER_CONFIG_SE_YSEL_16_WIDE_TILE             0x01
#define     V_028350_RASTER_CONFIG_SE_YSEL_32_WIDE_TILE             0x02
#define     V_028350_RASTER_CONFIG_SE_YSEL_64_WIDE_TILE             0x03
/* CIK */
#define R_028354_PA_SC_RASTER_CONFIG_1                                  0x028354
#define   S_028354_SE_PAIR_MAP(x)                                     (((unsigned)(x) & 0x03) << 0)
#define   G_028354_SE_PAIR_MAP(x)                                     (((x) >> 0) & 0x03)
#define   C_028354_SE_PAIR_MAP                                        0xFFFFFFFC
#define     V_028354_RASTER_CONFIG_SE_PAIR_MAP_0                    0x00
#define     V_028354_RASTER_CONFIG_SE_PAIR_MAP_1                    0x01
#define     V_028354_RASTER_CONFIG_SE_PAIR_MAP_2                    0x02
#define     V_028354_RASTER_CONFIG_SE_PAIR_MAP_3                    0x03
#define   S_028354_SE_PAIR_XSEL(x)                                    (((unsigned)(x) & 0x03) << 2)
#define   G_028354_SE_PAIR_XSEL(x)                                    (((x) >> 2) & 0x03)
#define   C_028354_SE_PAIR_XSEL                                       0xFFFFFFF3
#define     V_028354_RASTER_CONFIG_SE_PAIR_XSEL_8_WIDE_TILE         0x00
#define     V_028354_RASTER_CONFIG_SE_PAIR_XSEL_16_WIDE_TILE        0x01
#define     V_028354_RASTER_CONFIG_SE_PAIR_XSEL_32_WIDE_TILE        0x02
#define     V_028354_RASTER_CONFIG_SE_PAIR_XSEL_64_WIDE_TILE        0x03
#define   S_028354_SE_PAIR_YSEL(x)                                    (((unsigned)(x) & 0x03) << 4)
#define   G_028354_SE_PAIR_YSEL(x)                                    (((x) >> 4) & 0x03)
#define   C_028354_SE_PAIR_YSEL                                       0xFFFFFFCF
#define     V_028354_RASTER_CONFIG_SE_PAIR_YSEL_8_WIDE_TILE         0x00
#define     V_028354_RASTER_CONFIG_SE_PAIR_YSEL_16_WIDE_TILE        0x01
#define     V_028354_RASTER_CONFIG_SE_PAIR_YSEL_32_WIDE_TILE        0x02
#define     V_028354_RASTER_CONFIG_SE_PAIR_YSEL_64_WIDE_TILE        0x03
#define R_028358_PA_SC_SCREEN_EXTENT_CONTROL                            0x028358
#define   S_028358_SLICE_EVEN_ENABLE(x)                               (((unsigned)(x) & 0x03) << 0)
#define   G_028358_SLICE_EVEN_ENABLE(x)                               (((x) >> 0) & 0x03)
#define   C_028358_SLICE_EVEN_ENABLE                                  0xFFFFFFFC
#define   S_028358_SLICE_ODD_ENABLE(x)                                (((unsigned)(x) & 0x03) << 2)
#define   G_028358_SLICE_ODD_ENABLE(x)                                (((x) >> 2) & 0x03)
#define   C_028358_SLICE_ODD_ENABLE                                   0xFFFFFFF3
/*     */
#define R_028400_VGT_MAX_VTX_INDX                                       0x028400
#define R_028404_VGT_MIN_VTX_INDX                                       0x028404
#define R_028408_VGT_INDX_OFFSET                                        0x028408
#define R_02840C_VGT_MULTI_PRIM_IB_RESET_INDX                           0x02840C
#define R_028414_CB_BLEND_RED                                           0x028414
#define R_028418_CB_BLEND_GREEN                                         0x028418
#define R_02841C_CB_BLEND_BLUE                                          0x02841C
#define R_028420_CB_BLEND_ALPHA                                         0x028420
/* VI */
#define R_028424_CB_DCC_CONTROL                                         0x028424
#define   S_028424_OVERWRITE_COMBINER_DISABLE(x)                      (((unsigned)(x) & 0x1) << 0)
#define   G_028424_OVERWRITE_COMBINER_DISABLE(x)                      (((x) >> 0) & 0x1)
#define   C_028424_OVERWRITE_COMBINER_DISABLE                         0xFFFFFFFE
#define   S_028424_OVERWRITE_COMBINER_MRT_SHARING_DISABLE(x)          (((unsigned)(x) & 0x1) << 1)
#define   G_028424_OVERWRITE_COMBINER_MRT_SHARING_DISABLE(x)          (((x) >> 1) & 0x1)
#define   C_028424_OVERWRITE_COMBINER_MRT_SHARING_DISABLE             0xFFFFFFFD
#define   S_028424_OVERWRITE_COMBINER_WATERMARK(x)                    (((unsigned)(x) & 0x1F) << 2)
#define   G_028424_OVERWRITE_COMBINER_WATERMARK(x)                    (((x) >> 2) & 0x1F)
#define   C_028424_OVERWRITE_COMBINER_WATERMARK                       0xFFFFFF83
/*    */
#define R_02842C_DB_STENCIL_CONTROL                                     0x02842C
#define   S_02842C_STENCILFAIL(x)                                     (((unsigned)(x) & 0x0F) << 0)
#define   G_02842C_STENCILFAIL(x)                                     (((x) >> 0) & 0x0F)
#define   C_02842C_STENCILFAIL                                        0xFFFFFFF0
#define     V_02842C_STENCIL_KEEP                                   0x00
#define     V_02842C_STENCIL_ZERO                                   0x01
#define     V_02842C_STENCIL_ONES                                   0x02
#define     V_02842C_STENCIL_REPLACE_TEST                           0x03
#define     V_02842C_STENCIL_REPLACE_OP                             0x04
#define     V_02842C_STENCIL_ADD_CLAMP                              0x05
#define     V_02842C_STENCIL_SUB_CLAMP                              0x06
#define     V_02842C_STENCIL_INVERT                                 0x07
#define     V_02842C_STENCIL_ADD_WRAP                               0x08
#define     V_02842C_STENCIL_SUB_WRAP                               0x09
#define     V_02842C_STENCIL_AND                                    0x0A
#define     V_02842C_STENCIL_OR                                     0x0B
#define     V_02842C_STENCIL_XOR                                    0x0C
#define     V_02842C_STENCIL_NAND                                   0x0D
#define     V_02842C_STENCIL_NOR                                    0x0E
#define     V_02842C_STENCIL_XNOR                                   0x0F
#define   S_02842C_STENCILZPASS(x)                                    (((unsigned)(x) & 0x0F) << 4)
#define   G_02842C_STENCILZPASS(x)                                    (((x) >> 4) & 0x0F)
#define   C_02842C_STENCILZPASS                                       0xFFFFFF0F
#define     V_02842C_STENCIL_KEEP                                   0x00
#define     V_02842C_STENCIL_ZERO                                   0x01
#define     V_02842C_STENCIL_ONES                                   0x02
#define     V_02842C_STENCIL_REPLACE_TEST                           0x03
#define     V_02842C_STENCIL_REPLACE_OP                             0x04
#define     V_02842C_STENCIL_ADD_CLAMP                              0x05
#define     V_02842C_STENCIL_SUB_CLAMP                              0x06
#define     V_02842C_STENCIL_INVERT                                 0x07
#define     V_02842C_STENCIL_ADD_WRAP                               0x08
#define     V_02842C_STENCIL_SUB_WRAP                               0x09
#define     V_02842C_STENCIL_AND                                    0x0A
#define     V_02842C_STENCIL_OR                                     0x0B
#define     V_02842C_STENCIL_XOR                                    0x0C
#define     V_02842C_STENCIL_NAND                                   0x0D
#define     V_02842C_STENCIL_NOR                                    0x0E
#define     V_02842C_STENCIL_XNOR                                   0x0F
#define   S_02842C_STENCILZFAIL(x)                                    (((unsigned)(x) & 0x0F) << 8)
#define   G_02842C_STENCILZFAIL(x)                                    (((x) >> 8) & 0x0F)
#define   C_02842C_STENCILZFAIL                                       0xFFFFF0FF
#define     V_02842C_STENCIL_KEEP                                   0x00
#define     V_02842C_STENCIL_ZERO                                   0x01
#define     V_02842C_STENCIL_ONES                                   0x02
#define     V_02842C_STENCIL_REPLACE_TEST                           0x03
#define     V_02842C_STENCIL_REPLACE_OP                             0x04
#define     V_02842C_STENCIL_ADD_CLAMP                              0x05
#define     V_02842C_STENCIL_SUB_CLAMP                              0x06
#define     V_02842C_STENCIL_INVERT                                 0x07
#define     V_02842C_STENCIL_ADD_WRAP                               0x08
#define     V_02842C_STENCIL_SUB_WRAP                               0x09
#define     V_02842C_STENCIL_AND                                    0x0A
#define     V_02842C_STENCIL_OR                                     0x0B
#define     V_02842C_STENCIL_XOR                                    0x0C
#define     V_02842C_STENCIL_NAND                                   0x0D
#define     V_02842C_STENCIL_NOR                                    0x0E
#define     V_02842C_STENCIL_XNOR                                   0x0F
#define   S_02842C_STENCILFAIL_BF(x)                                  (((unsigned)(x) & 0x0F) << 12)
#define   G_02842C_STENCILFAIL_BF(x)                                  (((x) >> 12) & 0x0F)
#define   C_02842C_STENCILFAIL_BF                                     0xFFFF0FFF
#define     V_02842C_STENCIL_KEEP                                   0x00
#define     V_02842C_STENCIL_ZERO                                   0x01
#define     V_02842C_STENCIL_ONES                                   0x02
#define     V_02842C_STENCIL_REPLACE_TEST                           0x03
#define     V_02842C_STENCIL_REPLACE_OP                             0x04
#define     V_02842C_STENCIL_ADD_CLAMP                              0x05
#define     V_02842C_STENCIL_SUB_CLAMP                              0x06
#define     V_02842C_STENCIL_INVERT                                 0x07
#define     V_02842C_STENCIL_ADD_WRAP                               0x08
#define     V_02842C_STENCIL_SUB_WRAP                               0x09
#define     V_02842C_STENCIL_AND                                    0x0A
#define     V_02842C_STENCIL_OR                                     0x0B
#define     V_02842C_STENCIL_XOR                                    0x0C
#define     V_02842C_STENCIL_NAND                                   0x0D
#define     V_02842C_STENCIL_NOR                                    0x0E
#define     V_02842C_STENCIL_XNOR                                   0x0F
#define   S_02842C_STENCILZPASS_BF(x)                                 (((unsigned)(x) & 0x0F) << 16)
#define   G_02842C_STENCILZPASS_BF(x)                                 (((x) >> 16) & 0x0F)
#define   C_02842C_STENCILZPASS_BF                                    0xFFF0FFFF
#define     V_02842C_STENCIL_KEEP                                   0x00
#define     V_02842C_STENCIL_ZERO                                   0x01
#define     V_02842C_STENCIL_ONES                                   0x02
#define     V_02842C_STENCIL_REPLACE_TEST                           0x03
#define     V_02842C_STENCIL_REPLACE_OP                             0x04
#define     V_02842C_STENCIL_ADD_CLAMP                              0x05
#define     V_02842C_STENCIL_SUB_CLAMP                              0x06
#define     V_02842C_STENCIL_INVERT                                 0x07
#define     V_02842C_STENCIL_ADD_WRAP                               0x08
#define     V_02842C_STENCIL_SUB_WRAP                               0x09
#define     V_02842C_STENCIL_AND                                    0x0A
#define     V_02842C_STENCIL_OR                                     0x0B
#define     V_02842C_STENCIL_XOR                                    0x0C
#define     V_02842C_STENCIL_NAND                                   0x0D
#define     V_02842C_STENCIL_NOR                                    0x0E
#define     V_02842C_STENCIL_XNOR                                   0x0F
#define   S_02842C_STENCILZFAIL_BF(x)                                 (((unsigned)(x) & 0x0F) << 20)
#define   G_02842C_STENCILZFAIL_BF(x)                                 (((x) >> 20) & 0x0F)
#define   C_02842C_STENCILZFAIL_BF                                    0xFF0FFFFF
#define     V_02842C_STENCIL_KEEP                                   0x00
#define     V_02842C_STENCIL_ZERO                                   0x01
#define     V_02842C_STENCIL_ONES                                   0x02
#define     V_02842C_STENCIL_REPLACE_TEST                           0x03
#define     V_02842C_STENCIL_REPLACE_OP                             0x04
#define     V_02842C_STENCIL_ADD_CLAMP                              0x05
#define     V_02842C_STENCIL_SUB_CLAMP                              0x06
#define     V_02842C_STENCIL_INVERT                                 0x07
#define     V_02842C_STENCIL_ADD_WRAP                               0x08
#define     V_02842C_STENCIL_SUB_WRAP                               0x09
#define     V_02842C_STENCIL_AND                                    0x0A
#define     V_02842C_STENCIL_OR                                     0x0B
#define     V_02842C_STENCIL_XOR                                    0x0C
#define     V_02842C_STENCIL_NAND                                   0x0D
#define     V_02842C_STENCIL_NOR                                    0x0E
#define     V_02842C_STENCIL_XNOR                                   0x0F
#define R_028430_DB_STENCILREFMASK                                      0x028430
#define   S_028430_STENCILTESTVAL(x)                                  (((unsigned)(x) & 0xFF) << 0)
#define   G_028430_STENCILTESTVAL(x)                                  (((x) >> 0) & 0xFF)
#define   C_028430_STENCILTESTVAL                                     0xFFFFFF00
#define   S_028430_STENCILMASK(x)                                     (((unsigned)(x) & 0xFF) << 8)
#define   G_028430_STENCILMASK(x)                                     (((x) >> 8) & 0xFF)
#define   C_028430_STENCILMASK                                        0xFFFF00FF
#define   S_028430_STENCILWRITEMASK(x)                                (((unsigned)(x) & 0xFF) << 16)
#define   G_028430_STENCILWRITEMASK(x)                                (((x) >> 16) & 0xFF)
#define   C_028430_STENCILWRITEMASK                                   0xFF00FFFF
#define   S_028430_STENCILOPVAL(x)                                    (((unsigned)(x) & 0xFF) << 24)
#define   G_028430_STENCILOPVAL(x)                                    (((x) >> 24) & 0xFF)
#define   C_028430_STENCILOPVAL                                       0x00FFFFFF
#define R_028434_DB_STENCILREFMASK_BF                                   0x028434
#define   S_028434_STENCILTESTVAL_BF(x)                               (((unsigned)(x) & 0xFF) << 0)
#define   G_028434_STENCILTESTVAL_BF(x)                               (((x) >> 0) & 0xFF)
#define   C_028434_STENCILTESTVAL_BF                                  0xFFFFFF00
#define   S_028434_STENCILMASK_BF(x)                                  (((unsigned)(x) & 0xFF) << 8)
#define   G_028434_STENCILMASK_BF(x)                                  (((x) >> 8) & 0xFF)
#define   C_028434_STENCILMASK_BF                                     0xFFFF00FF
#define   S_028434_STENCILWRITEMASK_BF(x)                             (((unsigned)(x) & 0xFF) << 16)
#define   G_028434_STENCILWRITEMASK_BF(x)                             (((x) >> 16) & 0xFF)
#define   C_028434_STENCILWRITEMASK_BF                                0xFF00FFFF
#define   S_028434_STENCILOPVAL_BF(x)                                 (((unsigned)(x) & 0xFF) << 24)
#define   G_028434_STENCILOPVAL_BF(x)                                 (((x) >> 24) & 0xFF)
#define   C_028434_STENCILOPVAL_BF                                    0x00FFFFFF
#define R_02843C_PA_CL_VPORT_XSCALE                                     0x02843C
#define R_028440_PA_CL_VPORT_XOFFSET                                    0x028440
#define R_028444_PA_CL_VPORT_YSCALE                                     0x028444
#define R_028448_PA_CL_VPORT_YOFFSET                                    0x028448
#define R_02844C_PA_CL_VPORT_ZSCALE                                     0x02844C
#define R_028450_PA_CL_VPORT_ZOFFSET                                    0x028450
#define R_028454_PA_CL_VPORT_XSCALE_1                                   0x028454
#define R_028458_PA_CL_VPORT_XOFFSET_1                                  0x028458
#define R_02845C_PA_CL_VPORT_YSCALE_1                                   0x02845C
#define R_028460_PA_CL_VPORT_YOFFSET_1                                  0x028460
#define R_028464_PA_CL_VPORT_ZSCALE_1                                   0x028464
#define R_028468_PA_CL_VPORT_ZOFFSET_1                                  0x028468
#define R_02846C_PA_CL_VPORT_XSCALE_2                                   0x02846C
#define R_028470_PA_CL_VPORT_XOFFSET_2                                  0x028470
#define R_028474_PA_CL_VPORT_YSCALE_2                                   0x028474
#define R_028478_PA_CL_VPORT_YOFFSET_2                                  0x028478
#define R_02847C_PA_CL_VPORT_ZSCALE_2                                   0x02847C
#define R_028480_PA_CL_VPORT_ZOFFSET_2                                  0x028480
#define R_028484_PA_CL_VPORT_XSCALE_3                                   0x028484
#define R_028488_PA_CL_VPORT_XOFFSET_3                                  0x028488
#define R_02848C_PA_CL_VPORT_YSCALE_3                                   0x02848C
#define R_028490_PA_CL_VPORT_YOFFSET_3                                  0x028490
#define R_028494_PA_CL_VPORT_ZSCALE_3                                   0x028494
#define R_028498_PA_CL_VPORT_ZOFFSET_3                                  0x028498
#define R_02849C_PA_CL_VPORT_XSCALE_4                                   0x02849C
#define R_0284A0_PA_CL_VPORT_XOFFSET_4                                  0x0284A0
#define R_0284A4_PA_CL_VPORT_YSCALE_4                                   0x0284A4
#define R_0284A8_PA_CL_VPORT_YOFFSET_4                                  0x0284A8
#define R_0284AC_PA_CL_VPORT_ZSCALE_4                                   0x0284AC
#define R_0284B0_PA_CL_VPORT_ZOFFSET_4                                  0x0284B0
#define R_0284B4_PA_CL_VPORT_XSCALE_5                                   0x0284B4
#define R_0284B8_PA_CL_VPORT_XOFFSET_5                                  0x0284B8
#define R_0284BC_PA_CL_VPORT_YSCALE_5                                   0x0284BC
#define R_0284C0_PA_CL_VPORT_YOFFSET_5                                  0x0284C0
#define R_0284C4_PA_CL_VPORT_ZSCALE_5                                   0x0284C4
#define R_0284C8_PA_CL_VPORT_ZOFFSET_5                                  0x0284C8
#define R_0284CC_PA_CL_VPORT_XSCALE_6                                   0x0284CC
#define R_0284D0_PA_CL_VPORT_XOFFSET_6                                  0x0284D0
#define R_0284D4_PA_CL_VPORT_YSCALE_6                                   0x0284D4
#define R_0284D8_PA_CL_VPORT_YOFFSET_6                                  0x0284D8
#define R_0284DC_PA_CL_VPORT_ZSCALE_6                                   0x0284DC
#define R_0284E0_PA_CL_VPORT_ZOFFSET_6                                  0x0284E0
#define R_0284E4_PA_CL_VPORT_XSCALE_7                                   0x0284E4
#define R_0284E8_PA_CL_VPORT_XOFFSET_7                                  0x0284E8
#define R_0284EC_PA_CL_VPORT_YSCALE_7                                   0x0284EC
#define R_0284F0_PA_CL_VPORT_YOFFSET_7                                  0x0284F0
#define R_0284F4_PA_CL_VPORT_ZSCALE_7                                   0x0284F4
#define R_0284F8_PA_CL_VPORT_ZOFFSET_7                                  0x0284F8
#define R_0284FC_PA_CL_VPORT_XSCALE_8                                   0x0284FC
#define R_028500_PA_CL_VPORT_XOFFSET_8                                  0x028500
#define R_028504_PA_CL_VPORT_YSCALE_8                                   0x028504
#define R_028508_PA_CL_VPORT_YOFFSET_8                                  0x028508
#define R_02850C_PA_CL_VPORT_ZSCALE_8                                   0x02850C
#define R_028510_PA_CL_VPORT_ZOFFSET_8                                  0x028510
#define R_028514_PA_CL_VPORT_XSCALE_9                                   0x028514
#define R_028518_PA_CL_VPORT_XOFFSET_9                                  0x028518
#define R_02851C_PA_CL_VPORT_YSCALE_9                                   0x02851C
#define R_028520_PA_CL_VPORT_YOFFSET_9                                  0x028520
#define R_028524_PA_CL_VPORT_ZSCALE_9                                   0x028524
#define R_028528_PA_CL_VPORT_ZOFFSET_9                                  0x028528
#define R_02852C_PA_CL_VPORT_XSCALE_10                                  0x02852C
#define R_028530_PA_CL_VPORT_XOFFSET_10                                 0x028530
#define R_028534_PA_CL_VPORT_YSCALE_10                                  0x028534
#define R_028538_PA_CL_VPORT_YOFFSET_10                                 0x028538
#define R_02853C_PA_CL_VPORT_ZSCALE_10                                  0x02853C
#define R_028540_PA_CL_VPORT_ZOFFSET_10                                 0x028540
#define R_028544_PA_CL_VPORT_XSCALE_11                                  0x028544
#define R_028548_PA_CL_VPORT_XOFFSET_11                                 0x028548
#define R_02854C_PA_CL_VPORT_YSCALE_11                                  0x02854C
#define R_028550_PA_CL_VPORT_YOFFSET_11                                 0x028550
#define R_028554_PA_CL_VPORT_ZSCALE_11                                  0x028554
#define R_028558_PA_CL_VPORT_ZOFFSET_11                                 0x028558
#define R_02855C_PA_CL_VPORT_XSCALE_12                                  0x02855C
#define R_028560_PA_CL_VPORT_XOFFSET_12                                 0x028560
#define R_028564_PA_CL_VPORT_YSCALE_12                                  0x028564
#define R_028568_PA_CL_VPORT_YOFFSET_12                                 0x028568
#define R_02856C_PA_CL_VPORT_ZSCALE_12                                  0x02856C
#define R_028570_PA_CL_VPORT_ZOFFSET_12                                 0x028570
#define R_028574_PA_CL_VPORT_XSCALE_13                                  0x028574
#define R_028578_PA_CL_VPORT_XOFFSET_13                                 0x028578
#define R_02857C_PA_CL_VPORT_YSCALE_13                                  0x02857C
#define R_028580_PA_CL_VPORT_YOFFSET_13                                 0x028580
#define R_028584_PA_CL_VPORT_ZSCALE_13                                  0x028584
#define R_028588_PA_CL_VPORT_ZOFFSET_13                                 0x028588
#define R_02858C_PA_CL_VPORT_XSCALE_14                                  0x02858C
#define R_028590_PA_CL_VPORT_XOFFSET_14                                 0x028590
#define R_028594_PA_CL_VPORT_YSCALE_14                                  0x028594
#define R_028598_PA_CL_VPORT_YOFFSET_14                                 0x028598
#define R_02859C_PA_CL_VPORT_ZSCALE_14                                  0x02859C
#define R_0285A0_PA_CL_VPORT_ZOFFSET_14                                 0x0285A0
#define R_0285A4_PA_CL_VPORT_XSCALE_15                                  0x0285A4
#define R_0285A8_PA_CL_VPORT_XOFFSET_15                                 0x0285A8
#define R_0285AC_PA_CL_VPORT_YSCALE_15                                  0x0285AC
#define R_0285B0_PA_CL_VPORT_YOFFSET_15                                 0x0285B0
#define R_0285B4_PA_CL_VPORT_ZSCALE_15                                  0x0285B4
#define R_0285B8_PA_CL_VPORT_ZOFFSET_15                                 0x0285B8
#define R_0285BC_PA_CL_UCP_0_X                                          0x0285BC
#define R_0285C0_PA_CL_UCP_0_Y                                          0x0285C0
#define R_0285C4_PA_CL_UCP_0_Z                                          0x0285C4
#define R_0285C8_PA_CL_UCP_0_W                                          0x0285C8
#define R_0285CC_PA_CL_UCP_1_X                                          0x0285CC
#define R_0285D0_PA_CL_UCP_1_Y                                          0x0285D0
#define R_0285D4_PA_CL_UCP_1_Z                                          0x0285D4
#define R_0285D8_PA_CL_UCP_1_W                                          0x0285D8
#define R_0285DC_PA_CL_UCP_2_X                                          0x0285DC
#define R_0285E0_PA_CL_UCP_2_Y                                          0x0285E0
#define R_0285E4_PA_CL_UCP_2_Z                                          0x0285E4
#define R_0285E8_PA_CL_UCP_2_W                                          0x0285E8
#define R_0285EC_PA_CL_UCP_3_X                                          0x0285EC
#define R_0285F0_PA_CL_UCP_3_Y                                          0x0285F0
#define R_0285F4_PA_CL_UCP_3_Z                                          0x0285F4
#define R_0285F8_PA_CL_UCP_3_W                                          0x0285F8
#define R_0285FC_PA_CL_UCP_4_X                                          0x0285FC
#define R_028600_PA_CL_UCP_4_Y                                          0x028600
#define R_028604_PA_CL_UCP_4_Z                                          0x028604
#define R_028608_PA_CL_UCP_4_W                                          0x028608
#define R_02860C_PA_CL_UCP_5_X                                          0x02860C
#define R_028610_PA_CL_UCP_5_Y                                          0x028610
#define R_028614_PA_CL_UCP_5_Z                                          0x028614
#define R_028618_PA_CL_UCP_5_W                                          0x028618
#define R_028644_SPI_PS_INPUT_CNTL_0                                    0x028644
#define   S_028644_OFFSET(x)                                          (((unsigned)(x) & 0x3F) << 0)
#define   G_028644_OFFSET(x)                                          (((x) >> 0) & 0x3F)
#define   C_028644_OFFSET                                             0xFFFFFFC0
#define   S_028644_DEFAULT_VAL(x)                                     (((unsigned)(x) & 0x03) << 8)
#define   G_028644_DEFAULT_VAL(x)                                     (((x) >> 8) & 0x03)
#define   C_028644_DEFAULT_VAL                                        0xFFFFFCFF
#define     V_028644_X_0_0F                                         0x00
#define   S_028644_FLAT_SHADE(x)                                      (((unsigned)(x) & 0x1) << 10)
#define   G_028644_FLAT_SHADE(x)                                      (((x) >> 10) & 0x1)
#define   C_028644_FLAT_SHADE                                         0xFFFFFBFF
#define   S_028644_CYL_WRAP(x)                                        (((unsigned)(x) & 0x0F) << 13)
#define   G_028644_CYL_WRAP(x)                                        (((x) >> 13) & 0x0F)
#define   C_028644_CYL_WRAP                                           0xFFFE1FFF
#define   S_028644_PT_SPRITE_TEX(x)                                   (((unsigned)(x) & 0x1) << 17)
#define   G_028644_PT_SPRITE_TEX(x)                                   (((x) >> 17) & 0x1)
#define   C_028644_PT_SPRITE_TEX                                      0xFFFDFFFF
/* CIK */
#define   S_028644_DUP(x)                                             (((unsigned)(x) & 0x1) << 18)
#define   G_028644_DUP(x)                                             (((x) >> 18) & 0x1)
#define   C_028644_DUP                                                0xFFFBFFFF
/*     */
/* VI */
#define   S_028644_FP16_INTERP_MODE(x)                                (((unsigned)(x) & 0x1) << 19)
#define   G_028644_FP16_INTERP_MODE(x)                                (((x) >> 19) & 0x1)
#define   C_028644_FP16_INTERP_MODE                                   0xFFF7FFFF
#define   S_028644_USE_DEFAULT_ATTR1(x)                               (((unsigned)(x) & 0x1) << 20)
#define   G_028644_USE_DEFAULT_ATTR1(x)                               (((x) >> 20) & 0x1)
#define   C_028644_USE_DEFAULT_ATTR1                                  0xFFEFFFFF
#define   S_028644_DEFAULT_VAL_ATTR1(x)                               (((unsigned)(x) & 0x03) << 21)
#define   G_028644_DEFAULT_VAL_ATTR1(x)                               (((x) >> 21) & 0x03)
#define   C_028644_DEFAULT_VAL_ATTR1                                  0xFF9FFFFF
#define   S_028644_PT_SPRITE_TEX_ATTR1(x)                             (((unsigned)(x) & 0x1) << 23)
#define   G_028644_PT_SPRITE_TEX_ATTR1(x)                             (((x) >> 23) & 0x1)
#define   C_028644_PT_SPRITE_TEX_ATTR1                                0xFF7FFFFF
#define   S_028644_ATTR0_VALID(x)                                     (((unsigned)(x) & 0x1) << 24)
#define   G_028644_ATTR0_VALID(x)                                     (((x) >> 24) & 0x1)
#define   C_028644_ATTR0_VALID                                        0xFEFFFFFF
#define   S_028644_ATTR1_VALID(x)                                     (((unsigned)(x) & 0x1) << 25)
#define   G_028644_ATTR1_VALID(x)                                     (((x) >> 25) & 0x1)
#define   C_028644_ATTR1_VALID                                        0xFDFFFFFF
/*    */
#define R_028648_SPI_PS_INPUT_CNTL_1                                    0x028648
#define R_02864C_SPI_PS_INPUT_CNTL_2                                    0x02864C
#define R_028650_SPI_PS_INPUT_CNTL_3                                    0x028650
#define R_028654_SPI_PS_INPUT_CNTL_4                                    0x028654
#define R_028658_SPI_PS_INPUT_CNTL_5                                    0x028658
#define R_02865C_SPI_PS_INPUT_CNTL_6                                    0x02865C
#define R_028660_SPI_PS_INPUT_CNTL_7                                    0x028660
#define R_028664_SPI_PS_INPUT_CNTL_8                                    0x028664
#define R_028668_SPI_PS_INPUT_CNTL_9                                    0x028668
#define R_02866C_SPI_PS_INPUT_CNTL_10                                   0x02866C
#define R_028670_SPI_PS_INPUT_CNTL_11                                   0x028670
#define R_028674_SPI_PS_INPUT_CNTL_12                                   0x028674
#define R_028678_SPI_PS_INPUT_CNTL_13                                   0x028678
#define R_02867C_SPI_PS_INPUT_CNTL_14                                   0x02867C
#define R_028680_SPI_PS_INPUT_CNTL_15                                   0x028680
#define R_028684_SPI_PS_INPUT_CNTL_16                                   0x028684
#define R_028688_SPI_PS_INPUT_CNTL_17                                   0x028688
#define R_02868C_SPI_PS_INPUT_CNTL_18                                   0x02868C
#define R_028690_SPI_PS_INPUT_CNTL_19                                   0x028690
#define R_028694_SPI_PS_INPUT_CNTL_20                                   0x028694
#define R_028698_SPI_PS_INPUT_CNTL_21                                   0x028698
#define R_02869C_SPI_PS_INPUT_CNTL_22                                   0x02869C
#define R_0286A0_SPI_PS_INPUT_CNTL_23                                   0x0286A0
#define R_0286A4_SPI_PS_INPUT_CNTL_24                                   0x0286A4
#define R_0286A8_SPI_PS_INPUT_CNTL_25                                   0x0286A8
#define R_0286AC_SPI_PS_INPUT_CNTL_26                                   0x0286AC
#define R_0286B0_SPI_PS_INPUT_CNTL_27                                   0x0286B0
#define R_0286B4_SPI_PS_INPUT_CNTL_28                                   0x0286B4
#define R_0286B8_SPI_PS_INPUT_CNTL_29                                   0x0286B8
#define R_0286BC_SPI_PS_INPUT_CNTL_30                                   0x0286BC
#define R_0286C0_SPI_PS_INPUT_CNTL_31                                   0x0286C0
#define R_0286C4_SPI_VS_OUT_CONFIG                                      0x0286C4
#define   S_0286C4_VS_EXPORT_COUNT(x)                                 (((unsigned)(x) & 0x1F) << 1)
#define   G_0286C4_VS_EXPORT_COUNT(x)                                 (((x) >> 1) & 0x1F)
#define   C_0286C4_VS_EXPORT_COUNT                                    0xFFFFFFC1
#define   S_0286C4_VS_HALF_PACK(x)                                    (((unsigned)(x) & 0x1) << 6)
#define   G_0286C4_VS_HALF_PACK(x)                                    (((x) >> 6) & 0x1)
#define   C_0286C4_VS_HALF_PACK                                       0xFFFFFFBF
#define   S_0286C4_VS_EXPORTS_FOG(x)                                  (((unsigned)(x) & 0x1) << 7) /* not on CIK */
#define   G_0286C4_VS_EXPORTS_FOG(x)                                  (((x) >> 7) & 0x1) /* not on CIK */
#define   C_0286C4_VS_EXPORTS_FOG                                     0xFFFFFF7F /* not on CIK */
#define   S_0286C4_VS_OUT_FOG_VEC_ADDR(x)                             (((unsigned)(x) & 0x1F) << 8) /* not on CIK */
#define   G_0286C4_VS_OUT_FOG_VEC_ADDR(x)                             (((x) >> 8) & 0x1F) /* not on CIK */
#define   C_0286C4_VS_OUT_FOG_VEC_ADDR                                0xFFFFE0FF /* not on CIK */
#define R_0286CC_SPI_PS_INPUT_ENA                                       0x0286CC
#define   S_0286CC_PERSP_SAMPLE_ENA(x)                                (((unsigned)(x) & 0x1) << 0)
#define   G_0286CC_PERSP_SAMPLE_ENA(x)                                (((x) >> 0) & 0x1)
#define   C_0286CC_PERSP_SAMPLE_ENA                                   0xFFFFFFFE
#define   S_0286CC_PERSP_CENTER_ENA(x)                                (((unsigned)(x) & 0x1) << 1)
#define   G_0286CC_PERSP_CENTER_ENA(x)                                (((x) >> 1) & 0x1)
#define   C_0286CC_PERSP_CENTER_ENA                                   0xFFFFFFFD
#define   S_0286CC_PERSP_CENTROID_ENA(x)                              (((unsigned)(x) & 0x1) << 2)
#define   G_0286CC_PERSP_CENTROID_ENA(x)                              (((x) >> 2) & 0x1)
#define   C_0286CC_PERSP_CENTROID_ENA                                 0xFFFFFFFB
#define   S_0286CC_PERSP_PULL_MODEL_ENA(x)                            (((unsigned)(x) & 0x1) << 3)
#define   G_0286CC_PERSP_PULL_MODEL_ENA(x)                            (((x) >> 3) & 0x1)
#define   C_0286CC_PERSP_PULL_MODEL_ENA                               0xFFFFFFF7
#define   S_0286CC_LINEAR_SAMPLE_ENA(x)                               (((unsigned)(x) & 0x1) << 4)
#define   G_0286CC_LINEAR_SAMPLE_ENA(x)                               (((x) >> 4) & 0x1)
#define   C_0286CC_LINEAR_SAMPLE_ENA                                  0xFFFFFFEF
#define   S_0286CC_LINEAR_CENTER_ENA(x)                               (((unsigned)(x) & 0x1) << 5)
#define   G_0286CC_LINEAR_CENTER_ENA(x)                               (((x) >> 5) & 0x1)
#define   C_0286CC_LINEAR_CENTER_ENA                                  0xFFFFFFDF
#define   S_0286CC_LINEAR_CENTROID_ENA(x)                             (((unsigned)(x) & 0x1) << 6)
#define   G_0286CC_LINEAR_CENTROID_ENA(x)                             (((x) >> 6) & 0x1)
#define   C_0286CC_LINEAR_CENTROID_ENA                                0xFFFFFFBF
#define   S_0286CC_LINE_STIPPLE_TEX_ENA(x)                            (((unsigned)(x) & 0x1) << 7)
#define   G_0286CC_LINE_STIPPLE_TEX_ENA(x)                            (((x) >> 7) & 0x1)
#define   C_0286CC_LINE_STIPPLE_TEX_ENA                               0xFFFFFF7F
#define   S_0286CC_POS_X_FLOAT_ENA(x)                                 (((unsigned)(x) & 0x1) << 8)
#define   G_0286CC_POS_X_FLOAT_ENA(x)                                 (((x) >> 8) & 0x1)
#define   C_0286CC_POS_X_FLOAT_ENA                                    0xFFFFFEFF
#define   S_0286CC_POS_Y_FLOAT_ENA(x)                                 (((unsigned)(x) & 0x1) << 9)
#define   G_0286CC_POS_Y_FLOAT_ENA(x)                                 (((x) >> 9) & 0x1)
#define   C_0286CC_POS_Y_FLOAT_ENA                                    0xFFFFFDFF
#define   S_0286CC_POS_Z_FLOAT_ENA(x)                                 (((unsigned)(x) & 0x1) << 10)
#define   G_0286CC_POS_Z_FLOAT_ENA(x)                                 (((x) >> 10) & 0x1)
#define   C_0286CC_POS_Z_FLOAT_ENA                                    0xFFFFFBFF
#define   S_0286CC_POS_W_FLOAT_ENA(x)                                 (((unsigned)(x) & 0x1) << 11)
#define   G_0286CC_POS_W_FLOAT_ENA(x)                                 (((x) >> 11) & 0x1)
#define   C_0286CC_POS_W_FLOAT_ENA                                    0xFFFFF7FF
#define   S_0286CC_FRONT_FACE_ENA(x)                                  (((unsigned)(x) & 0x1) << 12)
#define   G_0286CC_FRONT_FACE_ENA(x)                                  (((x) >> 12) & 0x1)
#define   C_0286CC_FRONT_FACE_ENA                                     0xFFFFEFFF
#define   S_0286CC_ANCILLARY_ENA(x)                                   (((unsigned)(x) & 0x1) << 13)
#define   G_0286CC_ANCILLARY_ENA(x)                                   (((x) >> 13) & 0x1)
#define   C_0286CC_ANCILLARY_ENA                                      0xFFFFDFFF
#define   S_0286CC_SAMPLE_COVERAGE_ENA(x)                             (((unsigned)(x) & 0x1) << 14)
#define   G_0286CC_SAMPLE_COVERAGE_ENA(x)                             (((x) >> 14) & 0x1)
#define   C_0286CC_SAMPLE_COVERAGE_ENA                                0xFFFFBFFF
#define   S_0286CC_POS_FIXED_PT_ENA(x)                                (((unsigned)(x) & 0x1) << 15)
#define   G_0286CC_POS_FIXED_PT_ENA(x)                                (((x) >> 15) & 0x1)
#define   C_0286CC_POS_FIXED_PT_ENA                                   0xFFFF7FFF
#define R_0286D0_SPI_PS_INPUT_ADDR                                      0x0286D0
#define   S_0286D0_PERSP_SAMPLE_ENA(x)                                (((unsigned)(x) & 0x1) << 0)
#define   G_0286D0_PERSP_SAMPLE_ENA(x)                                (((x) >> 0) & 0x1)
#define   C_0286D0_PERSP_SAMPLE_ENA                                   0xFFFFFFFE
#define   S_0286D0_PERSP_CENTER_ENA(x)                                (((unsigned)(x) & 0x1) << 1)
#define   G_0286D0_PERSP_CENTER_ENA(x)                                (((x) >> 1) & 0x1)
#define   C_0286D0_PERSP_CENTER_ENA                                   0xFFFFFFFD
#define   S_0286D0_PERSP_CENTROID_ENA(x)                              (((unsigned)(x) & 0x1) << 2)
#define   G_0286D0_PERSP_CENTROID_ENA(x)                              (((x) >> 2) & 0x1)
#define   C_0286D0_PERSP_CENTROID_ENA                                 0xFFFFFFFB
#define   S_0286D0_PERSP_PULL_MODEL_ENA(x)                            (((unsigned)(x) & 0x1) << 3)
#define   G_0286D0_PERSP_PULL_MODEL_ENA(x)                            (((x) >> 3) & 0x1)
#define   C_0286D0_PERSP_PULL_MODEL_ENA                               0xFFFFFFF7
#define   S_0286D0_LINEAR_SAMPLE_ENA(x)                               (((unsigned)(x) & 0x1) << 4)
#define   G_0286D0_LINEAR_SAMPLE_ENA(x)                               (((x) >> 4) & 0x1)
#define   C_0286D0_LINEAR_SAMPLE_ENA                                  0xFFFFFFEF
#define   S_0286D0_LINEAR_CENTER_ENA(x)                               (((unsigned)(x) & 0x1) << 5)
#define   G_0286D0_LINEAR_CENTER_ENA(x)                               (((x) >> 5) & 0x1)
#define   C_0286D0_LINEAR_CENTER_ENA                                  0xFFFFFFDF
#define   S_0286D0_LINEAR_CENTROID_ENA(x)                             (((unsigned)(x) & 0x1) << 6)
#define   G_0286D0_LINEAR_CENTROID_ENA(x)                             (((x) >> 6) & 0x1)
#define   C_0286D0_LINEAR_CENTROID_ENA                                0xFFFFFFBF
#define   S_0286D0_LINE_STIPPLE_TEX_ENA(x)                            (((unsigned)(x) & 0x1) << 7)
#define   G_0286D0_LINE_STIPPLE_TEX_ENA(x)                            (((x) >> 7) & 0x1)
#define   C_0286D0_LINE_STIPPLE_TEX_ENA                               0xFFFFFF7F
#define   S_0286D0_POS_X_FLOAT_ENA(x)                                 (((unsigned)(x) & 0x1) << 8)
#define   G_0286D0_POS_X_FLOAT_ENA(x)                                 (((x) >> 8) & 0x1)
#define   C_0286D0_POS_X_FLOAT_ENA                                    0xFFFFFEFF
#define   S_0286D0_POS_Y_FLOAT_ENA(x)                                 (((unsigned)(x) & 0x1) << 9)
#define   G_0286D0_POS_Y_FLOAT_ENA(x)                                 (((x) >> 9) & 0x1)
#define   C_0286D0_POS_Y_FLOAT_ENA                                    0xFFFFFDFF
#define   S_0286D0_POS_Z_FLOAT_ENA(x)                                 (((unsigned)(x) & 0x1) << 10)
#define   G_0286D0_POS_Z_FLOAT_ENA(x)                                 (((x) >> 10) & 0x1)
#define   C_0286D0_POS_Z_FLOAT_ENA                                    0xFFFFFBFF
#define   S_0286D0_POS_W_FLOAT_ENA(x)                                 (((unsigned)(x) & 0x1) << 11)
#define   G_0286D0_POS_W_FLOAT_ENA(x)                                 (((x) >> 11) & 0x1)
#define   C_0286D0_POS_W_FLOAT_ENA                                    0xFFFFF7FF
#define   S_0286D0_FRONT_FACE_ENA(x)                                  (((unsigned)(x) & 0x1) << 12)
#define   G_0286D0_FRONT_FACE_ENA(x)                                  (((x) >> 12) & 0x1)
#define   C_0286D0_FRONT_FACE_ENA                                     0xFFFFEFFF
#define   S_0286D0_ANCILLARY_ENA(x)                                   (((unsigned)(x) & 0x1) << 13)
#define   G_0286D0_ANCILLARY_ENA(x)                                   (((x) >> 13) & 0x1)
#define   C_0286D0_ANCILLARY_ENA                                      0xFFFFDFFF
#define   S_0286D0_SAMPLE_COVERAGE_ENA(x)                             (((unsigned)(x) & 0x1) << 14)
#define   G_0286D0_SAMPLE_COVERAGE_ENA(x)                             (((x) >> 14) & 0x1)
#define   C_0286D0_SAMPLE_COVERAGE_ENA                                0xFFFFBFFF
#define   S_0286D0_POS_FIXED_PT_ENA(x)                                (((unsigned)(x) & 0x1) << 15)
#define   G_0286D0_POS_FIXED_PT_ENA(x)                                (((x) >> 15) & 0x1)
#define   C_0286D0_POS_FIXED_PT_ENA                                   0xFFFF7FFF
#define R_0286D4_SPI_INTERP_CONTROL_0                                   0x0286D4
#define   S_0286D4_FLAT_SHADE_ENA(x)                                  (((unsigned)(x) & 0x1) << 0)
#define   G_0286D4_FLAT_SHADE_ENA(x)                                  (((x) >> 0) & 0x1)
#define   C_0286D4_FLAT_SHADE_ENA                                     0xFFFFFFFE
#define   S_0286D4_PNT_SPRITE_ENA(x)                                  (((unsigned)(x) & 0x1) << 1)
#define   G_0286D4_PNT_SPRITE_ENA(x)                                  (((x) >> 1) & 0x1)
#define   C_0286D4_PNT_SPRITE_ENA                                     0xFFFFFFFD
#define   S_0286D4_PNT_SPRITE_OVRD_X(x)                               (((unsigned)(x) & 0x07) << 2)
#define   G_0286D4_PNT_SPRITE_OVRD_X(x)                               (((x) >> 2) & 0x07)
#define   C_0286D4_PNT_SPRITE_OVRD_X                                  0xFFFFFFE3
#define     V_0286D4_SPI_PNT_SPRITE_SEL_0                           0x00
#define     V_0286D4_SPI_PNT_SPRITE_SEL_1                           0x01
#define     V_0286D4_SPI_PNT_SPRITE_SEL_S                           0x02
#define     V_0286D4_SPI_PNT_SPRITE_SEL_T                           0x03
#define     V_0286D4_SPI_PNT_SPRITE_SEL_NONE                        0x04
#define   S_0286D4_PNT_SPRITE_OVRD_Y(x)                               (((unsigned)(x) & 0x07) << 5)
#define   G_0286D4_PNT_SPRITE_OVRD_Y(x)                               (((x) >> 5) & 0x07)
#define   C_0286D4_PNT_SPRITE_OVRD_Y                                  0xFFFFFF1F
#define     V_0286D4_SPI_PNT_SPRITE_SEL_0                           0x00
#define     V_0286D4_SPI_PNT_SPRITE_SEL_1                           0x01
#define     V_0286D4_SPI_PNT_SPRITE_SEL_S                           0x02
#define     V_0286D4_SPI_PNT_SPRITE_SEL_T                           0x03
#define     V_0286D4_SPI_PNT_SPRITE_SEL_NONE                        0x04
#define   S_0286D4_PNT_SPRITE_OVRD_Z(x)                               (((unsigned)(x) & 0x07) << 8)
#define   G_0286D4_PNT_SPRITE_OVRD_Z(x)                               (((x) >> 8) & 0x07)
#define   C_0286D4_PNT_SPRITE_OVRD_Z                                  0xFFFFF8FF
#define     V_0286D4_SPI_PNT_SPRITE_SEL_0                           0x00
#define     V_0286D4_SPI_PNT_SPRITE_SEL_1                           0x01
#define     V_0286D4_SPI_PNT_SPRITE_SEL_S                           0x02
#define     V_0286D4_SPI_PNT_SPRITE_SEL_T                           0x03
#define     V_0286D4_SPI_PNT_SPRITE_SEL_NONE                        0x04
#define   S_0286D4_PNT_SPRITE_OVRD_W(x)                               (((unsigned)(x) & 0x07) << 11)
#define   G_0286D4_PNT_SPRITE_OVRD_W(x)                               (((x) >> 11) & 0x07)
#define   C_0286D4_PNT_SPRITE_OVRD_W                                  0xFFFFC7FF
#define     V_0286D4_SPI_PNT_SPRITE_SEL_0                           0x00
#define     V_0286D4_SPI_PNT_SPRITE_SEL_1                           0x01
#define     V_0286D4_SPI_PNT_SPRITE_SEL_S                           0x02
#define     V_0286D4_SPI_PNT_SPRITE_SEL_T                           0x03
#define     V_0286D4_SPI_PNT_SPRITE_SEL_NONE                        0x04
#define   S_0286D4_PNT_SPRITE_TOP_1(x)                                (((unsigned)(x) & 0x1) << 14)
#define   G_0286D4_PNT_SPRITE_TOP_1(x)                                (((x) >> 14) & 0x1)
#define   C_0286D4_PNT_SPRITE_TOP_1                                   0xFFFFBFFF
#define R_0286D8_SPI_PS_IN_CONTROL                                      0x0286D8
#define   S_0286D8_NUM_INTERP(x)                                      (((unsigned)(x) & 0x3F) << 0)
#define   G_0286D8_NUM_INTERP(x)                                      (((x) >> 0) & 0x3F)
#define   C_0286D8_NUM_INTERP                                         0xFFFFFFC0
#define   S_0286D8_PARAM_GEN(x)                                       (((unsigned)(x) & 0x1) << 6)
#define   G_0286D8_PARAM_GEN(x)                                       (((x) >> 6) & 0x1)
#define   C_0286D8_PARAM_GEN                                          0xFFFFFFBF
#define   S_0286D8_FOG_ADDR(x)                                        (((unsigned)(x) & 0x7F) << 7) /* not on CIK */
#define   G_0286D8_FOG_ADDR(x)                                        (((x) >> 7) & 0x7F) /* not on CIK */
#define   C_0286D8_FOG_ADDR                                           0xFFFFC07F /* not on CIK */
#define   S_0286D8_BC_OPTIMIZE_DISABLE(x)                             (((unsigned)(x) & 0x1) << 14)
#define   G_0286D8_BC_OPTIMIZE_DISABLE(x)                             (((x) >> 14) & 0x1)
#define   C_0286D8_BC_OPTIMIZE_DISABLE                                0xFFFFBFFF
#define   S_0286D8_PASS_FOG_THROUGH_PS(x)                             (((unsigned)(x) & 0x1) << 15) /* not on CIK */
#define   G_0286D8_PASS_FOG_THROUGH_PS(x)                             (((x) >> 15) & 0x1) /* not on CIK */
#define   C_0286D8_PASS_FOG_THROUGH_PS                                0xFFFF7FFF /* not on CIK */
#define R_0286E0_SPI_BARYC_CNTL                                         0x0286E0
#define   S_0286E0_PERSP_CENTER_CNTL(x)                               (((unsigned)(x) & 0x1) << 0)
#define   G_0286E0_PERSP_CENTER_CNTL(x)                               (((x) >> 0) & 0x1)
#define   C_0286E0_PERSP_CENTER_CNTL                                  0xFFFFFFFE
#define   S_0286E0_PERSP_CENTROID_CNTL(x)                             (((unsigned)(x) & 0x1) << 4)
#define   G_0286E0_PERSP_CENTROID_CNTL(x)                             (((x) >> 4) & 0x1)
#define   C_0286E0_PERSP_CENTROID_CNTL                                0xFFFFFFEF
#define   S_0286E0_LINEAR_CENTER_CNTL(x)                              (((unsigned)(x) & 0x1) << 8)
#define   G_0286E0_LINEAR_CENTER_CNTL(x)                              (((x) >> 8) & 0x1)
#define   C_0286E0_LINEAR_CENTER_CNTL                                 0xFFFFFEFF
#define   S_0286E0_LINEAR_CENTROID_CNTL(x)                            (((unsigned)(x) & 0x1) << 12)
#define   G_0286E0_LINEAR_CENTROID_CNTL(x)                            (((x) >> 12) & 0x1)
#define   C_0286E0_LINEAR_CENTROID_CNTL                               0xFFFFEFFF
#define   S_0286E0_POS_FLOAT_LOCATION(x)                              (((unsigned)(x) & 0x03) << 16)
#define   G_0286E0_POS_FLOAT_LOCATION(x)                              (((x) >> 16) & 0x03)
#define   C_0286E0_POS_FLOAT_LOCATION                                 0xFFFCFFFF
#define     V_0286E0_X_CALCULATE_PER_PIXEL_FLOATING_POINT_POSITION_AT 0x00
#define   S_0286E0_POS_FLOAT_ULC(x)                                   (((unsigned)(x) & 0x1) << 20)
#define   G_0286E0_POS_FLOAT_ULC(x)                                   (((x) >> 20) & 0x1)
#define   C_0286E0_POS_FLOAT_ULC                                      0xFFEFFFFF
#define   S_0286E0_FRONT_FACE_ALL_BITS(x)                             (((unsigned)(x) & 0x1) << 24)
#define   G_0286E0_FRONT_FACE_ALL_BITS(x)                             (((x) >> 24) & 0x1)
#define   C_0286E0_FRONT_FACE_ALL_BITS                                0xFEFFFFFF
#define R_0286E8_SPI_TMPRING_SIZE                                       0x0286E8
#define   S_0286E8_WAVES(x)                                           (((unsigned)(x) & 0xFFF) << 0)
#define   G_0286E8_WAVES(x)                                           (((x) >> 0) & 0xFFF)
#define   C_0286E8_WAVES                                              0xFFFFF000
#define   S_0286E8_WAVESIZE(x)                                        (((unsigned)(x) & 0x1FFF) << 12)
#define   G_0286E8_WAVESIZE(x)                                        (((x) >> 12) & 0x1FFF)
#define   C_0286E8_WAVESIZE                                           0xFE000FFF
#define R_028704_SPI_WAVE_MGMT_1                                        0x028704 /* not on CIK */
#define   S_028704_NUM_PS_WAVES(x)                                    (((unsigned)(x) & 0x3F) << 0)
#define   G_028704_NUM_PS_WAVES(x)                                    (((x) >> 0) & 0x3F)
#define   C_028704_NUM_PS_WAVES                                       0xFFFFFFC0
#define   S_028704_NUM_VS_WAVES(x)                                    (((unsigned)(x) & 0x3F) << 6)
#define   G_028704_NUM_VS_WAVES(x)                                    (((x) >> 6) & 0x3F)
#define   C_028704_NUM_VS_WAVES                                       0xFFFFF03F
#define   S_028704_NUM_GS_WAVES(x)                                    (((unsigned)(x) & 0x3F) << 12)
#define   G_028704_NUM_GS_WAVES(x)                                    (((x) >> 12) & 0x3F)
#define   C_028704_NUM_GS_WAVES                                       0xFFFC0FFF
#define   S_028704_NUM_ES_WAVES(x)                                    (((unsigned)(x) & 0x3F) << 18)
#define   G_028704_NUM_ES_WAVES(x)                                    (((x) >> 18) & 0x3F)
#define   C_028704_NUM_ES_WAVES                                       0xFF03FFFF
#define   S_028704_NUM_HS_WAVES(x)                                    (((unsigned)(x) & 0x3F) << 24)
#define   G_028704_NUM_HS_WAVES(x)                                    (((x) >> 24) & 0x3F)
#define   C_028704_NUM_HS_WAVES                                       0xC0FFFFFF
#define R_028708_SPI_WAVE_MGMT_2                                        0x028708 /* not on CIK */
#define   S_028708_NUM_LS_WAVES(x)                                    (((unsigned)(x) & 0x3F) << 0)
#define   G_028708_NUM_LS_WAVES(x)                                    (((x) >> 0) & 0x3F)
#define   C_028708_NUM_LS_WAVES                                       0xFFFFFFC0
#define R_02870C_SPI_SHADER_POS_FORMAT                                  0x02870C
#define   S_02870C_POS0_EXPORT_FORMAT(x)                              (((unsigned)(x) & 0x0F) << 0)
#define   G_02870C_POS0_EXPORT_FORMAT(x)                              (((x) >> 0) & 0x0F)
#define   C_02870C_POS0_EXPORT_FORMAT                                 0xFFFFFFF0
#define     V_02870C_SPI_SHADER_NONE                                0x00
#define     V_02870C_SPI_SHADER_1COMP                               0x01
#define     V_02870C_SPI_SHADER_2COMP                               0x02
#define     V_02870C_SPI_SHADER_4COMPRESS                           0x03
#define     V_02870C_SPI_SHADER_4COMP                               0x04
#define   S_02870C_POS1_EXPORT_FORMAT(x)                              (((unsigned)(x) & 0x0F) << 4)
#define   G_02870C_POS1_EXPORT_FORMAT(x)                              (((x) >> 4) & 0x0F)
#define   C_02870C_POS1_EXPORT_FORMAT                                 0xFFFFFF0F
#define     V_02870C_SPI_SHADER_NONE                                0x00
#define     V_02870C_SPI_SHADER_1COMP                               0x01
#define     V_02870C_SPI_SHADER_2COMP                               0x02
#define     V_02870C_SPI_SHADER_4COMPRESS                           0x03
#define     V_02870C_SPI_SHADER_4COMP                               0x04
#define   S_02870C_POS2_EXPORT_FORMAT(x)                              (((unsigned)(x) & 0x0F) << 8)
#define   G_02870C_POS2_EXPORT_FORMAT(x)                              (((x) >> 8) & 0x0F)
#define   C_02870C_POS2_EXPORT_FORMAT                                 0xFFFFF0FF
#define     V_02870C_SPI_SHADER_NONE                                0x00
#define     V_02870C_SPI_SHADER_1COMP                               0x01
#define     V_02870C_SPI_SHADER_2COMP                               0x02
#define     V_02870C_SPI_SHADER_4COMPRESS                           0x03
#define     V_02870C_SPI_SHADER_4COMP                               0x04
#define   S_02870C_POS3_EXPORT_FORMAT(x)                              (((unsigned)(x) & 0x0F) << 12)
#define   G_02870C_POS3_EXPORT_FORMAT(x)                              (((x) >> 12) & 0x0F)
#define   C_02870C_POS3_EXPORT_FORMAT                                 0xFFFF0FFF
#define     V_02870C_SPI_SHADER_NONE                                0x00
#define     V_02870C_SPI_SHADER_1COMP                               0x01
#define     V_02870C_SPI_SHADER_2COMP                               0x02
#define     V_02870C_SPI_SHADER_4COMPRESS                           0x03
#define     V_02870C_SPI_SHADER_4COMP                               0x04
#define R_028710_SPI_SHADER_Z_FORMAT                                    0x028710
#define   S_028710_Z_EXPORT_FORMAT(x)                                 (((unsigned)(x) & 0x0F) << 0)
#define   G_028710_Z_EXPORT_FORMAT(x)                                 (((x) >> 0) & 0x0F)
#define   C_028710_Z_EXPORT_FORMAT                                    0xFFFFFFF0
#define     V_028710_SPI_SHADER_ZERO                                0x00
#define     V_028710_SPI_SHADER_32_R                                0x01
#define     V_028710_SPI_SHADER_32_GR                               0x02
#define     V_028710_SPI_SHADER_32_AR                               0x03
#define     V_028710_SPI_SHADER_FP16_ABGR                           0x04
#define     V_028710_SPI_SHADER_UNORM16_ABGR                        0x05
#define     V_028710_SPI_SHADER_SNORM16_ABGR                        0x06
#define     V_028710_SPI_SHADER_UINT16_ABGR                         0x07
#define     V_028710_SPI_SHADER_SINT16_ABGR                         0x08
#define     V_028710_SPI_SHADER_32_ABGR                             0x09
#define R_028714_SPI_SHADER_COL_FORMAT                                  0x028714
#define   S_028714_COL0_EXPORT_FORMAT(x)                              (((unsigned)(x) & 0x0F) << 0)
#define   G_028714_COL0_EXPORT_FORMAT(x)                              (((x) >> 0) & 0x0F)
#define   C_028714_COL0_EXPORT_FORMAT                                 0xFFFFFFF0
#define     V_028714_SPI_SHADER_ZERO                                0x00
#define     V_028714_SPI_SHADER_32_R                                0x01
#define     V_028714_SPI_SHADER_32_GR                               0x02
#define     V_028714_SPI_SHADER_32_AR                               0x03
#define     V_028714_SPI_SHADER_FP16_ABGR                           0x04
#define     V_028714_SPI_SHADER_UNORM16_ABGR                        0x05
#define     V_028714_SPI_SHADER_SNORM16_ABGR                        0x06
#define     V_028714_SPI_SHADER_UINT16_ABGR                         0x07
#define     V_028714_SPI_SHADER_SINT16_ABGR                         0x08
#define     V_028714_SPI_SHADER_32_ABGR                             0x09
#define   S_028714_COL1_EXPORT_FORMAT(x)                              (((unsigned)(x) & 0x0F) << 4)
#define   G_028714_COL1_EXPORT_FORMAT(x)                              (((x) >> 4) & 0x0F)
#define   C_028714_COL1_EXPORT_FORMAT                                 0xFFFFFF0F
#define     V_028714_SPI_SHADER_ZERO                                0x00
#define     V_028714_SPI_SHADER_32_R                                0x01
#define     V_028714_SPI_SHADER_32_GR                               0x02
#define     V_028714_SPI_SHADER_32_AR                               0x03
#define     V_028714_SPI_SHADER_FP16_ABGR                           0x04
#define     V_028714_SPI_SHADER_UNORM16_ABGR                        0x05
#define     V_028714_SPI_SHADER_SNORM16_ABGR                        0x06
#define     V_028714_SPI_SHADER_UINT16_ABGR                         0x07
#define     V_028714_SPI_SHADER_SINT16_ABGR                         0x08
#define     V_028714_SPI_SHADER_32_ABGR                             0x09
#define   S_028714_COL2_EXPORT_FORMAT(x)                              (((unsigned)(x) & 0x0F) << 8)
#define   G_028714_COL2_EXPORT_FORMAT(x)                              (((x) >> 8) & 0x0F)
#define   C_028714_COL2_EXPORT_FORMAT                                 0xFFFFF0FF
#define     V_028714_SPI_SHADER_ZERO                                0x00
#define     V_028714_SPI_SHADER_32_R                                0x01
#define     V_028714_SPI_SHADER_32_GR                               0x02
#define     V_028714_SPI_SHADER_32_AR                               0x03
#define     V_028714_SPI_SHADER_FP16_ABGR                           0x04
#define     V_028714_SPI_SHADER_UNORM16_ABGR                        0x05
#define     V_028714_SPI_SHADER_SNORM16_ABGR                        0x06
#define     V_028714_SPI_SHADER_UINT16_ABGR                         0x07
#define     V_028714_SPI_SHADER_SINT16_ABGR                         0x08
#define     V_028714_SPI_SHADER_32_ABGR                             0x09
#define   S_028714_COL3_EXPORT_FORMAT(x)                              (((unsigned)(x) & 0x0F) << 12)
#define   G_028714_COL3_EXPORT_FORMAT(x)                              (((x) >> 12) & 0x0F)
#define   C_028714_COL3_EXPORT_FORMAT                                 0xFFFF0FFF
#define     V_028714_SPI_SHADER_ZERO                                0x00
#define     V_028714_SPI_SHADER_32_R                                0x01
#define     V_028714_SPI_SHADER_32_GR                               0x02
#define     V_028714_SPI_SHADER_32_AR                               0x03
#define     V_028714_SPI_SHADER_FP16_ABGR                           0x04
#define     V_028714_SPI_SHADER_UNORM16_ABGR                        0x05
#define     V_028714_SPI_SHADER_SNORM16_ABGR                        0x06
#define     V_028714_SPI_SHADER_UINT16_ABGR                         0x07
#define     V_028714_SPI_SHADER_SINT16_ABGR                         0x08
#define     V_028714_SPI_SHADER_32_ABGR                             0x09
#define   S_028714_COL4_EXPORT_FORMAT(x)                              (((unsigned)(x) & 0x0F) << 16)
#define   G_028714_COL4_EXPORT_FORMAT(x)                              (((x) >> 16) & 0x0F)
#define   C_028714_COL4_EXPORT_FORMAT                                 0xFFF0FFFF
#define     V_028714_SPI_SHADER_ZERO                                0x00
#define     V_028714_SPI_SHADER_32_R                                0x01
#define     V_028714_SPI_SHADER_32_GR                               0x02
#define     V_028714_SPI_SHADER_32_AR                               0x03
#define     V_028714_SPI_SHADER_FP16_ABGR                           0x04
#define     V_028714_SPI_SHADER_UNORM16_ABGR                        0x05
#define     V_028714_SPI_SHADER_SNORM16_ABGR                        0x06
#define     V_028714_SPI_SHADER_UINT16_ABGR                         0x07
#define     V_028714_SPI_SHADER_SINT16_ABGR                         0x08
#define     V_028714_SPI_SHADER_32_ABGR                             0x09
#define   S_028714_COL5_EXPORT_FORMAT(x)                              (((unsigned)(x) & 0x0F) << 20)
#define   G_028714_COL5_EXPORT_FORMAT(x)                              (((x) >> 20) & 0x0F)
#define   C_028714_COL5_EXPORT_FORMAT                                 0xFF0FFFFF
#define     V_028714_SPI_SHADER_ZERO                                0x00
#define     V_028714_SPI_SHADER_32_R                                0x01
#define     V_028714_SPI_SHADER_32_GR                               0x02
#define     V_028714_SPI_SHADER_32_AR                               0x03
#define     V_028714_SPI_SHADER_FP16_ABGR                           0x04
#define     V_028714_SPI_SHADER_UNORM16_ABGR                        0x05
#define     V_028714_SPI_SHADER_SNORM16_ABGR                        0x06
#define     V_028714_SPI_SHADER_UINT16_ABGR                         0x07
#define     V_028714_SPI_SHADER_SINT16_ABGR                         0x08
#define     V_028714_SPI_SHADER_32_ABGR                             0x09
#define   S_028714_COL6_EXPORT_FORMAT(x)                              (((unsigned)(x) & 0x0F) << 24)
#define   G_028714_COL6_EXPORT_FORMAT(x)                              (((x) >> 24) & 0x0F)
#define   C_028714_COL6_EXPORT_FORMAT                                 0xF0FFFFFF
#define     V_028714_SPI_SHADER_ZERO                                0x00
#define     V_028714_SPI_SHADER_32_R                                0x01
#define     V_028714_SPI_SHADER_32_GR                               0x02
#define     V_028714_SPI_SHADER_32_AR                               0x03
#define     V_028714_SPI_SHADER_FP16_ABGR                           0x04
#define     V_028714_SPI_SHADER_UNORM16_ABGR                        0x05
#define     V_028714_SPI_SHADER_SNORM16_ABGR                        0x06
#define     V_028714_SPI_SHADER_UINT16_ABGR                         0x07
#define     V_028714_SPI_SHADER_SINT16_ABGR                         0x08
#define     V_028714_SPI_SHADER_32_ABGR                             0x09
#define   S_028714_COL7_EXPORT_FORMAT(x)                              (((unsigned)(x) & 0x0F) << 28)
#define   G_028714_COL7_EXPORT_FORMAT(x)                              (((x) >> 28) & 0x0F)
#define   C_028714_COL7_EXPORT_FORMAT                                 0x0FFFFFFF
#define     V_028714_SPI_SHADER_ZERO                                0x00
#define     V_028714_SPI_SHADER_32_R                                0x01
#define     V_028714_SPI_SHADER_32_GR                               0x02
#define     V_028714_SPI_SHADER_32_AR                               0x03
#define     V_028714_SPI_SHADER_FP16_ABGR                           0x04
#define     V_028714_SPI_SHADER_UNORM16_ABGR                        0x05
#define     V_028714_SPI_SHADER_SNORM16_ABGR                        0x06
#define     V_028714_SPI_SHADER_UINT16_ABGR                         0x07
#define     V_028714_SPI_SHADER_SINT16_ABGR                         0x08
#define     V_028714_SPI_SHADER_32_ABGR                             0x09
/* Stoney */
#define R_028754_SX_PS_DOWNCONVERT                                      0x028754
#define   S_028754_MRT0(x)                                            (((unsigned)(x) & 0x0F) << 0)
#define   G_028754_MRT0(x)                                            (((x) >> 0) & 0x0F)
#define   C_028754_MRT0                                               0xFFFFFFF0
#define     V_028754_SX_RT_EXPORT_NO_CONVERSION				0
#define     V_028754_SX_RT_EXPORT_32_R					1
#define     V_028754_SX_RT_EXPORT_32_A					2
#define     V_028754_SX_RT_EXPORT_10_11_11				3
#define     V_028754_SX_RT_EXPORT_2_10_10_10				4
#define     V_028754_SX_RT_EXPORT_8_8_8_8				5
#define     V_028754_SX_RT_EXPORT_5_6_5					6
#define     V_028754_SX_RT_EXPORT_1_5_5_5				7
#define     V_028754_SX_RT_EXPORT_4_4_4_4				8
#define     V_028754_SX_RT_EXPORT_16_16_GR				9
#define     V_028754_SX_RT_EXPORT_16_16_AR				10
#define   S_028754_MRT1(x)                                            (((unsigned)(x) & 0x0F) << 4)
#define   G_028754_MRT1(x)                                            (((x) >> 4) & 0x0F)
#define   C_028754_MRT1                                               0xFFFFFF0F
#define   S_028754_MRT2(x)                                            (((unsigned)(x) & 0x0F) << 8)
#define   G_028754_MRT2(x)                                            (((x) >> 8) & 0x0F)
#define   C_028754_MRT2                                               0xFFFFF0FF
#define   S_028754_MRT3(x)                                            (((unsigned)(x) & 0x0F) << 12)
#define   G_028754_MRT3(x)                                            (((x) >> 12) & 0x0F)
#define   C_028754_MRT3                                               0xFFFF0FFF
#define   S_028754_MRT4(x)                                            (((unsigned)(x) & 0x0F) << 16)
#define   G_028754_MRT4(x)                                            (((x) >> 16) & 0x0F)
#define   C_028754_MRT4                                               0xFFF0FFFF
#define   S_028754_MRT5(x)                                            (((unsigned)(x) & 0x0F) << 20)
#define   G_028754_MRT5(x)                                            (((x) >> 20) & 0x0F)
#define   C_028754_MRT5                                               0xFF0FFFFF
#define   S_028754_MRT6(x)                                            (((unsigned)(x) & 0x0F) << 24)
#define   G_028754_MRT6(x)                                            (((x) >> 24) & 0x0F)
#define   C_028754_MRT6                                               0xF0FFFFFF
#define   S_028754_MRT7(x)                                            (((unsigned)(x) & 0x0F) << 28)
#define   G_028754_MRT7(x)                                            (((x) >> 28) & 0x0F)
#define   C_028754_MRT7                                               0x0FFFFFFF
#define R_028758_SX_BLEND_OPT_EPSILON                                   0x028758
#define   S_028758_MRT0_EPSILON(x)                                    (((unsigned)(x) & 0x0F) << 0)
#define   G_028758_MRT0_EPSILON(x)                                    (((x) >> 0) & 0x0F)
#define   C_028758_MRT0_EPSILON                                       0xFFFFFFF0
#define      V_028758_EXACT						0
#define      V_028758_11BIT_FORMAT					1
#define      V_028758_10BIT_FORMAT					3
#define      V_028758_8BIT_FORMAT					7
#define      V_028758_6BIT_FORMAT					11
#define      V_028758_5BIT_FORMAT					13
#define      V_028758_4BIT_FORMAT					15
#define   S_028758_MRT1_EPSILON(x)                                    (((unsigned)(x) & 0x0F) << 4)
#define   G_028758_MRT1_EPSILON(x)                                    (((x) >> 4) & 0x0F)
#define   C_028758_MRT1_EPSILON                                       0xFFFFFF0F
#define   S_028758_MRT2_EPSILON(x)                                    (((unsigned)(x) & 0x0F) << 8)
#define   G_028758_MRT2_EPSILON(x)                                    (((x) >> 8) & 0x0F)
#define   C_028758_MRT2_EPSILON                                       0xFFFFF0FF
#define   S_028758_MRT3_EPSILON(x)                                    (((unsigned)(x) & 0x0F) << 12)
#define   G_028758_MRT3_EPSILON(x)                                    (((x) >> 12) & 0x0F)
#define   C_028758_MRT3_EPSILON                                       0xFFFF0FFF
#define   S_028758_MRT4_EPSILON(x)                                    (((unsigned)(x) & 0x0F) << 16)
#define   G_028758_MRT4_EPSILON(x)                                    (((x) >> 16) & 0x0F)
#define   C_028758_MRT4_EPSILON                                       0xFFF0FFFF
#define   S_028758_MRT5_EPSILON(x)                                    (((unsigned)(x) & 0x0F) << 20)
#define   G_028758_MRT5_EPSILON(x)                                    (((x) >> 20) & 0x0F)
#define   C_028758_MRT5_EPSILON                                       0xFF0FFFFF
#define   S_028758_MRT6_EPSILON(x)                                    (((unsigned)(x) & 0x0F) << 24)
#define   G_028758_MRT6_EPSILON(x)                                    (((x) >> 24) & 0x0F)
#define   C_028758_MRT6_EPSILON                                       0xF0FFFFFF
#define   S_028758_MRT7_EPSILON(x)                                    (((unsigned)(x) & 0x0F) << 28)
#define   G_028758_MRT7_EPSILON(x)                                    (((x) >> 28) & 0x0F)
#define   C_028758_MRT7_EPSILON                                       0x0FFFFFFF
#define R_02875C_SX_BLEND_OPT_CONTROL                                   0x02875C
#define   S_02875C_MRT0_COLOR_OPT_DISABLE(x)                          (((unsigned)(x) & 0x1) << 0)
#define   G_02875C_MRT0_COLOR_OPT_DISABLE(x)                          (((x) >> 0) & 0x1)
#define   C_02875C_MRT0_COLOR_OPT_DISABLE                             0xFFFFFFFE
#define   S_02875C_MRT0_ALPHA_OPT_DISABLE(x)                          (((unsigned)(x) & 0x1) << 1)
#define   G_02875C_MRT0_ALPHA_OPT_DISABLE(x)                          (((x) >> 1) & 0x1)
#define   C_02875C_MRT0_ALPHA_OPT_DISABLE                             0xFFFFFFFD
#define   S_02875C_MRT1_COLOR_OPT_DISABLE(x)                          (((unsigned)(x) & 0x1) << 4)
#define   G_02875C_MRT1_COLOR_OPT_DISABLE(x)                          (((x) >> 4) & 0x1)
#define   C_02875C_MRT1_COLOR_OPT_DISABLE                             0xFFFFFFEF
#define   S_02875C_MRT1_ALPHA_OPT_DISABLE(x)                          (((unsigned)(x) & 0x1) << 5)
#define   G_02875C_MRT1_ALPHA_OPT_DISABLE(x)                          (((x) >> 5) & 0x1)
#define   C_02875C_MRT1_ALPHA_OPT_DISABLE                             0xFFFFFFDF
#define   S_02875C_MRT2_COLOR_OPT_DISABLE(x)                          (((unsigned)(x) & 0x1) << 8)
#define   G_02875C_MRT2_COLOR_OPT_DISABLE(x)                          (((x) >> 8) & 0x1)
#define   C_02875C_MRT2_COLOR_OPT_DISABLE                             0xFFFFFEFF
#define   S_02875C_MRT2_ALPHA_OPT_DISABLE(x)                          (((unsigned)(x) & 0x1) << 9)
#define   G_02875C_MRT2_ALPHA_OPT_DISABLE(x)                          (((x) >> 9) & 0x1)
#define   C_02875C_MRT2_ALPHA_OPT_DISABLE                             0xFFFFFDFF
#define   S_02875C_MRT3_COLOR_OPT_DISABLE(x)                          (((unsigned)(x) & 0x1) << 12)
#define   G_02875C_MRT3_COLOR_OPT_DISABLE(x)                          (((x) >> 12) & 0x1)
#define   C_02875C_MRT3_COLOR_OPT_DISABLE                             0xFFFFEFFF
#define   S_02875C_MRT3_ALPHA_OPT_DISABLE(x)                          (((unsigned)(x) & 0x1) << 13)
#define   G_02875C_MRT3_ALPHA_OPT_DISABLE(x)                          (((x) >> 13) & 0x1)
#define   C_02875C_MRT3_ALPHA_OPT_DISABLE                             0xFFFFDFFF
#define   S_02875C_MRT4_COLOR_OPT_DISABLE(x)                          (((unsigned)(x) & 0x1) << 16)
#define   G_02875C_MRT4_COLOR_OPT_DISABLE(x)                          (((x) >> 16) & 0x1)
#define   C_02875C_MRT4_COLOR_OPT_DISABLE                             0xFFFEFFFF
#define   S_02875C_MRT4_ALPHA_OPT_DISABLE(x)                          (((unsigned)(x) & 0x1) << 17)
#define   G_02875C_MRT4_ALPHA_OPT_DISABLE(x)                          (((x) >> 17) & 0x1)
#define   C_02875C_MRT4_ALPHA_OPT_DISABLE                             0xFFFDFFFF
#define   S_02875C_MRT5_COLOR_OPT_DISABLE(x)                          (((unsigned)(x) & 0x1) << 20)
#define   G_02875C_MRT5_COLOR_OPT_DISABLE(x)                          (((x) >> 20) & 0x1)
#define   C_02875C_MRT5_COLOR_OPT_DISABLE                             0xFFEFFFFF
#define   S_02875C_MRT5_ALPHA_OPT_DISABLE(x)                          (((unsigned)(x) & 0x1) << 21)
#define   G_02875C_MRT5_ALPHA_OPT_DISABLE(x)                          (((x) >> 21) & 0x1)
#define   C_02875C_MRT5_ALPHA_OPT_DISABLE                             0xFFDFFFFF
#define   S_02875C_MRT6_COLOR_OPT_DISABLE(x)                          (((unsigned)(x) & 0x1) << 24)
#define   G_02875C_MRT6_COLOR_OPT_DISABLE(x)                          (((x) >> 24) & 0x1)
#define   C_02875C_MRT6_COLOR_OPT_DISABLE                             0xFEFFFFFF
#define   S_02875C_MRT6_ALPHA_OPT_DISABLE(x)                          (((unsigned)(x) & 0x1) << 25)
#define   G_02875C_MRT6_ALPHA_OPT_DISABLE(x)                          (((x) >> 25) & 0x1)
#define   C_02875C_MRT6_ALPHA_OPT_DISABLE                             0xFDFFFFFF
#define   S_02875C_MRT7_COLOR_OPT_DISABLE(x)                          (((unsigned)(x) & 0x1) << 28)
#define   G_02875C_MRT7_COLOR_OPT_DISABLE(x)                          (((x) >> 28) & 0x1)
#define   C_02875C_MRT7_COLOR_OPT_DISABLE                             0xEFFFFFFF
#define   S_02875C_MRT7_ALPHA_OPT_DISABLE(x)                          (((unsigned)(x) & 0x1) << 29)
#define   G_02875C_MRT7_ALPHA_OPT_DISABLE(x)                          (((x) >> 29) & 0x1)
#define   C_02875C_MRT7_ALPHA_OPT_DISABLE                             0xDFFFFFFF
#define   S_02875C_PIXEN_ZERO_OPT_DISABLE(x)                          (((unsigned)(x) & 0x1) << 31)
#define   G_02875C_PIXEN_ZERO_OPT_DISABLE(x)                          (((x) >> 31) & 0x1)
#define   C_02875C_PIXEN_ZERO_OPT_DISABLE                             0x7FFFFFFF
#define R_028760_SX_MRT0_BLEND_OPT                                      0x028760
#define   S_028760_COLOR_SRC_OPT(x)                                   (((unsigned)(x) & 0x07) << 0)
#define   G_028760_COLOR_SRC_OPT(x)                                   (((x) >> 0) & 0x07)
#define   C_028760_COLOR_SRC_OPT                                      0xFFFFFFF8
#define     V_028760_BLEND_OPT_PRESERVE_NONE_IGNORE_ALL			0
#define     V_028760_BLEND_OPT_PRESERVE_ALL_IGNORE_NONE			1
#define     V_028760_BLEND_OPT_PRESERVE_C1_IGNORE_C0			2
#define     V_028760_BLEND_OPT_PRESERVE_C0_IGNORE_C1			3
#define     V_028760_BLEND_OPT_PRESERVE_A1_IGNORE_A0			4
#define     V_028760_BLEND_OPT_PRESERVE_A0_IGNORE_A1			5
#define     V_028760_BLEND_OPT_PRESERVE_NONE_IGNORE_A0			6
#define     V_028760_BLEND_OPT_PRESERVE_NONE_IGNORE_NONE		7
#define   S_028760_COLOR_DST_OPT(x)                                   (((unsigned)(x) & 0x07) << 4)
#define   G_028760_COLOR_DST_OPT(x)                                   (((x) >> 4) & 0x07)
#define   C_028760_COLOR_DST_OPT                                      0xFFFFFF8F
#define   S_028760_COLOR_COMB_FCN(x)                                  (((unsigned)(x) & 0x07) << 8)
#define   G_028760_COLOR_COMB_FCN(x)                                  (((x) >> 8) & 0x07)
#define   C_028760_COLOR_COMB_FCN                                     0xFFFFF8FF
#define     V_028760_OPT_COMB_NONE					0
#define     V_028760_OPT_COMB_ADD					1
#define     V_028760_OPT_COMB_SUBTRACT					2
#define     V_028760_OPT_COMB_MIN					3
#define     V_028760_OPT_COMB_MAX					4
#define     V_028760_OPT_COMB_REVSUBTRACT				5
#define     V_028760_OPT_COMB_BLEND_DISABLED				6
#define     V_028760_OPT_COMB_SAFE_ADD					7
#define   S_028760_ALPHA_SRC_OPT(x)                                   (((unsigned)(x) & 0x07) << 16)
#define   G_028760_ALPHA_SRC_OPT(x)                                   (((x) >> 16) & 0x07)
#define   C_028760_ALPHA_SRC_OPT                                      0xFFF8FFFF
#define   S_028760_ALPHA_DST_OPT(x)                                   (((unsigned)(x) & 0x07) << 20)
#define   G_028760_ALPHA_DST_OPT(x)                                   (((x) >> 20) & 0x07)
#define   C_028760_ALPHA_DST_OPT                                      0xFF8FFFFF
#define   S_028760_ALPHA_COMB_FCN(x)                                  (((unsigned)(x) & 0x07) << 24)
#define   G_028760_ALPHA_COMB_FCN(x)                                  (((x) >> 24) & 0x07)
#define   C_028760_ALPHA_COMB_FCN                                     0xF8FFFFFF
#define R_028764_SX_MRT1_BLEND_OPT                                      0x028764
#define   S_028764_COLOR_SRC_OPT(x)                                   (((unsigned)(x) & 0x07) << 0)
#define   G_028764_COLOR_SRC_OPT(x)                                   (((x) >> 0) & 0x07)
#define   C_028764_COLOR_SRC_OPT                                      0xFFFFFFF8
#define   S_028764_COLOR_DST_OPT(x)                                   (((unsigned)(x) & 0x07) << 4)
#define   G_028764_COLOR_DST_OPT(x)                                   (((x) >> 4) & 0x07)
#define   C_028764_COLOR_DST_OPT                                      0xFFFFFF8F
#define   S_028764_COLOR_COMB_FCN(x)                                  (((unsigned)(x) & 0x07) << 8)
#define   G_028764_COLOR_COMB_FCN(x)                                  (((x) >> 8) & 0x07)
#define   C_028764_COLOR_COMB_FCN                                     0xFFFFF8FF
#define   S_028764_ALPHA_SRC_OPT(x)                                   (((unsigned)(x) & 0x07) << 16)
#define   G_028764_ALPHA_SRC_OPT(x)                                   (((x) >> 16) & 0x07)
#define   C_028764_ALPHA_SRC_OPT                                      0xFFF8FFFF
#define   S_028764_ALPHA_DST_OPT(x)                                   (((unsigned)(x) & 0x07) << 20)
#define   G_028764_ALPHA_DST_OPT(x)                                   (((x) >> 20) & 0x07)
#define   C_028764_ALPHA_DST_OPT                                      0xFF8FFFFF
#define   S_028764_ALPHA_COMB_FCN(x)                                  (((unsigned)(x) & 0x07) << 24)
#define   G_028764_ALPHA_COMB_FCN(x)                                  (((x) >> 24) & 0x07)
#define   C_028764_ALPHA_COMB_FCN                                     0xF8FFFFFF
#define R_028768_SX_MRT2_BLEND_OPT                                      0x028768
#define   S_028768_COLOR_SRC_OPT(x)                                   (((unsigned)(x) & 0x07) << 0)
#define   G_028768_COLOR_SRC_OPT(x)                                   (((x) >> 0) & 0x07)
#define   C_028768_COLOR_SRC_OPT                                      0xFFFFFFF8
#define   S_028768_COLOR_DST_OPT(x)                                   (((unsigned)(x) & 0x07) << 4)
#define   G_028768_COLOR_DST_OPT(x)                                   (((x) >> 4) & 0x07)
#define   C_028768_COLOR_DST_OPT                                      0xFFFFFF8F
#define   S_028768_COLOR_COMB_FCN(x)                                  (((unsigned)(x) & 0x07) << 8)
#define   G_028768_COLOR_COMB_FCN(x)                                  (((x) >> 8) & 0x07)
#define   C_028768_COLOR_COMB_FCN                                     0xFFFFF8FF
#define   S_028768_ALPHA_SRC_OPT(x)                                   (((unsigned)(x) & 0x07) << 16)
#define   G_028768_ALPHA_SRC_OPT(x)                                   (((x) >> 16) & 0x07)
#define   C_028768_ALPHA_SRC_OPT                                      0xFFF8FFFF
#define   S_028768_ALPHA_DST_OPT(x)                                   (((unsigned)(x) & 0x07) << 20)
#define   G_028768_ALPHA_DST_OPT(x)                                   (((x) >> 20) & 0x07)
#define   C_028768_ALPHA_DST_OPT                                      0xFF8FFFFF
#define   S_028768_ALPHA_COMB_FCN(x)                                  (((unsigned)(x) & 0x07) << 24)
#define   G_028768_ALPHA_COMB_FCN(x)                                  (((x) >> 24) & 0x07)
#define   C_028768_ALPHA_COMB_FCN                                     0xF8FFFFFF
#define R_02876C_SX_MRT3_BLEND_OPT                                      0x02876C
#define   S_02876C_COLOR_SRC_OPT(x)                                   (((unsigned)(x) & 0x07) << 0)
#define   G_02876C_COLOR_SRC_OPT(x)                                   (((x) >> 0) & 0x07)
#define   C_02876C_COLOR_SRC_OPT                                      0xFFFFFFF8
#define   S_02876C_COLOR_DST_OPT(x)                                   (((unsigned)(x) & 0x07) << 4)
#define   G_02876C_COLOR_DST_OPT(x)                                   (((x) >> 4) & 0x07)
#define   C_02876C_COLOR_DST_OPT                                      0xFFFFFF8F
#define   S_02876C_COLOR_COMB_FCN(x)                                  (((unsigned)(x) & 0x07) << 8)
#define   G_02876C_COLOR_COMB_FCN(x)                                  (((x) >> 8) & 0x07)
#define   C_02876C_COLOR_COMB_FCN                                     0xFFFFF8FF
#define   S_02876C_ALPHA_SRC_OPT(x)                                   (((unsigned)(x) & 0x07) << 16)
#define   G_02876C_ALPHA_SRC_OPT(x)                                   (((x) >> 16) & 0x07)
#define   C_02876C_ALPHA_SRC_OPT                                      0xFFF8FFFF
#define   S_02876C_ALPHA_DST_OPT(x)                                   (((unsigned)(x) & 0x07) << 20)
#define   G_02876C_ALPHA_DST_OPT(x)                                   (((x) >> 20) & 0x07)
#define   C_02876C_ALPHA_DST_OPT                                      0xFF8FFFFF
#define   S_02876C_ALPHA_COMB_FCN(x)                                  (((unsigned)(x) & 0x07) << 24)
#define   G_02876C_ALPHA_COMB_FCN(x)                                  (((x) >> 24) & 0x07)
#define   C_02876C_ALPHA_COMB_FCN                                     0xF8FFFFFF
#define R_028770_SX_MRT4_BLEND_OPT                                      0x028770
#define   S_028770_COLOR_SRC_OPT(x)                                   (((unsigned)(x) & 0x07) << 0)
#define   G_028770_COLOR_SRC_OPT(x)                                   (((x) >> 0) & 0x07)
#define   C_028770_COLOR_SRC_OPT                                      0xFFFFFFF8
#define   S_028770_COLOR_DST_OPT(x)                                   (((unsigned)(x) & 0x07) << 4)
#define   G_028770_COLOR_DST_OPT(x)                                   (((x) >> 4) & 0x07)
#define   C_028770_COLOR_DST_OPT                                      0xFFFFFF8F
#define   S_028770_COLOR_COMB_FCN(x)                                  (((unsigned)(x) & 0x07) << 8)
#define   G_028770_COLOR_COMB_FCN(x)                                  (((x) >> 8) & 0x07)
#define   C_028770_COLOR_COMB_FCN                                     0xFFFFF8FF
#define   S_028770_ALPHA_SRC_OPT(x)                                   (((unsigned)(x) & 0x07) << 16)
#define   G_028770_ALPHA_SRC_OPT(x)                                   (((x) >> 16) & 0x07)
#define   C_028770_ALPHA_SRC_OPT                                      0xFFF8FFFF
#define   S_028770_ALPHA_DST_OPT(x)                                   (((unsigned)(x) & 0x07) << 20)
#define   G_028770_ALPHA_DST_OPT(x)                                   (((x) >> 20) & 0x07)
#define   C_028770_ALPHA_DST_OPT                                      0xFF8FFFFF
#define   S_028770_ALPHA_COMB_FCN(x)                                  (((unsigned)(x) & 0x07) << 24)
#define   G_028770_ALPHA_COMB_FCN(x)                                  (((x) >> 24) & 0x07)
#define   C_028770_ALPHA_COMB_FCN                                     0xF8FFFFFF
#define R_028774_SX_MRT5_BLEND_OPT                                      0x028774
#define   S_028774_COLOR_SRC_OPT(x)                                   (((unsigned)(x) & 0x07) << 0)
#define   G_028774_COLOR_SRC_OPT(x)                                   (((x) >> 0) & 0x07)
#define   C_028774_COLOR_SRC_OPT                                      0xFFFFFFF8
#define   S_028774_COLOR_DST_OPT(x)                                   (((unsigned)(x) & 0x07) << 4)
#define   G_028774_COLOR_DST_OPT(x)                                   (((x) >> 4) & 0x07)
#define   C_028774_COLOR_DST_OPT                                      0xFFFFFF8F
#define   S_028774_COLOR_COMB_FCN(x)                                  (((unsigned)(x) & 0x07) << 8)
#define   G_028774_COLOR_COMB_FCN(x)                                  (((x) >> 8) & 0x07)
#define   C_028774_COLOR_COMB_FCN                                     0xFFFFF8FF
#define   S_028774_ALPHA_SRC_OPT(x)                                   (((unsigned)(x) & 0x07) << 16)
#define   G_028774_ALPHA_SRC_OPT(x)                                   (((x) >> 16) & 0x07)
#define   C_028774_ALPHA_SRC_OPT                                      0xFFF8FFFF
#define   S_028774_ALPHA_DST_OPT(x)                                   (((unsigned)(x) & 0x07) << 20)
#define   G_028774_ALPHA_DST_OPT(x)                                   (((x) >> 20) & 0x07)
#define   C_028774_ALPHA_DST_OPT                                      0xFF8FFFFF
#define   S_028774_ALPHA_COMB_FCN(x)                                  (((unsigned)(x) & 0x07) << 24)
#define   G_028774_ALPHA_COMB_FCN(x)                                  (((x) >> 24) & 0x07)
#define   C_028774_ALPHA_COMB_FCN                                     0xF8FFFFFF
#define R_028778_SX_MRT6_BLEND_OPT                                      0x028778
#define   S_028778_COLOR_SRC_OPT(x)                                   (((unsigned)(x) & 0x07) << 0)
#define   G_028778_COLOR_SRC_OPT(x)                                   (((x) >> 0) & 0x07)
#define   C_028778_COLOR_SRC_OPT                                      0xFFFFFFF8
#define   S_028778_COLOR_DST_OPT(x)                                   (((unsigned)(x) & 0x07) << 4)
#define   G_028778_COLOR_DST_OPT(x)                                   (((x) >> 4) & 0x07)
#define   C_028778_COLOR_DST_OPT                                      0xFFFFFF8F
#define   S_028778_COLOR_COMB_FCN(x)                                  (((unsigned)(x) & 0x07) << 8)
#define   G_028778_COLOR_COMB_FCN(x)                                  (((x) >> 8) & 0x07)
#define   C_028778_COLOR_COMB_FCN                                     0xFFFFF8FF
#define   S_028778_ALPHA_SRC_OPT(x)                                   (((unsigned)(x) & 0x07) << 16)
#define   G_028778_ALPHA_SRC_OPT(x)                                   (((x) >> 16) & 0x07)
#define   C_028778_ALPHA_SRC_OPT                                      0xFFF8FFFF
#define   S_028778_ALPHA_DST_OPT(x)                                   (((unsigned)(x) & 0x07) << 20)
#define   G_028778_ALPHA_DST_OPT(x)                                   (((x) >> 20) & 0x07)
#define   C_028778_ALPHA_DST_OPT                                      0xFF8FFFFF
#define   S_028778_ALPHA_COMB_FCN(x)                                  (((unsigned)(x) & 0x07) << 24)
#define   G_028778_ALPHA_COMB_FCN(x)                                  (((x) >> 24) & 0x07)
#define   C_028778_ALPHA_COMB_FCN                                     0xF8FFFFFF
#define R_02877C_SX_MRT7_BLEND_OPT                                      0x02877C
#define   S_02877C_COLOR_SRC_OPT(x)                                   (((unsigned)(x) & 0x07) << 0)
#define   G_02877C_COLOR_SRC_OPT(x)                                   (((x) >> 0) & 0x07)
#define   C_02877C_COLOR_SRC_OPT                                      0xFFFFFFF8
#define   S_02877C_COLOR_DST_OPT(x)                                   (((unsigned)(x) & 0x07) << 4)
#define   G_02877C_COLOR_DST_OPT(x)                                   (((x) >> 4) & 0x07)
#define   C_02877C_COLOR_DST_OPT                                      0xFFFFFF8F
#define   S_02877C_COLOR_COMB_FCN(x)                                  (((unsigned)(x) & 0x07) << 8)
#define   G_02877C_COLOR_COMB_FCN(x)                                  (((x) >> 8) & 0x07)
#define   C_02877C_COLOR_COMB_FCN                                     0xFFFFF8FF
#define   S_02877C_ALPHA_SRC_OPT(x)                                   (((unsigned)(x) & 0x07) << 16)
#define   G_02877C_ALPHA_SRC_OPT(x)                                   (((x) >> 16) & 0x07)
#define   C_02877C_ALPHA_SRC_OPT                                      0xFFF8FFFF
#define   S_02877C_ALPHA_DST_OPT(x)                                   (((unsigned)(x) & 0x07) << 20)
#define   G_02877C_ALPHA_DST_OPT(x)                                   (((x) >> 20) & 0x07)
#define   C_02877C_ALPHA_DST_OPT                                      0xFF8FFFFF
#define   S_02877C_ALPHA_COMB_FCN(x)                                  (((unsigned)(x) & 0x07) << 24)
#define   G_02877C_ALPHA_COMB_FCN(x)                                  (((x) >> 24) & 0x07)
#define   C_02877C_ALPHA_COMB_FCN                                     0xF8FFFFFF
/*        */
#define R_028780_CB_BLEND0_CONTROL                                      0x028780
#define   S_028780_COLOR_SRCBLEND(x)                                  (((unsigned)(x) & 0x1F) << 0)
#define   G_028780_COLOR_SRCBLEND(x)                                  (((x) >> 0) & 0x1F)
#define   C_028780_COLOR_SRCBLEND                                     0xFFFFFFE0
#define     V_028780_BLEND_ZERO                                     0x00
#define     V_028780_BLEND_ONE                                      0x01
#define     V_028780_BLEND_SRC_COLOR                                0x02
#define     V_028780_BLEND_ONE_MINUS_SRC_COLOR                      0x03
#define     V_028780_BLEND_SRC_ALPHA                                0x04
#define     V_028780_BLEND_ONE_MINUS_SRC_ALPHA                      0x05
#define     V_028780_BLEND_DST_ALPHA                                0x06
#define     V_028780_BLEND_ONE_MINUS_DST_ALPHA                      0x07
#define     V_028780_BLEND_DST_COLOR                                0x08
#define     V_028780_BLEND_ONE_MINUS_DST_COLOR                      0x09
#define     V_028780_BLEND_SRC_ALPHA_SATURATE                       0x0A
#define     V_028780_BLEND_CONSTANT_COLOR                           0x0D
#define     V_028780_BLEND_ONE_MINUS_CONSTANT_COLOR                 0x0E
#define     V_028780_BLEND_SRC1_COLOR                               0x0F
#define     V_028780_BLEND_INV_SRC1_COLOR                           0x10
#define     V_028780_BLEND_SRC1_ALPHA                               0x11
#define     V_028780_BLEND_INV_SRC1_ALPHA                           0x12
#define     V_028780_BLEND_CONSTANT_ALPHA                           0x13
#define     V_028780_BLEND_ONE_MINUS_CONSTANT_ALPHA                 0x14
#define   S_028780_COLOR_COMB_FCN(x)                                  (((unsigned)(x) & 0x07) << 5)
#define   G_028780_COLOR_COMB_FCN(x)                                  (((x) >> 5) & 0x07)
#define   C_028780_COLOR_COMB_FCN                                     0xFFFFFF1F
#define     V_028780_COMB_DST_PLUS_SRC                              0x00
#define     V_028780_COMB_SRC_MINUS_DST                             0x01
#define     V_028780_COMB_MIN_DST_SRC                               0x02
#define     V_028780_COMB_MAX_DST_SRC                               0x03
#define     V_028780_COMB_DST_MINUS_SRC                             0x04
#define   S_028780_COLOR_DESTBLEND(x)                                 (((unsigned)(x) & 0x1F) << 8)
#define   G_028780_COLOR_DESTBLEND(x)                                 (((x) >> 8) & 0x1F)
#define   C_028780_COLOR_DESTBLEND                                    0xFFFFE0FF
#define     V_028780_BLEND_ZERO                                     0x00
#define     V_028780_BLEND_ONE                                      0x01
#define     V_028780_BLEND_SRC_COLOR                                0x02
#define     V_028780_BLEND_ONE_MINUS_SRC_COLOR                      0x03
#define     V_028780_BLEND_SRC_ALPHA                                0x04
#define     V_028780_BLEND_ONE_MINUS_SRC_ALPHA                      0x05
#define     V_028780_BLEND_DST_ALPHA                                0x06
#define     V_028780_BLEND_ONE_MINUS_DST_ALPHA                      0x07
#define     V_028780_BLEND_DST_COLOR                                0x08
#define     V_028780_BLEND_ONE_MINUS_DST_COLOR                      0x09
#define     V_028780_BLEND_SRC_ALPHA_SATURATE                       0x0A
#define     V_028780_BLEND_CONSTANT_COLOR                           0x0D
#define     V_028780_BLEND_ONE_MINUS_CONSTANT_COLOR                 0x0E
#define     V_028780_BLEND_SRC1_COLOR                               0x0F
#define     V_028780_BLEND_INV_SRC1_COLOR                           0x10
#define     V_028780_BLEND_SRC1_ALPHA                               0x11
#define     V_028780_BLEND_INV_SRC1_ALPHA                           0x12
#define     V_028780_BLEND_CONSTANT_ALPHA                           0x13
#define     V_028780_BLEND_ONE_MINUS_CONSTANT_ALPHA                 0x14
#define   S_028780_ALPHA_SRCBLEND(x)                                  (((unsigned)(x) & 0x1F) << 16)
#define   G_028780_ALPHA_SRCBLEND(x)                                  (((x) >> 16) & 0x1F)
#define   C_028780_ALPHA_SRCBLEND                                     0xFFE0FFFF
#define     V_028780_BLEND_ZERO                                     0x00
#define     V_028780_BLEND_ONE                                      0x01
#define     V_028780_BLEND_SRC_COLOR                                0x02
#define     V_028780_BLEND_ONE_MINUS_SRC_COLOR                      0x03
#define     V_028780_BLEND_SRC_ALPHA                                0x04
#define     V_028780_BLEND_ONE_MINUS_SRC_ALPHA                      0x05
#define     V_028780_BLEND_DST_ALPHA                                0x06
#define     V_028780_BLEND_ONE_MINUS_DST_ALPHA                      0x07
#define     V_028780_BLEND_DST_COLOR                                0x08
#define     V_028780_BLEND_ONE_MINUS_DST_COLOR                      0x09
#define     V_028780_BLEND_SRC_ALPHA_SATURATE                       0x0A
#define     V_028780_BLEND_CONSTANT_COLOR                           0x0D
#define     V_028780_BLEND_ONE_MINUS_CONSTANT_COLOR                 0x0E
#define     V_028780_BLEND_SRC1_COLOR                               0x0F
#define     V_028780_BLEND_INV_SRC1_COLOR                           0x10
#define     V_028780_BLEND_SRC1_ALPHA                               0x11
#define     V_028780_BLEND_INV_SRC1_ALPHA                           0x12
#define     V_028780_BLEND_CONSTANT_ALPHA                           0x13
#define     V_028780_BLEND_ONE_MINUS_CONSTANT_ALPHA                 0x14
#define   S_028780_ALPHA_COMB_FCN(x)                                  (((unsigned)(x) & 0x07) << 21)
#define   G_028780_ALPHA_COMB_FCN(x)                                  (((x) >> 21) & 0x07)
#define   C_028780_ALPHA_COMB_FCN                                     0xFF1FFFFF
#define     V_028780_COMB_DST_PLUS_SRC                              0x00
#define     V_028780_COMB_SRC_MINUS_DST                             0x01
#define     V_028780_COMB_MIN_DST_SRC                               0x02
#define     V_028780_COMB_MAX_DST_SRC                               0x03
#define     V_028780_COMB_DST_MINUS_SRC                             0x04
#define   S_028780_ALPHA_DESTBLEND(x)                                 (((unsigned)(x) & 0x1F) << 24)
#define   G_028780_ALPHA_DESTBLEND(x)                                 (((x) >> 24) & 0x1F)
#define   C_028780_ALPHA_DESTBLEND                                    0xE0FFFFFF
#define     V_028780_BLEND_ZERO                                     0x00
#define     V_028780_BLEND_ONE                                      0x01
#define     V_028780_BLEND_SRC_COLOR                                0x02
#define     V_028780_BLEND_ONE_MINUS_SRC_COLOR                      0x03
#define     V_028780_BLEND_SRC_ALPHA                                0x04
#define     V_028780_BLEND_ONE_MINUS_SRC_ALPHA                      0x05
#define     V_028780_BLEND_DST_ALPHA                                0x06
#define     V_028780_BLEND_ONE_MINUS_DST_ALPHA                      0x07
#define     V_028780_BLEND_DST_COLOR                                0x08
#define     V_028780_BLEND_ONE_MINUS_DST_COLOR                      0x09
#define     V_028780_BLEND_SRC_ALPHA_SATURATE                       0x0A
#define     V_028780_BLEND_CONSTANT_COLOR                           0x0D
#define     V_028780_BLEND_ONE_MINUS_CONSTANT_COLOR                 0x0E
#define     V_028780_BLEND_SRC1_COLOR                               0x0F
#define     V_028780_BLEND_INV_SRC1_COLOR                           0x10
#define     V_028780_BLEND_SRC1_ALPHA                               0x11
#define     V_028780_BLEND_INV_SRC1_ALPHA                           0x12
#define     V_028780_BLEND_CONSTANT_ALPHA                           0x13
#define     V_028780_BLEND_ONE_MINUS_CONSTANT_ALPHA                 0x14
#define   S_028780_SEPARATE_ALPHA_BLEND(x)                            (((unsigned)(x) & 0x1) << 29)
#define   G_028780_SEPARATE_ALPHA_BLEND(x)                            (((x) >> 29) & 0x1)
#define   C_028780_SEPARATE_ALPHA_BLEND                               0xDFFFFFFF
#define   S_028780_ENABLE(x)                                          (((unsigned)(x) & 0x1) << 30)
#define   G_028780_ENABLE(x)                                          (((x) >> 30) & 0x1)
#define   C_028780_ENABLE                                             0xBFFFFFFF
#define   S_028780_DISABLE_ROP3(x)                                    (((unsigned)(x) & 0x1) << 31)
#define   G_028780_DISABLE_ROP3(x)                                    (((x) >> 31) & 0x1)
#define   C_028780_DISABLE_ROP3                                       0x7FFFFFFF
#define R_028784_CB_BLEND1_CONTROL                                      0x028784
#define R_028788_CB_BLEND2_CONTROL                                      0x028788
#define R_02878C_CB_BLEND3_CONTROL                                      0x02878C
#define R_028790_CB_BLEND4_CONTROL                                      0x028790
#define R_028794_CB_BLEND5_CONTROL                                      0x028794
#define R_028798_CB_BLEND6_CONTROL                                      0x028798
#define R_02879C_CB_BLEND7_CONTROL                                      0x02879C
#define R_0287CC_CS_COPY_STATE                                          0x0287CC
#define   S_0287CC_SRC_STATE_ID(x)                                    (((unsigned)(x) & 0x07) << 0)
#define   G_0287CC_SRC_STATE_ID(x)                                    (((x) >> 0) & 0x07)
#define   C_0287CC_SRC_STATE_ID                                       0xFFFFFFF8
#define R_0287D4_PA_CL_POINT_X_RAD                                      0x0287D4
#define R_0287D8_PA_CL_POINT_Y_RAD                                      0x0287D8
#define R_0287DC_PA_CL_POINT_SIZE                                       0x0287DC
#define R_0287E0_PA_CL_POINT_CULL_RAD                                   0x0287E0
#define R_0287E4_VGT_DMA_BASE_HI                                        0x0287E4
#define   S_0287E4_BASE_ADDR(x)                                       (((unsigned)(x) & 0xFF) << 0)
#define   G_0287E4_BASE_ADDR(x)                                       (((x) >> 0) & 0xFF)
#define   C_0287E4_BASE_ADDR                                          0xFFFFFF00
#define R_0287E8_VGT_DMA_BASE                                           0x0287E8
#define R_0287F0_VGT_DRAW_INITIATOR                                     0x0287F0
#define   S_0287F0_SOURCE_SELECT(x)                                   (((unsigned)(x) & 0x03) << 0)
#define   G_0287F0_SOURCE_SELECT(x)                                   (((x) >> 0) & 0x03)
#define   C_0287F0_SOURCE_SELECT                                      0xFFFFFFFC
#define     V_0287F0_DI_SRC_SEL_DMA                                 0x00
#define     V_0287F0_DI_SRC_SEL_IMMEDIATE                           0x01 /* not on CIK */
#define     V_0287F0_DI_SRC_SEL_AUTO_INDEX                          0x02
#define     V_0287F0_DI_SRC_SEL_RESERVED                            0x03
#define   S_0287F0_MAJOR_MODE(x)                                      (((unsigned)(x) & 0x03) << 2)
#define   G_0287F0_MAJOR_MODE(x)                                      (((x) >> 2) & 0x03)
#define   C_0287F0_MAJOR_MODE                                         0xFFFFFFF3
#define     V_0287F0_DI_MAJOR_MODE_0                                0x00
#define     V_0287F0_DI_MAJOR_MODE_1                                0x01
#define   S_0287F0_NOT_EOP(x)                                         (((unsigned)(x) & 0x1) << 5)
#define   G_0287F0_NOT_EOP(x)                                         (((x) >> 5) & 0x1)
#define   C_0287F0_NOT_EOP                                            0xFFFFFFDF
#define   S_0287F0_USE_OPAQUE(x)                                      (((unsigned)(x) & 0x1) << 6)
#define   G_0287F0_USE_OPAQUE(x)                                      (((x) >> 6) & 0x1)
#define   C_0287F0_USE_OPAQUE                                         0xFFFFFFBF
#define R_0287F4_VGT_IMMED_DATA                                         0x0287F4 /* not on CIK */
#define R_0287F8_VGT_EVENT_ADDRESS_REG                                  0x0287F8
#define   S_0287F8_ADDRESS_LOW(x)                                     (((unsigned)(x) & 0xFFFFFFF) << 0)
#define   G_0287F8_ADDRESS_LOW(x)                                     (((x) >> 0) & 0xFFFFFFF)
#define   C_0287F8_ADDRESS_LOW                                        0xF0000000
#define R_028800_DB_DEPTH_CONTROL                                       0x028800
#define   S_028800_STENCIL_ENABLE(x)                                  (((unsigned)(x) & 0x1) << 0)
#define   G_028800_STENCIL_ENABLE(x)                                  (((x) >> 0) & 0x1)
#define   C_028800_STENCIL_ENABLE                                     0xFFFFFFFE
#define   S_028800_Z_ENABLE(x)                                        (((unsigned)(x) & 0x1) << 1)
#define   G_028800_Z_ENABLE(x)                                        (((x) >> 1) & 0x1)
#define   C_028800_Z_ENABLE                                           0xFFFFFFFD
#define   S_028800_Z_WRITE_ENABLE(x)                                  (((unsigned)(x) & 0x1) << 2)
#define   G_028800_Z_WRITE_ENABLE(x)                                  (((x) >> 2) & 0x1)
#define   C_028800_Z_WRITE_ENABLE                                     0xFFFFFFFB
#define   S_028800_DEPTH_BOUNDS_ENABLE(x)                             (((unsigned)(x) & 0x1) << 3)
#define   G_028800_DEPTH_BOUNDS_ENABLE(x)                             (((x) >> 3) & 0x1)
#define   C_028800_DEPTH_BOUNDS_ENABLE                                0xFFFFFFF7
#define   S_028800_ZFUNC(x)                                           (((unsigned)(x) & 0x07) << 4)
#define   G_028800_ZFUNC(x)                                           (((x) >> 4) & 0x07)
#define   C_028800_ZFUNC                                              0xFFFFFF8F
#define     V_028800_FRAG_NEVER                                     0x00
#define     V_028800_FRAG_LESS                                      0x01
#define     V_028800_FRAG_EQUAL                                     0x02
#define     V_028800_FRAG_LEQUAL                                    0x03
#define     V_028800_FRAG_GREATER                                   0x04
#define     V_028800_FRAG_NOTEQUAL                                  0x05
#define     V_028800_FRAG_GEQUAL                                    0x06
#define     V_028800_FRAG_ALWAYS                                    0x07
#define   S_028800_BACKFACE_ENABLE(x)                                 (((unsigned)(x) & 0x1) << 7)
#define   G_028800_BACKFACE_ENABLE(x)                                 (((x) >> 7) & 0x1)
#define   C_028800_BACKFACE_ENABLE                                    0xFFFFFF7F
#define   S_028800_STENCILFUNC(x)                                     (((unsigned)(x) & 0x07) << 8)
#define   G_028800_STENCILFUNC(x)                                     (((x) >> 8) & 0x07)
#define   C_028800_STENCILFUNC                                        0xFFFFF8FF
#define     V_028800_REF_NEVER                                      0x00
#define     V_028800_REF_LESS                                       0x01
#define     V_028800_REF_EQUAL                                      0x02
#define     V_028800_REF_LEQUAL                                     0x03
#define     V_028800_REF_GREATER                                    0x04
#define     V_028800_REF_NOTEQUAL                                   0x05
#define     V_028800_REF_GEQUAL                                     0x06
#define     V_028800_REF_ALWAYS                                     0x07
#define   S_028800_STENCILFUNC_BF(x)                                  (((unsigned)(x) & 0x07) << 20)
#define   G_028800_STENCILFUNC_BF(x)                                  (((x) >> 20) & 0x07)
#define   C_028800_STENCILFUNC_BF                                     0xFF8FFFFF
#define     V_028800_REF_NEVER                                      0x00
#define     V_028800_REF_LESS                                       0x01
#define     V_028800_REF_EQUAL                                      0x02
#define     V_028800_REF_LEQUAL                                     0x03
#define     V_028800_REF_GREATER                                    0x04
#define     V_028800_REF_NOTEQUAL                                   0x05
#define     V_028800_REF_GEQUAL                                     0x06
#define     V_028800_REF_ALWAYS                                     0x07
#define   S_028800_ENABLE_COLOR_WRITES_ON_DEPTH_FAIL(x)               (((unsigned)(x) & 0x1) << 30)
#define   G_028800_ENABLE_COLOR_WRITES_ON_DEPTH_FAIL(x)               (((x) >> 30) & 0x1)
#define   C_028800_ENABLE_COLOR_WRITES_ON_DEPTH_FAIL                  0xBFFFFFFF
#define   S_028800_DISABLE_COLOR_WRITES_ON_DEPTH_PASS(x)              (((unsigned)(x) & 0x1) << 31)
#define   G_028800_DISABLE_COLOR_WRITES_ON_DEPTH_PASS(x)              (((x) >> 31) & 0x1)
#define   C_028800_DISABLE_COLOR_WRITES_ON_DEPTH_PASS                 0x7FFFFFFF
#define R_028804_DB_EQAA                                                0x028804
#define   S_028804_MAX_ANCHOR_SAMPLES(x)                              (((unsigned)(x) & 0x7) << 0)
#define   G_028804_MAX_ANCHOR_SAMPLES(x)                              (((x) >> 0) & 0x07)
#define   C_028804_MAX_ANCHOR_SAMPLES                                 0xFFFFFFF8
#define   S_028804_PS_ITER_SAMPLES(x)                                 (((unsigned)(x) & 0x7) << 4)
#define   G_028804_PS_ITER_SAMPLES(x)                                 (((x) >> 4) & 0x07)
#define   C_028804_PS_ITER_SAMPLES                                    0xFFFFFF8F
#define   S_028804_MASK_EXPORT_NUM_SAMPLES(x)                         (((unsigned)(x) & 0x7) << 8)
#define   G_028804_MASK_EXPORT_NUM_SAMPLES(x)                         (((x) >> 8) & 0x07)
#define   C_028804_MASK_EXPORT_NUM_SAMPLES                            0xFFFFF8FF
#define   S_028804_ALPHA_TO_MASK_NUM_SAMPLES(x)                       (((unsigned)(x) & 0x7) << 12)
#define   G_028804_ALPHA_TO_MASK_NUM_SAMPLES(x)                       (((x) >> 12) & 0x07)
#define   C_028804_ALPHA_TO_MASK_NUM_SAMPLES                          0xFFFF8FFF
#define   S_028804_HIGH_QUALITY_INTERSECTIONS(x)                      (((unsigned)(x) & 0x1) << 16)
#define   G_028804_HIGH_QUALITY_INTERSECTIONS(x)                      (((x) >> 16) & 0x1)
#define   C_028804_HIGH_QUALITY_INTERSECTIONS                         0xFFFEFFFF
#define   S_028804_INCOHERENT_EQAA_READS(x)                           (((unsigned)(x) & 0x1) << 17)
#define   G_028804_INCOHERENT_EQAA_READS(x)                           (((x) >> 17) & 0x1)
#define   C_028804_INCOHERENT_EQAA_READS                              0xFFFDFFFF
#define   S_028804_INTERPOLATE_COMP_Z(x)                              (((unsigned)(x) & 0x1) << 18)
#define   G_028804_INTERPOLATE_COMP_Z(x)                              (((x) >> 18) & 0x1)
#define   C_028804_INTERPOLATE_COMP_Z                                 0xFFFBFFFF
#define   S_028804_INTERPOLATE_SRC_Z(x)                               (((unsigned)(x) & 0x1) << 19)
#define   G_028804_INTERPOLATE_SRC_Z(x)                               (((x) >> 19) & 0x1)
#define   C_028804_INTERPOLATE_SRC_Z                                  0xFFF7FFFF
#define   S_028804_STATIC_ANCHOR_ASSOCIATIONS(x)                      (((unsigned)(x) & 0x1) << 20)
#define   G_028804_STATIC_ANCHOR_ASSOCIATIONS(x)                      (((x) >> 20) & 0x1)
#define   C_028804_STATIC_ANCHOR_ASSOCIATIONS                         0xFFEFFFFF
#define   S_028804_ALPHA_TO_MASK_EQAA_DISABLE(x)                      (((unsigned)(x) & 0x1) << 21)
#define   G_028804_ALPHA_TO_MASK_EQAA_DISABLE(x)                      (((x) >> 21) & 0x1)
#define   C_028804_ALPHA_TO_MASK_EQAA_DISABLE                         0xFFDFFFFF
#define   S_028804_OVERRASTERIZATION_AMOUNT(x)                        (((unsigned)(x) & 0x07) << 24)
#define   G_028804_OVERRASTERIZATION_AMOUNT(x)                        (((x) >> 24) & 0x07)
#define   C_028804_OVERRASTERIZATION_AMOUNT                           0xF8FFFFFF
#define   S_028804_ENABLE_POSTZ_OVERRASTERIZATION(x)                  (((unsigned)(x) & 0x1) << 27)
#define   G_028804_ENABLE_POSTZ_OVERRASTERIZATION(x)                  (((x) >> 27) & 0x1)
#define   C_028804_ENABLE_POSTZ_OVERRASTERIZATION                     0xF7FFFFFF
#define R_028808_CB_COLOR_CONTROL                                       0x028808
#define   S_028808_DISABLE_DUAL_QUAD(x)                               (((unsigned)(x) & 0x1) << 0)
#define   G_028808_DISABLE_DUAL_QUAD(x)                               (((x) >> 0) & 0x1)
#define   C_028808_DISABLE_DUAL_QUAD                                  0xFFFFFFFE
#define   S_028808_DEGAMMA_ENABLE(x)                                  (((unsigned)(x) & 0x1) << 3)
#define   G_028808_DEGAMMA_ENABLE(x)                                  (((x) >> 3) & 0x1)
#define   C_028808_DEGAMMA_ENABLE                                     0xFFFFFFF7
#define   S_028808_MODE(x)                                            (((unsigned)(x) & 0x07) << 4)
#define   G_028808_MODE(x)                                            (((x) >> 4) & 0x07)
#define   C_028808_MODE                                               0xFFFFFF8F
#define     V_028808_CB_DISABLE                                     0x00
#define     V_028808_CB_NORMAL                                      0x01
#define     V_028808_CB_ELIMINATE_FAST_CLEAR                        0x02
#define     V_028808_CB_RESOLVE                                     0x03
#define     V_028808_CB_FMASK_DECOMPRESS                            0x05
#define     V_028808_CB_DCC_DECOMPRESS                              0x06
#define   S_028808_ROP3(x)                                            (((unsigned)(x) & 0xFF) << 16)
#define   G_028808_ROP3(x)                                            (((x) >> 16) & 0xFF)
#define   C_028808_ROP3                                               0xFF00FFFF
#define     V_028808_X_0X00                                         0x00
#define     V_028808_X_0X05                                         0x05
#define     V_028808_X_0X0A                                         0x0A
#define     V_028808_X_0X0F                                         0x0F
#define     V_028808_X_0X11                                         0x11
#define     V_028808_X_0X22                                         0x22
#define     V_028808_X_0X33                                         0x33
#define     V_028808_X_0X44                                         0x44
#define     V_028808_X_0X50                                         0x50
#define     V_028808_X_0X55                                         0x55
#define     V_028808_X_0X5A                                         0x5A
#define     V_028808_X_0X5F                                         0x5F
#define     V_028808_X_0X66                                         0x66
#define     V_028808_X_0X77                                         0x77
#define     V_028808_X_0X88                                         0x88
#define     V_028808_X_0X99                                         0x99
#define     V_028808_X_0XA0                                         0xA0
#define     V_028808_X_0XA5                                         0xA5
#define     V_028808_X_0XAA                                         0xAA
#define     V_028808_X_0XAF                                         0xAF
#define     V_028808_X_0XBB                                         0xBB
#define     V_028808_X_0XCC                                         0xCC
#define     V_028808_X_0XDD                                         0xDD
#define     V_028808_X_0XEE                                         0xEE
#define     V_028808_X_0XF0                                         0xF0
#define     V_028808_X_0XF5                                         0xF5
#define     V_028808_X_0XFA                                         0xFA
#define     V_028808_X_0XFF                                         0xFF
#define R_02880C_DB_SHADER_CONTROL                                      0x02880C
#define   S_02880C_Z_EXPORT_ENABLE(x)                                 (((unsigned)(x) & 0x1) << 0)
#define   G_02880C_Z_EXPORT_ENABLE(x)                                 (((x) >> 0) & 0x1)
#define   C_02880C_Z_EXPORT_ENABLE                                    0xFFFFFFFE
#define   S_02880C_STENCIL_TEST_VAL_EXPORT_ENABLE(x)                  (((unsigned)(x) & 0x1) << 1)
#define   G_02880C_STENCIL_TEST_VAL_EXPORT_ENABLE(x)                  (((x) >> 1) & 0x1)
#define   C_02880C_STENCIL_TEST_VAL_EXPORT_ENABLE                     0xFFFFFFFD
#define   S_02880C_STENCIL_OP_VAL_EXPORT_ENABLE(x)                    (((unsigned)(x) & 0x1) << 2)
#define   G_02880C_STENCIL_OP_VAL_EXPORT_ENABLE(x)                    (((x) >> 2) & 0x1)
#define   C_02880C_STENCIL_OP_VAL_EXPORT_ENABLE                       0xFFFFFFFB
#define   S_02880C_Z_ORDER(x)                                         (((unsigned)(x) & 0x03) << 4)
#define   G_02880C_Z_ORDER(x)                                         (((x) >> 4) & 0x03)
#define   C_02880C_Z_ORDER                                            0xFFFFFFCF
#define     V_02880C_LATE_Z                                         0x00
#define     V_02880C_EARLY_Z_THEN_LATE_Z                            0x01
#define     V_02880C_RE_Z                                           0x02
#define     V_02880C_EARLY_Z_THEN_RE_Z                              0x03
#define   S_02880C_KILL_ENABLE(x)                                     (((unsigned)(x) & 0x1) << 6)
#define   G_02880C_KILL_ENABLE(x)                                     (((x) >> 6) & 0x1)
#define   C_02880C_KILL_ENABLE                                        0xFFFFFFBF
#define   S_02880C_COVERAGE_TO_MASK_ENABLE(x)                         (((unsigned)(x) & 0x1) << 7)
#define   G_02880C_COVERAGE_TO_MASK_ENABLE(x)                         (((x) >> 7) & 0x1)
#define   C_02880C_COVERAGE_TO_MASK_ENABLE                            0xFFFFFF7F
#define   S_02880C_MASK_EXPORT_ENABLE(x)                              (((unsigned)(x) & 0x1) << 8)
#define   G_02880C_MASK_EXPORT_ENABLE(x)                              (((x) >> 8) & 0x1)
#define   C_02880C_MASK_EXPORT_ENABLE                                 0xFFFFFEFF
#define   S_02880C_EXEC_ON_HIER_FAIL(x)                               (((unsigned)(x) & 0x1) << 9)
#define   G_02880C_EXEC_ON_HIER_FAIL(x)                               (((x) >> 9) & 0x1)
#define   C_02880C_EXEC_ON_HIER_FAIL                                  0xFFFFFDFF
#define   S_02880C_EXEC_ON_NOOP(x)                                    (((unsigned)(x) & 0x1) << 10)
#define   G_02880C_EXEC_ON_NOOP(x)                                    (((x) >> 10) & 0x1)
#define   C_02880C_EXEC_ON_NOOP                                       0xFFFFFBFF
#define   S_02880C_ALPHA_TO_MASK_DISABLE(x)                           (((unsigned)(x) & 0x1) << 11)
#define   G_02880C_ALPHA_TO_MASK_DISABLE(x)                           (((x) >> 11) & 0x1)
#define   C_02880C_ALPHA_TO_MASK_DISABLE                              0xFFFFF7FF
#define   S_02880C_DEPTH_BEFORE_SHADER(x)                             (((unsigned)(x) & 0x1) << 12)
#define   G_02880C_DEPTH_BEFORE_SHADER(x)                             (((x) >> 12) & 0x1)
#define   C_02880C_DEPTH_BEFORE_SHADER                                0xFFFFEFFF
/* CIK */
#define   S_02880C_CONSERVATIVE_Z_EXPORT(x)                           (((unsigned)(x) & 0x03) << 13)
#define   G_02880C_CONSERVATIVE_Z_EXPORT(x)                           (((x) >> 13) & 0x03)
#define   C_02880C_CONSERVATIVE_Z_EXPORT                              0xFFFF9FFF
#define     V_02880C_EXPORT_ANY_Z                                   0
#define     V_02880C_EXPORT_LESS_THAN_Z                             1
#define     V_02880C_EXPORT_GREATER_THAN_Z                          2
#define     V_02880C_EXPORT_RESERVED                                3
/*     */
/* Stoney */
#define   S_02880C_DUAL_QUAD_DISABLE(x)                               (((unsigned)(x) & 0x1) << 15)
#define   G_02880C_DUAL_QUAD_DISABLE(x)                               (((x) >> 15) & 0x1)
#define   C_02880C_DUAL_QUAD_DISABLE                                  0xFFFF7FFF
/*        */
#define R_028810_PA_CL_CLIP_CNTL                                        0x028810
#define   S_028810_UCP_ENA_0(x)                                       (((unsigned)(x) & 0x1) << 0)
#define   G_028810_UCP_ENA_0(x)                                       (((x) >> 0) & 0x1)
#define   C_028810_UCP_ENA_0                                          0xFFFFFFFE
#define   S_028810_UCP_ENA_1(x)                                       (((unsigned)(x) & 0x1) << 1)
#define   G_028810_UCP_ENA_1(x)                                       (((x) >> 1) & 0x1)
#define   C_028810_UCP_ENA_1                                          0xFFFFFFFD
#define   S_028810_UCP_ENA_2(x)                                       (((unsigned)(x) & 0x1) << 2)
#define   G_028810_UCP_ENA_2(x)                                       (((x) >> 2) & 0x1)
#define   C_028810_UCP_ENA_2                                          0xFFFFFFFB
#define   S_028810_UCP_ENA_3(x)                                       (((unsigned)(x) & 0x1) << 3)
#define   G_028810_UCP_ENA_3(x)                                       (((x) >> 3) & 0x1)
#define   C_028810_UCP_ENA_3                                          0xFFFFFFF7
#define   S_028810_UCP_ENA_4(x)                                       (((unsigned)(x) & 0x1) << 4)
#define   G_028810_UCP_ENA_4(x)                                       (((x) >> 4) & 0x1)
#define   C_028810_UCP_ENA_4                                          0xFFFFFFEF
#define   S_028810_UCP_ENA_5(x)                                       (((unsigned)(x) & 0x1) << 5)
#define   G_028810_UCP_ENA_5(x)                                       (((x) >> 5) & 0x1)
#define   C_028810_UCP_ENA_5                                          0xFFFFFFDF
#define   S_028810_PS_UCP_Y_SCALE_NEG(x)                              (((unsigned)(x) & 0x1) << 13)
#define   G_028810_PS_UCP_Y_SCALE_NEG(x)                              (((x) >> 13) & 0x1)
#define   C_028810_PS_UCP_Y_SCALE_NEG                                 0xFFFFDFFF
#define   S_028810_PS_UCP_MODE(x)                                     (((unsigned)(x) & 0x03) << 14)
#define   G_028810_PS_UCP_MODE(x)                                     (((x) >> 14) & 0x03)
#define   C_028810_PS_UCP_MODE                                        0xFFFF3FFF
#define   S_028810_CLIP_DISABLE(x)                                    (((unsigned)(x) & 0x1) << 16)
#define   G_028810_CLIP_DISABLE(x)                                    (((x) >> 16) & 0x1)
#define   C_028810_CLIP_DISABLE                                       0xFFFEFFFF
#define   S_028810_UCP_CULL_ONLY_ENA(x)                               (((unsigned)(x) & 0x1) << 17)
#define   G_028810_UCP_CULL_ONLY_ENA(x)                               (((x) >> 17) & 0x1)
#define   C_028810_UCP_CULL_ONLY_ENA                                  0xFFFDFFFF
#define   S_028810_BOUNDARY_EDGE_FLAG_ENA(x)                          (((unsigned)(x) & 0x1) << 18)
#define   G_028810_BOUNDARY_EDGE_FLAG_ENA(x)                          (((x) >> 18) & 0x1)
#define   C_028810_BOUNDARY_EDGE_FLAG_ENA                             0xFFFBFFFF
#define   S_028810_DX_CLIP_SPACE_DEF(x)                               (((unsigned)(x) & 0x1) << 19)
#define   G_028810_DX_CLIP_SPACE_DEF(x)                               (((x) >> 19) & 0x1)
#define   C_028810_DX_CLIP_SPACE_DEF                                  0xFFF7FFFF
#define   S_028810_DIS_CLIP_ERR_DETECT(x)                             (((unsigned)(x) & 0x1) << 20)
#define   G_028810_DIS_CLIP_ERR_DETECT(x)                             (((x) >> 20) & 0x1)
#define   C_028810_DIS_CLIP_ERR_DETECT                                0xFFEFFFFF
#define   S_028810_VTX_KILL_OR(x)                                     (((unsigned)(x) & 0x1) << 21)
#define   G_028810_VTX_KILL_OR(x)                                     (((x) >> 21) & 0x1)
#define   C_028810_VTX_KILL_OR                                        0xFFDFFFFF
#define   S_028810_DX_RASTERIZATION_KILL(x)                           (((unsigned)(x) & 0x1) << 22)
#define   G_028810_DX_RASTERIZATION_KILL(x)                           (((x) >> 22) & 0x1)
#define   C_028810_DX_RASTERIZATION_KILL                              0xFFBFFFFF
#define   S_028810_DX_LINEAR_ATTR_CLIP_ENA(x)                         (((unsigned)(x) & 0x1) << 24)
#define   G_028810_DX_LINEAR_ATTR_CLIP_ENA(x)                         (((x) >> 24) & 0x1)
#define   C_028810_DX_LINEAR_ATTR_CLIP_ENA                            0xFEFFFFFF
#define   S_028810_VTE_VPORT_PROVOKE_DISABLE(x)                       (((unsigned)(x) & 0x1) << 25)
#define   G_028810_VTE_VPORT_PROVOKE_DISABLE(x)                       (((x) >> 25) & 0x1)
#define   C_028810_VTE_VPORT_PROVOKE_DISABLE                          0xFDFFFFFF
#define   S_028810_ZCLIP_NEAR_DISABLE(x)                              (((unsigned)(x) & 0x1) << 26)
#define   G_028810_ZCLIP_NEAR_DISABLE(x)                              (((x) >> 26) & 0x1)
#define   C_028810_ZCLIP_NEAR_DISABLE                                 0xFBFFFFFF
#define   S_028810_ZCLIP_FAR_DISABLE(x)                               (((unsigned)(x) & 0x1) << 27)
#define   G_028810_ZCLIP_FAR_DISABLE(x)                               (((x) >> 27) & 0x1)
#define   C_028810_ZCLIP_FAR_DISABLE                                  0xF7FFFFFF
#define R_028814_PA_SU_SC_MODE_CNTL                                     0x028814
#define   S_028814_CULL_FRONT(x)                                      (((unsigned)(x) & 0x1) << 0)
#define   G_028814_CULL_FRONT(x)                                      (((x) >> 0) & 0x1)
#define   C_028814_CULL_FRONT                                         0xFFFFFFFE
#define   S_028814_CULL_BACK(x)                                       (((unsigned)(x) & 0x1) << 1)
#define   G_028814_CULL_BACK(x)                                       (((x) >> 1) & 0x1)
#define   C_028814_CULL_BACK                                          0xFFFFFFFD
#define   S_028814_FACE(x)                                            (((unsigned)(x) & 0x1) << 2)
#define   G_028814_FACE(x)                                            (((x) >> 2) & 0x1)
#define   C_028814_FACE                                               0xFFFFFFFB
#define   S_028814_POLY_MODE(x)                                       (((unsigned)(x) & 0x03) << 3)
#define   G_028814_POLY_MODE(x)                                       (((x) >> 3) & 0x03)
#define   C_028814_POLY_MODE                                          0xFFFFFFE7
#define     V_028814_X_DISABLE_POLY_MODE                            0x00
#define     V_028814_X_DUAL_MODE                                    0x01
#define   S_028814_POLYMODE_FRONT_PTYPE(x)                            (((unsigned)(x) & 0x07) << 5)
#define   G_028814_POLYMODE_FRONT_PTYPE(x)                            (((x) >> 5) & 0x07)
#define   C_028814_POLYMODE_FRONT_PTYPE                               0xFFFFFF1F
#define     V_028814_X_DRAW_POINTS                                  0x00
#define     V_028814_X_DRAW_LINES                                   0x01
#define     V_028814_X_DRAW_TRIANGLES                               0x02
#define   S_028814_POLYMODE_BACK_PTYPE(x)                             (((unsigned)(x) & 0x07) << 8)
#define   G_028814_POLYMODE_BACK_PTYPE(x)                             (((x) >> 8) & 0x07)
#define   C_028814_POLYMODE_BACK_PTYPE                                0xFFFFF8FF
#define     V_028814_X_DRAW_POINTS                                  0x00
#define     V_028814_X_DRAW_LINES                                   0x01
#define     V_028814_X_DRAW_TRIANGLES                               0x02
#define   S_028814_POLY_OFFSET_FRONT_ENABLE(x)                        (((unsigned)(x) & 0x1) << 11)
#define   G_028814_POLY_OFFSET_FRONT_ENABLE(x)                        (((x) >> 11) & 0x1)
#define   C_028814_POLY_OFFSET_FRONT_ENABLE                           0xFFFFF7FF
#define   S_028814_POLY_OFFSET_BACK_ENABLE(x)                         (((unsigned)(x) & 0x1) << 12)
#define   G_028814_POLY_OFFSET_BACK_ENABLE(x)                         (((x) >> 12) & 0x1)
#define   C_028814_POLY_OFFSET_BACK_ENABLE                            0xFFFFEFFF
#define   S_028814_POLY_OFFSET_PARA_ENABLE(x)                         (((unsigned)(x) & 0x1) << 13)
#define   G_028814_POLY_OFFSET_PARA_ENABLE(x)                         (((x) >> 13) & 0x1)
#define   C_028814_POLY_OFFSET_PARA_ENABLE                            0xFFFFDFFF
#define   S_028814_VTX_WINDOW_OFFSET_ENABLE(x)                        (((unsigned)(x) & 0x1) << 16)
#define   G_028814_VTX_WINDOW_OFFSET_ENABLE(x)                        (((x) >> 16) & 0x1)
#define   C_028814_VTX_WINDOW_OFFSET_ENABLE                           0xFFFEFFFF
#define   S_028814_PROVOKING_VTX_LAST(x)                              (((unsigned)(x) & 0x1) << 19)
#define   G_028814_PROVOKING_VTX_LAST(x)                              (((x) >> 19) & 0x1)
#define   C_028814_PROVOKING_VTX_LAST                                 0xFFF7FFFF
#define   S_028814_PERSP_CORR_DIS(x)                                  (((unsigned)(x) & 0x1) << 20)
#define   G_028814_PERSP_CORR_DIS(x)                                  (((x) >> 20) & 0x1)
#define   C_028814_PERSP_CORR_DIS                                     0xFFEFFFFF
#define   S_028814_MULTI_PRIM_IB_ENA(x)                               (((unsigned)(x) & 0x1) << 21)
#define   G_028814_MULTI_PRIM_IB_ENA(x)                               (((x) >> 21) & 0x1)
#define   C_028814_MULTI_PRIM_IB_ENA                                  0xFFDFFFFF
#define R_028818_PA_CL_VTE_CNTL                                         0x028818
#define   S_028818_VPORT_X_SCALE_ENA(x)                               (((unsigned)(x) & 0x1) << 0)
#define   G_028818_VPORT_X_SCALE_ENA(x)                               (((x) >> 0) & 0x1)
#define   C_028818_VPORT_X_SCALE_ENA                                  0xFFFFFFFE
#define   S_028818_VPORT_X_OFFSET_ENA(x)                              (((unsigned)(x) & 0x1) << 1)
#define   G_028818_VPORT_X_OFFSET_ENA(x)                              (((x) >> 1) & 0x1)
#define   C_028818_VPORT_X_OFFSET_ENA                                 0xFFFFFFFD
#define   S_028818_VPORT_Y_SCALE_ENA(x)                               (((unsigned)(x) & 0x1) << 2)
#define   G_028818_VPORT_Y_SCALE_ENA(x)                               (((x) >> 2) & 0x1)
#define   C_028818_VPORT_Y_SCALE_ENA                                  0xFFFFFFFB
#define   S_028818_VPORT_Y_OFFSET_ENA(x)                              (((unsigned)(x) & 0x1) << 3)
#define   G_028818_VPORT_Y_OFFSET_ENA(x)                              (((x) >> 3) & 0x1)
#define   C_028818_VPORT_Y_OFFSET_ENA                                 0xFFFFFFF7
#define   S_028818_VPORT_Z_SCALE_ENA(x)                               (((unsigned)(x) & 0x1) << 4)
#define   G_028818_VPORT_Z_SCALE_ENA(x)                               (((x) >> 4) & 0x1)
#define   C_028818_VPORT_Z_SCALE_ENA                                  0xFFFFFFEF
#define   S_028818_VPORT_Z_OFFSET_ENA(x)                              (((unsigned)(x) & 0x1) << 5)
#define   G_028818_VPORT_Z_OFFSET_ENA(x)                              (((x) >> 5) & 0x1)
#define   C_028818_VPORT_Z_OFFSET_ENA                                 0xFFFFFFDF
#define   S_028818_VTX_XY_FMT(x)                                      (((unsigned)(x) & 0x1) << 8)
#define   G_028818_VTX_XY_FMT(x)                                      (((x) >> 8) & 0x1)
#define   C_028818_VTX_XY_FMT                                         0xFFFFFEFF
#define   S_028818_VTX_Z_FMT(x)                                       (((unsigned)(x) & 0x1) << 9)
#define   G_028818_VTX_Z_FMT(x)                                       (((x) >> 9) & 0x1)
#define   C_028818_VTX_Z_FMT                                          0xFFFFFDFF
#define   S_028818_VTX_W0_FMT(x)                                      (((unsigned)(x) & 0x1) << 10)
#define   G_028818_VTX_W0_FMT(x)                                      (((x) >> 10) & 0x1)
#define   C_028818_VTX_W0_FMT                                         0xFFFFFBFF
#define R_02881C_PA_CL_VS_OUT_CNTL                                      0x02881C
#define   S_02881C_CLIP_DIST_ENA_0(x)                                 (((unsigned)(x) & 0x1) << 0)
#define   G_02881C_CLIP_DIST_ENA_0(x)                                 (((x) >> 0) & 0x1)
#define   C_02881C_CLIP_DIST_ENA_0                                    0xFFFFFFFE
#define   S_02881C_CLIP_DIST_ENA_1(x)                                 (((unsigned)(x) & 0x1) << 1)
#define   G_02881C_CLIP_DIST_ENA_1(x)                                 (((x) >> 1) & 0x1)
#define   C_02881C_CLIP_DIST_ENA_1                                    0xFFFFFFFD
#define   S_02881C_CLIP_DIST_ENA_2(x)                                 (((unsigned)(x) & 0x1) << 2)
#define   G_02881C_CLIP_DIST_ENA_2(x)                                 (((x) >> 2) & 0x1)
#define   C_02881C_CLIP_DIST_ENA_2                                    0xFFFFFFFB
#define   S_02881C_CLIP_DIST_ENA_3(x)                                 (((unsigned)(x) & 0x1) << 3)
#define   G_02881C_CLIP_DIST_ENA_3(x)                                 (((x) >> 3) & 0x1)
#define   C_02881C_CLIP_DIST_ENA_3                                    0xFFFFFFF7
#define   S_02881C_CLIP_DIST_ENA_4(x)                                 (((unsigned)(x) & 0x1) << 4)
#define   G_02881C_CLIP_DIST_ENA_4(x)                                 (((x) >> 4) & 0x1)
#define   C_02881C_CLIP_DIST_ENA_4                                    0xFFFFFFEF
#define   S_02881C_CLIP_DIST_ENA_5(x)                                 (((unsigned)(x) & 0x1) << 5)
#define   G_02881C_CLIP_DIST_ENA_5(x)                                 (((x) >> 5) & 0x1)
#define   C_02881C_CLIP_DIST_ENA_5                                    0xFFFFFFDF
#define   S_02881C_CLIP_DIST_ENA_6(x)                                 (((unsigned)(x) & 0x1) << 6)
#define   G_02881C_CLIP_DIST_ENA_6(x)                                 (((x) >> 6) & 0x1)
#define   C_02881C_CLIP_DIST_ENA_6                                    0xFFFFFFBF
#define   S_02881C_CLIP_DIST_ENA_7(x)                                 (((unsigned)(x) & 0x1) << 7)
#define   G_02881C_CLIP_DIST_ENA_7(x)                                 (((x) >> 7) & 0x1)
#define   C_02881C_CLIP_DIST_ENA_7                                    0xFFFFFF7F
#define   S_02881C_CULL_DIST_ENA_0(x)                                 (((unsigned)(x) & 0x1) << 8)
#define   G_02881C_CULL_DIST_ENA_0(x)                                 (((x) >> 8) & 0x1)
#define   C_02881C_CULL_DIST_ENA_0                                    0xFFFFFEFF
#define   S_02881C_CULL_DIST_ENA_1(x)                                 (((unsigned)(x) & 0x1) << 9)
#define   G_02881C_CULL_DIST_ENA_1(x)                                 (((x) >> 9) & 0x1)
#define   C_02881C_CULL_DIST_ENA_1                                    0xFFFFFDFF
#define   S_02881C_CULL_DIST_ENA_2(x)                                 (((unsigned)(x) & 0x1) << 10)
#define   G_02881C_CULL_DIST_ENA_2(x)                                 (((x) >> 10) & 0x1)
#define   C_02881C_CULL_DIST_ENA_2                                    0xFFFFFBFF
#define   S_02881C_CULL_DIST_ENA_3(x)                                 (((unsigned)(x) & 0x1) << 11)
#define   G_02881C_CULL_DIST_ENA_3(x)                                 (((x) >> 11) & 0x1)
#define   C_02881C_CULL_DIST_ENA_3                                    0xFFFFF7FF
#define   S_02881C_CULL_DIST_ENA_4(x)                                 (((unsigned)(x) & 0x1) << 12)
#define   G_02881C_CULL_DIST_ENA_4(x)                                 (((x) >> 12) & 0x1)
#define   C_02881C_CULL_DIST_ENA_4                                    0xFFFFEFFF
#define   S_02881C_CULL_DIST_ENA_5(x)                                 (((unsigned)(x) & 0x1) << 13)
#define   G_02881C_CULL_DIST_ENA_5(x)                                 (((x) >> 13) & 0x1)
#define   C_02881C_CULL_DIST_ENA_5                                    0xFFFFDFFF
#define   S_02881C_CULL_DIST_ENA_6(x)                                 (((unsigned)(x) & 0x1) << 14)
#define   G_02881C_CULL_DIST_ENA_6(x)                                 (((x) >> 14) & 0x1)
#define   C_02881C_CULL_DIST_ENA_6                                    0xFFFFBFFF
#define   S_02881C_CULL_DIST_ENA_7(x)                                 (((unsigned)(x) & 0x1) << 15)
#define   G_02881C_CULL_DIST_ENA_7(x)                                 (((x) >> 15) & 0x1)
#define   C_02881C_CULL_DIST_ENA_7                                    0xFFFF7FFF
#define   S_02881C_USE_VTX_POINT_SIZE(x)                              (((unsigned)(x) & 0x1) << 16)
#define   G_02881C_USE_VTX_POINT_SIZE(x)                              (((x) >> 16) & 0x1)
#define   C_02881C_USE_VTX_POINT_SIZE                                 0xFFFEFFFF
#define   S_02881C_USE_VTX_EDGE_FLAG(x)                               (((unsigned)(x) & 0x1) << 17)
#define   G_02881C_USE_VTX_EDGE_FLAG(x)                               (((x) >> 17) & 0x1)
#define   C_02881C_USE_VTX_EDGE_FLAG                                  0xFFFDFFFF
#define   S_02881C_USE_VTX_RENDER_TARGET_INDX(x)                      (((unsigned)(x) & 0x1) << 18)
#define   G_02881C_USE_VTX_RENDER_TARGET_INDX(x)                      (((x) >> 18) & 0x1)
#define   C_02881C_USE_VTX_RENDER_TARGET_INDX                         0xFFFBFFFF
#define   S_02881C_USE_VTX_VIEWPORT_INDX(x)                           (((unsigned)(x) & 0x1) << 19)
#define   G_02881C_USE_VTX_VIEWPORT_INDX(x)                           (((x) >> 19) & 0x1)
#define   C_02881C_USE_VTX_VIEWPORT_INDX                              0xFFF7FFFF
#define   S_02881C_USE_VTX_KILL_FLAG(x)                               (((unsigned)(x) & 0x1) << 20)
#define   G_02881C_USE_VTX_KILL_FLAG(x)                               (((x) >> 20) & 0x1)
#define   C_02881C_USE_VTX_KILL_FLAG                                  0xFFEFFFFF
#define   S_02881C_VS_OUT_MISC_VEC_ENA(x)                             (((unsigned)(x) & 0x1) << 21)
#define   G_02881C_VS_OUT_MISC_VEC_ENA(x)                             (((x) >> 21) & 0x1)
#define   C_02881C_VS_OUT_MISC_VEC_ENA                                0xFFDFFFFF
#define   S_02881C_VS_OUT_CCDIST0_VEC_ENA(x)                          (((unsigned)(x) & 0x1) << 22)
#define   G_02881C_VS_OUT_CCDIST0_VEC_ENA(x)                          (((x) >> 22) & 0x1)
#define   C_02881C_VS_OUT_CCDIST0_VEC_ENA                             0xFFBFFFFF
#define   S_02881C_VS_OUT_CCDIST1_VEC_ENA(x)                          (((unsigned)(x) & 0x1) << 23)
#define   G_02881C_VS_OUT_CCDIST1_VEC_ENA(x)                          (((x) >> 23) & 0x1)
#define   C_02881C_VS_OUT_CCDIST1_VEC_ENA                             0xFF7FFFFF
#define   S_02881C_VS_OUT_MISC_SIDE_BUS_ENA(x)                        (((unsigned)(x) & 0x1) << 24)
#define   G_02881C_VS_OUT_MISC_SIDE_BUS_ENA(x)                        (((x) >> 24) & 0x1)
#define   C_02881C_VS_OUT_MISC_SIDE_BUS_ENA                           0xFEFFFFFF
#define   S_02881C_USE_VTX_GS_CUT_FLAG(x)                             (((unsigned)(x) & 0x1) << 25)
#define   G_02881C_USE_VTX_GS_CUT_FLAG(x)                             (((x) >> 25) & 0x1)
#define   C_02881C_USE_VTX_GS_CUT_FLAG                                0xFDFFFFFF
/* VI */
#define   S_02881C_USE_VTX_LINE_WIDTH(x)                              (((unsigned)(x) & 0x1) << 26)
#define   G_02881C_USE_VTX_LINE_WIDTH(x)                              (((x) >> 26) & 0x1)
#define   C_02881C_USE_VTX_LINE_WIDTH                                 0xFBFFFFFF
/*    */
#define R_028820_PA_CL_NANINF_CNTL                                      0x028820
#define   S_028820_VTE_XY_INF_DISCARD(x)                              (((unsigned)(x) & 0x1) << 0)
#define   G_028820_VTE_XY_INF_DISCARD(x)                              (((x) >> 0) & 0x1)
#define   C_028820_VTE_XY_INF_DISCARD                                 0xFFFFFFFE
#define   S_028820_VTE_Z_INF_DISCARD(x)                               (((unsigned)(x) & 0x1) << 1)
#define   G_028820_VTE_Z_INF_DISCARD(x)                               (((x) >> 1) & 0x1)
#define   C_028820_VTE_Z_INF_DISCARD                                  0xFFFFFFFD
#define   S_028820_VTE_W_INF_DISCARD(x)                               (((unsigned)(x) & 0x1) << 2)
#define   G_028820_VTE_W_INF_DISCARD(x)                               (((x) >> 2) & 0x1)
#define   C_028820_VTE_W_INF_DISCARD                                  0xFFFFFFFB
#define   S_028820_VTE_0XNANINF_IS_0(x)                               (((unsigned)(x) & 0x1) << 3)
#define   G_028820_VTE_0XNANINF_IS_0(x)                               (((x) >> 3) & 0x1)
#define   C_028820_VTE_0XNANINF_IS_0                                  0xFFFFFFF7
#define   S_028820_VTE_XY_NAN_RETAIN(x)                               (((unsigned)(x) & 0x1) << 4)
#define   G_028820_VTE_XY_NAN_RETAIN(x)                               (((x) >> 4) & 0x1)
#define   C_028820_VTE_XY_NAN_RETAIN                                  0xFFFFFFEF
#define   S_028820_VTE_Z_NAN_RETAIN(x)                                (((unsigned)(x) & 0x1) << 5)
#define   G_028820_VTE_Z_NAN_RETAIN(x)                                (((x) >> 5) & 0x1)
#define   C_028820_VTE_Z_NAN_RETAIN                                   0xFFFFFFDF
#define   S_028820_VTE_W_NAN_RETAIN(x)                                (((unsigned)(x) & 0x1) << 6)
#define   G_028820_VTE_W_NAN_RETAIN(x)                                (((x) >> 6) & 0x1)
#define   C_028820_VTE_W_NAN_RETAIN                                   0xFFFFFFBF
#define   S_028820_VTE_W_RECIP_NAN_IS_0(x)                            (((unsigned)(x) & 0x1) << 7)
#define   G_028820_VTE_W_RECIP_NAN_IS_0(x)                            (((x) >> 7) & 0x1)
#define   C_028820_VTE_W_RECIP_NAN_IS_0                               0xFFFFFF7F
#define   S_028820_VS_XY_NAN_TO_INF(x)                                (((unsigned)(x) & 0x1) << 8)
#define   G_028820_VS_XY_NAN_TO_INF(x)                                (((x) >> 8) & 0x1)
#define   C_028820_VS_XY_NAN_TO_INF                                   0xFFFFFEFF
#define   S_028820_VS_XY_INF_RETAIN(x)                                (((unsigned)(x) & 0x1) << 9)
#define   G_028820_VS_XY_INF_RETAIN(x)                                (((x) >> 9) & 0x1)
#define   C_028820_VS_XY_INF_RETAIN                                   0xFFFFFDFF
#define   S_028820_VS_Z_NAN_TO_INF(x)                                 (((unsigned)(x) & 0x1) << 10)
#define   G_028820_VS_Z_NAN_TO_INF(x)                                 (((x) >> 10) & 0x1)
#define   C_028820_VS_Z_NAN_TO_INF                                    0xFFFFFBFF
#define   S_028820_VS_Z_INF_RETAIN(x)                                 (((unsigned)(x) & 0x1) << 11)
#define   G_028820_VS_Z_INF_RETAIN(x)                                 (((x) >> 11) & 0x1)
#define   C_028820_VS_Z_INF_RETAIN                                    0xFFFFF7FF
#define   S_028820_VS_W_NAN_TO_INF(x)                                 (((unsigned)(x) & 0x1) << 12)
#define   G_028820_VS_W_NAN_TO_INF(x)                                 (((x) >> 12) & 0x1)
#define   C_028820_VS_W_NAN_TO_INF                                    0xFFFFEFFF
#define   S_028820_VS_W_INF_RETAIN(x)                                 (((unsigned)(x) & 0x1) << 13)
#define   G_028820_VS_W_INF_RETAIN(x)                                 (((x) >> 13) & 0x1)
#define   C_028820_VS_W_INF_RETAIN                                    0xFFFFDFFF
#define   S_028820_VS_CLIP_DIST_INF_DISCARD(x)                        (((unsigned)(x) & 0x1) << 14)
#define   G_028820_VS_CLIP_DIST_INF_DISCARD(x)                        (((x) >> 14) & 0x1)
#define   C_028820_VS_CLIP_DIST_INF_DISCARD                           0xFFFFBFFF
#define   S_028820_VTE_NO_OUTPUT_NEG_0(x)                             (((unsigned)(x) & 0x1) << 20)
#define   G_028820_VTE_NO_OUTPUT_NEG_0(x)                             (((x) >> 20) & 0x1)
#define   C_028820_VTE_NO_OUTPUT_NEG_0                                0xFFEFFFFF
#define R_028824_PA_SU_LINE_STIPPLE_CNTL                                0x028824
#define   S_028824_LINE_STIPPLE_RESET(x)                              (((unsigned)(x) & 0x03) << 0)
#define   G_028824_LINE_STIPPLE_RESET(x)                              (((x) >> 0) & 0x03)
#define   C_028824_LINE_STIPPLE_RESET                                 0xFFFFFFFC
#define   S_028824_EXPAND_FULL_LENGTH(x)                              (((unsigned)(x) & 0x1) << 2)
#define   G_028824_EXPAND_FULL_LENGTH(x)                              (((x) >> 2) & 0x1)
#define   C_028824_EXPAND_FULL_LENGTH                                 0xFFFFFFFB
#define   S_028824_FRACTIONAL_ACCUM(x)                                (((unsigned)(x) & 0x1) << 3)
#define   G_028824_FRACTIONAL_ACCUM(x)                                (((x) >> 3) & 0x1)
#define   C_028824_FRACTIONAL_ACCUM                                   0xFFFFFFF7
#define   S_028824_DIAMOND_ADJUST(x)                                  (((unsigned)(x) & 0x1) << 4)
#define   G_028824_DIAMOND_ADJUST(x)                                  (((x) >> 4) & 0x1)
#define   C_028824_DIAMOND_ADJUST                                     0xFFFFFFEF
#define R_028828_PA_SU_LINE_STIPPLE_SCALE                               0x028828
#define R_02882C_PA_SU_PRIM_FILTER_CNTL                                 0x02882C
#define   S_02882C_TRIANGLE_FILTER_DISABLE(x)                         (((unsigned)(x) & 0x1) << 0)
#define   G_02882C_TRIANGLE_FILTER_DISABLE(x)                         (((x) >> 0) & 0x1)
#define   C_02882C_TRIANGLE_FILTER_DISABLE                            0xFFFFFFFE
#define   S_02882C_LINE_FILTER_DISABLE(x)                             (((unsigned)(x) & 0x1) << 1)
#define   G_02882C_LINE_FILTER_DISABLE(x)                             (((x) >> 1) & 0x1)
#define   C_02882C_LINE_FILTER_DISABLE                                0xFFFFFFFD
#define   S_02882C_POINT_FILTER_DISABLE(x)                            (((unsigned)(x) & 0x1) << 2)
#define   G_02882C_POINT_FILTER_DISABLE(x)                            (((x) >> 2) & 0x1)
#define   C_02882C_POINT_FILTER_DISABLE                               0xFFFFFFFB
#define   S_02882C_RECTANGLE_FILTER_DISABLE(x)                        (((unsigned)(x) & 0x1) << 3)
#define   G_02882C_RECTANGLE_FILTER_DISABLE(x)                        (((x) >> 3) & 0x1)
#define   C_02882C_RECTANGLE_FILTER_DISABLE                           0xFFFFFFF7
#define   S_02882C_TRIANGLE_EXPAND_ENA(x)                             (((unsigned)(x) & 0x1) << 4)
#define   G_02882C_TRIANGLE_EXPAND_ENA(x)                             (((x) >> 4) & 0x1)
#define   C_02882C_TRIANGLE_EXPAND_ENA                                0xFFFFFFEF
#define   S_02882C_LINE_EXPAND_ENA(x)                                 (((unsigned)(x) & 0x1) << 5)
#define   G_02882C_LINE_EXPAND_ENA(x)                                 (((x) >> 5) & 0x1)
#define   C_02882C_LINE_EXPAND_ENA                                    0xFFFFFFDF
#define   S_02882C_POINT_EXPAND_ENA(x)                                (((unsigned)(x) & 0x1) << 6)
#define   G_02882C_POINT_EXPAND_ENA(x)                                (((x) >> 6) & 0x1)
#define   C_02882C_POINT_EXPAND_ENA                                   0xFFFFFFBF
#define   S_02882C_RECTANGLE_EXPAND_ENA(x)                            (((unsigned)(x) & 0x1) << 7)
#define   G_02882C_RECTANGLE_EXPAND_ENA(x)                            (((x) >> 7) & 0x1)
#define   C_02882C_RECTANGLE_EXPAND_ENA                               0xFFFFFF7F
#define   S_02882C_PRIM_EXPAND_CONSTANT(x)                            (((unsigned)(x) & 0xFF) << 8)
#define   G_02882C_PRIM_EXPAND_CONSTANT(x)                            (((x) >> 8) & 0xFF)
#define   C_02882C_PRIM_EXPAND_CONSTANT                               0xFFFF00FF
/* CIK */
#define   S_02882C_XMAX_RIGHT_EXCLUSION(x)                            (((unsigned)(x) & 0x1) << 30)
#define   G_02882C_XMAX_RIGHT_EXCLUSION(x)                            (((x) >> 30) & 0x1)
#define   C_02882C_XMAX_RIGHT_EXCLUSION                               0xBFFFFFFF
#define   S_02882C_YMAX_BOTTOM_EXCLUSION(x)                           (((unsigned)(x) & 0x1) << 31)
#define   G_02882C_YMAX_BOTTOM_EXCLUSION(x)                           (((x) >> 31) & 0x1)
#define   C_02882C_YMAX_BOTTOM_EXCLUSION                              0x7FFFFFFF
/*     */
#define R_028830_PA_SU_SMALL_PRIM_FILTER_CNTL                           0x028830 /* Polaris */
#define   S_028830_SMALL_PRIM_FILTER_ENABLE(x)                        (((x) & 0x1) << 0)
#define   C_028830_SMALL_PRIM_FILTER_ENABLE                           0xFFFFFFFE
#define   S_028830_TRIANGLE_FILTER_DISABLE(x)                         (((x) & 0x1) << 1)
#define   S_028830_LINE_FILTER_DISABLE(x)                             (((x) & 0x1) << 2)
#define   S_028830_POINT_FILTER_DISABLE(x)                            (((x) & 0x1) << 3)
#define   S_028830_RECTANGLE_FILTER_DISABLE(x)                        (((x) & 0x1) << 4)
#define R_028A00_PA_SU_POINT_SIZE                                       0x028A00
#define   S_028A00_HEIGHT(x)                                          (((unsigned)(x) & 0xFFFF) << 0)
#define   G_028A00_HEIGHT(x)                                          (((x) >> 0) & 0xFFFF)
#define   C_028A00_HEIGHT                                             0xFFFF0000
#define   S_028A00_WIDTH(x)                                           (((unsigned)(x) & 0xFFFF) << 16)
#define   G_028A00_WIDTH(x)                                           (((x) >> 16) & 0xFFFF)
#define   C_028A00_WIDTH                                              0x0000FFFF
#define R_028A04_PA_SU_POINT_MINMAX                                     0x028A04
#define   S_028A04_MIN_SIZE(x)                                        (((unsigned)(x) & 0xFFFF) << 0)
#define   G_028A04_MIN_SIZE(x)                                        (((x) >> 0) & 0xFFFF)
#define   C_028A04_MIN_SIZE                                           0xFFFF0000
#define   S_028A04_MAX_SIZE(x)                                        (((unsigned)(x) & 0xFFFF) << 16)
#define   G_028A04_MAX_SIZE(x)                                        (((x) >> 16) & 0xFFFF)
#define   C_028A04_MAX_SIZE                                           0x0000FFFF
#define R_028A08_PA_SU_LINE_CNTL                                        0x028A08
#define   S_028A08_WIDTH(x)                                           (((unsigned)(x) & 0xFFFF) << 0)
#define   G_028A08_WIDTH(x)                                           (((x) >> 0) & 0xFFFF)
#define   C_028A08_WIDTH                                              0xFFFF0000
#define R_028A0C_PA_SC_LINE_STIPPLE                                     0x028A0C
#define   S_028A0C_LINE_PATTERN(x)                                    (((unsigned)(x) & 0xFFFF) << 0)
#define   G_028A0C_LINE_PATTERN(x)                                    (((x) >> 0) & 0xFFFF)
#define   C_028A0C_LINE_PATTERN                                       0xFFFF0000
#define   S_028A0C_REPEAT_COUNT(x)                                    (((unsigned)(x) & 0xFF) << 16)
#define   G_028A0C_REPEAT_COUNT(x)                                    (((x) >> 16) & 0xFF)
#define   C_028A0C_REPEAT_COUNT                                       0xFF00FFFF
#define   S_028A0C_PATTERN_BIT_ORDER(x)                               (((unsigned)(x) & 0x1) << 28)
#define   G_028A0C_PATTERN_BIT_ORDER(x)                               (((x) >> 28) & 0x1)
#define   C_028A0C_PATTERN_BIT_ORDER                                  0xEFFFFFFF
#define   S_028A0C_AUTO_RESET_CNTL(x)                                 (((unsigned)(x) & 0x03) << 29)
#define   G_028A0C_AUTO_RESET_CNTL(x)                                 (((x) >> 29) & 0x03)
#define   C_028A0C_AUTO_RESET_CNTL                                    0x9FFFFFFF
#define R_028A10_VGT_OUTPUT_PATH_CNTL                                   0x028A10
#define   S_028A10_PATH_SELECT(x)                                     (((unsigned)(x) & 0x07) << 0)
#define   G_028A10_PATH_SELECT(x)                                     (((x) >> 0) & 0x07)
#define   C_028A10_PATH_SELECT                                        0xFFFFFFF8
#define     V_028A10_VGT_OUTPATH_VTX_REUSE                          0x00
#define     V_028A10_VGT_OUTPATH_TESS_EN                            0x01
#define     V_028A10_VGT_OUTPATH_PASSTHRU                           0x02
#define     V_028A10_VGT_OUTPATH_GS_BLOCK                           0x03
#define     V_028A10_VGT_OUTPATH_HS_BLOCK                           0x04
#define R_028A14_VGT_HOS_CNTL                                           0x028A14
#define   S_028A14_TESS_MODE(x)                                       (((unsigned)(x) & 0x03) << 0)
#define   G_028A14_TESS_MODE(x)                                       (((x) >> 0) & 0x03)
#define   C_028A14_TESS_MODE                                          0xFFFFFFFC
#define R_028A18_VGT_HOS_MAX_TESS_LEVEL                                 0x028A18
#define R_028A1C_VGT_HOS_MIN_TESS_LEVEL                                 0x028A1C
#define R_028A20_VGT_HOS_REUSE_DEPTH                                    0x028A20
#define   S_028A20_REUSE_DEPTH(x)                                     (((unsigned)(x) & 0xFF) << 0)
#define   G_028A20_REUSE_DEPTH(x)                                     (((x) >> 0) & 0xFF)
#define   C_028A20_REUSE_DEPTH                                        0xFFFFFF00
#define R_028A24_VGT_GROUP_PRIM_TYPE                                    0x028A24
#define   S_028A24_PRIM_TYPE(x)                                       (((unsigned)(x) & 0x1F) << 0)
#define   G_028A24_PRIM_TYPE(x)                                       (((x) >> 0) & 0x1F)
#define   C_028A24_PRIM_TYPE                                          0xFFFFFFE0
#define     V_028A24_VGT_GRP_3D_POINT                               0x00
#define     V_028A24_VGT_GRP_3D_LINE                                0x01
#define     V_028A24_VGT_GRP_3D_TRI                                 0x02
#define     V_028A24_VGT_GRP_3D_RECT                                0x03
#define     V_028A24_VGT_GRP_3D_QUAD                                0x04
#define     V_028A24_VGT_GRP_2D_COPY_RECT_V0                        0x05
#define     V_028A24_VGT_GRP_2D_COPY_RECT_V1                        0x06
#define     V_028A24_VGT_GRP_2D_COPY_RECT_V2                        0x07
#define     V_028A24_VGT_GRP_2D_COPY_RECT_V3                        0x08
#define     V_028A24_VGT_GRP_2D_FILL_RECT                           0x09
#define     V_028A24_VGT_GRP_2D_LINE                                0x0A
#define     V_028A24_VGT_GRP_2D_TRI                                 0x0B
#define     V_028A24_VGT_GRP_PRIM_INDEX_LINE                        0x0C
#define     V_028A24_VGT_GRP_PRIM_INDEX_TRI                         0x0D
#define     V_028A24_VGT_GRP_PRIM_INDEX_QUAD                        0x0E
#define     V_028A24_VGT_GRP_3D_LINE_ADJ                            0x0F
#define     V_028A24_VGT_GRP_3D_TRI_ADJ                             0x10
#define     V_028A24_VGT_GRP_3D_PATCH                               0x11
#define   S_028A24_RETAIN_ORDER(x)                                    (((unsigned)(x) & 0x1) << 14)
#define   G_028A24_RETAIN_ORDER(x)                                    (((x) >> 14) & 0x1)
#define   C_028A24_RETAIN_ORDER                                       0xFFFFBFFF
#define   S_028A24_RETAIN_QUADS(x)                                    (((unsigned)(x) & 0x1) << 15)
#define   G_028A24_RETAIN_QUADS(x)                                    (((x) >> 15) & 0x1)
#define   C_028A24_RETAIN_QUADS                                       0xFFFF7FFF
#define   S_028A24_PRIM_ORDER(x)                                      (((unsigned)(x) & 0x07) << 16)
#define   G_028A24_PRIM_ORDER(x)                                      (((x) >> 16) & 0x07)
#define   C_028A24_PRIM_ORDER                                         0xFFF8FFFF
#define     V_028A24_VGT_GRP_LIST                                   0x00
#define     V_028A24_VGT_GRP_STRIP                                  0x01
#define     V_028A24_VGT_GRP_FAN                                    0x02
#define     V_028A24_VGT_GRP_LOOP                                   0x03
#define     V_028A24_VGT_GRP_POLYGON                                0x04
#define R_028A28_VGT_GROUP_FIRST_DECR                                   0x028A28
#define   S_028A28_FIRST_DECR(x)                                      (((unsigned)(x) & 0x0F) << 0)
#define   G_028A28_FIRST_DECR(x)                                      (((x) >> 0) & 0x0F)
#define   C_028A28_FIRST_DECR                                         0xFFFFFFF0
#define R_028A2C_VGT_GROUP_DECR                                         0x028A2C
#define   S_028A2C_DECR(x)                                            (((unsigned)(x) & 0x0F) << 0)
#define   G_028A2C_DECR(x)                                            (((x) >> 0) & 0x0F)
#define   C_028A2C_DECR                                               0xFFFFFFF0
#define R_028A30_VGT_GROUP_VECT_0_CNTL                                  0x028A30
#define   S_028A30_COMP_X_EN(x)                                       (((unsigned)(x) & 0x1) << 0)
#define   G_028A30_COMP_X_EN(x)                                       (((x) >> 0) & 0x1)
#define   C_028A30_COMP_X_EN                                          0xFFFFFFFE
#define   S_028A30_COMP_Y_EN(x)                                       (((unsigned)(x) & 0x1) << 1)
#define   G_028A30_COMP_Y_EN(x)                                       (((x) >> 1) & 0x1)
#define   C_028A30_COMP_Y_EN                                          0xFFFFFFFD
#define   S_028A30_COMP_Z_EN(x)                                       (((unsigned)(x) & 0x1) << 2)
#define   G_028A30_COMP_Z_EN(x)                                       (((x) >> 2) & 0x1)
#define   C_028A30_COMP_Z_EN                                          0xFFFFFFFB
#define   S_028A30_COMP_W_EN(x)                                       (((unsigned)(x) & 0x1) << 3)
#define   G_028A30_COMP_W_EN(x)                                       (((x) >> 3) & 0x1)
#define   C_028A30_COMP_W_EN                                          0xFFFFFFF7
#define   S_028A30_STRIDE(x)                                          (((unsigned)(x) & 0xFF) << 8)
#define   G_028A30_STRIDE(x)                                          (((x) >> 8) & 0xFF)
#define   C_028A30_STRIDE                                             0xFFFF00FF
#define   S_028A30_SHIFT(x)                                           (((unsigned)(x) & 0xFF) << 16)
#define   G_028A30_SHIFT(x)                                           (((x) >> 16) & 0xFF)
#define   C_028A30_SHIFT                                              0xFF00FFFF
#define R_028A34_VGT_GROUP_VECT_1_CNTL                                  0x028A34
#define   S_028A34_COMP_X_EN(x)                                       (((unsigned)(x) & 0x1) << 0)
#define   G_028A34_COMP_X_EN(x)                                       (((x) >> 0) & 0x1)
#define   C_028A34_COMP_X_EN                                          0xFFFFFFFE
#define   S_028A34_COMP_Y_EN(x)                                       (((unsigned)(x) & 0x1) << 1)
#define   G_028A34_COMP_Y_EN(x)                                       (((x) >> 1) & 0x1)
#define   C_028A34_COMP_Y_EN                                          0xFFFFFFFD
#define   S_028A34_COMP_Z_EN(x)                                       (((unsigned)(x) & 0x1) << 2)
#define   G_028A34_COMP_Z_EN(x)                                       (((x) >> 2) & 0x1)
#define   C_028A34_COMP_Z_EN                                          0xFFFFFFFB
#define   S_028A34_COMP_W_EN(x)                                       (((unsigned)(x) & 0x1) << 3)
#define   G_028A34_COMP_W_EN(x)                                       (((x) >> 3) & 0x1)
#define   C_028A34_COMP_W_EN                                          0xFFFFFFF7
#define   S_028A34_STRIDE(x)                                          (((unsigned)(x) & 0xFF) << 8)
#define   G_028A34_STRIDE(x)                                          (((x) >> 8) & 0xFF)
#define   C_028A34_STRIDE                                             0xFFFF00FF
#define   S_028A34_SHIFT(x)                                           (((unsigned)(x) & 0xFF) << 16)
#define   G_028A34_SHIFT(x)                                           (((x) >> 16) & 0xFF)
#define   C_028A34_SHIFT                                              0xFF00FFFF
#define R_028A38_VGT_GROUP_VECT_0_FMT_CNTL                              0x028A38
#define   S_028A38_X_CONV(x)                                          (((unsigned)(x) & 0x0F) << 0)
#define   G_028A38_X_CONV(x)                                          (((x) >> 0) & 0x0F)
#define   C_028A38_X_CONV                                             0xFFFFFFF0
#define     V_028A38_VGT_GRP_INDEX_16                               0x00
#define     V_028A38_VGT_GRP_INDEX_32                               0x01
#define     V_028A38_VGT_GRP_UINT_16                                0x02
#define     V_028A38_VGT_GRP_UINT_32                                0x03
#define     V_028A38_VGT_GRP_SINT_16                                0x04
#define     V_028A38_VGT_GRP_SINT_32                                0x05
#define     V_028A38_VGT_GRP_FLOAT_32                               0x06
#define     V_028A38_VGT_GRP_AUTO_PRIM                              0x07
#define     V_028A38_VGT_GRP_FIX_1_23_TO_FLOAT                      0x08
#define   S_028A38_X_OFFSET(x)                                        (((unsigned)(x) & 0x0F) << 4)
#define   G_028A38_X_OFFSET(x)                                        (((x) >> 4) & 0x0F)
#define   C_028A38_X_OFFSET                                           0xFFFFFF0F
#define   S_028A38_Y_CONV(x)                                          (((unsigned)(x) & 0x0F) << 8)
#define   G_028A38_Y_CONV(x)                                          (((x) >> 8) & 0x0F)
#define   C_028A38_Y_CONV                                             0xFFFFF0FF
#define     V_028A38_VGT_GRP_INDEX_16                               0x00
#define     V_028A38_VGT_GRP_INDEX_32                               0x01
#define     V_028A38_VGT_GRP_UINT_16                                0x02
#define     V_028A38_VGT_GRP_UINT_32                                0x03
#define     V_028A38_VGT_GRP_SINT_16                                0x04
#define     V_028A38_VGT_GRP_SINT_32                                0x05
#define     V_028A38_VGT_GRP_FLOAT_32                               0x06
#define     V_028A38_VGT_GRP_AUTO_PRIM                              0x07
#define     V_028A38_VGT_GRP_FIX_1_23_TO_FLOAT                      0x08
#define   S_028A38_Y_OFFSET(x)                                        (((unsigned)(x) & 0x0F) << 12)
#define   G_028A38_Y_OFFSET(x)                                        (((x) >> 12) & 0x0F)
#define   C_028A38_Y_OFFSET                                           0xFFFF0FFF
#define   S_028A38_Z_CONV(x)                                          (((unsigned)(x) & 0x0F) << 16)
#define   G_028A38_Z_CONV(x)                                          (((x) >> 16) & 0x0F)
#define   C_028A38_Z_CONV                                             0xFFF0FFFF
#define     V_028A38_VGT_GRP_INDEX_16                               0x00
#define     V_028A38_VGT_GRP_INDEX_32                               0x01
#define     V_028A38_VGT_GRP_UINT_16                                0x02
#define     V_028A38_VGT_GRP_UINT_32                                0x03
#define     V_028A38_VGT_GRP_SINT_16                                0x04
#define     V_028A38_VGT_GRP_SINT_32                                0x05
#define     V_028A38_VGT_GRP_FLOAT_32                               0x06
#define     V_028A38_VGT_GRP_AUTO_PRIM                              0x07
#define     V_028A38_VGT_GRP_FIX_1_23_TO_FLOAT                      0x08
#define   S_028A38_Z_OFFSET(x)                                        (((unsigned)(x) & 0x0F) << 20)
#define   G_028A38_Z_OFFSET(x)                                        (((x) >> 20) & 0x0F)
#define   C_028A38_Z_OFFSET                                           0xFF0FFFFF
#define   S_028A38_W_CONV(x)                                          (((unsigned)(x) & 0x0F) << 24)
#define   G_028A38_W_CONV(x)                                          (((x) >> 24) & 0x0F)
#define   C_028A38_W_CONV                                             0xF0FFFFFF
#define     V_028A38_VGT_GRP_INDEX_16                               0x00
#define     V_028A38_VGT_GRP_INDEX_32                               0x01
#define     V_028A38_VGT_GRP_UINT_16                                0x02
#define     V_028A38_VGT_GRP_UINT_32                                0x03
#define     V_028A38_VGT_GRP_SINT_16                                0x04
#define     V_028A38_VGT_GRP_SINT_32                                0x05
#define     V_028A38_VGT_GRP_FLOAT_32                               0x06
#define     V_028A38_VGT_GRP_AUTO_PRIM                              0x07
#define     V_028A38_VGT_GRP_FIX_1_23_TO_FLOAT                      0x08
#define   S_028A38_W_OFFSET(x)                                        (((unsigned)(x) & 0x0F) << 28)
#define   G_028A38_W_OFFSET(x)                                        (((x) >> 28) & 0x0F)
#define   C_028A38_W_OFFSET                                           0x0FFFFFFF
#define R_028A3C_VGT_GROUP_VECT_1_FMT_CNTL                              0x028A3C
#define   S_028A3C_X_CONV(x)                                          (((unsigned)(x) & 0x0F) << 0)
#define   G_028A3C_X_CONV(x)                                          (((x) >> 0) & 0x0F)
#define   C_028A3C_X_CONV                                             0xFFFFFFF0
#define     V_028A3C_VGT_GRP_INDEX_16                               0x00
#define     V_028A3C_VGT_GRP_INDEX_32                               0x01
#define     V_028A3C_VGT_GRP_UINT_16                                0x02
#define     V_028A3C_VGT_GRP_UINT_32                                0x03
#define     V_028A3C_VGT_GRP_SINT_16                                0x04
#define     V_028A3C_VGT_GRP_SINT_32                                0x05
#define     V_028A3C_VGT_GRP_FLOAT_32                               0x06
#define     V_028A3C_VGT_GRP_AUTO_PRIM                              0x07
#define     V_028A3C_VGT_GRP_FIX_1_23_TO_FLOAT                      0x08
#define   S_028A3C_X_OFFSET(x)                                        (((unsigned)(x) & 0x0F) << 4)
#define   G_028A3C_X_OFFSET(x)                                        (((x) >> 4) & 0x0F)
#define   C_028A3C_X_OFFSET                                           0xFFFFFF0F
#define   S_028A3C_Y_CONV(x)                                          (((unsigned)(x) & 0x0F) << 8)
#define   G_028A3C_Y_CONV(x)                                          (((x) >> 8) & 0x0F)
#define   C_028A3C_Y_CONV                                             0xFFFFF0FF
#define     V_028A3C_VGT_GRP_INDEX_16                               0x00
#define     V_028A3C_VGT_GRP_INDEX_32                               0x01
#define     V_028A3C_VGT_GRP_UINT_16                                0x02
#define     V_028A3C_VGT_GRP_UINT_32                                0x03
#define     V_028A3C_VGT_GRP_SINT_16                                0x04
#define     V_028A3C_VGT_GRP_SINT_32                                0x05
#define     V_028A3C_VGT_GRP_FLOAT_32                               0x06
#define     V_028A3C_VGT_GRP_AUTO_PRIM                              0x07
#define     V_028A3C_VGT_GRP_FIX_1_23_TO_FLOAT                      0x08
#define   S_028A3C_Y_OFFSET(x)                                        (((unsigned)(x) & 0x0F) << 12)
#define   G_028A3C_Y_OFFSET(x)                                        (((x) >> 12) & 0x0F)
#define   C_028A3C_Y_OFFSET                                           0xFFFF0FFF
#define   S_028A3C_Z_CONV(x)                                          (((unsigned)(x) & 0x0F) << 16)
#define   G_028A3C_Z_CONV(x)                                          (((x) >> 16) & 0x0F)
#define   C_028A3C_Z_CONV                                             0xFFF0FFFF
#define     V_028A3C_VGT_GRP_INDEX_16                               0x00
#define     V_028A3C_VGT_GRP_INDEX_32                               0x01
#define     V_028A3C_VGT_GRP_UINT_16                                0x02
#define     V_028A3C_VGT_GRP_UINT_32                                0x03
#define     V_028A3C_VGT_GRP_SINT_16                                0x04
#define     V_028A3C_VGT_GRP_SINT_32                                0x05
#define     V_028A3C_VGT_GRP_FLOAT_32                               0x06
#define     V_028A3C_VGT_GRP_AUTO_PRIM                              0x07
#define     V_028A3C_VGT_GRP_FIX_1_23_TO_FLOAT                      0x08
#define   S_028A3C_Z_OFFSET(x)                                        (((unsigned)(x) & 0x0F) << 20)
#define   G_028A3C_Z_OFFSET(x)                                        (((x) >> 20) & 0x0F)
#define   C_028A3C_Z_OFFSET                                           0xFF0FFFFF
#define   S_028A3C_W_CONV(x)                                          (((unsigned)(x) & 0x0F) << 24)
#define   G_028A3C_W_CONV(x)                                          (((x) >> 24) & 0x0F)
#define   C_028A3C_W_CONV                                             0xF0FFFFFF
#define     V_028A3C_VGT_GRP_INDEX_16                               0x00
#define     V_028A3C_VGT_GRP_INDEX_32                               0x01
#define     V_028A3C_VGT_GRP_UINT_16                                0x02
#define     V_028A3C_VGT_GRP_UINT_32                                0x03
#define     V_028A3C_VGT_GRP_SINT_16                                0x04
#define     V_028A3C_VGT_GRP_SINT_32                                0x05
#define     V_028A3C_VGT_GRP_FLOAT_32                               0x06
#define     V_028A3C_VGT_GRP_AUTO_PRIM                              0x07
#define     V_028A3C_VGT_GRP_FIX_1_23_TO_FLOAT                      0x08
#define   S_028A3C_W_OFFSET(x)                                        (((unsigned)(x) & 0x0F) << 28)
#define   G_028A3C_W_OFFSET(x)                                        (((x) >> 28) & 0x0F)
#define   C_028A3C_W_OFFSET                                           0x0FFFFFFF
#define R_028A40_VGT_GS_MODE                                            0x028A40
#define   S_028A40_MODE(x)                                            (((unsigned)(x) & 0x07) << 0)
#define   G_028A40_MODE(x)                                            (((x) >> 0) & 0x07)
#define   C_028A40_MODE                                               0xFFFFFFF8
#define     V_028A40_GS_OFF                                         0x00
#define     V_028A40_GS_SCENARIO_A                                  0x01
#define     V_028A40_GS_SCENARIO_B                                  0x02
#define     V_028A40_GS_SCENARIO_G                                  0x03
#define     V_028A40_GS_SCENARIO_C                                  0x04
#define     V_028A40_SPRITE_EN                                      0x05
#define   S_028A40_RESERVED_0(x)                                      (((unsigned)(x) & 0x1) << 3)
#define   G_028A40_RESERVED_0(x)                                      (((x) >> 3) & 0x1)
#define   C_028A40_RESERVED_0                                         0xFFFFFFF7
#define   S_028A40_CUT_MODE(x)                                        (((unsigned)(x) & 0x03) << 4)
#define   G_028A40_CUT_MODE(x)                                        (((x) >> 4) & 0x03)
#define   C_028A40_CUT_MODE                                           0xFFFFFFCF
#define     V_028A40_GS_CUT_1024                                    0x00
#define     V_028A40_GS_CUT_512                                     0x01
#define     V_028A40_GS_CUT_256                                     0x02
#define     V_028A40_GS_CUT_128                                     0x03
#define   S_028A40_RESERVED_1(x)                                      (((unsigned)(x) & 0x1F) << 6)
#define   G_028A40_RESERVED_1(x)                                      (((x) >> 6) & 0x1F)
#define   C_028A40_RESERVED_1                                         0xFFFFF83F
#define   S_028A40_GS_C_PACK_EN(x)                                    (((unsigned)(x) & 0x1) << 11)
#define   G_028A40_GS_C_PACK_EN(x)                                    (((x) >> 11) & 0x1)
#define   C_028A40_GS_C_PACK_EN                                       0xFFFFF7FF
#define   S_028A40_RESERVED_2(x)                                      (((unsigned)(x) & 0x1) << 12)
#define   G_028A40_RESERVED_2(x)                                      (((x) >> 12) & 0x1)
#define   C_028A40_RESERVED_2                                         0xFFFFEFFF
#define   S_028A40_ES_PASSTHRU(x)                                     (((unsigned)(x) & 0x1) << 13)
#define   G_028A40_ES_PASSTHRU(x)                                     (((x) >> 13) & 0x1)
#define   C_028A40_ES_PASSTHRU                                        0xFFFFDFFF
/* SI-CIK */
#define   S_028A40_COMPUTE_MODE(x)                                    (((unsigned)(x) & 0x1) << 14)
#define   G_028A40_COMPUTE_MODE(x)                                    (((x) >> 14) & 0x1)
#define   C_028A40_COMPUTE_MODE                                       0xFFFFBFFF
#define   S_028A40_FAST_COMPUTE_MODE(x)                               (((unsigned)(x) & 0x1) << 15)
#define   G_028A40_FAST_COMPUTE_MODE(x)                               (((x) >> 15) & 0x1)
#define   C_028A40_FAST_COMPUTE_MODE                                  0xFFFF7FFF
#define   S_028A40_ELEMENT_INFO_EN(x)                                 (((unsigned)(x) & 0x1) << 16)
#define   G_028A40_ELEMENT_INFO_EN(x)                                 (((x) >> 16) & 0x1)
#define   C_028A40_ELEMENT_INFO_EN                                    0xFFFEFFFF
/*        */
#define   S_028A40_PARTIAL_THD_AT_EOI(x)                              (((unsigned)(x) & 0x1) << 17)
#define   G_028A40_PARTIAL_THD_AT_EOI(x)                              (((x) >> 17) & 0x1)
#define   C_028A40_PARTIAL_THD_AT_EOI                                 0xFFFDFFFF
#define   S_028A40_SUPPRESS_CUTS(x)                                   (((unsigned)(x) & 0x1) << 18)
#define   G_028A40_SUPPRESS_CUTS(x)                                   (((x) >> 18) & 0x1)
#define   C_028A40_SUPPRESS_CUTS                                      0xFFFBFFFF
#define   S_028A40_ES_WRITE_OPTIMIZE(x)                               (((unsigned)(x) & 0x1) << 19)
#define   G_028A40_ES_WRITE_OPTIMIZE(x)                               (((x) >> 19) & 0x1)
#define   C_028A40_ES_WRITE_OPTIMIZE                                  0xFFF7FFFF
#define   S_028A40_GS_WRITE_OPTIMIZE(x)                               (((unsigned)(x) & 0x1) << 20)
#define   G_028A40_GS_WRITE_OPTIMIZE(x)                               (((x) >> 20) & 0x1)
#define   C_028A40_GS_WRITE_OPTIMIZE                                  0xFFEFFFFF
/* CIK */
#define   S_028A40_ONCHIP(x)                                          (((unsigned)(x) & 0x03) << 21)
#define   G_028A40_ONCHIP(x)                                          (((x) >> 21) & 0x03)
#define   C_028A40_ONCHIP                                             0xFF9FFFFF
#define     V_028A40_X_0_OFFCHIP_GS                                 0x00
#define     V_028A40_X_3_ES_AND_GS_ARE_ONCHIP                       0x03
#define R_028A44_VGT_GS_ONCHIP_CNTL                                     0x028A44
#define   S_028A44_ES_VERTS_PER_SUBGRP(x)                             (((unsigned)(x) & 0x7FF) << 0)
#define   G_028A44_ES_VERTS_PER_SUBGRP(x)                             (((x) >> 0) & 0x7FF)
#define   C_028A44_ES_VERTS_PER_SUBGRP                                0xFFFFF800
#define   S_028A44_GS_PRIMS_PER_SUBGRP(x)                             (((unsigned)(x) & 0x7FF) << 11)
#define   G_028A44_GS_PRIMS_PER_SUBGRP(x)                             (((x) >> 11) & 0x7FF)
#define   C_028A44_GS_PRIMS_PER_SUBGRP                                0xFFC007FF
/*     */
#define R_028A48_PA_SC_MODE_CNTL_0                                      0x028A48
#define   S_028A48_MSAA_ENABLE(x)                                     (((unsigned)(x) & 0x1) << 0)
#define   G_028A48_MSAA_ENABLE(x)                                     (((x) >> 0) & 0x1)
#define   C_028A48_MSAA_ENABLE                                        0xFFFFFFFE
#define   S_028A48_VPORT_SCISSOR_ENABLE(x)                            (((unsigned)(x) & 0x1) << 1)
#define   G_028A48_VPORT_SCISSOR_ENABLE(x)                            (((x) >> 1) & 0x1)
#define   C_028A48_VPORT_SCISSOR_ENABLE                               0xFFFFFFFD
#define   S_028A48_LINE_STIPPLE_ENABLE(x)                             (((unsigned)(x) & 0x1) << 2)
#define   G_028A48_LINE_STIPPLE_ENABLE(x)                             (((x) >> 2) & 0x1)
#define   C_028A48_LINE_STIPPLE_ENABLE                                0xFFFFFFFB
#define   S_028A48_SEND_UNLIT_STILES_TO_PKR(x)                        (((unsigned)(x) & 0x1) << 3)
#define   G_028A48_SEND_UNLIT_STILES_TO_PKR(x)                        (((x) >> 3) & 0x1)
#define   C_028A48_SEND_UNLIT_STILES_TO_PKR                           0xFFFFFFF7
#define R_028A4C_PA_SC_MODE_CNTL_1                                      0x028A4C
#define   S_028A4C_WALK_SIZE(x)                                       (((unsigned)(x) & 0x1) << 0)
#define   G_028A4C_WALK_SIZE(x)                                       (((x) >> 0) & 0x1)
#define   C_028A4C_WALK_SIZE                                          0xFFFFFFFE
#define   S_028A4C_WALK_ALIGNMENT(x)                                  (((unsigned)(x) & 0x1) << 1)
#define   G_028A4C_WALK_ALIGNMENT(x)                                  (((x) >> 1) & 0x1)
#define   C_028A4C_WALK_ALIGNMENT                                     0xFFFFFFFD
#define   S_028A4C_WALK_ALIGN8_PRIM_FITS_ST(x)                        (((unsigned)(x) & 0x1) << 2)
#define   G_028A4C_WALK_ALIGN8_PRIM_FITS_ST(x)                        (((x) >> 2) & 0x1)
#define   C_028A4C_WALK_ALIGN8_PRIM_FITS_ST                           0xFFFFFFFB
#define   S_028A4C_WALK_FENCE_ENABLE(x)                               (((unsigned)(x) & 0x1) << 3)
#define   G_028A4C_WALK_FENCE_ENABLE(x)                               (((x) >> 3) & 0x1)
#define   C_028A4C_WALK_FENCE_ENABLE                                  0xFFFFFFF7
#define   S_028A4C_WALK_FENCE_SIZE(x)                                 (((unsigned)(x) & 0x07) << 4)
#define   G_028A4C_WALK_FENCE_SIZE(x)                                 (((x) >> 4) & 0x07)
#define   C_028A4C_WALK_FENCE_SIZE                                    0xFFFFFF8F
#define   S_028A4C_SUPERTILE_WALK_ORDER_ENABLE(x)                     (((unsigned)(x) & 0x1) << 7)
#define   G_028A4C_SUPERTILE_WALK_ORDER_ENABLE(x)                     (((x) >> 7) & 0x1)
#define   C_028A4C_SUPERTILE_WALK_ORDER_ENABLE                        0xFFFFFF7F
#define   S_028A4C_TILE_WALK_ORDER_ENABLE(x)                          (((unsigned)(x) & 0x1) << 8)
#define   G_028A4C_TILE_WALK_ORDER_ENABLE(x)                          (((x) >> 8) & 0x1)
#define   C_028A4C_TILE_WALK_ORDER_ENABLE                             0xFFFFFEFF
#define   S_028A4C_TILE_COVER_DISABLE(x)                              (((unsigned)(x) & 0x1) << 9)
#define   G_028A4C_TILE_COVER_DISABLE(x)                              (((x) >> 9) & 0x1)
#define   C_028A4C_TILE_COVER_DISABLE                                 0xFFFFFDFF
#define   S_028A4C_TILE_COVER_NO_SCISSOR(x)                           (((unsigned)(x) & 0x1) << 10)
#define   G_028A4C_TILE_COVER_NO_SCISSOR(x)                           (((x) >> 10) & 0x1)
#define   C_028A4C_TILE_COVER_NO_SCISSOR                              0xFFFFFBFF
#define   S_028A4C_ZMM_LINE_EXTENT(x)                                 (((unsigned)(x) & 0x1) << 11)
#define   G_028A4C_ZMM_LINE_EXTENT(x)                                 (((x) >> 11) & 0x1)
#define   C_028A4C_ZMM_LINE_EXTENT                                    0xFFFFF7FF
#define   S_028A4C_ZMM_LINE_OFFSET(x)                                 (((unsigned)(x) & 0x1) << 12)
#define   G_028A4C_ZMM_LINE_OFFSET(x)                                 (((x) >> 12) & 0x1)
#define   C_028A4C_ZMM_LINE_OFFSET                                    0xFFFFEFFF
#define   S_028A4C_ZMM_RECT_EXTENT(x)                                 (((unsigned)(x) & 0x1) << 13)
#define   G_028A4C_ZMM_RECT_EXTENT(x)                                 (((x) >> 13) & 0x1)
#define   C_028A4C_ZMM_RECT_EXTENT                                    0xFFFFDFFF
#define   S_028A4C_KILL_PIX_POST_HI_Z(x)                              (((unsigned)(x) & 0x1) << 14)
#define   G_028A4C_KILL_PIX_POST_HI_Z(x)                              (((x) >> 14) & 0x1)
#define   C_028A4C_KILL_PIX_POST_HI_Z                                 0xFFFFBFFF
#define   S_028A4C_KILL_PIX_POST_DETAIL_MASK(x)                       (((unsigned)(x) & 0x1) << 15)
#define   G_028A4C_KILL_PIX_POST_DETAIL_MASK(x)                       (((x) >> 15) & 0x1)
#define   C_028A4C_KILL_PIX_POST_DETAIL_MASK                          0xFFFF7FFF
#define   S_028A4C_PS_ITER_SAMPLE(x)                                  (((unsigned)(x) & 0x1) << 16)
#define   G_028A4C_PS_ITER_SAMPLE(x)                                  (((x) >> 16) & 0x1)
#define   C_028A4C_PS_ITER_SAMPLE                                     0xFFFEFFFF
#define   S_028A4C_MULTI_SHADER_ENGINE_PRIM_DISCARD_ENABLE(x)         (((unsigned)(x) & 0x1) << 17)
#define   G_028A4C_MULTI_SHADER_ENGINE_PRIM_DISCARD_ENABLE(x)         (((x) >> 17) & 0x1)
#define   C_028A4C_MULTI_SHADER_ENGINE_PRIM_DISCARD_ENABLE            0xFFFDFFFF
#define   S_028A4C_MULTI_GPU_SUPERTILE_ENABLE(x)                      (((unsigned)(x) & 0x1) << 18)
#define   G_028A4C_MULTI_GPU_SUPERTILE_ENABLE(x)                      (((x) >> 18) & 0x1)
#define   C_028A4C_MULTI_GPU_SUPERTILE_ENABLE                         0xFFFBFFFF
#define   S_028A4C_GPU_ID_OVERRIDE_ENABLE(x)                          (((unsigned)(x) & 0x1) << 19)
#define   G_028A4C_GPU_ID_OVERRIDE_ENABLE(x)                          (((x) >> 19) & 0x1)
#define   C_028A4C_GPU_ID_OVERRIDE_ENABLE                             0xFFF7FFFF
#define   S_028A4C_GPU_ID_OVERRIDE(x)                                 (((unsigned)(x) & 0x0F) << 20)
#define   G_028A4C_GPU_ID_OVERRIDE(x)                                 (((x) >> 20) & 0x0F)
#define   C_028A4C_GPU_ID_OVERRIDE                                    0xFF0FFFFF
#define   S_028A4C_MULTI_GPU_PRIM_DISCARD_ENABLE(x)                   (((unsigned)(x) & 0x1) << 24)
#define   G_028A4C_MULTI_GPU_PRIM_DISCARD_ENABLE(x)                   (((x) >> 24) & 0x1)
#define   C_028A4C_MULTI_GPU_PRIM_DISCARD_ENABLE                      0xFEFFFFFF
#define   S_028A4C_FORCE_EOV_CNTDWN_ENABLE(x)                         (((unsigned)(x) & 0x1) << 25)
#define   G_028A4C_FORCE_EOV_CNTDWN_ENABLE(x)                         (((x) >> 25) & 0x1)
#define   C_028A4C_FORCE_EOV_CNTDWN_ENABLE                            0xFDFFFFFF
#define   S_028A4C_FORCE_EOV_REZ_ENABLE(x)                            (((unsigned)(x) & 0x1) << 26)
#define   G_028A4C_FORCE_EOV_REZ_ENABLE(x)                            (((x) >> 26) & 0x1)
#define   C_028A4C_FORCE_EOV_REZ_ENABLE                               0xFBFFFFFF
#define   S_028A4C_OUT_OF_ORDER_PRIMITIVE_ENABLE(x)                   (((unsigned)(x) & 0x1) << 27)
#define   G_028A4C_OUT_OF_ORDER_PRIMITIVE_ENABLE(x)                   (((x) >> 27) & 0x1)
#define   C_028A4C_OUT_OF_ORDER_PRIMITIVE_ENABLE                      0xF7FFFFFF
#define   S_028A4C_OUT_OF_ORDER_WATER_MARK(x)                         (((unsigned)(x) & 0x07) << 28)
#define   G_028A4C_OUT_OF_ORDER_WATER_MARK(x)                         (((x) >> 28) & 0x07)
#define   C_028A4C_OUT_OF_ORDER_WATER_MARK                            0x8FFFFFFF
#define R_028A50_VGT_ENHANCE                                            0x028A50
#define R_028A54_VGT_GS_PER_ES                                          0x028A54
#define   S_028A54_GS_PER_ES(x)                                       (((unsigned)(x) & 0x7FF) << 0)
#define   G_028A54_GS_PER_ES(x)                                       (((x) >> 0) & 0x7FF)
#define   C_028A54_GS_PER_ES                                          0xFFFFF800
#define R_028A58_VGT_ES_PER_GS                                          0x028A58
#define   S_028A58_ES_PER_GS(x)                                       (((unsigned)(x) & 0x7FF) << 0)
#define   G_028A58_ES_PER_GS(x)                                       (((x) >> 0) & 0x7FF)
#define   C_028A58_ES_PER_GS                                          0xFFFFF800
#define R_028A5C_VGT_GS_PER_VS                                          0x028A5C
#define   S_028A5C_GS_PER_VS(x)                                       (((unsigned)(x) & 0x0F) << 0)
#define   G_028A5C_GS_PER_VS(x)                                       (((x) >> 0) & 0x0F)
#define   C_028A5C_GS_PER_VS                                          0xFFFFFFF0
#define R_028A60_VGT_GSVS_RING_OFFSET_1                                 0x028A60
#define   S_028A60_OFFSET(x)                                          (((unsigned)(x) & 0x7FFF) << 0)
#define   G_028A60_OFFSET(x)                                          (((x) >> 0) & 0x7FFF)
#define   C_028A60_OFFSET                                             0xFFFF8000
#define R_028A64_VGT_GSVS_RING_OFFSET_2                                 0x028A64
#define   S_028A64_OFFSET(x)                                          (((unsigned)(x) & 0x7FFF) << 0)
#define   G_028A64_OFFSET(x)                                          (((x) >> 0) & 0x7FFF)
#define   C_028A64_OFFSET                                             0xFFFF8000
#define R_028A68_VGT_GSVS_RING_OFFSET_3                                 0x028A68
#define   S_028A68_OFFSET(x)                                          (((unsigned)(x) & 0x7FFF) << 0)
#define   G_028A68_OFFSET(x)                                          (((x) >> 0) & 0x7FFF)
#define   C_028A68_OFFSET                                             0xFFFF8000
#define R_028A6C_VGT_GS_OUT_PRIM_TYPE                                   0x028A6C
#define   S_028A6C_OUTPRIM_TYPE(x)                                    (((unsigned)(x) & 0x3F) << 0)
#define   G_028A6C_OUTPRIM_TYPE(x)                                    (((x) >> 0) & 0x3F)
#define   C_028A6C_OUTPRIM_TYPE                                       0xFFFFFFC0
#define     V_028A6C_OUTPRIM_TYPE_POINTLIST            0
#define     V_028A6C_OUTPRIM_TYPE_LINESTRIP            1
#define     V_028A6C_OUTPRIM_TYPE_TRISTRIP             2
#define   S_028A6C_OUTPRIM_TYPE_1(x)                                  (((unsigned)(x) & 0x3F) << 8)
#define   G_028A6C_OUTPRIM_TYPE_1(x)                                  (((x) >> 8) & 0x3F)
#define   C_028A6C_OUTPRIM_TYPE_1                                     0xFFFFC0FF
#define   S_028A6C_OUTPRIM_TYPE_2(x)                                  (((unsigned)(x) & 0x3F) << 16)
#define   G_028A6C_OUTPRIM_TYPE_2(x)                                  (((x) >> 16) & 0x3F)
#define   C_028A6C_OUTPRIM_TYPE_2                                     0xFFC0FFFF
#define   S_028A6C_OUTPRIM_TYPE_3(x)                                  (((unsigned)(x) & 0x3F) << 22)
#define   G_028A6C_OUTPRIM_TYPE_3(x)                                  (((x) >> 22) & 0x3F)
#define   C_028A6C_OUTPRIM_TYPE_3                                     0xF03FFFFF
#define   S_028A6C_UNIQUE_TYPE_PER_STREAM(x)                          (((unsigned)(x) & 0x1) << 31)
#define   G_028A6C_UNIQUE_TYPE_PER_STREAM(x)                          (((x) >> 31) & 0x1)
#define   C_028A6C_UNIQUE_TYPE_PER_STREAM                             0x7FFFFFFF
#define R_028A70_IA_ENHANCE                                             0x028A70
#define R_028A74_VGT_DMA_SIZE                                           0x028A74
#define R_028A78_VGT_DMA_MAX_SIZE                                       0x028A78
#define R_028A7C_VGT_DMA_INDEX_TYPE                                     0x028A7C
#define   S_028A7C_INDEX_TYPE(x)                                      (((unsigned)(x) & 0x03) << 0)
#define   G_028A7C_INDEX_TYPE(x)                                      (((x) >> 0) & 0x03)
#define   C_028A7C_INDEX_TYPE                                         0xFFFFFFFC
#define     V_028A7C_VGT_INDEX_16                                   0x00
#define     V_028A7C_VGT_INDEX_32                                   0x01
#define     V_028A7C_VGT_INDEX_8                                    0x02 /* VI */
#define   S_028A7C_SWAP_MODE(x)                                       (((unsigned)(x) & 0x03) << 2)
#define   G_028A7C_SWAP_MODE(x)                                       (((x) >> 2) & 0x03)
#define   C_028A7C_SWAP_MODE                                          0xFFFFFFF3
#define     V_028A7C_VGT_DMA_SWAP_NONE                              0x00
#define     V_028A7C_VGT_DMA_SWAP_16_BIT                            0x01
#define     V_028A7C_VGT_DMA_SWAP_32_BIT                            0x02
#define     V_028A7C_VGT_DMA_SWAP_WORD                              0x03
/* CIK */
#define   S_028A7C_BUF_TYPE(x)                                        (((unsigned)(x) & 0x03) << 4)
#define   G_028A7C_BUF_TYPE(x)                                        (((x) >> 4) & 0x03)
#define   C_028A7C_BUF_TYPE                                           0xFFFFFFCF
#define     V_028A7C_VGT_DMA_BUF_MEM                                0x00
#define     V_028A7C_VGT_DMA_BUF_RING                               0x01
#define     V_028A7C_VGT_DMA_BUF_SETUP                              0x02
#define   S_028A7C_RDREQ_POLICY(x)                                    (((unsigned)(x) & 0x03) << 6)
#define   G_028A7C_RDREQ_POLICY(x)                                    (((x) >> 6) & 0x03)
#define   C_028A7C_RDREQ_POLICY                                       0xFFFFFF3F
#define     V_028A7C_VGT_POLICY_LRU                                 0x00
#define     V_028A7C_VGT_POLICY_STREAM                              0x01
#define   S_028A7C_RDREQ_POLICY_VI(x)                                 (((unsigned)(x) & 0x1) << 6)
#define   G_028A7C_RDREQ_POLICY_VI(x)                                 (((x) >> 6) & 0x1)
#define   C_028A7C_RDREQ_POLICY_VI                                    0xFFFFFFBF
#define   S_028A7C_ATC(x)                                             (((unsigned)(x) & 0x1) << 8)
#define   G_028A7C_ATC(x)                                             (((x) >> 8) & 0x1)
#define   C_028A7C_ATC                                                0xFFFFFEFF
#define   S_028A7C_NOT_EOP(x)                                         (((unsigned)(x) & 0x1) << 9)
#define   G_028A7C_NOT_EOP(x)                                         (((x) >> 9) & 0x1)
#define   C_028A7C_NOT_EOP                                            0xFFFFFDFF
#define   S_028A7C_REQ_PATH(x)                                        (((unsigned)(x) & 0x1) << 10)
#define   G_028A7C_REQ_PATH(x)                                        (((x) >> 10) & 0x1)
#define   C_028A7C_REQ_PATH                                           0xFFFFFBFF
/*     */
/* VI */
#define   S_028A7C_MTYPE(x)                                           (((unsigned)(x) & 0x03) << 11)
#define   G_028A7C_MTYPE(x)                                           (((x) >> 11) & 0x03)
#define   C_028A7C_MTYPE                                              0xFFFFE7FF
/*    */
#define R_028A80_WD_ENHANCE                                             0x028A80
#define R_028A84_VGT_PRIMITIVEID_EN                                     0x028A84
#define   S_028A84_PRIMITIVEID_EN(x)                                  (((unsigned)(x) & 0x1) << 0)
#define   G_028A84_PRIMITIVEID_EN(x)                                  (((x) >> 0) & 0x1)
#define   C_028A84_PRIMITIVEID_EN                                     0xFFFFFFFE
#define   S_028A84_DISABLE_RESET_ON_EOI(x)                            (((unsigned)(x) & 0x1) << 1) /* not on CIK */
#define   G_028A84_DISABLE_RESET_ON_EOI(x)                            (((x) >> 1) & 0x1) /* not on CIK */
#define   C_028A84_DISABLE_RESET_ON_EOI                               0xFFFFFFFD /* not on CIK */
#define R_028A88_VGT_DMA_NUM_INSTANCES                                  0x028A88
#define R_028A8C_VGT_PRIMITIVEID_RESET                                  0x028A8C
#define R_028A90_VGT_EVENT_INITIATOR                                    0x028A90
#define   S_028A90_EVENT_TYPE(x)                                      (((unsigned)(x) & 0x3F) << 0)
#define   G_028A90_EVENT_TYPE(x)                                      (((x) >> 0) & 0x3F)
#define   C_028A90_EVENT_TYPE                                         0xFFFFFFC0
#define     V_028A90_SAMPLE_STREAMOUTSTATS1                         0x01
#define     V_028A90_SAMPLE_STREAMOUTSTATS2                         0x02
#define     V_028A90_SAMPLE_STREAMOUTSTATS3                         0x03
#define     V_028A90_CACHE_FLUSH_TS                                 0x04
#define     V_028A90_CONTEXT_DONE                                   0x05
#define     V_028A90_CACHE_FLUSH                                    0x06
#define     V_028A90_CS_PARTIAL_FLUSH                               0x07
#define     V_028A90_VGT_STREAMOUT_SYNC                             0x08
#define     V_028A90_VGT_STREAMOUT_RESET                            0x0A
#define     V_028A90_END_OF_PIPE_INCR_DE                            0x0B
#define     V_028A90_END_OF_PIPE_IB_END                             0x0C
#define     V_028A90_RST_PIX_CNT                                    0x0D
#define     V_028A90_VS_PARTIAL_FLUSH                               0x0F
#define     V_028A90_PS_PARTIAL_FLUSH                               0x10
#define     V_028A90_FLUSH_HS_OUTPUT                                0x11
#define     V_028A90_FLUSH_LS_OUTPUT                                0x12
#define     V_028A90_CACHE_FLUSH_AND_INV_TS_EVENT                   0x14
#define     V_028A90_ZPASS_DONE                                     0x15
#define     V_028A90_CACHE_FLUSH_AND_INV_EVENT                      0x16
#define     V_028A90_PERFCOUNTER_START                              0x17
#define     V_028A90_PERFCOUNTER_STOP                               0x18
#define     V_028A90_PIPELINESTAT_START                             0x19
#define     V_028A90_PIPELINESTAT_STOP                              0x1A
#define     V_028A90_PERFCOUNTER_SAMPLE                             0x1B
#define     V_028A90_FLUSH_ES_OUTPUT                                0x1C
#define     V_028A90_FLUSH_GS_OUTPUT                                0x1D
#define     V_028A90_SAMPLE_PIPELINESTAT                            0x1E
#define     V_028A90_SO_VGTSTREAMOUT_FLUSH                          0x1F
#define     V_028A90_SAMPLE_STREAMOUTSTATS                          0x20
#define     V_028A90_RESET_VTX_CNT                                  0x21
#define     V_028A90_BLOCK_CONTEXT_DONE                             0x22
#define     V_028A90_CS_CONTEXT_DONE                                0x23
#define     V_028A90_VGT_FLUSH                                      0x24
#define     V_028A90_SC_SEND_DB_VPZ                                 0x27
#define     V_028A90_BOTTOM_OF_PIPE_TS                              0x28
#define     V_028A90_DB_CACHE_FLUSH_AND_INV                         0x2A
#define     V_028A90_FLUSH_AND_INV_DB_DATA_TS                       0x2B
#define     V_028A90_FLUSH_AND_INV_DB_META                          0x2C
#define     V_028A90_FLUSH_AND_INV_CB_DATA_TS                       0x2D
#define     V_028A90_FLUSH_AND_INV_CB_META                          0x2E
#define     V_028A90_CS_DONE                                        0x2F
#define     V_028A90_PS_DONE                                        0x30
#define     V_028A90_FLUSH_AND_INV_CB_PIXEL_DATA                    0x31
#define     V_028A90_THREAD_TRACE_START                             0x33
#define     V_028A90_THREAD_TRACE_STOP                              0x34
#define     V_028A90_THREAD_TRACE_MARKER                            0x35
#define     V_028A90_THREAD_TRACE_FLUSH                             0x36
#define     V_028A90_THREAD_TRACE_FINISH                            0x37
/* CIK */
#define     V_028A90_PIXEL_PIPE_STAT_CONTROL                        0x38
#define     V_028A90_PIXEL_PIPE_STAT_DUMP                           0x39
#define     V_028A90_PIXEL_PIPE_STAT_RESET                          0x3A
/*     */
#define   S_028A90_ADDRESS_HI(x)                                      (((unsigned)(x) & 0x1FF) << 18)
#define   G_028A90_ADDRESS_HI(x)                                      (((x) >> 18) & 0x1FF)
#define   C_028A90_ADDRESS_HI                                         0xF803FFFF
#define   S_028A90_EXTENDED_EVENT(x)                                  (((unsigned)(x) & 0x1) << 27)
#define   G_028A90_EXTENDED_EVENT(x)                                  (((x) >> 27) & 0x1)
#define   C_028A90_EXTENDED_EVENT                                     0xF7FFFFFF
#define R_028A94_VGT_MULTI_PRIM_IB_RESET_EN                             0x028A94
#define   S_028A94_RESET_EN(x)                                        (((unsigned)(x) & 0x1) << 0)
#define   G_028A94_RESET_EN(x)                                        (((x) >> 0) & 0x1)
#define   C_028A94_RESET_EN                                           0xFFFFFFFE
#define R_028AA0_VGT_INSTANCE_STEP_RATE_0                               0x028AA0
#define R_028AA4_VGT_INSTANCE_STEP_RATE_1                               0x028AA4
#define R_028AA8_IA_MULTI_VGT_PARAM                                     0x028AA8
#define   S_028AA8_PRIMGROUP_SIZE(x)                                  (((unsigned)(x) & 0xFFFF) << 0)
#define   G_028AA8_PRIMGROUP_SIZE(x)                                  (((x) >> 0) & 0xFFFF)
#define   C_028AA8_PRIMGROUP_SIZE                                     0xFFFF0000
#define   S_028AA8_PARTIAL_VS_WAVE_ON(x)                              (((unsigned)(x) & 0x1) << 16)
#define   G_028AA8_PARTIAL_VS_WAVE_ON(x)                              (((x) >> 16) & 0x1)
#define   C_028AA8_PARTIAL_VS_WAVE_ON                                 0xFFFEFFFF
#define   S_028AA8_SWITCH_ON_EOP(x)                                   (((unsigned)(x) & 0x1) << 17)
#define   G_028AA8_SWITCH_ON_EOP(x)                                   (((x) >> 17) & 0x1)
#define   C_028AA8_SWITCH_ON_EOP                                      0xFFFDFFFF
#define   S_028AA8_PARTIAL_ES_WAVE_ON(x)                              (((unsigned)(x) & 0x1) << 18)
#define   G_028AA8_PARTIAL_ES_WAVE_ON(x)                              (((x) >> 18) & 0x1)
#define   C_028AA8_PARTIAL_ES_WAVE_ON                                 0xFFFBFFFF
#define   S_028AA8_SWITCH_ON_EOI(x)                                   (((unsigned)(x) & 0x1) << 19)
#define   G_028AA8_SWITCH_ON_EOI(x)                                   (((x) >> 19) & 0x1)
#define   C_028AA8_SWITCH_ON_EOI                                      0xFFF7FFFF
/* CIK */
#define   S_028AA8_WD_SWITCH_ON_EOP(x)                                (((unsigned)(x) & 0x1) << 20)
#define   G_028AA8_WD_SWITCH_ON_EOP(x)                                (((x) >> 20) & 0x1)
#define   C_028AA8_WD_SWITCH_ON_EOP                                   0xFFEFFFFF
/* VI */
#define   S_028AA8_MAX_PRIMGRP_IN_WAVE(x)                             (((unsigned)(x) & 0x0F) << 28)
#define   G_028AA8_MAX_PRIMGRP_IN_WAVE(x)                             (((x) >> 28) & 0x0F)
#define   C_028AA8_MAX_PRIMGRP_IN_WAVE                                0x0FFFFFFF
/*     */
#define R_028AAC_VGT_ESGS_RING_ITEMSIZE                                 0x028AAC
#define   S_028AAC_ITEMSIZE(x)                                        (((unsigned)(x) & 0x7FFF) << 0)
#define   G_028AAC_ITEMSIZE(x)                                        (((x) >> 0) & 0x7FFF)
#define   C_028AAC_ITEMSIZE                                           0xFFFF8000
#define R_028AB0_VGT_GSVS_RING_ITEMSIZE                                 0x028AB0
#define   S_028AB0_ITEMSIZE(x)                                        (((unsigned)(x) & 0x7FFF) << 0)
#define   G_028AB0_ITEMSIZE(x)                                        (((x) >> 0) & 0x7FFF)
#define   C_028AB0_ITEMSIZE                                           0xFFFF8000
#define R_028AB4_VGT_REUSE_OFF                                          0x028AB4
#define   S_028AB4_REUSE_OFF(x)                                       (((unsigned)(x) & 0x1) << 0)
#define   G_028AB4_REUSE_OFF(x)                                       (((x) >> 0) & 0x1)
#define   C_028AB4_REUSE_OFF                                          0xFFFFFFFE
#define R_028AB8_VGT_VTX_CNT_EN                                         0x028AB8
#define   S_028AB8_VTX_CNT_EN(x)                                      (((unsigned)(x) & 0x1) << 0)
#define   G_028AB8_VTX_CNT_EN(x)                                      (((x) >> 0) & 0x1)
#define   C_028AB8_VTX_CNT_EN                                         0xFFFFFFFE
#define R_028ABC_DB_HTILE_SURFACE                                       0x028ABC
#define   S_028ABC_LINEAR(x)                                          (((unsigned)(x) & 0x1) << 0)
#define   G_028ABC_LINEAR(x)                                          (((x) >> 0) & 0x1)
#define   C_028ABC_LINEAR                                             0xFFFFFFFE
#define   S_028ABC_FULL_CACHE(x)                                      (((unsigned)(x) & 0x1) << 1)
#define   G_028ABC_FULL_CACHE(x)                                      (((x) >> 1) & 0x1)
#define   C_028ABC_FULL_CACHE                                         0xFFFFFFFD
#define   S_028ABC_HTILE_USES_PRELOAD_WIN(x)                          (((unsigned)(x) & 0x1) << 2)
#define   G_028ABC_HTILE_USES_PRELOAD_WIN(x)                          (((x) >> 2) & 0x1)
#define   C_028ABC_HTILE_USES_PRELOAD_WIN                             0xFFFFFFFB
#define   S_028ABC_PRELOAD(x)                                         (((unsigned)(x) & 0x1) << 3)
#define   G_028ABC_PRELOAD(x)                                         (((x) >> 3) & 0x1)
#define   C_028ABC_PRELOAD                                            0xFFFFFFF7
#define   S_028ABC_PREFETCH_WIDTH(x)                                  (((unsigned)(x) & 0x3F) << 4)
#define   G_028ABC_PREFETCH_WIDTH(x)                                  (((x) >> 4) & 0x3F)
#define   C_028ABC_PREFETCH_WIDTH                                     0xFFFFFC0F
#define   S_028ABC_PREFETCH_HEIGHT(x)                                 (((unsigned)(x) & 0x3F) << 10)
#define   G_028ABC_PREFETCH_HEIGHT(x)                                 (((x) >> 10) & 0x3F)
#define   C_028ABC_PREFETCH_HEIGHT                                    0xFFFF03FF
#define   S_028ABC_DST_OUTSIDE_ZERO_TO_ONE(x)                         (((unsigned)(x) & 0x1) << 16)
#define   G_028ABC_DST_OUTSIDE_ZERO_TO_ONE(x)                         (((x) >> 16) & 0x1)
#define   C_028ABC_DST_OUTSIDE_ZERO_TO_ONE                            0xFFFEFFFF
/* VI */
#define   S_028ABC_TC_COMPATIBLE(x)                                   (((unsigned)(x) & 0x1) << 17)
#define   G_028ABC_TC_COMPATIBLE(x)                                   (((x) >> 17) & 0x1)
#define   C_028ABC_TC_COMPATIBLE                                      0xFFFDFFFF
/*    */
#define R_028AC0_DB_SRESULTS_COMPARE_STATE0                             0x028AC0
#define   S_028AC0_COMPAREFUNC0(x)                                    (((unsigned)(x) & 0x07) << 0)
#define   G_028AC0_COMPAREFUNC0(x)                                    (((x) >> 0) & 0x07)
#define   C_028AC0_COMPAREFUNC0                                       0xFFFFFFF8
#define     V_028AC0_REF_NEVER                                      0x00
#define     V_028AC0_REF_LESS                                       0x01
#define     V_028AC0_REF_EQUAL                                      0x02
#define     V_028AC0_REF_LEQUAL                                     0x03
#define     V_028AC0_REF_GREATER                                    0x04
#define     V_028AC0_REF_NOTEQUAL                                   0x05
#define     V_028AC0_REF_GEQUAL                                     0x06
#define     V_028AC0_REF_ALWAYS                                     0x07
#define   S_028AC0_COMPAREVALUE0(x)                                   (((unsigned)(x) & 0xFF) << 4)
#define   G_028AC0_COMPAREVALUE0(x)                                   (((x) >> 4) & 0xFF)
#define   C_028AC0_COMPAREVALUE0                                      0xFFFFF00F
#define   S_028AC0_COMPAREMASK0(x)                                    (((unsigned)(x) & 0xFF) << 12)
#define   G_028AC0_COMPAREMASK0(x)                                    (((x) >> 12) & 0xFF)
#define   C_028AC0_COMPAREMASK0                                       0xFFF00FFF
#define   S_028AC0_ENABLE0(x)                                         (((unsigned)(x) & 0x1) << 24)
#define   G_028AC0_ENABLE0(x)                                         (((x) >> 24) & 0x1)
#define   C_028AC0_ENABLE0                                            0xFEFFFFFF
#define R_028AC4_DB_SRESULTS_COMPARE_STATE1                             0x028AC4
#define   S_028AC4_COMPAREFUNC1(x)                                    (((unsigned)(x) & 0x07) << 0)
#define   G_028AC4_COMPAREFUNC1(x)                                    (((x) >> 0) & 0x07)
#define   C_028AC4_COMPAREFUNC1                                       0xFFFFFFF8
#define     V_028AC4_REF_NEVER                                      0x00
#define     V_028AC4_REF_LESS                                       0x01
#define     V_028AC4_REF_EQUAL                                      0x02
#define     V_028AC4_REF_LEQUAL                                     0x03
#define     V_028AC4_REF_GREATER                                    0x04
#define     V_028AC4_REF_NOTEQUAL                                   0x05
#define     V_028AC4_REF_GEQUAL                                     0x06
#define     V_028AC4_REF_ALWAYS                                     0x07
#define   S_028AC4_COMPAREVALUE1(x)                                   (((unsigned)(x) & 0xFF) << 4)
#define   G_028AC4_COMPAREVALUE1(x)                                   (((x) >> 4) & 0xFF)
#define   C_028AC4_COMPAREVALUE1                                      0xFFFFF00F
#define   S_028AC4_COMPAREMASK1(x)                                    (((unsigned)(x) & 0xFF) << 12)
#define   G_028AC4_COMPAREMASK1(x)                                    (((x) >> 12) & 0xFF)
#define   C_028AC4_COMPAREMASK1                                       0xFFF00FFF
#define   S_028AC4_ENABLE1(x)                                         (((unsigned)(x) & 0x1) << 24)
#define   G_028AC4_ENABLE1(x)                                         (((x) >> 24) & 0x1)
#define   C_028AC4_ENABLE1                                            0xFEFFFFFF
#define R_028AC8_DB_PRELOAD_CONTROL                                     0x028AC8
#define   S_028AC8_START_X(x)                                         (((unsigned)(x) & 0xFF) << 0)
#define   G_028AC8_START_X(x)                                         (((x) >> 0) & 0xFF)
#define   C_028AC8_START_X                                            0xFFFFFF00
#define   S_028AC8_START_Y(x)                                         (((unsigned)(x) & 0xFF) << 8)
#define   G_028AC8_START_Y(x)                                         (((x) >> 8) & 0xFF)
#define   C_028AC8_START_Y                                            0xFFFF00FF
#define   S_028AC8_MAX_X(x)                                           (((unsigned)(x) & 0xFF) << 16)
#define   G_028AC8_MAX_X(x)                                           (((x) >> 16) & 0xFF)
#define   C_028AC8_MAX_X                                              0xFF00FFFF
#define   S_028AC8_MAX_Y(x)                                           (((unsigned)(x) & 0xFF) << 24)
#define   G_028AC8_MAX_Y(x)                                           (((x) >> 24) & 0xFF)
#define   C_028AC8_MAX_Y                                              0x00FFFFFF
#define R_028AD0_VGT_STRMOUT_BUFFER_SIZE_0                              0x028AD0
#define R_028AD4_VGT_STRMOUT_VTX_STRIDE_0                               0x028AD4
#define   S_028AD4_STRIDE(x)                                          (((unsigned)(x) & 0x3FF) << 0)
#define   G_028AD4_STRIDE(x)                                          (((x) >> 0) & 0x3FF)
#define   C_028AD4_STRIDE                                             0xFFFFFC00
#define R_028ADC_VGT_STRMOUT_BUFFER_OFFSET_0                            0x028ADC
#define R_028AE0_VGT_STRMOUT_BUFFER_SIZE_1                              0x028AE0
#define R_028AE4_VGT_STRMOUT_VTX_STRIDE_1                               0x028AE4
#define   S_028AE4_STRIDE(x)                                          (((unsigned)(x) & 0x3FF) << 0)
#define   G_028AE4_STRIDE(x)                                          (((x) >> 0) & 0x3FF)
#define   C_028AE4_STRIDE                                             0xFFFFFC00
#define R_028AEC_VGT_STRMOUT_BUFFER_OFFSET_1                            0x028AEC
#define R_028AF0_VGT_STRMOUT_BUFFER_SIZE_2                              0x028AF0
#define R_028AF4_VGT_STRMOUT_VTX_STRIDE_2                               0x028AF4
#define   S_028AF4_STRIDE(x)                                          (((unsigned)(x) & 0x3FF) << 0)
#define   G_028AF4_STRIDE(x)                                          (((x) >> 0) & 0x3FF)
#define   C_028AF4_STRIDE                                             0xFFFFFC00
#define R_028AFC_VGT_STRMOUT_BUFFER_OFFSET_2                            0x028AFC
#define R_028B00_VGT_STRMOUT_BUFFER_SIZE_3                              0x028B00
#define R_028B04_VGT_STRMOUT_VTX_STRIDE_3                               0x028B04
#define   S_028B04_STRIDE(x)                                          (((unsigned)(x) & 0x3FF) << 0)
#define   G_028B04_STRIDE(x)                                          (((x) >> 0) & 0x3FF)
#define   C_028B04_STRIDE                                             0xFFFFFC00
#define R_028B0C_VGT_STRMOUT_BUFFER_OFFSET_3                            0x028B0C
#define R_028B28_VGT_STRMOUT_DRAW_OPAQUE_OFFSET                         0x028B28
#define R_028B2C_VGT_STRMOUT_DRAW_OPAQUE_BUFFER_FILLED_SIZE             0x028B2C
#define R_028B30_VGT_STRMOUT_DRAW_OPAQUE_VERTEX_STRIDE                  0x028B30
#define   S_028B30_VERTEX_STRIDE(x)                                   (((unsigned)(x) & 0x1FF) << 0)
#define   G_028B30_VERTEX_STRIDE(x)                                   (((x) >> 0) & 0x1FF)
#define   C_028B30_VERTEX_STRIDE                                      0xFFFFFE00
#define R_028B38_VGT_GS_MAX_VERT_OUT                                    0x028B38
#define   S_028B38_MAX_VERT_OUT(x)                                    (((unsigned)(x) & 0x7FF) << 0)
#define   G_028B38_MAX_VERT_OUT(x)                                    (((x) >> 0) & 0x7FF)
#define   C_028B38_MAX_VERT_OUT                                       0xFFFFF800
/* VI */
#define R_028B50_VGT_TESS_DISTRIBUTION                                  0x028B50
#define   S_028B50_ACCUM_ISOLINE(x)                                   (((unsigned)(x) & 0xFF) << 0)
#define   G_028B50_ACCUM_ISOLINE(x)                                   (((x) >> 0) & 0xFF)
#define   C_028B50_ACCUM_ISOLINE                                      0xFFFFFF00
#define   S_028B50_ACCUM_TRI(x)                                       (((unsigned)(x) & 0xFF) << 8)
#define   G_028B50_ACCUM_TRI(x)                                       (((x) >> 8) & 0xFF)
#define   C_028B50_ACCUM_TRI                                          0xFFFF00FF
#define   S_028B50_ACCUM_QUAD(x)                                      (((unsigned)(x) & 0xFF) << 16)
#define   G_028B50_ACCUM_QUAD(x)                                      (((x) >> 16) & 0xFF)
#define   C_028B50_ACCUM_QUAD                                         0xFF00FFFF
#define   S_028B50_DONUT_SPLIT(x)                                     (((unsigned)(x) & 0x1F) << 24)
#define   G_028B50_DONUT_SPLIT(x)                                     (((x) >> 24) & 0x1F)
#define   C_028B50_DONUT_SPLIT                                        0xE0FFFFFF
#define   S_028B50_TRAP_SPLIT(x)                                      (((unsigned)(x) & 0x7) << 29) /* Fiji+ */
#define   G_028B50_TRAP_SPLIT(x)                                      (((x) >> 29) & 0x7)
#define   C_028B50_TRAP_SPLIT                                         0x1FFFFFFF
/*    */
#define R_028B54_VGT_SHADER_STAGES_EN                                   0x028B54
#define   S_028B54_LS_EN(x)                                           (((unsigned)(x) & 0x03) << 0)
#define   G_028B54_LS_EN(x)                                           (((x) >> 0) & 0x03)
#define   C_028B54_LS_EN                                              0xFFFFFFFC
#define     V_028B54_LS_STAGE_OFF                                   0x00
#define     V_028B54_LS_STAGE_ON                                    0x01
#define     V_028B54_CS_STAGE_ON                                    0x02
#define   S_028B54_HS_EN(x)                                           (((unsigned)(x) & 0x1) << 2)
#define   G_028B54_HS_EN(x)                                           (((x) >> 2) & 0x1)
#define   C_028B54_HS_EN                                              0xFFFFFFFB
#define   S_028B54_ES_EN(x)                                           (((unsigned)(x) & 0x03) << 3)
#define   G_028B54_ES_EN(x)                                           (((x) >> 3) & 0x03)
#define   C_028B54_ES_EN                                              0xFFFFFFE7
#define     V_028B54_ES_STAGE_OFF                                   0x00
#define     V_028B54_ES_STAGE_DS                                    0x01
#define     V_028B54_ES_STAGE_REAL                                  0x02
#define   S_028B54_GS_EN(x)                                           (((unsigned)(x) & 0x1) << 5)
#define   G_028B54_GS_EN(x)                                           (((x) >> 5) & 0x1)
#define   C_028B54_GS_EN                                              0xFFFFFFDF
#define   S_028B54_VS_EN(x)                                           (((unsigned)(x) & 0x03) << 6)
#define   G_028B54_VS_EN(x)                                           (((x) >> 6) & 0x03)
#define   C_028B54_VS_EN                                              0xFFFFFF3F
#define     V_028B54_VS_STAGE_REAL                                  0x00
#define     V_028B54_VS_STAGE_DS                                    0x01
#define     V_028B54_VS_STAGE_COPY_SHADER                           0x02
#define   S_028B54_DYNAMIC_HS(x)                                      (((unsigned)(x) & 0x1) << 8)
#define   G_028B54_DYNAMIC_HS(x)                                      (((x) >> 8) & 0x1)
#define   C_028B54_DYNAMIC_HS                                         0xFFFFFEFF
/* VI */
#define   S_028B54_DISPATCH_DRAW_EN(x)                                (((unsigned)(x) & 0x1) << 9)
#define   G_028B54_DISPATCH_DRAW_EN(x)                                (((x) >> 9) & 0x1)
#define   C_028B54_DISPATCH_DRAW_EN                                   0xFFFFFDFF
#define   S_028B54_DIS_DEALLOC_ACCUM_0(x)                             (((unsigned)(x) & 0x1) << 10)
#define   G_028B54_DIS_DEALLOC_ACCUM_0(x)                             (((x) >> 10) & 0x1)
#define   C_028B54_DIS_DEALLOC_ACCUM_0                                0xFFFFFBFF
#define   S_028B54_DIS_DEALLOC_ACCUM_1(x)                             (((unsigned)(x) & 0x1) << 11)
#define   G_028B54_DIS_DEALLOC_ACCUM_1(x)                             (((x) >> 11) & 0x1)
#define   C_028B54_DIS_DEALLOC_ACCUM_1                                0xFFFFF7FF
#define   S_028B54_VS_WAVE_ID_EN(x)                                   (((unsigned)(x) & 0x1) << 12)
#define   G_028B54_VS_WAVE_ID_EN(x)                                   (((x) >> 12) & 0x1)
#define   C_028B54_VS_WAVE_ID_EN                                      0xFFFFEFFF
/*    */
#define R_028B58_VGT_LS_HS_CONFIG                                       0x028B58
#define   S_028B58_NUM_PATCHES(x)                                     (((unsigned)(x) & 0xFF) << 0)
#define   G_028B58_NUM_PATCHES(x)                                     (((x) >> 0) & 0xFF)
#define   C_028B58_NUM_PATCHES                                        0xFFFFFF00
#define   S_028B58_HS_NUM_INPUT_CP(x)                                 (((unsigned)(x) & 0x3F) << 8)
#define   G_028B58_HS_NUM_INPUT_CP(x)                                 (((x) >> 8) & 0x3F)
#define   C_028B58_HS_NUM_INPUT_CP                                    0xFFFFC0FF
#define   S_028B58_HS_NUM_OUTPUT_CP(x)                                (((unsigned)(x) & 0x3F) << 14)
#define   G_028B58_HS_NUM_OUTPUT_CP(x)                                (((x) >> 14) & 0x3F)
#define   C_028B58_HS_NUM_OUTPUT_CP                                   0xFFF03FFF
#define R_028B5C_VGT_GS_VERT_ITEMSIZE                                   0x028B5C
#define   S_028B5C_ITEMSIZE(x)                                        (((unsigned)(x) & 0x7FFF) << 0)
#define   G_028B5C_ITEMSIZE(x)                                        (((x) >> 0) & 0x7FFF)
#define   C_028B5C_ITEMSIZE                                           0xFFFF8000
#define R_028B60_VGT_GS_VERT_ITEMSIZE_1                                 0x028B60
#define   S_028B60_ITEMSIZE(x)                                        (((unsigned)(x) & 0x7FFF) << 0)
#define   G_028B60_ITEMSIZE(x)                                        (((x) >> 0) & 0x7FFF)
#define   C_028B60_ITEMSIZE                                           0xFFFF8000
#define R_028B64_VGT_GS_VERT_ITEMSIZE_2                                 0x028B64
#define   S_028B64_ITEMSIZE(x)                                        (((unsigned)(x) & 0x7FFF) << 0)
#define   G_028B64_ITEMSIZE(x)                                        (((x) >> 0) & 0x7FFF)
#define   C_028B64_ITEMSIZE                                           0xFFFF8000
#define R_028B68_VGT_GS_VERT_ITEMSIZE_3                                 0x028B68
#define   S_028B68_ITEMSIZE(x)                                        (((unsigned)(x) & 0x7FFF) << 0)
#define   G_028B68_ITEMSIZE(x)                                        (((x) >> 0) & 0x7FFF)
#define   C_028B68_ITEMSIZE                                           0xFFFF8000
#define R_028B6C_VGT_TF_PARAM                                           0x028B6C
#define   S_028B6C_TYPE(x)                                            (((unsigned)(x) & 0x03) << 0)
#define   G_028B6C_TYPE(x)                                            (((x) >> 0) & 0x03)
#define   C_028B6C_TYPE                                               0xFFFFFFFC
#define     V_028B6C_TESS_ISOLINE                                   0x00
#define     V_028B6C_TESS_TRIANGLE                                  0x01
#define     V_028B6C_TESS_QUAD                                      0x02
#define   S_028B6C_PARTITIONING(x)                                    (((unsigned)(x) & 0x07) << 2)
#define   G_028B6C_PARTITIONING(x)                                    (((x) >> 2) & 0x07)
#define   C_028B6C_PARTITIONING                                       0xFFFFFFE3
#define     V_028B6C_PART_INTEGER                                   0x00
#define     V_028B6C_PART_POW2                                      0x01
#define     V_028B6C_PART_FRAC_ODD                                  0x02
#define     V_028B6C_PART_FRAC_EVEN                                 0x03
#define   S_028B6C_TOPOLOGY(x)                                        (((unsigned)(x) & 0x07) << 5)
#define   G_028B6C_TOPOLOGY(x)                                        (((x) >> 5) & 0x07)
#define   C_028B6C_TOPOLOGY                                           0xFFFFFF1F
#define     V_028B6C_OUTPUT_POINT                                   0x00
#define     V_028B6C_OUTPUT_LINE                                    0x01
#define     V_028B6C_OUTPUT_TRIANGLE_CW                             0x02
#define     V_028B6C_OUTPUT_TRIANGLE_CCW                            0x03
#define   S_028B6C_RESERVED_REDUC_AXIS(x)                             (((unsigned)(x) & 0x1) << 8) /* not on CIK */
#define   G_028B6C_RESERVED_REDUC_AXIS(x)                             (((x) >> 8) & 0x1) /* not on CIK */
#define   C_028B6C_RESERVED_REDUC_AXIS                                0xFFFFFEFF /* not on CIK */
#define   S_028B6C_DEPRECATED(x)                                      (((unsigned)(x) & 0x1) << 9)
#define   G_028B6C_DEPRECATED(x)                                      (((x) >> 9) & 0x1)
#define   C_028B6C_DEPRECATED                                         0xFFFFFDFF
#define   S_028B6C_NUM_DS_WAVES_PER_SIMD(x)                           (((unsigned)(x) & 0x0F) << 10)
#define   G_028B6C_NUM_DS_WAVES_PER_SIMD(x)                           (((x) >> 10) & 0x0F)
#define   C_028B6C_NUM_DS_WAVES_PER_SIMD                              0xFFFFC3FF
#define   S_028B6C_DISABLE_DONUTS(x)                                  (((unsigned)(x) & 0x1) << 14)
#define   G_028B6C_DISABLE_DONUTS(x)                                  (((x) >> 14) & 0x1)
#define   C_028B6C_DISABLE_DONUTS                                     0xFFFFBFFF
/* CIK */
#define   S_028B6C_RDREQ_POLICY(x)                                    (((unsigned)(x) & 0x03) << 15)
#define   G_028B6C_RDREQ_POLICY(x)                                    (((x) >> 15) & 0x03)
#define   C_028B6C_RDREQ_POLICY                                       0xFFFE7FFF
#define     V_028B6C_VGT_POLICY_LRU                                 0x00
#define     V_028B6C_VGT_POLICY_STREAM                              0x01
#define     V_028B6C_VGT_POLICY_BYPASS                              0x02
/*     */
/* VI */
#define   S_028B6C_RDREQ_POLICY_VI(x)                                 (((unsigned)(x) & 0x1) << 15)
#define   G_028B6C_RDREQ_POLICY_VI(x)                                 (((x) >> 15) & 0x1)
#define   C_028B6C_RDREQ_POLICY_VI                                    0xFFFF7FFF
#define   S_028B6C_DISTRIBUTION_MODE(x)                               (((unsigned)(x) & 0x03) << 17)
#define   G_028B6C_DISTRIBUTION_MODE(x)                               (((x) >> 17) & 0x03)
#define   C_028B6C_DISTRIBUTION_MODE                                  0xFFF9FFFF
#define     V_028B6C_DISTRIBUTION_MODE_NO_DIST                      0x00
#define     V_028B6C_DISTRIBUTION_MODE_PATCHES                      0x01
#define     V_028B6C_DISTRIBUTION_MODE_DONUTS                       0x02
#define     V_028B6C_DISTRIBUTION_MODE_TRAPEZOIDS                   0x03 /* Fiji+ */
#define   S_028B6C_MTYPE(x)                                           (((unsigned)(x) & 0x03) << 19)
#define   G_028B6C_MTYPE(x)                                           (((x) >> 19) & 0x03)
#define   C_028B6C_MTYPE                                              0xFFE7FFFF
/*    */
#define R_028B70_DB_ALPHA_TO_MASK                                       0x028B70
#define   S_028B70_ALPHA_TO_MASK_ENABLE(x)                            (((unsigned)(x) & 0x1) << 0)
#define   G_028B70_ALPHA_TO_MASK_ENABLE(x)                            (((x) >> 0) & 0x1)
#define   C_028B70_ALPHA_TO_MASK_ENABLE                               0xFFFFFFFE
#define   S_028B70_ALPHA_TO_MASK_OFFSET0(x)                           (((unsigned)(x) & 0x03) << 8)
#define   G_028B70_ALPHA_TO_MASK_OFFSET0(x)                           (((x) >> 8) & 0x03)
#define   C_028B70_ALPHA_TO_MASK_OFFSET0                              0xFFFFFCFF
#define   S_028B70_ALPHA_TO_MASK_OFFSET1(x)                           (((unsigned)(x) & 0x03) << 10)
#define   G_028B70_ALPHA_TO_MASK_OFFSET1(x)                           (((x) >> 10) & 0x03)
#define   C_028B70_ALPHA_TO_MASK_OFFSET1                              0xFFFFF3FF
#define   S_028B70_ALPHA_TO_MASK_OFFSET2(x)                           (((unsigned)(x) & 0x03) << 12)
#define   G_028B70_ALPHA_TO_MASK_OFFSET2(x)                           (((x) >> 12) & 0x03)
#define   C_028B70_ALPHA_TO_MASK_OFFSET2                              0xFFFFCFFF
#define   S_028B70_ALPHA_TO_MASK_OFFSET3(x)                           (((unsigned)(x) & 0x03) << 14)
#define   G_028B70_ALPHA_TO_MASK_OFFSET3(x)                           (((x) >> 14) & 0x03)
#define   C_028B70_ALPHA_TO_MASK_OFFSET3                              0xFFFF3FFF
#define   S_028B70_OFFSET_ROUND(x)                                    (((unsigned)(x) & 0x1) << 16)
#define   G_028B70_OFFSET_ROUND(x)                                    (((x) >> 16) & 0x1)
#define   C_028B70_OFFSET_ROUND                                       0xFFFEFFFF
/* CIK */
#define R_028B74_VGT_DISPATCH_DRAW_INDEX                                0x028B74
/*     */
#define R_028B78_PA_SU_POLY_OFFSET_DB_FMT_CNTL                          0x028B78
#define   S_028B78_POLY_OFFSET_NEG_NUM_DB_BITS(x)                     (((unsigned)(x) & 0xFF) << 0)
#define   G_028B78_POLY_OFFSET_NEG_NUM_DB_BITS(x)                     (((x) >> 0) & 0xFF)
#define   C_028B78_POLY_OFFSET_NEG_NUM_DB_BITS                        0xFFFFFF00
#define   S_028B78_POLY_OFFSET_DB_IS_FLOAT_FMT(x)                     (((unsigned)(x) & 0x1) << 8)
#define   G_028B78_POLY_OFFSET_DB_IS_FLOAT_FMT(x)                     (((x) >> 8) & 0x1)
#define   C_028B78_POLY_OFFSET_DB_IS_FLOAT_FMT                        0xFFFFFEFF
#define R_028B7C_PA_SU_POLY_OFFSET_CLAMP                                0x028B7C
#define R_028B80_PA_SU_POLY_OFFSET_FRONT_SCALE                          0x028B80
#define R_028B84_PA_SU_POLY_OFFSET_FRONT_OFFSET                         0x028B84
#define R_028B88_PA_SU_POLY_OFFSET_BACK_SCALE                           0x028B88
#define R_028B8C_PA_SU_POLY_OFFSET_BACK_OFFSET                          0x028B8C
#define R_028B90_VGT_GS_INSTANCE_CNT                                    0x028B90
#define   S_028B90_ENABLE(x)                                          (((unsigned)(x) & 0x1) << 0)
#define   G_028B90_ENABLE(x)                                          (((x) >> 0) & 0x1)
#define   C_028B90_ENABLE                                             0xFFFFFFFE
#define   S_028B90_CNT(x)                                             (((unsigned)(x) & 0x7F) << 2)
#define   G_028B90_CNT(x)                                             (((x) >> 2) & 0x7F)
#define   C_028B90_CNT                                                0xFFFFFE03
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
#define   S_028B94_RAST_STREAM_MASK(x)                                (((unsigned)(x) & 0x0F) << 8)
#define   G_028B94_RAST_STREAM_MASK(x)                                (((x) >> 8) & 0x0F)
#define   C_028B94_RAST_STREAM_MASK                                   0xFFFFF0FF
#define   S_028B94_USE_RAST_STREAM_MASK(x)                            (((unsigned)(x) & 0x1) << 31)
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
#define R_028BD4_PA_SC_CENTROID_PRIORITY_0                              0x028BD4
#define   S_028BD4_DISTANCE_0(x)                                      (((unsigned)(x) & 0x0F) << 0)
#define   G_028BD4_DISTANCE_0(x)                                      (((x) >> 0) & 0x0F)
#define   C_028BD4_DISTANCE_0                                         0xFFFFFFF0
#define   S_028BD4_DISTANCE_1(x)                                      (((unsigned)(x) & 0x0F) << 4)
#define   G_028BD4_DISTANCE_1(x)                                      (((x) >> 4) & 0x0F)
#define   C_028BD4_DISTANCE_1                                         0xFFFFFF0F
#define   S_028BD4_DISTANCE_2(x)                                      (((unsigned)(x) & 0x0F) << 8)
#define   G_028BD4_DISTANCE_2(x)                                      (((x) >> 8) & 0x0F)
#define   C_028BD4_DISTANCE_2                                         0xFFFFF0FF
#define   S_028BD4_DISTANCE_3(x)                                      (((unsigned)(x) & 0x0F) << 12)
#define   G_028BD4_DISTANCE_3(x)                                      (((x) >> 12) & 0x0F)
#define   C_028BD4_DISTANCE_3                                         0xFFFF0FFF
#define   S_028BD4_DISTANCE_4(x)                                      (((unsigned)(x) & 0x0F) << 16)
#define   G_028BD4_DISTANCE_4(x)                                      (((x) >> 16) & 0x0F)
#define   C_028BD4_DISTANCE_4                                         0xFFF0FFFF
#define   S_028BD4_DISTANCE_5(x)                                      (((unsigned)(x) & 0x0F) << 20)
#define   G_028BD4_DISTANCE_5(x)                                      (((x) >> 20) & 0x0F)
#define   C_028BD4_DISTANCE_5                                         0xFF0FFFFF
#define   S_028BD4_DISTANCE_6(x)                                      (((unsigned)(x) & 0x0F) << 24)
#define   G_028BD4_DISTANCE_6(x)                                      (((x) >> 24) & 0x0F)
#define   C_028BD4_DISTANCE_6                                         0xF0FFFFFF
#define   S_028BD4_DISTANCE_7(x)                                      (((unsigned)(x) & 0x0F) << 28)
#define   G_028BD4_DISTANCE_7(x)                                      (((x) >> 28) & 0x0F)
#define   C_028BD4_DISTANCE_7                                         0x0FFFFFFF
#define R_028BD8_PA_SC_CENTROID_PRIORITY_1                              0x028BD8
#define   S_028BD8_DISTANCE_8(x)                                      (((unsigned)(x) & 0x0F) << 0)
#define   G_028BD8_DISTANCE_8(x)                                      (((x) >> 0) & 0x0F)
#define   C_028BD8_DISTANCE_8                                         0xFFFFFFF0
#define   S_028BD8_DISTANCE_9(x)                                      (((unsigned)(x) & 0x0F) << 4)
#define   G_028BD8_DISTANCE_9(x)                                      (((x) >> 4) & 0x0F)
#define   C_028BD8_DISTANCE_9                                         0xFFFFFF0F
#define   S_028BD8_DISTANCE_10(x)                                     (((unsigned)(x) & 0x0F) << 8)
#define   G_028BD8_DISTANCE_10(x)                                     (((x) >> 8) & 0x0F)
#define   C_028BD8_DISTANCE_10                                        0xFFFFF0FF
#define   S_028BD8_DISTANCE_11(x)                                     (((unsigned)(x) & 0x0F) << 12)
#define   G_028BD8_DISTANCE_11(x)                                     (((x) >> 12) & 0x0F)
#define   C_028BD8_DISTANCE_11                                        0xFFFF0FFF
#define   S_028BD8_DISTANCE_12(x)                                     (((unsigned)(x) & 0x0F) << 16)
#define   G_028BD8_DISTANCE_12(x)                                     (((x) >> 16) & 0x0F)
#define   C_028BD8_DISTANCE_12                                        0xFFF0FFFF
#define   S_028BD8_DISTANCE_13(x)                                     (((unsigned)(x) & 0x0F) << 20)
#define   G_028BD8_DISTANCE_13(x)                                     (((x) >> 20) & 0x0F)
#define   C_028BD8_DISTANCE_13                                        0xFF0FFFFF
#define   S_028BD8_DISTANCE_14(x)                                     (((unsigned)(x) & 0x0F) << 24)
#define   G_028BD8_DISTANCE_14(x)                                     (((x) >> 24) & 0x0F)
#define   C_028BD8_DISTANCE_14                                        0xF0FFFFFF
#define   S_028BD8_DISTANCE_15(x)                                     (((unsigned)(x) & 0x0F) << 28)
#define   G_028BD8_DISTANCE_15(x)                                     (((x) >> 28) & 0x0F)
#define   C_028BD8_DISTANCE_15                                        0x0FFFFFFF
#define R_028BDC_PA_SC_LINE_CNTL                                        0x028BDC
#define   S_028BDC_EXPAND_LINE_WIDTH(x)                               (((unsigned)(x) & 0x1) << 9)
#define   G_028BDC_EXPAND_LINE_WIDTH(x)                               (((x) >> 9) & 0x1)
#define   C_028BDC_EXPAND_LINE_WIDTH                                  0xFFFFFDFF
#define   S_028BDC_LAST_PIXEL(x)                                      (((unsigned)(x) & 0x1) << 10)
#define   G_028BDC_LAST_PIXEL(x)                                      (((x) >> 10) & 0x1)
#define   C_028BDC_LAST_PIXEL                                         0xFFFFFBFF
#define   S_028BDC_PERPENDICULAR_ENDCAP_ENA(x)                        (((unsigned)(x) & 0x1) << 11)
#define   G_028BDC_PERPENDICULAR_ENDCAP_ENA(x)                        (((x) >> 11) & 0x1)
#define   C_028BDC_PERPENDICULAR_ENDCAP_ENA                           0xFFFFF7FF
#define   S_028BDC_DX10_DIAMOND_TEST_ENA(x)                           (((unsigned)(x) & 0x1) << 12)
#define   G_028BDC_DX10_DIAMOND_TEST_ENA(x)                           (((x) >> 12) & 0x1)
#define   C_028BDC_DX10_DIAMOND_TEST_ENA                              0xFFFFEFFF
#define R_028BE0_PA_SC_AA_CONFIG                                        0x028BE0
#define   S_028BE0_MSAA_NUM_SAMPLES(x)                                (((unsigned)(x) & 0x7) << 0)
#define   G_028BE0_MSAA_NUM_SAMPLES(x)                                (((x) >> 0) & 0x07)
#define   C_028BE0_MSAA_NUM_SAMPLES                                   0xFFFFFFF8
#define   S_028BE0_AA_MASK_CENTROID_DTMN(x)                           (((unsigned)(x) & 0x1) << 4)
#define   G_028BE0_AA_MASK_CENTROID_DTMN(x)                           (((x) >> 4) & 0x1)
#define   C_028BE0_AA_MASK_CENTROID_DTMN                              0xFFFFFFEF
#define   S_028BE0_MAX_SAMPLE_DIST(x)                                 (((unsigned)(x) & 0xf) << 13)
#define   G_028BE0_MAX_SAMPLE_DIST(x)                                 (((x) >> 13) & 0x0F)
#define   C_028BE0_MAX_SAMPLE_DIST                                    0xFFFE1FFF
#define   S_028BE0_MSAA_EXPOSED_SAMPLES(x)                            (((unsigned)(x) & 0x7) << 20)
#define   G_028BE0_MSAA_EXPOSED_SAMPLES(x)                            (((x) >> 20) & 0x07)
#define   C_028BE0_MSAA_EXPOSED_SAMPLES                               0xFF8FFFFF
#define   S_028BE0_DETAIL_TO_EXPOSED_MODE(x)                          (((unsigned)(x) & 0x3) << 24)
#define   G_028BE0_DETAIL_TO_EXPOSED_MODE(x)                          (((x) >> 24) & 0x03)
#define   C_028BE0_DETAIL_TO_EXPOSED_MODE                             0xFCFFFFFF
#define R_028BE4_PA_SU_VTX_CNTL                                         0x028BE4
#define   S_028BE4_PIX_CENTER(x)                                      (((unsigned)(x) & 0x1) << 0)
#define   G_028BE4_PIX_CENTER(x)                                      (((x) >> 0) & 0x1)
#define   C_028BE4_PIX_CENTER                                         0xFFFFFFFE
#define   S_028BE4_ROUND_MODE(x)                                      (((unsigned)(x) & 0x03) << 1)
#define   G_028BE4_ROUND_MODE(x)                                      (((x) >> 1) & 0x03)
#define   C_028BE4_ROUND_MODE                                         0xFFFFFFF9
#define     V_028BE4_X_TRUNCATE                                     0x00
#define     V_028BE4_X_ROUND                                        0x01
#define     V_028BE4_X_ROUND_TO_EVEN                                0x02
#define     V_028BE4_X_ROUND_TO_ODD                                 0x03
#define   S_028BE4_QUANT_MODE(x)                                      (((unsigned)(x) & 0x07) << 3)
#define   G_028BE4_QUANT_MODE(x)                                      (((x) >> 3) & 0x07)
#define   C_028BE4_QUANT_MODE                                         0xFFFFFFC7
#define     V_028BE4_X_16_8_FIXED_POINT_1_16TH                      0x00
#define     V_028BE4_X_16_8_FIXED_POINT_1_8TH                       0x01
#define     V_028BE4_X_16_8_FIXED_POINT_1_4TH                       0x02
#define     V_028BE4_X_16_8_FIXED_POINT_1_2                         0x03
#define     V_028BE4_X_16_8_FIXED_POINT_1                           0x04
#define     V_028BE4_X_16_8_FIXED_POINT_1_256TH                     0x05
#define     V_028BE4_X_14_10_FIXED_POINT_1_1024TH                   0x06
#define     V_028BE4_X_12_12_FIXED_POINT_1_4096TH                   0x07
#define R_028BE8_PA_CL_GB_VERT_CLIP_ADJ                                 0x028BE8
#define R_028BEC_PA_CL_GB_VERT_DISC_ADJ                                 0x028BEC
#define R_028BF0_PA_CL_GB_HORZ_CLIP_ADJ                                 0x028BF0
#define R_028BF4_PA_CL_GB_HORZ_DISC_ADJ                                 0x028BF4
#define R_028BF8_PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y0_0                      0x028BF8
#define   S_028BF8_S0_X(x)                                            (((unsigned)(x) & 0x0F) << 0)
#define   G_028BF8_S0_X(x)                                            (((x) >> 0) & 0x0F)
#define   C_028BF8_S0_X                                               0xFFFFFFF0
#define   S_028BF8_S0_Y(x)                                            (((unsigned)(x) & 0x0F) << 4)
#define   G_028BF8_S0_Y(x)                                            (((x) >> 4) & 0x0F)
#define   C_028BF8_S0_Y                                               0xFFFFFF0F
#define   S_028BF8_S1_X(x)                                            (((unsigned)(x) & 0x0F) << 8)
#define   G_028BF8_S1_X(x)                                            (((x) >> 8) & 0x0F)
#define   C_028BF8_S1_X                                               0xFFFFF0FF
#define   S_028BF8_S1_Y(x)                                            (((unsigned)(x) & 0x0F) << 12)
#define   G_028BF8_S1_Y(x)                                            (((x) >> 12) & 0x0F)
#define   C_028BF8_S1_Y                                               0xFFFF0FFF
#define   S_028BF8_S2_X(x)                                            (((unsigned)(x) & 0x0F) << 16)
#define   G_028BF8_S2_X(x)                                            (((x) >> 16) & 0x0F)
#define   C_028BF8_S2_X                                               0xFFF0FFFF
#define   S_028BF8_S2_Y(x)                                            (((unsigned)(x) & 0x0F) << 20)
#define   G_028BF8_S2_Y(x)                                            (((x) >> 20) & 0x0F)
#define   C_028BF8_S2_Y                                               0xFF0FFFFF
#define   S_028BF8_S3_X(x)                                            (((unsigned)(x) & 0x0F) << 24)
#define   G_028BF8_S3_X(x)                                            (((x) >> 24) & 0x0F)
#define   C_028BF8_S3_X                                               0xF0FFFFFF
#define   S_028BF8_S3_Y(x)                                            (((unsigned)(x) & 0x0F) << 28)
#define   G_028BF8_S3_Y(x)                                            (((x) >> 28) & 0x0F)
#define   C_028BF8_S3_Y                                               0x0FFFFFFF
#define R_028BFC_PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y0_1                      0x028BFC
#define   S_028BFC_S4_X(x)                                            (((unsigned)(x) & 0x0F) << 0)
#define   G_028BFC_S4_X(x)                                            (((x) >> 0) & 0x0F)
#define   C_028BFC_S4_X                                               0xFFFFFFF0
#define   S_028BFC_S4_Y(x)                                            (((unsigned)(x) & 0x0F) << 4)
#define   G_028BFC_S4_Y(x)                                            (((x) >> 4) & 0x0F)
#define   C_028BFC_S4_Y                                               0xFFFFFF0F
#define   S_028BFC_S5_X(x)                                            (((unsigned)(x) & 0x0F) << 8)
#define   G_028BFC_S5_X(x)                                            (((x) >> 8) & 0x0F)
#define   C_028BFC_S5_X                                               0xFFFFF0FF
#define   S_028BFC_S5_Y(x)                                            (((unsigned)(x) & 0x0F) << 12)
#define   G_028BFC_S5_Y(x)                                            (((x) >> 12) & 0x0F)
#define   C_028BFC_S5_Y                                               0xFFFF0FFF
#define   S_028BFC_S6_X(x)                                            (((unsigned)(x) & 0x0F) << 16)
#define   G_028BFC_S6_X(x)                                            (((x) >> 16) & 0x0F)
#define   C_028BFC_S6_X                                               0xFFF0FFFF
#define   S_028BFC_S6_Y(x)                                            (((unsigned)(x) & 0x0F) << 20)
#define   G_028BFC_S6_Y(x)                                            (((x) >> 20) & 0x0F)
#define   C_028BFC_S6_Y                                               0xFF0FFFFF
#define   S_028BFC_S7_X(x)                                            (((unsigned)(x) & 0x0F) << 24)
#define   G_028BFC_S7_X(x)                                            (((x) >> 24) & 0x0F)
#define   C_028BFC_S7_X                                               0xF0FFFFFF
#define   S_028BFC_S7_Y(x)                                            (((unsigned)(x) & 0x0F) << 28)
#define   G_028BFC_S7_Y(x)                                            (((x) >> 28) & 0x0F)
#define   C_028BFC_S7_Y                                               0x0FFFFFFF
#define R_028C00_PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y0_2                      0x028C00
#define   S_028C00_S8_X(x)                                            (((unsigned)(x) & 0x0F) << 0)
#define   G_028C00_S8_X(x)                                            (((x) >> 0) & 0x0F)
#define   C_028C00_S8_X                                               0xFFFFFFF0
#define   S_028C00_S8_Y(x)                                            (((unsigned)(x) & 0x0F) << 4)
#define   G_028C00_S8_Y(x)                                            (((x) >> 4) & 0x0F)
#define   C_028C00_S8_Y                                               0xFFFFFF0F
#define   S_028C00_S9_X(x)                                            (((unsigned)(x) & 0x0F) << 8)
#define   G_028C00_S9_X(x)                                            (((x) >> 8) & 0x0F)
#define   C_028C00_S9_X                                               0xFFFFF0FF
#define   S_028C00_S9_Y(x)                                            (((unsigned)(x) & 0x0F) << 12)
#define   G_028C00_S9_Y(x)                                            (((x) >> 12) & 0x0F)
#define   C_028C00_S9_Y                                               0xFFFF0FFF
#define   S_028C00_S10_X(x)                                           (((unsigned)(x) & 0x0F) << 16)
#define   G_028C00_S10_X(x)                                           (((x) >> 16) & 0x0F)
#define   C_028C00_S10_X                                              0xFFF0FFFF
#define   S_028C00_S10_Y(x)                                           (((unsigned)(x) & 0x0F) << 20)
#define   G_028C00_S10_Y(x)                                           (((x) >> 20) & 0x0F)
#define   C_028C00_S10_Y                                              0xFF0FFFFF
#define   S_028C00_S11_X(x)                                           (((unsigned)(x) & 0x0F) << 24)
#define   G_028C00_S11_X(x)                                           (((x) >> 24) & 0x0F)
#define   C_028C00_S11_X                                              0xF0FFFFFF
#define   S_028C00_S11_Y(x)                                           (((unsigned)(x) & 0x0F) << 28)
#define   G_028C00_S11_Y(x)                                           (((x) >> 28) & 0x0F)
#define   C_028C00_S11_Y                                              0x0FFFFFFF
#define R_028C04_PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y0_3                      0x028C04
#define   S_028C04_S12_X(x)                                           (((unsigned)(x) & 0x0F) << 0)
#define   G_028C04_S12_X(x)                                           (((x) >> 0) & 0x0F)
#define   C_028C04_S12_X                                              0xFFFFFFF0
#define   S_028C04_S12_Y(x)                                           (((unsigned)(x) & 0x0F) << 4)
#define   G_028C04_S12_Y(x)                                           (((x) >> 4) & 0x0F)
#define   C_028C04_S12_Y                                              0xFFFFFF0F
#define   S_028C04_S13_X(x)                                           (((unsigned)(x) & 0x0F) << 8)
#define   G_028C04_S13_X(x)                                           (((x) >> 8) & 0x0F)
#define   C_028C04_S13_X                                              0xFFFFF0FF
#define   S_028C04_S13_Y(x)                                           (((unsigned)(x) & 0x0F) << 12)
#define   G_028C04_S13_Y(x)                                           (((x) >> 12) & 0x0F)
#define   C_028C04_S13_Y                                              0xFFFF0FFF
#define   S_028C04_S14_X(x)                                           (((unsigned)(x) & 0x0F) << 16)
#define   G_028C04_S14_X(x)                                           (((x) >> 16) & 0x0F)
#define   C_028C04_S14_X                                              0xFFF0FFFF
#define   S_028C04_S14_Y(x)                                           (((unsigned)(x) & 0x0F) << 20)
#define   G_028C04_S14_Y(x)                                           (((x) >> 20) & 0x0F)
#define   C_028C04_S14_Y                                              0xFF0FFFFF
#define   S_028C04_S15_X(x)                                           (((unsigned)(x) & 0x0F) << 24)
#define   G_028C04_S15_X(x)                                           (((x) >> 24) & 0x0F)
#define   C_028C04_S15_X                                              0xF0FFFFFF
#define   S_028C04_S15_Y(x)                                           (((unsigned)(x) & 0x0F) << 28)
#define   G_028C04_S15_Y(x)                                           (((x) >> 28) & 0x0F)
#define   C_028C04_S15_Y                                              0x0FFFFFFF
#define R_028C08_PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y0_0                      0x028C08
#define   S_028C08_S0_X(x)                                            (((unsigned)(x) & 0x0F) << 0)
#define   G_028C08_S0_X(x)                                            (((x) >> 0) & 0x0F)
#define   C_028C08_S0_X                                               0xFFFFFFF0
#define   S_028C08_S0_Y(x)                                            (((unsigned)(x) & 0x0F) << 4)
#define   G_028C08_S0_Y(x)                                            (((x) >> 4) & 0x0F)
#define   C_028C08_S0_Y                                               0xFFFFFF0F
#define   S_028C08_S1_X(x)                                            (((unsigned)(x) & 0x0F) << 8)
#define   G_028C08_S1_X(x)                                            (((x) >> 8) & 0x0F)
#define   C_028C08_S1_X                                               0xFFFFF0FF
#define   S_028C08_S1_Y(x)                                            (((unsigned)(x) & 0x0F) << 12)
#define   G_028C08_S1_Y(x)                                            (((x) >> 12) & 0x0F)
#define   C_028C08_S1_Y                                               0xFFFF0FFF
#define   S_028C08_S2_X(x)                                            (((unsigned)(x) & 0x0F) << 16)
#define   G_028C08_S2_X(x)                                            (((x) >> 16) & 0x0F)
#define   C_028C08_S2_X                                               0xFFF0FFFF
#define   S_028C08_S2_Y(x)                                            (((unsigned)(x) & 0x0F) << 20)
#define   G_028C08_S2_Y(x)                                            (((x) >> 20) & 0x0F)
#define   C_028C08_S2_Y                                               0xFF0FFFFF
#define   S_028C08_S3_X(x)                                            (((unsigned)(x) & 0x0F) << 24)
#define   G_028C08_S3_X(x)                                            (((x) >> 24) & 0x0F)
#define   C_028C08_S3_X                                               0xF0FFFFFF
#define   S_028C08_S3_Y(x)                                            (((unsigned)(x) & 0x0F) << 28)
#define   G_028C08_S3_Y(x)                                            (((x) >> 28) & 0x0F)
#define   C_028C08_S3_Y                                               0x0FFFFFFF
#define R_028C0C_PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y0_1                      0x028C0C
#define   S_028C0C_S4_X(x)                                            (((unsigned)(x) & 0x0F) << 0)
#define   G_028C0C_S4_X(x)                                            (((x) >> 0) & 0x0F)
#define   C_028C0C_S4_X                                               0xFFFFFFF0
#define   S_028C0C_S4_Y(x)                                            (((unsigned)(x) & 0x0F) << 4)
#define   G_028C0C_S4_Y(x)                                            (((x) >> 4) & 0x0F)
#define   C_028C0C_S4_Y                                               0xFFFFFF0F
#define   S_028C0C_S5_X(x)                                            (((unsigned)(x) & 0x0F) << 8)
#define   G_028C0C_S5_X(x)                                            (((x) >> 8) & 0x0F)
#define   C_028C0C_S5_X                                               0xFFFFF0FF
#define   S_028C0C_S5_Y(x)                                            (((unsigned)(x) & 0x0F) << 12)
#define   G_028C0C_S5_Y(x)                                            (((x) >> 12) & 0x0F)
#define   C_028C0C_S5_Y                                               0xFFFF0FFF
#define   S_028C0C_S6_X(x)                                            (((unsigned)(x) & 0x0F) << 16)
#define   G_028C0C_S6_X(x)                                            (((x) >> 16) & 0x0F)
#define   C_028C0C_S6_X                                               0xFFF0FFFF
#define   S_028C0C_S6_Y(x)                                            (((unsigned)(x) & 0x0F) << 20)
#define   G_028C0C_S6_Y(x)                                            (((x) >> 20) & 0x0F)
#define   C_028C0C_S6_Y                                               0xFF0FFFFF
#define   S_028C0C_S7_X(x)                                            (((unsigned)(x) & 0x0F) << 24)
#define   G_028C0C_S7_X(x)                                            (((x) >> 24) & 0x0F)
#define   C_028C0C_S7_X                                               0xF0FFFFFF
#define   S_028C0C_S7_Y(x)                                            (((unsigned)(x) & 0x0F) << 28)
#define   G_028C0C_S7_Y(x)                                            (((x) >> 28) & 0x0F)
#define   C_028C0C_S7_Y                                               0x0FFFFFFF
#define R_028C10_PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y0_2                      0x028C10
#define   S_028C10_S8_X(x)                                            (((unsigned)(x) & 0x0F) << 0)
#define   G_028C10_S8_X(x)                                            (((x) >> 0) & 0x0F)
#define   C_028C10_S8_X                                               0xFFFFFFF0
#define   S_028C10_S8_Y(x)                                            (((unsigned)(x) & 0x0F) << 4)
#define   G_028C10_S8_Y(x)                                            (((x) >> 4) & 0x0F)
#define   C_028C10_S8_Y                                               0xFFFFFF0F
#define   S_028C10_S9_X(x)                                            (((unsigned)(x) & 0x0F) << 8)
#define   G_028C10_S9_X(x)                                            (((x) >> 8) & 0x0F)
#define   C_028C10_S9_X                                               0xFFFFF0FF
#define   S_028C10_S9_Y(x)                                            (((unsigned)(x) & 0x0F) << 12)
#define   G_028C10_S9_Y(x)                                            (((x) >> 12) & 0x0F)
#define   C_028C10_S9_Y                                               0xFFFF0FFF
#define   S_028C10_S10_X(x)                                           (((unsigned)(x) & 0x0F) << 16)
#define   G_028C10_S10_X(x)                                           (((x) >> 16) & 0x0F)
#define   C_028C10_S10_X                                              0xFFF0FFFF
#define   S_028C10_S10_Y(x)                                           (((unsigned)(x) & 0x0F) << 20)
#define   G_028C10_S10_Y(x)                                           (((x) >> 20) & 0x0F)
#define   C_028C10_S10_Y                                              0xFF0FFFFF
#define   S_028C10_S11_X(x)                                           (((unsigned)(x) & 0x0F) << 24)
#define   G_028C10_S11_X(x)                                           (((x) >> 24) & 0x0F)
#define   C_028C10_S11_X                                              0xF0FFFFFF
#define   S_028C10_S11_Y(x)                                           (((unsigned)(x) & 0x0F) << 28)
#define   G_028C10_S11_Y(x)                                           (((x) >> 28) & 0x0F)
#define   C_028C10_S11_Y                                              0x0FFFFFFF
#define R_028C14_PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y0_3                      0x028C14
#define   S_028C14_S12_X(x)                                           (((unsigned)(x) & 0x0F) << 0)
#define   G_028C14_S12_X(x)                                           (((x) >> 0) & 0x0F)
#define   C_028C14_S12_X                                              0xFFFFFFF0
#define   S_028C14_S12_Y(x)                                           (((unsigned)(x) & 0x0F) << 4)
#define   G_028C14_S12_Y(x)                                           (((x) >> 4) & 0x0F)
#define   C_028C14_S12_Y                                              0xFFFFFF0F
#define   S_028C14_S13_X(x)                                           (((unsigned)(x) & 0x0F) << 8)
#define   G_028C14_S13_X(x)                                           (((x) >> 8) & 0x0F)
#define   C_028C14_S13_X                                              0xFFFFF0FF
#define   S_028C14_S13_Y(x)                                           (((unsigned)(x) & 0x0F) << 12)
#define   G_028C14_S13_Y(x)                                           (((x) >> 12) & 0x0F)
#define   C_028C14_S13_Y                                              0xFFFF0FFF
#define   S_028C14_S14_X(x)                                           (((unsigned)(x) & 0x0F) << 16)
#define   G_028C14_S14_X(x)                                           (((x) >> 16) & 0x0F)
#define   C_028C14_S14_X                                              0xFFF0FFFF
#define   S_028C14_S14_Y(x)                                           (((unsigned)(x) & 0x0F) << 20)
#define   G_028C14_S14_Y(x)                                           (((x) >> 20) & 0x0F)
#define   C_028C14_S14_Y                                              0xFF0FFFFF
#define   S_028C14_S15_X(x)                                           (((unsigned)(x) & 0x0F) << 24)
#define   G_028C14_S15_X(x)                                           (((x) >> 24) & 0x0F)
#define   C_028C14_S15_X                                              0xF0FFFFFF
#define   S_028C14_S15_Y(x)                                           (((unsigned)(x) & 0x0F) << 28)
#define   G_028C14_S15_Y(x)                                           (((x) >> 28) & 0x0F)
#define   C_028C14_S15_Y                                              0x0FFFFFFF
#define R_028C18_PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y1_0                      0x028C18
#define   S_028C18_S0_X(x)                                            (((unsigned)(x) & 0x0F) << 0)
#define   G_028C18_S0_X(x)                                            (((x) >> 0) & 0x0F)
#define   C_028C18_S0_X                                               0xFFFFFFF0
#define   S_028C18_S0_Y(x)                                            (((unsigned)(x) & 0x0F) << 4)
#define   G_028C18_S0_Y(x)                                            (((x) >> 4) & 0x0F)
#define   C_028C18_S0_Y                                               0xFFFFFF0F
#define   S_028C18_S1_X(x)                                            (((unsigned)(x) & 0x0F) << 8)
#define   G_028C18_S1_X(x)                                            (((x) >> 8) & 0x0F)
#define   C_028C18_S1_X                                               0xFFFFF0FF
#define   S_028C18_S1_Y(x)                                            (((unsigned)(x) & 0x0F) << 12)
#define   G_028C18_S1_Y(x)                                            (((x) >> 12) & 0x0F)
#define   C_028C18_S1_Y                                               0xFFFF0FFF
#define   S_028C18_S2_X(x)                                            (((unsigned)(x) & 0x0F) << 16)
#define   G_028C18_S2_X(x)                                            (((x) >> 16) & 0x0F)
#define   C_028C18_S2_X                                               0xFFF0FFFF
#define   S_028C18_S2_Y(x)                                            (((unsigned)(x) & 0x0F) << 20)
#define   G_028C18_S2_Y(x)                                            (((x) >> 20) & 0x0F)
#define   C_028C18_S2_Y                                               0xFF0FFFFF
#define   S_028C18_S3_X(x)                                            (((unsigned)(x) & 0x0F) << 24)
#define   G_028C18_S3_X(x)                                            (((x) >> 24) & 0x0F)
#define   C_028C18_S3_X                                               0xF0FFFFFF
#define   S_028C18_S3_Y(x)                                            (((unsigned)(x) & 0x0F) << 28)
#define   G_028C18_S3_Y(x)                                            (((x) >> 28) & 0x0F)
#define   C_028C18_S3_Y                                               0x0FFFFFFF
#define R_028C1C_PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y1_1                      0x028C1C
#define   S_028C1C_S4_X(x)                                            (((unsigned)(x) & 0x0F) << 0)
#define   G_028C1C_S4_X(x)                                            (((x) >> 0) & 0x0F)
#define   C_028C1C_S4_X                                               0xFFFFFFF0
#define   S_028C1C_S4_Y(x)                                            (((unsigned)(x) & 0x0F) << 4)
#define   G_028C1C_S4_Y(x)                                            (((x) >> 4) & 0x0F)
#define   C_028C1C_S4_Y                                               0xFFFFFF0F
#define   S_028C1C_S5_X(x)                                            (((unsigned)(x) & 0x0F) << 8)
#define   G_028C1C_S5_X(x)                                            (((x) >> 8) & 0x0F)
#define   C_028C1C_S5_X                                               0xFFFFF0FF
#define   S_028C1C_S5_Y(x)                                            (((unsigned)(x) & 0x0F) << 12)
#define   G_028C1C_S5_Y(x)                                            (((x) >> 12) & 0x0F)
#define   C_028C1C_S5_Y                                               0xFFFF0FFF
#define   S_028C1C_S6_X(x)                                            (((unsigned)(x) & 0x0F) << 16)
#define   G_028C1C_S6_X(x)                                            (((x) >> 16) & 0x0F)
#define   C_028C1C_S6_X                                               0xFFF0FFFF
#define   S_028C1C_S6_Y(x)                                            (((unsigned)(x) & 0x0F) << 20)
#define   G_028C1C_S6_Y(x)                                            (((x) >> 20) & 0x0F)
#define   C_028C1C_S6_Y                                               0xFF0FFFFF
#define   S_028C1C_S7_X(x)                                            (((unsigned)(x) & 0x0F) << 24)
#define   G_028C1C_S7_X(x)                                            (((x) >> 24) & 0x0F)
#define   C_028C1C_S7_X                                               0xF0FFFFFF
#define   S_028C1C_S7_Y(x)                                            (((unsigned)(x) & 0x0F) << 28)
#define   G_028C1C_S7_Y(x)                                            (((x) >> 28) & 0x0F)
#define   C_028C1C_S7_Y                                               0x0FFFFFFF
#define R_028C20_PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y1_2                      0x028C20
#define   S_028C20_S8_X(x)                                            (((unsigned)(x) & 0x0F) << 0)
#define   G_028C20_S8_X(x)                                            (((x) >> 0) & 0x0F)
#define   C_028C20_S8_X                                               0xFFFFFFF0
#define   S_028C20_S8_Y(x)                                            (((unsigned)(x) & 0x0F) << 4)
#define   G_028C20_S8_Y(x)                                            (((x) >> 4) & 0x0F)
#define   C_028C20_S8_Y                                               0xFFFFFF0F
#define   S_028C20_S9_X(x)                                            (((unsigned)(x) & 0x0F) << 8)
#define   G_028C20_S9_X(x)                                            (((x) >> 8) & 0x0F)
#define   C_028C20_S9_X                                               0xFFFFF0FF
#define   S_028C20_S9_Y(x)                                            (((unsigned)(x) & 0x0F) << 12)
#define   G_028C20_S9_Y(x)                                            (((x) >> 12) & 0x0F)
#define   C_028C20_S9_Y                                               0xFFFF0FFF
#define   S_028C20_S10_X(x)                                           (((unsigned)(x) & 0x0F) << 16)
#define   G_028C20_S10_X(x)                                           (((x) >> 16) & 0x0F)
#define   C_028C20_S10_X                                              0xFFF0FFFF
#define   S_028C20_S10_Y(x)                                           (((unsigned)(x) & 0x0F) << 20)
#define   G_028C20_S10_Y(x)                                           (((x) >> 20) & 0x0F)
#define   C_028C20_S10_Y                                              0xFF0FFFFF
#define   S_028C20_S11_X(x)                                           (((unsigned)(x) & 0x0F) << 24)
#define   G_028C20_S11_X(x)                                           (((x) >> 24) & 0x0F)
#define   C_028C20_S11_X                                              0xF0FFFFFF
#define   S_028C20_S11_Y(x)                                           (((unsigned)(x) & 0x0F) << 28)
#define   G_028C20_S11_Y(x)                                           (((x) >> 28) & 0x0F)
#define   C_028C20_S11_Y                                              0x0FFFFFFF
#define R_028C24_PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y1_3                      0x028C24
#define   S_028C24_S12_X(x)                                           (((unsigned)(x) & 0x0F) << 0)
#define   G_028C24_S12_X(x)                                           (((x) >> 0) & 0x0F)
#define   C_028C24_S12_X                                              0xFFFFFFF0
#define   S_028C24_S12_Y(x)                                           (((unsigned)(x) & 0x0F) << 4)
#define   G_028C24_S12_Y(x)                                           (((x) >> 4) & 0x0F)
#define   C_028C24_S12_Y                                              0xFFFFFF0F
#define   S_028C24_S13_X(x)                                           (((unsigned)(x) & 0x0F) << 8)
#define   G_028C24_S13_X(x)                                           (((x) >> 8) & 0x0F)
#define   C_028C24_S13_X                                              0xFFFFF0FF
#define   S_028C24_S13_Y(x)                                           (((unsigned)(x) & 0x0F) << 12)
#define   G_028C24_S13_Y(x)                                           (((x) >> 12) & 0x0F)
#define   C_028C24_S13_Y                                              0xFFFF0FFF
#define   S_028C24_S14_X(x)                                           (((unsigned)(x) & 0x0F) << 16)
#define   G_028C24_S14_X(x)                                           (((x) >> 16) & 0x0F)
#define   C_028C24_S14_X                                              0xFFF0FFFF
#define   S_028C24_S14_Y(x)                                           (((unsigned)(x) & 0x0F) << 20)
#define   G_028C24_S14_Y(x)                                           (((x) >> 20) & 0x0F)
#define   C_028C24_S14_Y                                              0xFF0FFFFF
#define   S_028C24_S15_X(x)                                           (((unsigned)(x) & 0x0F) << 24)
#define   G_028C24_S15_X(x)                                           (((x) >> 24) & 0x0F)
#define   C_028C24_S15_X                                              0xF0FFFFFF
#define   S_028C24_S15_Y(x)                                           (((unsigned)(x) & 0x0F) << 28)
#define   G_028C24_S15_Y(x)                                           (((x) >> 28) & 0x0F)
#define   C_028C24_S15_Y                                              0x0FFFFFFF
#define R_028C28_PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y1_0                      0x028C28
#define   S_028C28_S0_X(x)                                            (((unsigned)(x) & 0x0F) << 0)
#define   G_028C28_S0_X(x)                                            (((x) >> 0) & 0x0F)
#define   C_028C28_S0_X                                               0xFFFFFFF0
#define   S_028C28_S0_Y(x)                                            (((unsigned)(x) & 0x0F) << 4)
#define   G_028C28_S0_Y(x)                                            (((x) >> 4) & 0x0F)
#define   C_028C28_S0_Y                                               0xFFFFFF0F
#define   S_028C28_S1_X(x)                                            (((unsigned)(x) & 0x0F) << 8)
#define   G_028C28_S1_X(x)                                            (((x) >> 8) & 0x0F)
#define   C_028C28_S1_X                                               0xFFFFF0FF
#define   S_028C28_S1_Y(x)                                            (((unsigned)(x) & 0x0F) << 12)
#define   G_028C28_S1_Y(x)                                            (((x) >> 12) & 0x0F)
#define   C_028C28_S1_Y                                               0xFFFF0FFF
#define   S_028C28_S2_X(x)                                            (((unsigned)(x) & 0x0F) << 16)
#define   G_028C28_S2_X(x)                                            (((x) >> 16) & 0x0F)
#define   C_028C28_S2_X                                               0xFFF0FFFF
#define   S_028C28_S2_Y(x)                                            (((unsigned)(x) & 0x0F) << 20)
#define   G_028C28_S2_Y(x)                                            (((x) >> 20) & 0x0F)
#define   C_028C28_S2_Y                                               0xFF0FFFFF
#define   S_028C28_S3_X(x)                                            (((unsigned)(x) & 0x0F) << 24)
#define   G_028C28_S3_X(x)                                            (((x) >> 24) & 0x0F)
#define   C_028C28_S3_X                                               0xF0FFFFFF
#define   S_028C28_S3_Y(x)                                            (((unsigned)(x) & 0x0F) << 28)
#define   G_028C28_S3_Y(x)                                            (((x) >> 28) & 0x0F)
#define   C_028C28_S3_Y                                               0x0FFFFFFF
#define R_028C2C_PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y1_1                      0x028C2C
#define   S_028C2C_S4_X(x)                                            (((unsigned)(x) & 0x0F) << 0)
#define   G_028C2C_S4_X(x)                                            (((x) >> 0) & 0x0F)
#define   C_028C2C_S4_X                                               0xFFFFFFF0
#define   S_028C2C_S4_Y(x)                                            (((unsigned)(x) & 0x0F) << 4)
#define   G_028C2C_S4_Y(x)                                            (((x) >> 4) & 0x0F)
#define   C_028C2C_S4_Y                                               0xFFFFFF0F
#define   S_028C2C_S5_X(x)                                            (((unsigned)(x) & 0x0F) << 8)
#define   G_028C2C_S5_X(x)                                            (((x) >> 8) & 0x0F)
#define   C_028C2C_S5_X                                               0xFFFFF0FF
#define   S_028C2C_S5_Y(x)                                            (((unsigned)(x) & 0x0F) << 12)
#define   G_028C2C_S5_Y(x)                                            (((x) >> 12) & 0x0F)
#define   C_028C2C_S5_Y                                               0xFFFF0FFF
#define   S_028C2C_S6_X(x)                                            (((unsigned)(x) & 0x0F) << 16)
#define   G_028C2C_S6_X(x)                                            (((x) >> 16) & 0x0F)
#define   C_028C2C_S6_X                                               0xFFF0FFFF
#define   S_028C2C_S6_Y(x)                                            (((unsigned)(x) & 0x0F) << 20)
#define   G_028C2C_S6_Y(x)                                            (((x) >> 20) & 0x0F)
#define   C_028C2C_S6_Y                                               0xFF0FFFFF
#define   S_028C2C_S7_X(x)                                            (((unsigned)(x) & 0x0F) << 24)
#define   G_028C2C_S7_X(x)                                            (((x) >> 24) & 0x0F)
#define   C_028C2C_S7_X                                               0xF0FFFFFF
#define   S_028C2C_S7_Y(x)                                            (((unsigned)(x) & 0x0F) << 28)
#define   G_028C2C_S7_Y(x)                                            (((x) >> 28) & 0x0F)
#define   C_028C2C_S7_Y                                               0x0FFFFFFF
#define R_028C30_PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y1_2                      0x028C30
#define   S_028C30_S8_X(x)                                            (((unsigned)(x) & 0x0F) << 0)
#define   G_028C30_S8_X(x)                                            (((x) >> 0) & 0x0F)
#define   C_028C30_S8_X                                               0xFFFFFFF0
#define   S_028C30_S8_Y(x)                                            (((unsigned)(x) & 0x0F) << 4)
#define   G_028C30_S8_Y(x)                                            (((x) >> 4) & 0x0F)
#define   C_028C30_S8_Y                                               0xFFFFFF0F
#define   S_028C30_S9_X(x)                                            (((unsigned)(x) & 0x0F) << 8)
#define   G_028C30_S9_X(x)                                            (((x) >> 8) & 0x0F)
#define   C_028C30_S9_X                                               0xFFFFF0FF
#define   S_028C30_S9_Y(x)                                            (((unsigned)(x) & 0x0F) << 12)
#define   G_028C30_S9_Y(x)                                            (((x) >> 12) & 0x0F)
#define   C_028C30_S9_Y                                               0xFFFF0FFF
#define   S_028C30_S10_X(x)                                           (((unsigned)(x) & 0x0F) << 16)
#define   G_028C30_S10_X(x)                                           (((x) >> 16) & 0x0F)
#define   C_028C30_S10_X                                              0xFFF0FFFF
#define   S_028C30_S10_Y(x)                                           (((unsigned)(x) & 0x0F) << 20)
#define   G_028C30_S10_Y(x)                                           (((x) >> 20) & 0x0F)
#define   C_028C30_S10_Y                                              0xFF0FFFFF
#define   S_028C30_S11_X(x)                                           (((unsigned)(x) & 0x0F) << 24)
#define   G_028C30_S11_X(x)                                           (((x) >> 24) & 0x0F)
#define   C_028C30_S11_X                                              0xF0FFFFFF
#define   S_028C30_S11_Y(x)                                           (((unsigned)(x) & 0x0F) << 28)
#define   G_028C30_S11_Y(x)                                           (((x) >> 28) & 0x0F)
#define   C_028C30_S11_Y                                              0x0FFFFFFF
#define R_028C34_PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y1_3                      0x028C34
#define   S_028C34_S12_X(x)                                           (((unsigned)(x) & 0x0F) << 0)
#define   G_028C34_S12_X(x)                                           (((x) >> 0) & 0x0F)
#define   C_028C34_S12_X                                              0xFFFFFFF0
#define   S_028C34_S12_Y(x)                                           (((unsigned)(x) & 0x0F) << 4)
#define   G_028C34_S12_Y(x)                                           (((x) >> 4) & 0x0F)
#define   C_028C34_S12_Y                                              0xFFFFFF0F
#define   S_028C34_S13_X(x)                                           (((unsigned)(x) & 0x0F) << 8)
#define   G_028C34_S13_X(x)                                           (((x) >> 8) & 0x0F)
#define   C_028C34_S13_X                                              0xFFFFF0FF
#define   S_028C34_S13_Y(x)                                           (((unsigned)(x) & 0x0F) << 12)
#define   G_028C34_S13_Y(x)                                           (((x) >> 12) & 0x0F)
#define   C_028C34_S13_Y                                              0xFFFF0FFF
#define   S_028C34_S14_X(x)                                           (((unsigned)(x) & 0x0F) << 16)
#define   G_028C34_S14_X(x)                                           (((x) >> 16) & 0x0F)
#define   C_028C34_S14_X                                              0xFFF0FFFF
#define   S_028C34_S14_Y(x)                                           (((unsigned)(x) & 0x0F) << 20)
#define   G_028C34_S14_Y(x)                                           (((x) >> 20) & 0x0F)
#define   C_028C34_S14_Y                                              0xFF0FFFFF
#define   S_028C34_S15_X(x)                                           (((unsigned)(x) & 0x0F) << 24)
#define   G_028C34_S15_X(x)                                           (((x) >> 24) & 0x0F)
#define   C_028C34_S15_X                                              0xF0FFFFFF
#define   S_028C34_S15_Y(x)                                           (((unsigned)(x) & 0x0F) << 28)
#define   G_028C34_S15_Y(x)                                           (((x) >> 28) & 0x0F)
#define   C_028C34_S15_Y                                              0x0FFFFFFF
#define R_028C38_PA_SC_AA_MASK_X0Y0_X1Y0                                0x028C38
#define   S_028C38_AA_MASK_X0Y0(x)                                    (((unsigned)(x) & 0xFFFF) << 0)
#define   G_028C38_AA_MASK_X0Y0(x)                                    (((x) >> 0) & 0xFFFF)
#define   C_028C38_AA_MASK_X0Y0                                       0xFFFF0000
#define   S_028C38_AA_MASK_X1Y0(x)                                    (((unsigned)(x) & 0xFFFF) << 16)
#define   G_028C38_AA_MASK_X1Y0(x)                                    (((x) >> 16) & 0xFFFF)
#define   C_028C38_AA_MASK_X1Y0                                       0x0000FFFF
#define R_028C3C_PA_SC_AA_MASK_X0Y1_X1Y1                                0x028C3C
#define   S_028C3C_AA_MASK_X0Y1(x)                                    (((unsigned)(x) & 0xFFFF) << 0)
#define   G_028C3C_AA_MASK_X0Y1(x)                                    (((x) >> 0) & 0xFFFF)
#define   C_028C3C_AA_MASK_X0Y1                                       0xFFFF0000
#define   S_028C3C_AA_MASK_X1Y1(x)                                    (((unsigned)(x) & 0xFFFF) << 16)
#define   G_028C3C_AA_MASK_X1Y1(x)                                    (((x) >> 16) & 0xFFFF)
#define   C_028C3C_AA_MASK_X1Y1                                       0x0000FFFF
/* Stoney */
#define R_028C40_PA_SC_SHADER_CONTROL                                   0x028C40
#define   S_028C40_REALIGN_DQUADS_AFTER_N_WAVES(x)                    (((unsigned)(x) & 0x03) << 0)
#define   G_028C40_REALIGN_DQUADS_AFTER_N_WAVES(x)                    (((x) >> 0) & 0x03)
#define   C_028C40_REALIGN_DQUADS_AFTER_N_WAVES                       0xFFFFFFFC
/*        */
#define R_028C58_VGT_VERTEX_REUSE_BLOCK_CNTL                            0x028C58
#define   S_028C58_VTX_REUSE_DEPTH(x)                                 (((unsigned)(x) & 0xFF) << 0)
#define   G_028C58_VTX_REUSE_DEPTH(x)                                 (((x) >> 0) & 0xFF)
#define   C_028C58_VTX_REUSE_DEPTH                                    0xFFFFFF00
#define R_028C5C_VGT_OUT_DEALLOC_CNTL                                   0x028C5C
#define   S_028C5C_DEALLOC_DIST(x)                                    (((unsigned)(x) & 0x7F) << 0)
#define   G_028C5C_DEALLOC_DIST(x)                                    (((x) >> 0) & 0x7F)
#define   C_028C5C_DEALLOC_DIST                                       0xFFFFFF80
#define R_028C60_CB_COLOR0_BASE                                         0x028C60
#define R_028C64_CB_COLOR0_PITCH                                        0x028C64
#define   S_028C64_TILE_MAX(x)                                        (((unsigned)(x) & 0x7FF) << 0)
#define   G_028C64_TILE_MAX(x)                                        (((x) >> 0) & 0x7FF)
#define   C_028C64_TILE_MAX                                           0xFFFFF800
/* CIK */
#define   S_028C64_FMASK_TILE_MAX(x)                                  (((unsigned)(x) & 0x7FF) << 20)
#define   G_028C64_FMASK_TILE_MAX(x)                                  (((x) >> 20) & 0x7FF)
#define   C_028C64_FMASK_TILE_MAX                                     0x800FFFFF
/*     */
#define R_028C68_CB_COLOR0_SLICE                                        0x028C68
#define   S_028C68_TILE_MAX(x)                                        (((unsigned)(x) & 0x3FFFFF) << 0)
#define   G_028C68_TILE_MAX(x)                                        (((x) >> 0) & 0x3FFFFF)
#define   C_028C68_TILE_MAX                                           0xFFC00000
#define R_028C6C_CB_COLOR0_VIEW                                         0x028C6C
#define   S_028C6C_SLICE_START(x)                                     (((unsigned)(x) & 0x7FF) << 0)
#define   G_028C6C_SLICE_START(x)                                     (((x) >> 0) & 0x7FF)
#define   C_028C6C_SLICE_START                                        0xFFFFF800
#define   S_028C6C_SLICE_MAX(x)                                       (((unsigned)(x) & 0x7FF) << 13)
#define   G_028C6C_SLICE_MAX(x)                                       (((x) >> 13) & 0x7FF)
#define   C_028C6C_SLICE_MAX                                          0xFF001FFF
#define R_028C70_CB_COLOR0_INFO                                         0x028C70
#define   S_028C70_ENDIAN(x)                                          (((unsigned)(x) & 0x03) << 0)
#define   G_028C70_ENDIAN(x)                                          (((x) >> 0) & 0x03)
#define   C_028C70_ENDIAN                                             0xFFFFFFFC
#define     V_028C70_ENDIAN_NONE                                    0x00
#define     V_028C70_ENDIAN_8IN16                                   0x01
#define     V_028C70_ENDIAN_8IN32                                   0x02
#define     V_028C70_ENDIAN_8IN64                                   0x03
#define   S_028C70_FORMAT(x)                                          (((unsigned)(x) & 0x1F) << 2)
#define   G_028C70_FORMAT(x)                                          (((x) >> 2) & 0x1F)
#define   C_028C70_FORMAT                                             0xFFFFFF83
#define     V_028C70_COLOR_INVALID                                  0x00
#define     V_028C70_COLOR_8                                        0x01
#define     V_028C70_COLOR_16                                       0x02
#define     V_028C70_COLOR_8_8                                      0x03
#define     V_028C70_COLOR_32                                       0x04
#define     V_028C70_COLOR_16_16                                    0x05
#define     V_028C70_COLOR_10_11_11                                 0x06
#define     V_028C70_COLOR_11_11_10                                 0x07
#define     V_028C70_COLOR_10_10_10_2                               0x08
#define     V_028C70_COLOR_2_10_10_10                               0x09
#define     V_028C70_COLOR_8_8_8_8                                  0x0A
#define     V_028C70_COLOR_32_32                                    0x0B
#define     V_028C70_COLOR_16_16_16_16                              0x0C
#define     V_028C70_COLOR_32_32_32_32                              0x0E
#define     V_028C70_COLOR_5_6_5                                    0x10
#define     V_028C70_COLOR_1_5_5_5                                  0x11
#define     V_028C70_COLOR_5_5_5_1                                  0x12
#define     V_028C70_COLOR_4_4_4_4                                  0x13
#define     V_028C70_COLOR_8_24                                     0x14
#define     V_028C70_COLOR_24_8                                     0x15
#define     V_028C70_COLOR_X24_8_32_FLOAT                           0x16
#define   S_028C70_LINEAR_GENERAL(x)                                  (((unsigned)(x) & 0x1) << 7)
#define   G_028C70_LINEAR_GENERAL(x)                                  (((x) >> 7) & 0x1)
#define   C_028C70_LINEAR_GENERAL                                     0xFFFFFF7F
#define   S_028C70_NUMBER_TYPE(x)                                     (((unsigned)(x) & 0x07) << 8)
#define   G_028C70_NUMBER_TYPE(x)                                     (((x) >> 8) & 0x07)
#define   C_028C70_NUMBER_TYPE                                        0xFFFFF8FF
#define     V_028C70_NUMBER_UNORM                                   0x00
#define     V_028C70_NUMBER_SNORM                                   0x01
#define     V_028C70_NUMBER_UINT                                    0x04
#define     V_028C70_NUMBER_SINT                                    0x05
#define     V_028C70_NUMBER_SRGB                                    0x06
#define     V_028C70_NUMBER_FLOAT                                   0x07
#define   S_028C70_COMP_SWAP(x)                                       (((unsigned)(x) & 0x03) << 11)
#define   G_028C70_COMP_SWAP(x)                                       (((x) >> 11) & 0x03)
#define   C_028C70_COMP_SWAP                                          0xFFFFE7FF
#define     V_028C70_SWAP_STD                                       0x00
#define     V_028C70_SWAP_ALT                                       0x01
#define     V_028C70_SWAP_STD_REV                                   0x02
#define     V_028C70_SWAP_ALT_REV                                   0x03
#define   S_028C70_FAST_CLEAR(x)                                      (((unsigned)(x) & 0x1) << 13)
#define   G_028C70_FAST_CLEAR(x)                                      (((x) >> 13) & 0x1)
#define   C_028C70_FAST_CLEAR                                         0xFFFFDFFF
#define   S_028C70_COMPRESSION(x)                                     (((unsigned)(x) & 0x1) << 14)
#define   G_028C70_COMPRESSION(x)                                     (((x) >> 14) & 0x1)
#define   C_028C70_COMPRESSION                                        0xFFFFBFFF
#define   S_028C70_BLEND_CLAMP(x)                                     (((unsigned)(x) & 0x1) << 15)
#define   G_028C70_BLEND_CLAMP(x)                                     (((x) >> 15) & 0x1)
#define   C_028C70_BLEND_CLAMP                                        0xFFFF7FFF
#define   S_028C70_BLEND_BYPASS(x)                                    (((unsigned)(x) & 0x1) << 16)
#define   G_028C70_BLEND_BYPASS(x)                                    (((x) >> 16) & 0x1)
#define   C_028C70_BLEND_BYPASS                                       0xFFFEFFFF
#define   S_028C70_SIMPLE_FLOAT(x)                                    (((unsigned)(x) & 0x1) << 17)
#define   G_028C70_SIMPLE_FLOAT(x)                                    (((x) >> 17) & 0x1)
#define   C_028C70_SIMPLE_FLOAT                                       0xFFFDFFFF
#define   S_028C70_ROUND_MODE(x)                                      (((unsigned)(x) & 0x1) << 18)
#define   G_028C70_ROUND_MODE(x)                                      (((x) >> 18) & 0x1)
#define   C_028C70_ROUND_MODE                                         0xFFFBFFFF
#define   S_028C70_CMASK_IS_LINEAR(x)                                 (((unsigned)(x) & 0x1) << 19)
#define   G_028C70_CMASK_IS_LINEAR(x)                                 (((x) >> 19) & 0x1)
#define   C_028C70_CMASK_IS_LINEAR                                    0xFFF7FFFF
#define   S_028C70_BLEND_OPT_DONT_RD_DST(x)                           (((unsigned)(x) & 0x07) << 20)
#define   G_028C70_BLEND_OPT_DONT_RD_DST(x)                           (((x) >> 20) & 0x07)
#define   C_028C70_BLEND_OPT_DONT_RD_DST                              0xFF8FFFFF
#define     V_028C70_FORCE_OPT_AUTO                                 0x00
#define     V_028C70_FORCE_OPT_DISABLE                              0x01
#define     V_028C70_FORCE_OPT_ENABLE_IF_SRC_A_0                    0x02
#define     V_028C70_FORCE_OPT_ENABLE_IF_SRC_RGB_0                  0x03
#define     V_028C70_FORCE_OPT_ENABLE_IF_SRC_ARGB_0                 0x04
#define     V_028C70_FORCE_OPT_ENABLE_IF_SRC_A_1                    0x05
#define     V_028C70_FORCE_OPT_ENABLE_IF_SRC_RGB_1                  0x06
#define     V_028C70_FORCE_OPT_ENABLE_IF_SRC_ARGB_1                 0x07
#define   S_028C70_BLEND_OPT_DISCARD_PIXEL(x)                         (((unsigned)(x) & 0x07) << 23)
#define   G_028C70_BLEND_OPT_DISCARD_PIXEL(x)                         (((x) >> 23) & 0x07)
#define   C_028C70_BLEND_OPT_DISCARD_PIXEL                            0xFC7FFFFF
#define     V_028C70_FORCE_OPT_AUTO                                 0x00
#define     V_028C70_FORCE_OPT_DISABLE                              0x01
#define     V_028C70_FORCE_OPT_ENABLE_IF_SRC_A_0                    0x02
#define     V_028C70_FORCE_OPT_ENABLE_IF_SRC_RGB_0                  0x03
#define     V_028C70_FORCE_OPT_ENABLE_IF_SRC_ARGB_0                 0x04
#define     V_028C70_FORCE_OPT_ENABLE_IF_SRC_A_1                    0x05
#define     V_028C70_FORCE_OPT_ENABLE_IF_SRC_RGB_1                  0x06
#define     V_028C70_FORCE_OPT_ENABLE_IF_SRC_ARGB_1                 0x07
/* CIK */
#define   S_028C70_FMASK_COMPRESSION_DISABLE(x)                       (((unsigned)(x) & 0x1) << 26)
#define   G_028C70_FMASK_COMPRESSION_DISABLE(x)                       (((x) >> 26) & 0x1)
#define   C_028C70_FMASK_COMPRESSION_DISABLE                          0xFBFFFFFF
/*     */
/* VI */
#define   S_028C70_FMASK_COMPRESS_1FRAG_ONLY(x)                       (((unsigned)(x) & 0x1) << 27)
#define   G_028C70_FMASK_COMPRESS_1FRAG_ONLY(x)                       (((x) >> 27) & 0x1)
#define   C_028C70_FMASK_COMPRESS_1FRAG_ONLY                          0xF7FFFFFF
#define   S_028C70_DCC_ENABLE(x)                                      (((unsigned)(x) & 0x1) << 28)
#define   G_028C70_DCC_ENABLE(x)                                      (((x) >> 28) & 0x1)
#define   C_028C70_DCC_ENABLE                                         0xEFFFFFFF
#define   S_028C70_CMASK_ADDR_TYPE(x)                                 (((unsigned)(x) & 0x03) << 29)
#define   G_028C70_CMASK_ADDR_TYPE(x)                                 (((x) >> 29) & 0x03)
#define   C_028C70_CMASK_ADDR_TYPE                                    0x9FFFFFFF
/*    */
#define R_028C74_CB_COLOR0_ATTRIB                                       0x028C74
#define   S_028C74_TILE_MODE_INDEX(x)                                 (((unsigned)(x) & 0x1F) << 0)
#define   G_028C74_TILE_MODE_INDEX(x)                                 (((x) >> 0) & 0x1F)
#define   C_028C74_TILE_MODE_INDEX                                    0xFFFFFFE0
#define   S_028C74_FMASK_TILE_MODE_INDEX(x)                           (((unsigned)(x) & 0x1F) << 5)
#define   G_028C74_FMASK_TILE_MODE_INDEX(x)                           (((x) >> 5) & 0x1F)
#define   C_028C74_FMASK_TILE_MODE_INDEX                              0xFFFFFC1F
#define   S_028C74_FMASK_BANK_HEIGHT(x)                               (((unsigned)(x) & 0x03) << 10)
#define   G_028C74_FMASK_BANK_HEIGHT(x)                               (((x) >> 10) & 0x03)
#define   C_028C74_FMASK_BANK_HEIGHT                                  0xFFFFF3FF
#define   S_028C74_NUM_SAMPLES(x)                                     (((unsigned)(x) & 0x07) << 12)
#define   G_028C74_NUM_SAMPLES(x)                                     (((x) >> 12) & 0x07)
#define   C_028C74_NUM_SAMPLES                                        0xFFFF8FFF
#define   S_028C74_NUM_FRAGMENTS(x)                                   (((unsigned)(x) & 0x03) << 15)
#define   G_028C74_NUM_FRAGMENTS(x)                                   (((x) >> 15) & 0x03)
#define   C_028C74_NUM_FRAGMENTS                                      0xFFFE7FFF
#define   S_028C74_FORCE_DST_ALPHA_1(x)                               (((unsigned)(x) & 0x1) << 17)
#define   G_028C74_FORCE_DST_ALPHA_1(x)                               (((x) >> 17) & 0x1)
#define   C_028C74_FORCE_DST_ALPHA_1                                  0xFFFDFFFF
/* VI */
#define R_028C78_CB_COLOR0_DCC_CONTROL                                  0x028C78
#define   S_028C78_OVERWRITE_COMBINER_DISABLE(x)                      (((unsigned)(x) & 0x1) << 0)
#define   G_028C78_OVERWRITE_COMBINER_DISABLE(x)                      (((x) >> 0) & 0x1)
#define   C_028C78_OVERWRITE_COMBINER_DISABLE                         0xFFFFFFFE
#define   S_028C78_KEY_CLEAR_ENABLE(x)                                (((unsigned)(x) & 0x1) << 1)
#define   G_028C78_KEY_CLEAR_ENABLE(x)                                (((x) >> 1) & 0x1)
#define   C_028C78_KEY_CLEAR_ENABLE                                   0xFFFFFFFD
#define   S_028C78_MAX_UNCOMPRESSED_BLOCK_SIZE(x)                     (((unsigned)(x) & 0x03) << 2)
#define   G_028C78_MAX_UNCOMPRESSED_BLOCK_SIZE(x)                     (((x) >> 2) & 0x03)
#define   C_028C78_MAX_UNCOMPRESSED_BLOCK_SIZE                        0xFFFFFFF3
#define   S_028C78_MIN_COMPRESSED_BLOCK_SIZE(x)                       (((unsigned)(x) & 0x1) << 4)
#define   G_028C78_MIN_COMPRESSED_BLOCK_SIZE(x)                       (((x) >> 4) & 0x1)
#define   C_028C78_MIN_COMPRESSED_BLOCK_SIZE                          0xFFFFFFEF
#define   S_028C78_MAX_COMPRESSED_BLOCK_SIZE(x)                       (((unsigned)(x) & 0x03) << 5)
#define   G_028C78_MAX_COMPRESSED_BLOCK_SIZE(x)                       (((x) >> 5) & 0x03)
#define   C_028C78_MAX_COMPRESSED_BLOCK_SIZE                          0xFFFFFF9F
#define   S_028C78_COLOR_TRANSFORM(x)                                 (((unsigned)(x) & 0x03) << 7)
#define   G_028C78_COLOR_TRANSFORM(x)                                 (((x) >> 7) & 0x03)
#define   C_028C78_COLOR_TRANSFORM                                    0xFFFFFE7F
#define   S_028C78_INDEPENDENT_64B_BLOCKS(x)                          (((unsigned)(x) & 0x1) << 9)
#define   G_028C78_INDEPENDENT_64B_BLOCKS(x)                          (((x) >> 9) & 0x1)
#define   C_028C78_INDEPENDENT_64B_BLOCKS                             0xFFFFFDFF
#define   S_028C78_LOSSY_RGB_PRECISION(x)                             (((unsigned)(x) & 0x0F) << 10)
#define   G_028C78_LOSSY_RGB_PRECISION(x)                             (((x) >> 10) & 0x0F)
#define   C_028C78_LOSSY_RGB_PRECISION                                0xFFFFC3FF
#define   S_028C78_LOSSY_ALPHA_PRECISION(x)                           (((unsigned)(x) & 0x0F) << 14)
#define   G_028C78_LOSSY_ALPHA_PRECISION(x)                           (((x) >> 14) & 0x0F)
#define   C_028C78_LOSSY_ALPHA_PRECISION                              0xFFFC3FFF
/*    */
#define R_028C7C_CB_COLOR0_CMASK                                        0x028C7C
#define R_028C80_CB_COLOR0_CMASK_SLICE                                  0x028C80
#define   S_028C80_TILE_MAX(x)                                        (((unsigned)(x) & 0x3FFF) << 0)
#define   G_028C80_TILE_MAX(x)                                        (((x) >> 0) & 0x3FFF)
#define   C_028C80_TILE_MAX                                           0xFFFFC000
#define R_028C84_CB_COLOR0_FMASK                                        0x028C84
#define R_028C88_CB_COLOR0_FMASK_SLICE                                  0x028C88
#define   S_028C88_TILE_MAX(x)                                        (((unsigned)(x) & 0x3FFFFF) << 0)
#define   G_028C88_TILE_MAX(x)                                        (((x) >> 0) & 0x3FFFFF)
#define   C_028C88_TILE_MAX                                           0xFFC00000
#define R_028C8C_CB_COLOR0_CLEAR_WORD0                                  0x028C8C
#define R_028C90_CB_COLOR0_CLEAR_WORD1                                  0x028C90
#define R_028C94_CB_COLOR0_DCC_BASE                                     0x028C94 /* VI */
#define R_028C9C_CB_COLOR1_BASE                                         0x028C9C
#define R_028CA0_CB_COLOR1_PITCH                                        0x028CA0
#define R_028CA4_CB_COLOR1_SLICE                                        0x028CA4
#define R_028CA8_CB_COLOR1_VIEW                                         0x028CA8
#define R_028CAC_CB_COLOR1_INFO                                         0x028CAC
#define R_028CB0_CB_COLOR1_ATTRIB                                       0x028CB0
#define R_028CB4_CB_COLOR1_DCC_CONTROL                                  0x028CB4 /* VI */
#define R_028CB8_CB_COLOR1_CMASK                                        0x028CB8
#define R_028CBC_CB_COLOR1_CMASK_SLICE                                  0x028CBC
#define R_028CC0_CB_COLOR1_FMASK                                        0x028CC0
#define R_028CC4_CB_COLOR1_FMASK_SLICE                                  0x028CC4
#define R_028CC8_CB_COLOR1_CLEAR_WORD0                                  0x028CC8
#define R_028CCC_CB_COLOR1_CLEAR_WORD1                                  0x028CCC
#define R_028CD0_CB_COLOR1_DCC_BASE                                     0x028CD0 /* VI */
#define R_028CD8_CB_COLOR2_BASE                                         0x028CD8
#define R_028CDC_CB_COLOR2_PITCH                                        0x028CDC
#define R_028CE0_CB_COLOR2_SLICE                                        0x028CE0
#define R_028CE4_CB_COLOR2_VIEW                                         0x028CE4
#define R_028CE8_CB_COLOR2_INFO                                         0x028CE8
#define R_028CEC_CB_COLOR2_ATTRIB                                       0x028CEC
#define R_028CF0_CB_COLOR2_DCC_CONTROL                                  0x028CF0 /* VI */
#define R_028CF4_CB_COLOR2_CMASK                                        0x028CF4
#define R_028CF8_CB_COLOR2_CMASK_SLICE                                  0x028CF8
#define R_028CFC_CB_COLOR2_FMASK                                        0x028CFC
#define R_028D00_CB_COLOR2_FMASK_SLICE                                  0x028D00
#define R_028D04_CB_COLOR2_CLEAR_WORD0                                  0x028D04
#define R_028D08_CB_COLOR2_CLEAR_WORD1                                  0x028D08
#define R_028D0C_CB_COLOR2_DCC_BASE                                     0x028D0C /* VI */
#define R_028D14_CB_COLOR3_BASE                                         0x028D14
#define R_028D18_CB_COLOR3_PITCH                                        0x028D18
#define R_028D1C_CB_COLOR3_SLICE                                        0x028D1C
#define R_028D20_CB_COLOR3_VIEW                                         0x028D20
#define R_028D24_CB_COLOR3_INFO                                         0x028D24
#define R_028D28_CB_COLOR3_ATTRIB                                       0x028D28
#define R_028D2C_CB_COLOR3_DCC_CONTROL                                  0x028D2C /* VI */
#define R_028D30_CB_COLOR3_CMASK                                        0x028D30
#define R_028D34_CB_COLOR3_CMASK_SLICE                                  0x028D34
#define R_028D38_CB_COLOR3_FMASK                                        0x028D38
#define R_028D3C_CB_COLOR3_FMASK_SLICE                                  0x028D3C
#define R_028D40_CB_COLOR3_CLEAR_WORD0                                  0x028D40
#define R_028D44_CB_COLOR3_CLEAR_WORD1                                  0x028D44
#define R_028D48_CB_COLOR3_DCC_BASE                                     0x028D48 /* VI */
#define R_028D50_CB_COLOR4_BASE                                         0x028D50
#define R_028D54_CB_COLOR4_PITCH                                        0x028D54
#define R_028D58_CB_COLOR4_SLICE                                        0x028D58
#define R_028D5C_CB_COLOR4_VIEW                                         0x028D5C
#define R_028D60_CB_COLOR4_INFO                                         0x028D60
#define R_028D64_CB_COLOR4_ATTRIB                                       0x028D64
#define R_028D68_CB_COLOR4_DCC_CONTROL                                  0x028D68 /* VI */
#define R_028D6C_CB_COLOR4_CMASK                                        0x028D6C
#define R_028D70_CB_COLOR4_CMASK_SLICE                                  0x028D70
#define R_028D74_CB_COLOR4_FMASK                                        0x028D74
#define R_028D78_CB_COLOR4_FMASK_SLICE                                  0x028D78
#define R_028D7C_CB_COLOR4_CLEAR_WORD0                                  0x028D7C
#define R_028D80_CB_COLOR4_CLEAR_WORD1                                  0x028D80
#define R_028D84_CB_COLOR4_DCC_BASE                                     0x028D84 /* VI */
#define R_028D8C_CB_COLOR5_BASE                                         0x028D8C
#define R_028D90_CB_COLOR5_PITCH                                        0x028D90
#define R_028D94_CB_COLOR5_SLICE                                        0x028D94
#define R_028D98_CB_COLOR5_VIEW                                         0x028D98
#define R_028D9C_CB_COLOR5_INFO                                         0x028D9C
#define R_028DA0_CB_COLOR5_ATTRIB                                       0x028DA0
#define R_028DA4_CB_COLOR5_DCC_CONTROL                                  0x028DA4 /* VI */
#define R_028DA8_CB_COLOR5_CMASK                                        0x028DA8
#define R_028DAC_CB_COLOR5_CMASK_SLICE                                  0x028DAC
#define R_028DB0_CB_COLOR5_FMASK                                        0x028DB0
#define R_028DB4_CB_COLOR5_FMASK_SLICE                                  0x028DB4
#define R_028DB8_CB_COLOR5_CLEAR_WORD0                                  0x028DB8
#define R_028DBC_CB_COLOR5_CLEAR_WORD1                                  0x028DBC
#define R_028DC0_CB_COLOR5_DCC_BASE                                     0x028DC0 /* VI */
#define R_028DC8_CB_COLOR6_BASE                                         0x028DC8
#define R_028DCC_CB_COLOR6_PITCH                                        0x028DCC
#define R_028DD0_CB_COLOR6_SLICE                                        0x028DD0
#define R_028DD4_CB_COLOR6_VIEW                                         0x028DD4
#define R_028DD8_CB_COLOR6_INFO                                         0x028DD8
#define R_028DDC_CB_COLOR6_ATTRIB                                       0x028DDC
#define R_028DE0_CB_COLOR6_DCC_CONTROL                                  0x028DE0 /* VI */
#define R_028DE4_CB_COLOR6_CMASK                                        0x028DE4
#define R_028DE8_CB_COLOR6_CMASK_SLICE                                  0x028DE8
#define R_028DEC_CB_COLOR6_FMASK                                        0x028DEC
#define R_028DF0_CB_COLOR6_FMASK_SLICE                                  0x028DF0
#define R_028DF4_CB_COLOR6_CLEAR_WORD0                                  0x028DF4
#define R_028DF8_CB_COLOR6_CLEAR_WORD1                                  0x028DF8
#define R_028DFC_CB_COLOR6_DCC_BASE                                     0x028DFC /* VI */
#define R_028E04_CB_COLOR7_BASE                                         0x028E04
#define R_028E08_CB_COLOR7_PITCH                                        0x028E08
#define R_028E0C_CB_COLOR7_SLICE                                        0x028E0C
#define R_028E10_CB_COLOR7_VIEW                                         0x028E10
#define R_028E14_CB_COLOR7_INFO                                         0x028E14
#define R_028E18_CB_COLOR7_ATTRIB                                       0x028E18
#define R_028E1C_CB_COLOR7_DCC_CONTROL                                  0x028E1C /* VI */
#define R_028E20_CB_COLOR7_CMASK                                        0x028E20
#define R_028E24_CB_COLOR7_CMASK_SLICE                                  0x028E24
#define R_028E28_CB_COLOR7_FMASK                                        0x028E28
#define R_028E2C_CB_COLOR7_FMASK_SLICE                                  0x028E2C
#define R_028E30_CB_COLOR7_CLEAR_WORD0                                  0x028E30
#define R_028E34_CB_COLOR7_CLEAR_WORD1                                  0x028E34
#define R_028E38_CB_COLOR7_DCC_BASE                                     0x028E38 /* VI */

/* SI async DMA packets */
#define SI_DMA_PACKET(cmd, sub_cmd, n) ((((unsigned)(cmd) & 0xF) << 28) |    \
                                       (((unsigned)(sub_cmd) & 0xFF) << 20) |\
                                       (((unsigned)(n) & 0xFFFFF) << 0))
/* SI async DMA Packet types */
#define    SI_DMA_PACKET_WRITE                     0x2
#define    SI_DMA_PACKET_COPY                      0x3
#define    SI_DMA_COPY_MAX_BYTE_ALIGNED_SIZE       0xfffe0
/* The documentation says 0xffff8 is the maximum size in dwords, which is
 * 0x3fffe0 in bytes. */
#define    SI_DMA_COPY_MAX_DWORD_ALIGNED_SIZE      0x3fffe0
#define    SI_DMA_COPY_DWORD_ALIGNED               0x00
#define    SI_DMA_COPY_BYTE_ALIGNED                0x40
#define    SI_DMA_COPY_TILED                       0x8
#define    SI_DMA_PACKET_INDIRECT_BUFFER           0x4
#define    SI_DMA_PACKET_SEMAPHORE                 0x5
#define    SI_DMA_PACKET_FENCE                     0x6
#define    SI_DMA_PACKET_TRAP                      0x7
#define    SI_DMA_PACKET_SRBM_WRITE                0x9
#define    SI_DMA_PACKET_CONSTANT_FILL             0xd
#define    SI_DMA_PACKET_NOP                       0xf

/* CIK async DMA packets */
#define CIK_SDMA_PACKET(op, sub_op, n)   ((((unsigned)(n) & 0xFFFF) << 16) |	\
					 (((unsigned)(sub_op) & 0xFF) << 8) |	\
					 (((unsigned)(op) & 0xFF) << 0))
/* CIK async DMA packet types */
#define    CIK_SDMA_OPCODE_NOP                     0x0
#define    CIK_SDMA_OPCODE_COPY                    0x1
#define        CIK_SDMA_COPY_SUB_OPCODE_LINEAR            0x0
#define        CIK_SDMA_COPY_SUB_OPCODE_TILED             0x1
#define        CIK_SDMA_COPY_SUB_OPCODE_SOA               0x3
#define        CIK_SDMA_COPY_SUB_OPCODE_LINEAR_SUB_WINDOW 0x4
#define        CIK_SDMA_COPY_SUB_OPCODE_TILED_SUB_WINDOW  0x5
#define        CIK_SDMA_COPY_SUB_OPCODE_T2T_SUB_WINDOW    0x6
#define    CIK_SDMA_OPCODE_WRITE                   0x2
#define        SDMA_WRITE_SUB_OPCODE_LINEAR               0x0
#define        SDMA_WRTIE_SUB_OPCODE_TILED                0x1
#define    CIK_SDMA_OPCODE_INDIRECT_BUFFER         0x4
#define    CIK_SDMA_PACKET_FENCE                   0x5
#define    CIK_SDMA_PACKET_TRAP                    0x6
#define    CIK_SDMA_PACKET_SEMAPHORE               0x7
#define    CIK_SDMA_PACKET_CONSTANT_FILL           0xb
#define    CIK_SDMA_PACKET_SRBM_WRITE              0xe
#define    CIK_SDMA_COPY_MAX_SIZE                  0x3fffe0

#endif /* _SID_H */

