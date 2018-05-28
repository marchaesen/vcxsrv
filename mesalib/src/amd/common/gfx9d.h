/*
 * Vega 3D Registers
 *
 * Copyright (C) 2016  Advanced Micro Devices, Inc.
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

#ifndef GFX9D_H
#define GFX9D_H

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
#define   S_008008_UTCL2_BUSY(x)                                      (((unsigned)(x) & 0x1) << 15)
#define   G_008008_UTCL2_BUSY(x)                                      (((x) >> 15) & 0x1)
#define   C_008008_UTCL2_BUSY                                         0xFFFF7FFF
#define   S_008008_EA_BUSY(x)                                         (((unsigned)(x) & 0x1) << 16)
#define   G_008008_EA_BUSY(x)                                         (((x) >> 16) & 0x1)
#define   C_008008_EA_BUSY                                            0xFFFEFFFF
#define   S_008008_RMI_BUSY(x)                                        (((unsigned)(x) & 0x1) << 17)
#define   G_008008_RMI_BUSY(x)                                        (((x) >> 17) & 0x1)
#define   C_008008_RMI_BUSY                                           0xFFFDFFFF
#define   S_008008_UTCL2_RQ_PENDING(x)                                (((unsigned)(x) & 0x1) << 18)
#define   G_008008_UTCL2_RQ_PENDING(x)                                (((x) >> 18) & 0x1)
#define   C_008008_UTCL2_RQ_PENDING                                   0xFFFBFFFF
#define   S_008008_CPF_RQ_PENDING(x)                                  (((unsigned)(x) & 0x1) << 19)
#define   G_008008_CPF_RQ_PENDING(x)                                  (((x) >> 19) & 0x1)
#define   C_008008_CPF_RQ_PENDING                                     0xFFF7FFFF
#define   S_008008_EA_LINK_BUSY(x)                                    (((unsigned)(x) & 0x1) << 20)
#define   G_008008_EA_LINK_BUSY(x)                                    (((x) >> 20) & 0x1)
#define   C_008008_EA_LINK_BUSY                                       0xFFEFFFFF
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
#define   S_008008_CPAXI_BUSY(x)                                      (((unsigned)(x) & 0x1) << 31)
#define   G_008008_CPAXI_BUSY(x)                                      (((x) >> 31) & 0x1)
#define   C_008008_CPAXI_BUSY                                         0x7FFFFFFF
#define R_008010_GRBM_STATUS                                            0x008010
#define   S_008010_ME0PIPE0_CMDFIFO_AVAIL(x)                          (((unsigned)(x) & 0x0F) << 0)
#define   G_008010_ME0PIPE0_CMDFIFO_AVAIL(x)                          (((x) >> 0) & 0x0F)
#define   C_008010_ME0PIPE0_CMDFIFO_AVAIL                             0xFFFFFFF0
#define   S_008010_RSMU_RQ_PENDING(x)                                 (((unsigned)(x) & 0x1) << 5)
#define   G_008010_RSMU_RQ_PENDING(x)                                 (((x) >> 5) & 0x1)
#define   C_008010_RSMU_RQ_PENDING                                    0xFFFFFFDF
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
#define R_008014_GRBM_STATUS_SE0                                        0x008014
#define   S_008014_DB_CLEAN(x)                                        (((unsigned)(x) & 0x1) << 1)
#define   G_008014_DB_CLEAN(x)                                        (((x) >> 1) & 0x1)
#define   C_008014_DB_CLEAN                                           0xFFFFFFFD
#define   S_008014_CB_CLEAN(x)                                        (((unsigned)(x) & 0x1) << 2)
#define   G_008014_CB_CLEAN(x)                                        (((x) >> 2) & 0x1)
#define   C_008014_CB_CLEAN                                           0xFFFFFFFB
#define   S_008014_RMI_BUSY(x)                                        (((unsigned)(x) & 0x1) << 21)
#define   G_008014_RMI_BUSY(x)                                        (((x) >> 21) & 0x1)
#define   C_008014_RMI_BUSY                                           0xFFDFFFFF
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
#define   S_008018_RMI_BUSY(x)                                        (((unsigned)(x) & 0x1) << 21)
#define   G_008018_RMI_BUSY(x)                                        (((x) >> 21) & 0x1)
#define   C_008018_RMI_BUSY                                           0xFFDFFFFF
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
#define   S_008038_RMI_BUSY(x)                                        (((unsigned)(x) & 0x1) << 21)
#define   G_008038_RMI_BUSY(x)                                        (((x) >> 21) & 0x1)
#define   C_008038_RMI_BUSY                                           0xFFDFFFFF
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
#define   S_00803C_RMI_BUSY(x)                                        (((unsigned)(x) & 0x1) << 21)
#define   G_00803C_RMI_BUSY(x)                                        (((x) >> 21) & 0x1)
#define   C_00803C_RMI_BUSY                                           0xFFDFFFFF
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
#define   S_0301F0_TC_NC_ACTION_ENA(x)                                (((unsigned)(x) & 0x1) << 3)
#define   G_0301F0_TC_NC_ACTION_ENA(x)                                (((x) >> 3) & 0x1)
#define   C_0301F0_TC_NC_ACTION_ENA                                   0xFFFFFFF7
#define   S_0301F0_TC_WC_ACTION_ENA(x)                                (((unsigned)(x) & 0x1) << 4)
#define   G_0301F0_TC_WC_ACTION_ENA(x)                                (((x) >> 4) & 0x1)
#define   C_0301F0_TC_WC_ACTION_ENA                                   0xFFFFFFEF
#define   S_0301F0_TC_INV_METADATA_ACTION_ENA(x)                      (((unsigned)(x) & 0x1) << 5)
#define   G_0301F0_TC_INV_METADATA_ACTION_ENA(x)                      (((x) >> 5) & 0x1)
#define   C_0301F0_TC_INV_METADATA_ACTION_ENA                         0xFFFFFFDF
#define   S_0301F0_TCL1_VOL_ACTION_ENA(x)                             (((unsigned)(x) & 0x1) << 15)
#define   G_0301F0_TCL1_VOL_ACTION_ENA(x)                             (((x) >> 15) & 0x1)
#define   C_0301F0_TCL1_VOL_ACTION_ENA                                0xFFFF7FFF
#define   S_0301F0_TC_WB_ACTION_ENA(x)                                (((unsigned)(x) & 0x1) << 18)
#define   G_0301F0_TC_WB_ACTION_ENA(x)                                (((x) >> 18) & 0x1)
#define   C_0301F0_TC_WB_ACTION_ENA                                   0xFFFBFFFF
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
#define   S_0301F0_SH_KCACHE_WB_ACTION_ENA(x)                         (((unsigned)(x) & 0x1) << 30)
#define   G_0301F0_SH_KCACHE_WB_ACTION_ENA(x)                         (((x) >> 30) & 0x1)
#define   C_0301F0_SH_KCACHE_WB_ACTION_ENA                            0xBFFFFFFF
#define R_0301F4_CP_COHER_SIZE                                          0x0301F4
#define R_0301F8_CP_COHER_BASE                                          0x0301F8
#define R_0301FC_CP_COHER_STATUS                                        0x0301FC
#define   S_0301FC_MEID(x)                                            (((unsigned)(x) & 0x03) << 24)
#define   G_0301FC_MEID(x)                                            (((x) >> 24) & 0x03)
#define   C_0301FC_MEID                                               0xFCFFFFFF
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
#define   S_008210_UTCL2IU_BUSY(x)                                    (((unsigned)(x) & 0x1) << 13)
#define   G_008210_UTCL2IU_BUSY(x)                                    (((x) >> 13) & 0x1)
#define   C_008210_UTCL2IU_BUSY                                       0xFFFFDFFF
#define   S_008210_SAVE_RESTORE_BUSY(x)                               (((unsigned)(x) & 0x1) << 14)
#define   G_008210_SAVE_RESTORE_BUSY(x)                               (((x) >> 14) & 0x1)
#define   C_008210_SAVE_RESTORE_BUSY                                  0xFFFFBFFF
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
#define   S_008218_UTCL2IU_WAITING_ON_FREE(x)                         (((unsigned)(x) & 0x1) << 22)
#define   G_008218_UTCL2IU_WAITING_ON_FREE(x)                         (((x) >> 22) & 0x1)
#define   C_008218_UTCL2IU_WAITING_ON_FREE                            0xFFBFFFFF
#define   S_008218_UTCL2IU_WAITING_ON_TAGS(x)                         (((unsigned)(x) & 0x1) << 23)
#define   G_008218_UTCL2IU_WAITING_ON_TAGS(x)                         (((x) >> 23) & 0x1)
#define   C_008218_UTCL2IU_WAITING_ON_TAGS                            0xFF7FFFFF
#define   S_008218_UTCL1_WAITING_ON_TRANS(x)                          (((unsigned)(x) & 0x1) << 24)
#define   G_008218_UTCL1_WAITING_ON_TRANS(x)                          (((x) >> 24) & 0x1)
#define   C_008218_UTCL1_WAITING_ON_TRANS                             0xFEFFFFFF
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
#define   S_00821C_UTCL2IU_BUSY(x)                                    (((unsigned)(x) & 0x1) << 17)
#define   G_00821C_UTCL2IU_BUSY(x)                                    (((x) >> 17) & 0x1)
#define   C_00821C_UTCL2IU_BUSY                                       0xFFFDFFFF
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
#define   S_008224_UTCL2IU_WAITING_ON_FREE(x)                         (((unsigned)(x) & 0x1) << 7)
#define   G_008224_UTCL2IU_WAITING_ON_FREE(x)                         (((x) >> 7) & 0x1)
#define   C_008224_UTCL2IU_WAITING_ON_FREE                            0xFFFFFF7F
#define   S_008224_UTCL2IU_WAITING_ON_TAGS(x)                         (((unsigned)(x) & 0x1) << 8)
#define   G_008224_UTCL2IU_WAITING_ON_TAGS(x)                         (((x) >> 8) & 0x1)
#define   C_008224_UTCL2IU_WAITING_ON_TAGS                            0xFFFFFEFF
#define   S_008224_GFX_UTCL1_WAITING_ON_TRANS(x)                      (((unsigned)(x) & 0x1) << 9)
#define   G_008224_GFX_UTCL1_WAITING_ON_TRANS(x)                      (((x) >> 9) & 0x1)
#define   C_008224_GFX_UTCL1_WAITING_ON_TRANS                         0xFFFFFDFF
#define   S_008224_CMP_UTCL1_WAITING_ON_TRANS(x)                      (((unsigned)(x) & 0x1) << 10)
#define   G_008224_CMP_UTCL1_WAITING_ON_TRANS(x)                      (((x) >> 10) & 0x1)
#define   C_008224_CMP_UTCL1_WAITING_ON_TRANS                         0xFFFFFBFF
#define   S_008224_RCIU_WAITING_ON_FREE(x)                            (((unsigned)(x) & 0x1) << 11)
#define   G_008224_RCIU_WAITING_ON_FREE(x)                            (((x) >> 11) & 0x1)
#define   C_008224_RCIU_WAITING_ON_FREE                               0xFFFFF7FF
#define R_030230_CP_COHER_SIZE_HI                                       0x030230
#define   S_030230_COHER_SIZE_HI_256B(x)                              (((unsigned)(x) & 0xFF) << 0)
#define   G_030230_COHER_SIZE_HI_256B(x)                              (((x) >> 0) & 0xFF)
#define   C_030230_COHER_SIZE_HI_256B                                 0xFFFFFF00
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
#define   S_008670_UTCL2IU_WAITING_ON_FREE(x)                         (((unsigned)(x) & 0x1) << 18)
#define   G_008670_UTCL2IU_WAITING_ON_FREE(x)                         (((x) >> 18) & 0x1)
#define   C_008670_UTCL2IU_WAITING_ON_FREE                            0xFFFBFFFF
#define   S_008670_UTCL2IU_WAITING_ON_TAGS(x)                         (((unsigned)(x) & 0x1) << 19)
#define   G_008670_UTCL2IU_WAITING_ON_TAGS(x)                         (((x) >> 19) & 0x1)
#define   C_008670_UTCL2IU_WAITING_ON_TAGS                            0xFFF7FFFF
#define   S_008670_UTCL1_WAITING_ON_TRANS(x)                          (((unsigned)(x) & 0x1) << 20)
#define   G_008670_UTCL1_WAITING_ON_TRANS(x)                          (((x) >> 20) & 0x1)
#define   C_008670_UTCL1_WAITING_ON_TRANS                             0xFFEFFFFF
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
#define   S_008680_UTCL2IU_BUSY(x)                                    (((unsigned)(x) & 0x1) << 14)
#define   G_008680_UTCL2IU_BUSY(x)                                    (((x) >> 14) & 0x1)
#define   C_008680_UTCL2IU_BUSY                                       0xFFFFBFFF
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
#define R_030904_VGT_GSVS_RING_SIZE                                     0x030904
#define R_030908_VGT_PRIMITIVE_TYPE                                     0x030908
#define   S_030908_PRIM_TYPE(x)                                       (((unsigned)(x) & 0x3F) << 0)
#define   G_030908_PRIM_TYPE(x)                                       (((x) >> 0) & 0x3F)
#define   C_030908_PRIM_TYPE                                          0xFFFFFFC0
#define R_03090C_VGT_INDEX_TYPE                                         0x03090C
#define   S_03090C_INDEX_TYPE(x)                                      (((unsigned)(x) & 0x03) << 0)
#define   G_03090C_INDEX_TYPE(x)                                      (((x) >> 0) & 0x03)
#define   C_03090C_INDEX_TYPE                                         0xFFFFFFFC
#define   S_03090C_PRIMGEN_EN(x)                                      (((unsigned)(x) & 0x1) << 8)
#define   G_03090C_PRIMGEN_EN(x)                                      (((x) >> 8) & 0x1)
#define   C_03090C_PRIMGEN_EN                                         0xFFFFFEFF
#define R_030910_VGT_STRMOUT_BUFFER_FILLED_SIZE_0                       0x030910
#define R_030914_VGT_STRMOUT_BUFFER_FILLED_SIZE_1                       0x030914
#define R_030918_VGT_STRMOUT_BUFFER_FILLED_SIZE_2                       0x030918
#define R_03091C_VGT_STRMOUT_BUFFER_FILLED_SIZE_3                       0x03091C
#define R_030920_VGT_MAX_VTX_INDX                                       0x030920
#define R_030924_VGT_MIN_VTX_INDX                                       0x030924
#define R_030928_VGT_INDX_OFFSET                                        0x030928
#define R_03092C_VGT_MULTI_PRIM_IB_RESET_EN                             0x03092C
#define   S_03092C_RESET_EN(x)                                        (((unsigned)(x) & 0x1) << 0)
#define   G_03092C_RESET_EN(x)                                        (((x) >> 0) & 0x1)
#define   C_03092C_RESET_EN                                           0xFFFFFFFE
#define   S_03092C_MATCH_ALL_BITS(x)                                  (((unsigned)(x) & 0x1) << 1)
#define   G_03092C_MATCH_ALL_BITS(x)                                  (((x) >> 1) & 0x1)
#define   C_03092C_MATCH_ALL_BITS                                     0xFFFFFFFD
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
#define R_030940_VGT_TF_MEMORY_BASE                                     0x030940
#define R_030944_VGT_TF_MEMORY_BASE_HI                                  0x030944
#define   S_030944_BASE_HI(x)                                         (((unsigned)(x) & 0xFF) << 0)
#define   G_030944_BASE_HI(x)                                         (((x) >> 0) & 0xFF)
#define   C_030944_BASE_HI                                            0xFFFFFF00
#define R_030948_WD_POS_BUF_BASE                                        0x030948
#define R_03094C_WD_POS_BUF_BASE_HI                                     0x03094C
#define   S_03094C_BASE_HI(x)                                         (((unsigned)(x) & 0xFF) << 0)
#define   G_03094C_BASE_HI(x)                                         (((x) >> 0) & 0xFF)
#define   C_03094C_BASE_HI                                            0xFFFFFF00
#define R_030950_WD_CNTL_SB_BUF_BASE                                    0x030950
#define R_030954_WD_CNTL_SB_BUF_BASE_HI                                 0x030954
#define   S_030954_BASE_HI(x)                                         (((unsigned)(x) & 0xFF) << 0)
#define   G_030954_BASE_HI(x)                                         (((x) >> 0) & 0xFF)
#define   C_030954_BASE_HI                                            0xFFFFFF00
#define R_030958_WD_INDEX_BUF_BASE                                      0x030958
#define R_03095C_WD_INDEX_BUF_BASE_HI                                   0x03095C
#define   S_03095C_BASE_HI(x)                                         (((unsigned)(x) & 0xFF) << 0)
#define   G_03095C_BASE_HI(x)                                         (((x) >> 0) & 0xFF)
#define   C_03095C_BASE_HI                                            0xFFFFFF00
#define R_030960_IA_MULTI_VGT_PARAM                                     0x030960
#define   S_030960_PRIMGROUP_SIZE(x)                                  (((unsigned)(x) & 0xFFFF) << 0)
#define   G_030960_PRIMGROUP_SIZE(x)                                  (((x) >> 0) & 0xFFFF)
#define   C_030960_PRIMGROUP_SIZE                                     0xFFFF0000
#define   S_030960_PARTIAL_VS_WAVE_ON(x)                              (((unsigned)(x) & 0x1) << 16)
#define   G_030960_PARTIAL_VS_WAVE_ON(x)                              (((x) >> 16) & 0x1)
#define   C_030960_PARTIAL_VS_WAVE_ON                                 0xFFFEFFFF
#define   S_030960_SWITCH_ON_EOP(x)                                   (((unsigned)(x) & 0x1) << 17)
#define   G_030960_SWITCH_ON_EOP(x)                                   (((x) >> 17) & 0x1)
#define   C_030960_SWITCH_ON_EOP                                      0xFFFDFFFF
#define   S_030960_PARTIAL_ES_WAVE_ON(x)                              (((unsigned)(x) & 0x1) << 18)
#define   G_030960_PARTIAL_ES_WAVE_ON(x)                              (((x) >> 18) & 0x1)
#define   C_030960_PARTIAL_ES_WAVE_ON                                 0xFFFBFFFF
#define   S_030960_SWITCH_ON_EOI(x)                                   (((unsigned)(x) & 0x1) << 19)
#define   G_030960_SWITCH_ON_EOI(x)                                   (((x) >> 19) & 0x1)
#define   C_030960_SWITCH_ON_EOI                                      0xFFF7FFFF
#define   S_030960_WD_SWITCH_ON_EOP(x)                                (((unsigned)(x) & 0x1) << 20)
#define   G_030960_WD_SWITCH_ON_EOP(x)                                (((x) >> 20) & 0x1)
#define   C_030960_WD_SWITCH_ON_EOP                                   0xFFEFFFFF
#define   S_030960_EN_INST_OPT_BASIC(x)                               (((unsigned)(x) & 0x1) << 21)
#define   G_030960_EN_INST_OPT_BASIC(x)                               (((x) >> 21) & 0x1)
#define   C_030960_EN_INST_OPT_BASIC                                  0xFFDFFFFF
#define   S_030960_EN_INST_OPT_ADV(x)                                 (((unsigned)(x) & 0x1) << 22)
#define   G_030960_EN_INST_OPT_ADV(x)                                 (((x) >> 22) & 0x1)
#define   C_030960_EN_INST_OPT_ADV                                    0xFFBFFFFF
#define   S_030960_HW_USE_ONLY(x)                                     (((unsigned)(x) & 0x1) << 23)
#define   G_030960_HW_USE_ONLY(x)                                     (((x) >> 23) & 0x1)
#define   C_030960_HW_USE_ONLY                                        0xFF7FFFFF
#define R_030968_VGT_INSTANCE_BASE_ID                                   0x030968
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
#define R_030D20_SQC_CACHES                                             0x030D20
#define   S_030D20_TARGET_INST(x)                                     (((unsigned)(x) & 0x1) << 0)
#define   G_030D20_TARGET_INST(x)                                     (((x) >> 0) & 0x1)
#define   C_030D20_TARGET_INST                                        0xFFFFFFFE
#define   S_030D20_TARGET_DATA(x)                                     (((unsigned)(x) & 0x1) << 1)
#define   G_030D20_TARGET_DATA(x)                                     (((x) >> 1) & 0x1)
#define   C_030D20_TARGET_DATA                                        0xFFFFFFFD
#define   S_030D20_INVALIDATE(x)                                      (((unsigned)(x) & 0x1) << 2)
#define   G_030D20_INVALIDATE(x)                                      (((x) >> 2) & 0x1)
#define   C_030D20_INVALIDATE                                         0xFFFFFFFB
#define   S_030D20_WRITEBACK(x)                                       (((unsigned)(x) & 0x1) << 3)
#define   G_030D20_WRITEBACK(x)                                       (((x) >> 3) & 0x1)
#define   C_030D20_WRITEBACK                                          0xFFFFFFF7
#define   S_030D20_VOL(x)                                             (((unsigned)(x) & 0x1) << 4)
#define   G_030D20_VOL(x)                                             (((x) >> 4) & 0x1)
#define   C_030D20_VOL                                                0xFFFFFFEF
#define   S_030D20_COMPLETE(x)                                        (((unsigned)(x) & 0x1) << 16)
#define   G_030D20_COMPLETE(x)                                        (((x) >> 16) & 0x1)
#define   C_030D20_COMPLETE                                           0xFFFEFFFF
#define R_030D24_SQC_WRITEBACK                                          0x030D24
#define   S_030D24_DWB(x)                                             (((unsigned)(x) & 0x1) << 0)
#define   G_030D24_DWB(x)                                             (((x) >> 0) & 0x1)
#define   C_030D24_DWB                                                0xFFFFFFFE
#define   S_030D24_DIRTY(x)                                           (((unsigned)(x) & 0x1) << 1)
#define   G_030D24_DIRTY(x)                                           (((x) >> 1) & 0x1)
#define   C_030D24_DIRTY                                              0xFFFFFFFD
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
#define   S_008F0C_DST_SEL_Y(x)                                       (((unsigned)(x) & 0x07) << 3)
#define   G_008F0C_DST_SEL_Y(x)                                       (((x) >> 3) & 0x07)
#define   C_008F0C_DST_SEL_Y                                          0xFFFFFFC7
#define   S_008F0C_DST_SEL_Z(x)                                       (((unsigned)(x) & 0x07) << 6)
#define   G_008F0C_DST_SEL_Z(x)                                       (((x) >> 6) & 0x07)
#define   C_008F0C_DST_SEL_Z                                          0xFFFFFE3F
#define   S_008F0C_DST_SEL_W(x)                                       (((unsigned)(x) & 0x07) << 9)
#define   G_008F0C_DST_SEL_W(x)                                       (((x) >> 9) & 0x07)
#define   C_008F0C_DST_SEL_W                                          0xFFFFF1FF
#define   S_008F0C_NUM_FORMAT(x)                                      (((unsigned)(x) & 0x07) << 12)
#define   G_008F0C_NUM_FORMAT(x)                                      (((x) >> 12) & 0x07)
#define   C_008F0C_NUM_FORMAT                                         0xFFFF8FFF
#define   S_008F0C_DATA_FORMAT(x)                                     (((unsigned)(x) & 0x0F) << 15)
#define   G_008F0C_DATA_FORMAT(x)                                     (((x) >> 15) & 0x0F)
#define   C_008F0C_DATA_FORMAT                                        0xFFF87FFF
#define   S_008F0C_USER_VM_ENABLE(x)                                  (((unsigned)(x) & 0x1) << 19)
#define   G_008F0C_USER_VM_ENABLE(x)                                  (((x) >> 19) & 0x1)
#define   C_008F0C_USER_VM_ENABLE                                     0xFFF7FFFF
#define   S_008F0C_USER_VM_MODE(x)                                    (((unsigned)(x) & 0x1) << 20)
#define   G_008F0C_USER_VM_MODE(x)                                    (((x) >> 20) & 0x1)
#define   C_008F0C_USER_VM_MODE                                       0xFFEFFFFF
#define   S_008F0C_INDEX_STRIDE(x)                                    (((unsigned)(x) & 0x03) << 21)
#define   G_008F0C_INDEX_STRIDE(x)                                    (((x) >> 21) & 0x03)
#define   C_008F0C_INDEX_STRIDE                                       0xFF9FFFFF
#define   S_008F0C_ADD_TID_ENABLE(x)                                  (((unsigned)(x) & 0x1) << 23)
#define   G_008F0C_ADD_TID_ENABLE(x)                                  (((x) >> 23) & 0x1)
#define   C_008F0C_ADD_TID_ENABLE                                     0xFF7FFFFF
#define   S_008F0C_NV(x)                                              (((unsigned)(x) & 0x1) << 27)
#define   G_008F0C_NV(x)                                              (((x) >> 27) & 0x1)
#define   C_008F0C_NV                                                 0xF7FFFFFF
#define   S_008F0C_TYPE(x)                                            (((unsigned)(x) & 0x03) << 30)
#define   G_008F0C_TYPE(x)                                            (((x) >> 30) & 0x03)
#define   C_008F0C_TYPE                                               0x3FFFFFFF
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
#define   S_008F14_DATA_FORMAT_GFX9(x)                                (((unsigned)(x) & 0x3F) << 20)
#define   G_008F14_DATA_FORMAT_GFX9(x)                                (((x) >> 20) & 0x3F)
#define   C_008F14_DATA_FORMAT_GFX9                                   0xFC0FFFFF
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
#define     V_008F14_IMG_DATA_FORMAT_8_AS_8_8_8_8                   0x17
#define     V_008F14_IMG_DATA_FORMAT_ETC2_RGB                       0x18
#define     V_008F14_IMG_DATA_FORMAT_ETC2_RGBA                      0x19
#define     V_008F14_IMG_DATA_FORMAT_ETC2_R                         0x1A
#define     V_008F14_IMG_DATA_FORMAT_ETC2_RG                        0x1B
#define     V_008F14_IMG_DATA_FORMAT_ETC2_RGBA1                     0x1C
#define     V_008F14_IMG_DATA_FORMAT_RESERVED_29                    0x1D
#define     V_008F14_IMG_DATA_FORMAT_RESERVED_30                    0x1E
#define     V_008F14_IMG_DATA_FORMAT_6E4                            0x1F
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
#define     V_008F14_IMG_DATA_FORMAT_16_AS_32_32                    0x2A
#define     V_008F14_IMG_DATA_FORMAT_16_AS_16_16_16_16_GFX9         0x2B
#define     V_008F14_IMG_DATA_FORMAT_16_AS_32_32_32_32_GFX9         0x2C
#define     V_008F14_IMG_DATA_FORMAT_FMASK                          0x2D /* NUM_FORMAT selects the format */
#define     V_008F14_IMG_DATA_FORMAT_ASTC_2D_LDR                    0x2E /* NUM_FORMAT selects the block size */
#define     V_008F14_IMG_DATA_FORMAT_ASTC_2D_HDR                    0x2F /* ditto */
#define     V_008F14_IMG_DATA_FORMAT_ASTC_2D_LDR_SRGB               0x30 /* ditto */
#define     V_008F14_IMG_DATA_FORMAT_ASTC_3D_LDR                    0x31 /* ditto */
#define     V_008F14_IMG_DATA_FORMAT_ASTC_3D_HDR                    0x32 /* ditto */
#define     V_008F14_IMG_DATA_FORMAT_ASTC_3D_LDR_SRGB               0x33 /* ditto */
#define     V_008F14_IMG_DATA_FORMAT_N_IN_16                        0x34
#define     V_008F14_IMG_DATA_FORMAT_N_IN_16_16                     0x35
#define     V_008F14_IMG_DATA_FORMAT_N_IN_16_16_16_16               0x36
#define     V_008F14_IMG_DATA_FORMAT_N_IN_16_AS_16_16_16_16         0x37
#define     V_008F14_IMG_DATA_FORMAT_RESERVED_56                    0x38
#define     V_008F14_IMG_DATA_FORMAT_4_4                            0x39
#define     V_008F14_IMG_DATA_FORMAT_6_5_5                          0x3A
#define     V_008F14_IMG_DATA_FORMAT_S8_16                          0x3B
#define     V_008F14_IMG_DATA_FORMAT_S8_32                          0x3C
#define     V_008F14_IMG_DATA_FORMAT_8_AS_32                        0x3D
#define     V_008F14_IMG_DATA_FORMAT_8_AS_32_32                     0x3E
#define     V_008F14_IMG_DATA_FORMAT_32_AS_32_32_32_32              0x3F
#define   S_008F14_NUM_FORMAT_GFX9(x)                                 (((unsigned)(x) & 0x0F) << 26)
#define   G_008F14_NUM_FORMAT_GFX9(x)                                 (((x) >> 26) & 0x0F)
#define   C_008F14_NUM_FORMAT_GFX9                                    0xC3FFFFFF
#define     V_008F14_IMG_NUM_FORMAT_UNORM                           0x00
#define     V_008F14_IMG_NUM_FORMAT_SNORM                           0x01
#define     V_008F14_IMG_NUM_FORMAT_USCALED                         0x02
#define     V_008F14_IMG_NUM_FORMAT_SSCALED                         0x03
#define     V_008F14_IMG_NUM_FORMAT_UINT                            0x04
#define     V_008F14_IMG_NUM_FORMAT_SINT                            0x05
#define     V_008F14_IMG_NUM_FORMAT_RESERVED_6                      0x06
#define     V_008F14_IMG_NUM_FORMAT_FLOAT                           0x07
#define     V_008F14_IMG_NUM_FORMAT_METADATA                        0x08
#define     V_008F14_IMG_NUM_FORMAT_SRGB                            0x09
#define     V_008F14_IMG_NUM_FORMAT_UNORM_UINT                      0x0A
#define   S_008F14_NUM_FORMAT_FMASK(x)                                (((unsigned)(x) & 0x0F) << 26)
#define   G_008F14_NUM_FORMAT_FMASK(x)                                (((x) >> 26) & 0x0F)
#define   C_008F14_NUM_FORMAT_FMASK                                   0xC3FFFFFF
#define     V_008F14_IMG_FMASK_8_2_1                                0x00
#define     V_008F14_IMG_FMASK_8_4_1                                0x01
#define     V_008F14_IMG_FMASK_8_8_1                                0x02
#define     V_008F14_IMG_FMASK_8_2_2                                0x03
#define     V_008F14_IMG_FMASK_8_4_2                                0x04
#define     V_008F14_IMG_FMASK_8_4_4                                0x05
#define     V_008F14_IMG_FMASK_16_16_1                              0x06
#define     V_008F14_IMG_FMASK_16_8_2                               0x07
#define     V_008F14_IMG_FMASK_32_16_2                              0x08
#define     V_008F14_IMG_FMASK_32_8_4                               0x09
#define     V_008F14_IMG_FMASK_32_8_8                               0x0A
#define     V_008F14_IMG_FMASK_64_16_4                              0x0B
#define     V_008F14_IMG_FMASK_64_16_8                              0x0C
#define   S_008F14_NUM_FORMAT_ASTC_2D(x)                              (((unsigned)(x) & 0x0F) << 26)
#define   G_008F14_NUM_FORMAT_ASTC_2D(x)                              (((x) >> 26) & 0x0F)
#define   C_008F14_NUM_FORMAT_ASTC_2D                               0xC3FFFFFF
#define     V_008F14_IMG_ASTC_2D_4x4                                0x00
#define     V_008F14_IMG_ASTC_2D_5x4                                0x01
#define     V_008F14_IMG_ASTC_2D_5x5                                0x02
#define     V_008F14_IMG_ASTC_2D_6x5                                0x03
#define     V_008F14_IMG_ASTC_2D_6x6                                0x04
#define     V_008F14_IMG_ASTC_2D_8x5                                0x05
#define     V_008F14_IMG_ASTC_2D_8x6                                0x06
#define     V_008F14_IMG_ASTC_2D_8x8                                0x07
#define     V_008F14_IMG_ASTC_2D_10x5                               0x08
#define     V_008F14_IMG_ASTC_2D_10x6                               0x09
#define     V_008F14_IMG_ASTC_2D_10x8                               0x0A
#define     V_008F14_IMG_ASTC_2D_10x10                              0x0B
#define     V_008F14_IMG_ASTC_2D_12x10                              0x0C
#define     V_008F14_IMG_ASTC_2D_12x12                              0x0D
#define   S_008F14_NUM_FORMAT_ASTC_3D(x)                              (((unsigned)(x) & 0x0F) << 26)
#define   G_008F14_NUM_FORMAT_ASTC_3D(x)                              (((x) >> 26) & 0x0F)
#define   C_008F14_NUM_FORMAT_ASTC_3D                               0xC3FFFFFF
#define     V_008F14_IMG_ASTC_3D_3x3x3                              0x00
#define     V_008F14_IMG_ASTC_3D_4x3x3                              0x01
#define     V_008F14_IMG_ASTC_3D_4x4x3                              0x02
#define     V_008F14_IMG_ASTC_3D_4x4x4                              0x03
#define     V_008F14_IMG_ASTC_3D_5x4x4                              0x04
#define     V_008F14_IMG_ASTC_3D_5x5x4                              0x05
#define     V_008F14_IMG_ASTC_3D_5x5x5                              0x06
#define     V_008F14_IMG_ASTC_3D_6x5x5                              0x07
#define     V_008F14_IMG_ASTC_3D_6x6x5                              0x08
#define     V_008F14_IMG_ASTC_3D_6x6x6                              0x09
#define   S_008F14_NV(x)                                              (((unsigned)(x) & 0x1) << 30)
#define   G_008F14_NV(x)                                              (((x) >> 30) & 0x1)
#define   C_008F14_NV                                                 0xBFFFFFFF
#define   S_008F14_META_DIRECT(x)                                     (((unsigned)(x) & 0x1) << 31)
#define   G_008F14_META_DIRECT(x)                                     (((x) >> 31) & 0x1)
#define   C_008F14_META_DIRECT                                        0x7FFFFFFF
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
#define R_030F1C_DB_OCCLUSION_COUNT3_HI                                 0x030F1C
#define   S_030F1C_COUNT_HI(x)                                        (((unsigned)(x) & 0x7FFFFFFF) << 0)
#define   G_030F1C_COUNT_HI(x)                                        (((x) >> 0) & 0x7FFFFFFF)
#define   C_030F1C_COUNT_HI                                           0x80000000
#define R_008F1C_SQ_IMG_RSRC_WORD3                                      0x008F1C
#define   S_008F1C_DST_SEL_X(x)                                       (((unsigned)(x) & 0x07) << 0)
#define   G_008F1C_DST_SEL_X(x)                                       (((x) >> 0) & 0x07)
#define   C_008F1C_DST_SEL_X                                          0xFFFFFFF8
#define   S_008F1C_DST_SEL_Y(x)                                       (((unsigned)(x) & 0x07) << 3)
#define   G_008F1C_DST_SEL_Y(x)                                       (((x) >> 3) & 0x07)
#define   C_008F1C_DST_SEL_Y                                          0xFFFFFFC7
#define   S_008F1C_DST_SEL_Z(x)                                       (((unsigned)(x) & 0x07) << 6)
#define   G_008F1C_DST_SEL_Z(x)                                       (((x) >> 6) & 0x07)
#define   C_008F1C_DST_SEL_Z                                          0xFFFFFE3F
#define   S_008F1C_DST_SEL_W(x)                                       (((unsigned)(x) & 0x07) << 9)
#define   G_008F1C_DST_SEL_W(x)                                       (((x) >> 9) & 0x07)
#define   C_008F1C_DST_SEL_W                                          0xFFFFF1FF
#define   S_008F1C_BASE_LEVEL(x)                                      (((unsigned)(x) & 0x0F) << 12)
#define   G_008F1C_BASE_LEVEL(x)                                      (((x) >> 12) & 0x0F)
#define   C_008F1C_BASE_LEVEL                                         0xFFFF0FFF
#define   S_008F1C_LAST_LEVEL(x)                                      (((unsigned)(x) & 0x0F) << 16)
#define   G_008F1C_LAST_LEVEL(x)                                      (((x) >> 16) & 0x0F)
#define   C_008F1C_LAST_LEVEL                                         0xFFF0FFFF
#define   S_008F1C_SW_MODE(x)                                         (((unsigned)(x) & 0x1F) << 20)
#define   G_008F1C_SW_MODE(x)                                         (((x) >> 20) & 0x1F)
#define   C_008F1C_SW_MODE                                            0xFE0FFFFF
#define   S_008F1C_TYPE(x)                                            (((unsigned)(x) & 0x0F) << 28)
#define   G_008F1C_TYPE(x)                                            (((x) >> 28) & 0x0F)
#define   C_008F1C_TYPE                                               0x0FFFFFFF
#define R_008F20_SQ_IMG_RSRC_WORD4                                      0x008F20
#define   S_008F20_DEPTH(x)                                           (((unsigned)(x) & 0x1FFF) << 0)
#define   G_008F20_DEPTH(x)                                           (((x) >> 0) & 0x1FFF)
#define   C_008F20_DEPTH                                              0xFFFFE000
#define   S_008F20_PITCH_GFX9(x)                                      (((unsigned)(x) & 0xFFFF) << 13)
#define   G_008F20_PITCH_GFX9(x)                                      (((x) >> 13) & 0xFFFF)
#define   C_008F20_PITCH_GFX9                                         0xE0001FFF
#define   S_008F20_BC_SWIZZLE(x)                                      (((unsigned)(x) & 0x07) << 29)
#define   G_008F20_BC_SWIZZLE(x)                                      (((x) >> 29) & 0x07)
#define   C_008F20_BC_SWIZZLE                                         0x1FFFFFFF
#define     V_008F20_BC_SWIZZLE_XYZW					0
#define     V_008F20_BC_SWIZZLE_XWYZ					1
#define     V_008F20_BC_SWIZZLE_WZYX					2
#define     V_008F20_BC_SWIZZLE_WXYZ					3
#define     V_008F20_BC_SWIZZLE_ZYXW					4
#define     V_008F20_BC_SWIZZLE_YXWZ					5
#define R_008F24_SQ_IMG_RSRC_WORD5                                      0x008F24
#define   S_008F24_BASE_ARRAY(x)                                      (((unsigned)(x) & 0x1FFF) << 0)
#define   G_008F24_BASE_ARRAY(x)                                      (((x) >> 0) & 0x1FFF)
#define   C_008F24_BASE_ARRAY                                         0xFFFFE000
#define   S_008F24_ARRAY_PITCH(x)                                     (((unsigned)(x) & 0x0F) << 13)
#define   G_008F24_ARRAY_PITCH(x)                                     (((x) >> 13) & 0x0F)
#define   C_008F24_ARRAY_PITCH                                        0xFFFE1FFF
#define   S_008F24_META_DATA_ADDRESS(x)                               (((unsigned)(x) & 0xFF) << 17)
#define   G_008F24_META_DATA_ADDRESS(x)                               (((x) >> 17) & 0xFF)
#define   C_008F24_META_DATA_ADDRESS                                  0xFE01FFFF
#define   S_008F24_META_LINEAR(x)                                     (((unsigned)(x) & 0x1) << 25)
#define   G_008F24_META_LINEAR(x)                                     (((x) >> 25) & 0x1)
#define   C_008F24_META_LINEAR                                        0xFDFFFFFF
#define   S_008F24_META_PIPE_ALIGNED(x)                               (((unsigned)(x) & 0x1) << 26)
#define   G_008F24_META_PIPE_ALIGNED(x)                               (((x) >> 26) & 0x1)
#define   C_008F24_META_PIPE_ALIGNED                                  0xFBFFFFFF
#define   S_008F24_META_RB_ALIGNED(x)                                 (((unsigned)(x) & 0x1) << 27)
#define   G_008F24_META_RB_ALIGNED(x)                                 (((x) >> 27) & 0x1)
#define   C_008F24_META_RB_ALIGNED                                    0xF7FFFFFF
#define   S_008F24_MAX_MIP(x)                                         (((unsigned)(x) & 0x0F) << 28)
#define   G_008F24_MAX_MIP(x)                                         (((x) >> 28) & 0x0F)
#define   C_008F24_MAX_MIP                                            0x0FFFFFFF
#define R_008F28_SQ_IMG_RSRC_WORD6                                      0x008F28
#define   S_008F28_MIN_LOD_WARN(x)                                    (((unsigned)(x) & 0xFFF) << 0)
#define   G_008F28_MIN_LOD_WARN(x)                                    (((x) >> 0) & 0xFFF)
#define   C_008F28_MIN_LOD_WARN                                       0xFFFFF000
#define   S_008F28_COUNTER_BANK_ID(x)                                 (((unsigned)(x) & 0xFF) << 12)
#define   G_008F28_COUNTER_BANK_ID(x)                                 (((x) >> 12) & 0xFF)
#define   C_008F28_COUNTER_BANK_ID                                    0xFFF00FFF
#define   S_008F28_LOD_HDW_CNT_EN(x)                                  (((unsigned)(x) & 0x1) << 20)
#define   G_008F28_LOD_HDW_CNT_EN(x)                                  (((x) >> 20) & 0x1)
#define   C_008F28_LOD_HDW_CNT_EN                                     0xFFEFFFFF
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
#define R_008F2C_SQ_IMG_RSRC_WORD7                                      0x008F2C
#define R_008F30_SQ_IMG_SAMP_WORD0                                      0x008F30
#define   S_008F30_CLAMP_X(x)                                         (((unsigned)(x) & 0x07) << 0)
#define   G_008F30_CLAMP_X(x)                                         (((x) >> 0) & 0x07)
#define   C_008F30_CLAMP_X                                            0xFFFFFFF8
#define   S_008F30_CLAMP_Y(x)                                         (((unsigned)(x) & 0x07) << 3)
#define   G_008F30_CLAMP_Y(x)                                         (((x) >> 3) & 0x07)
#define   C_008F30_CLAMP_Y                                            0xFFFFFFC7
#define   S_008F30_CLAMP_Z(x)                                         (((unsigned)(x) & 0x07) << 6)
#define   G_008F30_CLAMP_Z(x)                                         (((x) >> 6) & 0x07)
#define   C_008F30_CLAMP_Z                                            0xFFFFFE3F
#define   S_008F30_MAX_ANISO_RATIO(x)                                 (((unsigned)(x) & 0x07) << 9)
#define   G_008F30_MAX_ANISO_RATIO(x)                                 (((x) >> 9) & 0x07)
#define   C_008F30_MAX_ANISO_RATIO                                    0xFFFFF1FF
#define   S_008F30_DEPTH_COMPARE_FUNC(x)                              (((unsigned)(x) & 0x07) << 12)
#define   G_008F30_DEPTH_COMPARE_FUNC(x)                              (((x) >> 12) & 0x07)
#define   C_008F30_DEPTH_COMPARE_FUNC                                 0xFFFF8FFF
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
#define   S_008F30_COMPAT_MODE(x)                                     (((unsigned)(x) & 0x1) << 31)
#define   G_008F30_COMPAT_MODE(x)                                     (((x) >> 31) & 0x1)
#define   C_008F30_COMPAT_MODE                                        0x7FFFFFFF
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
#define   S_008F38_XY_MIN_FILTER(x)                                   (((unsigned)(x) & 0x03) << 22)
#define   G_008F38_XY_MIN_FILTER(x)                                   (((x) >> 22) & 0x03)
#define   C_008F38_XY_MIN_FILTER                                      0xFF3FFFFF
#define   S_008F38_Z_FILTER(x)                                        (((unsigned)(x) & 0x03) << 24)
#define   G_008F38_Z_FILTER(x)                                        (((x) >> 24) & 0x03)
#define   C_008F38_Z_FILTER                                           0xFCFFFFFF
#define   S_008F38_MIP_FILTER(x)                                      (((unsigned)(x) & 0x03) << 26)
#define   G_008F38_MIP_FILTER(x)                                      (((x) >> 26) & 0x03)
#define   C_008F38_MIP_FILTER                                         0xF3FFFFFF
#define   S_008F38_MIP_POINT_PRECLAMP(x)                              (((unsigned)(x) & 0x1) << 28)
#define   G_008F38_MIP_POINT_PRECLAMP(x)                              (((x) >> 28) & 0x1)
#define   C_008F38_MIP_POINT_PRECLAMP                                 0xEFFFFFFF
#define   S_008F38_BLEND_ZERO_PRT(x)                                  (((unsigned)(x) & 0x1) << 29)
#define   G_008F38_BLEND_ZERO_PRT(x)                                  (((x) >> 29) & 0x1)
#define   C_008F38_BLEND_ZERO_PRT                                     0xDFFFFFFF
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
#define   S_008F3C_SKIP_DEGAMMA(x)                                    (((unsigned)(x) & 0x1) << 12)
#define   G_008F3C_SKIP_DEGAMMA(x)                                    (((x) >> 12) & 0x1)
#define   C_008F3C_SKIP_DEGAMMA                                       0xFFFFEFFF
#define   S_008F3C_BORDER_COLOR_TYPE(x)                               (((unsigned)(x) & 0x03) << 30)
#define   G_008F3C_BORDER_COLOR_TYPE(x)                               (((x) >> 30) & 0x03)
#define   C_008F3C_BORDER_COLOR_TYPE                                  0x3FFFFFFF
#define R_030FF8_DB_ZPASS_COUNT_LOW                                     0x030FF8
#define R_030FFC_DB_ZPASS_COUNT_HI                                      0x030FFC
#define   S_030FFC_COUNT_HI(x)                                        (((unsigned)(x) & 0x7FFFFFFF) << 0)
#define   G_030FFC_COUNT_HI(x)                                        (((x) >> 0) & 0x7FFFFFFF)
#define   C_030FFC_COUNT_HI                                           0x80000000
#define R_031100_SPI_CONFIG_CNTL                                        0x031100
#define   S_031100_GPR_WRITE_PRIORITY(x)                              (((unsigned)(x) & 0x1FFFFF) << 0)
#define   G_031100_GPR_WRITE_PRIORITY(x)                              (((x) >> 0) & 0x1FFFFF)
#define   C_031100_GPR_WRITE_PRIORITY                                 0xFFE00000
#define   S_031100_EXP_PRIORITY_ORDER(x)                              (((unsigned)(x) & 0x07) << 21)
#define   G_031100_EXP_PRIORITY_ORDER(x)                              (((x) >> 21) & 0x07)
#define   C_031100_EXP_PRIORITY_ORDER                                 0xFF1FFFFF
#define   S_031100_ENABLE_SQG_TOP_EVENTS(x)                           (((unsigned)(x) & 0x1) << 24)
#define   G_031100_ENABLE_SQG_TOP_EVENTS(x)                           (((x) >> 24) & 0x1)
#define   C_031100_ENABLE_SQG_TOP_EVENTS                              0xFEFFFFFF
#define   S_031100_ENABLE_SQG_BOP_EVENTS(x)                           (((unsigned)(x) & 0x1) << 25)
#define   G_031100_ENABLE_SQG_BOP_EVENTS(x)                           (((x) >> 25) & 0x1)
#define   C_031100_ENABLE_SQG_BOP_EVENTS                              0xFDFFFFFF
#define   S_031100_RSRC_MGMT_RESET(x)                                 (((unsigned)(x) & 0x1) << 26)
#define   G_031100_RSRC_MGMT_RESET(x)                                 (((x) >> 26) & 0x1)
#define   C_031100_RSRC_MGMT_RESET                                    0xFBFFFFFF
#define   S_031100_TTRACE_STALL_ALL(x)                                (((unsigned)(x) & 0x1) << 27)
#define   G_031100_TTRACE_STALL_ALL(x)                                (((x) >> 27) & 0x1)
#define   C_031100_TTRACE_STALL_ALL                                   0xF7FFFFFF
#define   S_031100_ALLOC_ARB_LRU_ENA(x)                               (((unsigned)(x) & 0x1) << 28)
#define   G_031100_ALLOC_ARB_LRU_ENA(x)                               (((x) >> 28) & 0x1)
#define   C_031100_ALLOC_ARB_LRU_ENA                                  0xEFFFFFFF
#define   S_031100_EXP_ARB_LRU_ENA(x)                                 (((unsigned)(x) & 0x1) << 29)
#define   G_031100_EXP_ARB_LRU_ENA(x)                                 (((x) >> 29) & 0x1)
#define   C_031100_EXP_ARB_LRU_ENA                                    0xDFFFFFFF
#define   S_031100_PS_PKR_PRIORITY_CNTL(x)                            (((unsigned)(x) & 0x03) << 30)
#define   G_031100_PS_PKR_PRIORITY_CNTL(x)                            (((x) >> 30) & 0x03)
#define   C_031100_PS_PKR_PRIORITY_CNTL                               0x3FFFFFFF
#define R_031104_SPI_CONFIG_CNTL_1                                      0x031104
#define   S_031104_VTX_DONE_DELAY(x)                                  (((unsigned)(x) & 0x0F) << 0)
#define   G_031104_VTX_DONE_DELAY(x)                                  (((x) >> 0) & 0x0F)
#define   C_031104_VTX_DONE_DELAY                                     0xFFFFFFF0
#define   S_031104_INTERP_ONE_PRIM_PER_ROW(x)                         (((unsigned)(x) & 0x1) << 4)
#define   G_031104_INTERP_ONE_PRIM_PER_ROW(x)                         (((x) >> 4) & 0x1)
#define   C_031104_INTERP_ONE_PRIM_PER_ROW                            0xFFFFFFEF
#define   S_031104_BATON_RESET_DISABLE(x)                             (((unsigned)(x) & 0x1) << 5)
#define   G_031104_BATON_RESET_DISABLE(x)                             (((x) >> 5) & 0x1)
#define   C_031104_BATON_RESET_DISABLE                                0xFFFFFFDF
#define   S_031104_PC_LIMIT_ENABLE(x)                                 (((unsigned)(x) & 0x1) << 6)
#define   G_031104_PC_LIMIT_ENABLE(x)                                 (((x) >> 6) & 0x1)
#define   C_031104_PC_LIMIT_ENABLE                                    0xFFFFFFBF
#define   S_031104_PC_LIMIT_STRICT(x)                                 (((unsigned)(x) & 0x1) << 7)
#define   G_031104_PC_LIMIT_STRICT(x)                                 (((x) >> 7) & 0x1)
#define   C_031104_PC_LIMIT_STRICT                                    0xFFFFFF7F
#define   S_031104_CRC_SIMD_ID_WADDR_DISABLE(x)                       (((unsigned)(x) & 0x1) << 8)
#define   G_031104_CRC_SIMD_ID_WADDR_DISABLE(x)                       (((x) >> 8) & 0x1)
#define   C_031104_CRC_SIMD_ID_WADDR_DISABLE                          0xFFFFFEFF
#define   S_031104_LBPW_CU_CHK_MODE(x)                                (((unsigned)(x) & 0x1) << 9)
#define   G_031104_LBPW_CU_CHK_MODE(x)                                (((x) >> 9) & 0x1)
#define   C_031104_LBPW_CU_CHK_MODE                                   0xFFFFFDFF
#define   S_031104_LBPW_CU_CHK_CNT(x)                                 (((unsigned)(x) & 0x0F) << 10)
#define   G_031104_LBPW_CU_CHK_CNT(x)                                 (((x) >> 10) & 0x0F)
#define   C_031104_LBPW_CU_CHK_CNT                                    0xFFFFC3FF
#define   S_031104_CSC_PWR_SAVE_DISABLE(x)                            (((unsigned)(x) & 0x1) << 14)
#define   G_031104_CSC_PWR_SAVE_DISABLE(x)                            (((x) >> 14) & 0x1)
#define   C_031104_CSC_PWR_SAVE_DISABLE                               0xFFFFBFFF
#define   S_031104_CSG_PWR_SAVE_DISABLE(x)                            (((unsigned)(x) & 0x1) << 15)
#define   G_031104_CSG_PWR_SAVE_DISABLE(x)                            (((x) >> 15) & 0x1)
#define   C_031104_CSG_PWR_SAVE_DISABLE                               0xFFFF7FFF
#define   S_031104_PC_LIMIT_SIZE(x)                                   (((unsigned)(x) & 0xFFFF) << 16)
#define   G_031104_PC_LIMIT_SIZE(x)                                   (((x) >> 16) & 0xFFFF)
#define   C_031104_PC_LIMIT_SIZE                                      0x0000FFFF
#define R_031108_SPI_CONFIG_CNTL_2                                      0x031108
#define   S_031108_CONTEXT_SAVE_WAIT_GDS_REQUEST_CYCLE_OVHD(x)        (((unsigned)(x) & 0x0F) << 0)
#define   G_031108_CONTEXT_SAVE_WAIT_GDS_REQUEST_CYCLE_OVHD(x)        (((x) >> 0) & 0x0F)
#define   C_031108_CONTEXT_SAVE_WAIT_GDS_REQUEST_CYCLE_OVHD           0xFFFFFFF0
#define   S_031108_CONTEXT_SAVE_WAIT_GDS_GRANT_CYCLE_OVHD(x)          (((unsigned)(x) & 0x0F) << 4)
#define   G_031108_CONTEXT_SAVE_WAIT_GDS_GRANT_CYCLE_OVHD(x)          (((x) >> 4) & 0x0F)
#define   C_031108_CONTEXT_SAVE_WAIT_GDS_GRANT_CYCLE_OVHD             0xFFFFFF0F
#define R_0098F8_GB_ADDR_CONFIG                                         0x0098F8
#define   S_0098F8_NUM_PIPES(x)                                       (((unsigned)(x) & 0x07) << 0)
#define   G_0098F8_NUM_PIPES(x)                                       (((x) >> 0) & 0x07)
#define   C_0098F8_NUM_PIPES                                          0xFFFFFFF8
#define   S_0098F8_PIPE_INTERLEAVE_SIZE_GFX9(x)                       (((unsigned)(x) & 0x07) << 3)
#define   G_0098F8_PIPE_INTERLEAVE_SIZE_GFX9(x)                       (((x) >> 3) & 0x07)
#define   C_0098F8_PIPE_INTERLEAVE_SIZE_GFX9                          0xFFFFFFC7
#define   S_0098F8_MAX_COMPRESSED_FRAGS(x)                            (((unsigned)(x) & 0x03) << 6)
#define   G_0098F8_MAX_COMPRESSED_FRAGS(x)                            (((x) >> 6) & 0x03)
#define   C_0098F8_MAX_COMPRESSED_FRAGS                               0xFFFFFF3F
#define   S_0098F8_BANK_INTERLEAVE_SIZE(x)                            (((unsigned)(x) & 0x07) << 8)
#define   G_0098F8_BANK_INTERLEAVE_SIZE(x)                            (((x) >> 8) & 0x07)
#define   C_0098F8_BANK_INTERLEAVE_SIZE                               0xFFFFF8FF
#define   S_0098F8_NUM_BANKS(x)                                       (((unsigned)(x) & 0x07) << 12)
#define   G_0098F8_NUM_BANKS(x)                                       (((x) >> 12) & 0x07)
#define   C_0098F8_NUM_BANKS                                          0xFFFF8FFF
#define   S_0098F8_SHADER_ENGINE_TILE_SIZE(x)                         (((unsigned)(x) & 0x07) << 16)
#define   G_0098F8_SHADER_ENGINE_TILE_SIZE(x)                         (((x) >> 16) & 0x07)
#define   C_0098F8_SHADER_ENGINE_TILE_SIZE                            0xFFF8FFFF
#define   S_0098F8_NUM_SHADER_ENGINES_GFX9(x)                         (((unsigned)(x) & 0x03) << 19)
#define   G_0098F8_NUM_SHADER_ENGINES_GFX9(x)                         (((x) >> 19) & 0x03)
#define   C_0098F8_NUM_SHADER_ENGINES_GFX9                            0xFFE7FFFF
#define   S_0098F8_NUM_GPUS_GFX9(x)                                   (((unsigned)(x) & 0x07) << 21)
#define   G_0098F8_NUM_GPUS_GFX9(x)                                   (((x) >> 21) & 0x07)
#define   C_0098F8_NUM_GPUS_GFX9                                      0xFF1FFFFF
#define   S_0098F8_MULTI_GPU_TILE_SIZE(x)                             (((unsigned)(x) & 0x03) << 24)
#define   G_0098F8_MULTI_GPU_TILE_SIZE(x)                             (((x) >> 24) & 0x03)
#define   C_0098F8_MULTI_GPU_TILE_SIZE                                0xFCFFFFFF
#define   S_0098F8_NUM_RB_PER_SE(x)                                   (((unsigned)(x) & 0x03) << 26)
#define   G_0098F8_NUM_RB_PER_SE(x)                                   (((x) >> 26) & 0x03)
#define   C_0098F8_NUM_RB_PER_SE                                      0xF3FFFFFF
#define   S_0098F8_ROW_SIZE(x)                                        (((unsigned)(x) & 0x03) << 28)
#define   G_0098F8_ROW_SIZE(x)                                        (((x) >> 28) & 0x03)
#define   C_0098F8_ROW_SIZE                                           0xCFFFFFFF
#define   S_0098F8_NUM_LOWER_PIPES(x)                                 (((unsigned)(x) & 0x1) << 30)
#define   G_0098F8_NUM_LOWER_PIPES(x)                                 (((x) >> 30) & 0x1)
#define   C_0098F8_NUM_LOWER_PIPES                                    0xBFFFFFFF
#define   S_0098F8_SE_ENABLE(x)                                       (((unsigned)(x) & 0x1) << 31)
#define   G_0098F8_SE_ENABLE(x)                                       (((x) >> 31) & 0x1)
#define   C_0098F8_SE_ENABLE                                          0x7FFFFFFF
#define R_009910_GB_TILE_MODE0                                          0x009910
#define   S_009910_ARRAY_MODE(x)                                      (((unsigned)(x) & 0x0F) << 2)
#define   G_009910_ARRAY_MODE(x)                                      (((x) >> 2) & 0x0F)
#define   C_009910_ARRAY_MODE                                         0xFFFFFFC3
#define   S_009910_PIPE_CONFIG(x)                                     (((unsigned)(x) & 0x1F) << 6)
#define   G_009910_PIPE_CONFIG(x)                                     (((x) >> 6) & 0x1F)
#define   C_009910_PIPE_CONFIG                                        0xFFFFF83F
#define   S_009910_TILE_SPLIT(x)                                      (((unsigned)(x) & 0x07) << 11)
#define   G_009910_TILE_SPLIT(x)                                      (((x) >> 11) & 0x07)
#define   C_009910_TILE_SPLIT                                         0xFFFFC7FF
#define   S_009910_MICRO_TILE_MODE_NEW(x)                             (((unsigned)(x) & 0x07) << 22)
#define   G_009910_MICRO_TILE_MODE_NEW(x)                             (((x) >> 22) & 0x07)
#define   C_009910_MICRO_TILE_MODE_NEW                                0xFE3FFFFF
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
#define   S_00B01C_SIMD_DISABLE(x)                                    (((unsigned)(x) & 0x0F) << 26)
#define   G_00B01C_SIMD_DISABLE(x)                                    (((x) >> 26) & 0x0F)
#define   C_00B01C_SIMD_DISABLE                                       0xC3FFFFFF
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
#define   S_00B028_CDBG_USER(x)                                       (((unsigned)(x) & 0x1) << 28)
#define   G_00B028_CDBG_USER(x)                                       (((x) >> 28) & 0x1)
#define   C_00B028_CDBG_USER                                          0xEFFFFFFF
#define   S_00B028_FP16_OVFL(x)                                       (((unsigned)(x) & 0x1) << 29)
#define   G_00B028_FP16_OVFL(x)                                       (((x) >> 29) & 0x1)
#define   C_00B028_FP16_OVFL                                          0xDFFFFFFF
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
#define   S_00B02C_EXCP_EN(x)                                         (((unsigned)(x) & 0x1FF) << 16)
#define   G_00B02C_EXCP_EN(x)                                         (((x) >> 16) & 0x1FF)
#define   C_00B02C_EXCP_EN                                            0xFE00FFFF
#define   S_00B02C_LOAD_COLLISION_WAVEID(x)                           (((unsigned)(x) & 0x1) << 25)
#define   G_00B02C_LOAD_COLLISION_WAVEID(x)                           (((x) >> 25) & 0x1)
#define   C_00B02C_LOAD_COLLISION_WAVEID                              0xFDFFFFFF
#define   S_00B02C_LOAD_INTRAWAVE_COLLISION(x)                        (((unsigned)(x) & 0x1) << 26)
#define   G_00B02C_LOAD_INTRAWAVE_COLLISION(x)                        (((x) >> 26) & 0x1)
#define   C_00B02C_LOAD_INTRAWAVE_COLLISION                           0xFBFFFFFF
#define   S_00B02C_SKIP_USGPR0(x)                                     (((unsigned)(x) & 0x1) << 27)
#define   G_00B02C_SKIP_USGPR0(x)                                     (((x) >> 27) & 0x1)
#define   C_00B02C_SKIP_USGPR0                                        0xF7FFFFFF
#define   S_00B02C_USER_SGPR_MSB(x)                                   (((unsigned)(x) & 0x1) << 28)
#define   G_00B02C_USER_SGPR_MSB(x)                                   (((x) >> 28) & 0x1)
#define   C_00B02C_USER_SGPR_MSB                                      0xEFFFFFFF
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
#define R_00B070_SPI_SHADER_USER_DATA_PS_16                             0x00B070
#define R_00B074_SPI_SHADER_USER_DATA_PS_17                             0x00B074
#define R_00B078_SPI_SHADER_USER_DATA_PS_18                             0x00B078
#define R_00B07C_SPI_SHADER_USER_DATA_PS_19                             0x00B07C
#define R_00B080_SPI_SHADER_USER_DATA_PS_20                             0x00B080
#define R_00B084_SPI_SHADER_USER_DATA_PS_21                             0x00B084
#define R_00B088_SPI_SHADER_USER_DATA_PS_22                             0x00B088
#define R_00B08C_SPI_SHADER_USER_DATA_PS_23                             0x00B08C
#define R_00B090_SPI_SHADER_USER_DATA_PS_24                             0x00B090
#define R_00B094_SPI_SHADER_USER_DATA_PS_25                             0x00B094
#define R_00B098_SPI_SHADER_USER_DATA_PS_26                             0x00B098
#define R_00B09C_SPI_SHADER_USER_DATA_PS_27                             0x00B09C
#define R_00B0A0_SPI_SHADER_USER_DATA_PS_28                             0x00B0A0
#define R_00B0A4_SPI_SHADER_USER_DATA_PS_29                             0x00B0A4
#define R_00B0A8_SPI_SHADER_USER_DATA_PS_30                             0x00B0A8
#define R_00B0AC_SPI_SHADER_USER_DATA_PS_31                             0x00B0AC
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
#define   S_00B118_SIMD_DISABLE(x)                                    (((unsigned)(x) & 0x0F) << 26)
#define   G_00B118_SIMD_DISABLE(x)                                    (((x) >> 26) & 0x0F)
#define   C_00B118_SIMD_DISABLE                                       0xC3FFFFFF
#define R_00B11C_SPI_SHADER_LATE_ALLOC_VS                               0x00B11C
#define   S_00B11C_LIMIT(x)                                           (((unsigned)(x) & 0x3F) << 0)
#define   G_00B11C_LIMIT(x)                                           (((x) >> 0) & 0x3F)
#define   C_00B11C_LIMIT                                              0xFFFFFFC0
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
#define   S_00B128_CDBG_USER(x)                                       (((unsigned)(x) & 0x1) << 30)
#define   G_00B128_CDBG_USER(x)                                       (((x) >> 30) & 0x1)
#define   C_00B128_CDBG_USER                                          0xBFFFFFFF
#define   S_00B128_FP16_OVFL(x)                                       (((unsigned)(x) & 0x1) << 31)
#define   G_00B128_FP16_OVFL(x)                                       (((x) >> 31) & 0x1)
#define   C_00B128_FP16_OVFL                                          0x7FFFFFFF
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
#define   S_00B12C_EXCP_EN(x)                                         (((unsigned)(x) & 0x1FF) << 13)
#define   G_00B12C_EXCP_EN(x)                                         (((x) >> 13) & 0x1FF)
#define   C_00B12C_EXCP_EN                                            0xFFC01FFF
#define   S_00B12C_PC_BASE_EN(x)                                      (((unsigned)(x) & 0x1) << 22)
#define   G_00B12C_PC_BASE_EN(x)                                      (((x) >> 22) & 0x1)
#define   C_00B12C_PC_BASE_EN                                         0xFFBFFFFF
#define   S_00B12C_DISPATCH_DRAW_EN(x)                                (((unsigned)(x) & 0x1) << 24)
#define   G_00B12C_DISPATCH_DRAW_EN(x)                                (((x) >> 24) & 0x1)
#define   C_00B12C_DISPATCH_DRAW_EN                                   0xFEFFFFFF
#define   S_00B12C_SKIP_USGPR0(x)                                     (((unsigned)(x) & 0x1) << 27)
#define   G_00B12C_SKIP_USGPR0(x)                                     (((x) >> 27) & 0x1)
#define   C_00B12C_SKIP_USGPR0                                        0xF7FFFFFF
#define   S_00B12C_USER_SGPR_MSB(x)                                   (((unsigned)(x) & 0x1) << 28)
#define   G_00B12C_USER_SGPR_MSB(x)                                   (((x) >> 28) & 0x1)
#define   C_00B12C_USER_SGPR_MSB                                      0xEFFFFFFF
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
#define R_00B170_SPI_SHADER_USER_DATA_VS_16                             0x00B170
#define R_00B174_SPI_SHADER_USER_DATA_VS_17                             0x00B174
#define R_00B178_SPI_SHADER_USER_DATA_VS_18                             0x00B178
#define R_00B17C_SPI_SHADER_USER_DATA_VS_19                             0x00B17C
#define R_00B180_SPI_SHADER_USER_DATA_VS_20                             0x00B180
#define R_00B184_SPI_SHADER_USER_DATA_VS_21                             0x00B184
#define R_00B188_SPI_SHADER_USER_DATA_VS_22                             0x00B188
#define R_00B18C_SPI_SHADER_USER_DATA_VS_23                             0x00B18C
#define R_00B190_SPI_SHADER_USER_DATA_VS_24                             0x00B190
#define R_00B194_SPI_SHADER_USER_DATA_VS_25                             0x00B194
#define R_00B198_SPI_SHADER_USER_DATA_VS_26                             0x00B198
#define R_00B19C_SPI_SHADER_USER_DATA_VS_27                             0x00B19C
#define R_00B1A0_SPI_SHADER_USER_DATA_VS_28                             0x00B1A0
#define R_00B1A4_SPI_SHADER_USER_DATA_VS_29                             0x00B1A4
#define R_00B1A8_SPI_SHADER_USER_DATA_VS_30                             0x00B1A8
#define R_00B1AC_SPI_SHADER_USER_DATA_VS_31                             0x00B1AC
#define R_00B1F0_SPI_SHADER_PGM_RSRC2_GS_VS                             0x00B1F0
#define   S_00B1F0_SCRATCH_EN(x)                                      (((unsigned)(x) & 0x1) << 0)
#define   G_00B1F0_SCRATCH_EN(x)                                      (((x) >> 0) & 0x1)
#define   C_00B1F0_SCRATCH_EN                                         0xFFFFFFFE
#define   S_00B1F0_USER_SGPR(x)                                       (((unsigned)(x) & 0x1F) << 1)
#define   G_00B1F0_USER_SGPR(x)                                       (((x) >> 1) & 0x1F)
#define   C_00B1F0_USER_SGPR                                          0xFFFFFFC1
#define   S_00B1F0_TRAP_PRESENT(x)                                    (((unsigned)(x) & 0x1) << 6)
#define   G_00B1F0_TRAP_PRESENT(x)                                    (((x) >> 6) & 0x1)
#define   C_00B1F0_TRAP_PRESENT                                       0xFFFFFFBF
#define   S_00B1F0_EXCP_EN(x)                                         (((unsigned)(x) & 0x1FF) << 7)
#define   G_00B1F0_EXCP_EN(x)                                         (((x) >> 7) & 0x1FF)
#define   C_00B1F0_EXCP_EN                                            0xFFFF007F
#define   S_00B1F0_VGPR_COMP_CNT(x)                                   (((unsigned)(x) & 0x03) << 16)
#define   G_00B1F0_VGPR_COMP_CNT(x)                                   (((x) >> 16) & 0x03)
#define   C_00B1F0_VGPR_COMP_CNT                                      0xFFFCFFFF
#define   S_00B1F0_OC_LDS_EN(x)                                       (((unsigned)(x) & 0x1) << 18)
#define   G_00B1F0_OC_LDS_EN(x)                                       (((x) >> 18) & 0x1)
#define   C_00B1F0_OC_LDS_EN                                          0xFFFBFFFF
#define   S_00B1F0_LDS_SIZE(x)                                        (((unsigned)(x) & 0xFF) << 19)
#define   G_00B1F0_LDS_SIZE(x)                                        (((x) >> 19) & 0xFF)
#define   C_00B1F0_LDS_SIZE                                           0xF807FFFF
#define   S_00B1F0_SKIP_USGPR0(x)                                     (((unsigned)(x) & 0x1) << 27)
#define   G_00B1F0_SKIP_USGPR0(x)                                     (((x) >> 27) & 0x1)
#define   C_00B1F0_SKIP_USGPR0                                        0xF7FFFFFF
#define   S_00B1F0_USER_SGPR_MSB(x)                                   (((unsigned)(x) & 0x1) << 28)
#define   G_00B1F0_USER_SGPR_MSB(x)                                   (((x) >> 28) & 0x1)
#define   C_00B1F0_USER_SGPR_MSB                                      0xEFFFFFFF
#define R_00B204_SPI_SHADER_PGM_RSRC4_GS                                0x00B204
#define   S_00B204_GROUP_FIFO_DEPTH(x)                                (((unsigned)(x) & 0x7F) << 0)
#define   G_00B204_GROUP_FIFO_DEPTH(x)                                (((x) >> 0) & 0x7F)
#define   C_00B204_GROUP_FIFO_DEPTH                                   0xFFFFFF80
#define   S_00B204_SPI_SHADER_LATE_ALLOC_GS(x)                        (((unsigned)(x) & 0x7F) << 7)
#define   G_00B204_SPI_SHADER_LATE_ALLOC_GS(x)                        (((x) >> 7) & 0x7F)
#define   C_00B204_SPI_SHADER_LATE_ALLOC_GS                           0xFFFFC07F
#define R_00B208_SPI_SHADER_USER_DATA_ADDR_LO_GS                        0x00B208
#define R_00B20C_SPI_SHADER_USER_DATA_ADDR_HI_GS                        0x00B20C
#define R_00B210_SPI_SHADER_PGM_LO_ES                                   0x00B210
#define R_00B214_SPI_SHADER_PGM_HI_ES                                   0x00B214
#define   S_00B214_MEM_BASE(x)                                        (((unsigned)(x) & 0xFF) << 0)
#define   G_00B214_MEM_BASE(x)                                        (((x) >> 0) & 0xFF)
#define   C_00B214_MEM_BASE                                           0xFFFFFF00
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
#define   S_00B21C_SIMD_DISABLE(x)                                    (((unsigned)(x) & 0x0F) << 26)
#define   G_00B21C_SIMD_DISABLE(x)                                    (((x) >> 26) & 0x0F)
#define   C_00B21C_SIMD_DISABLE                                       0xC3FFFFFF
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
#define   S_00B228_CDBG_USER(x)                                       (((unsigned)(x) & 0x1) << 28)
#define   G_00B228_CDBG_USER(x)                                       (((x) >> 28) & 0x1)
#define   C_00B228_CDBG_USER                                          0xEFFFFFFF
#define   S_00B228_GS_VGPR_COMP_CNT(x)                                (((unsigned)(x) & 0x03) << 29)
#define   G_00B228_GS_VGPR_COMP_CNT(x)                                (((x) >> 29) & 0x03)
#define   C_00B228_GS_VGPR_COMP_CNT                                   0x9FFFFFFF
#define   S_00B228_FP16_OVFL(x)                                       (((unsigned)(x) & 0x1) << 31)
#define   G_00B228_FP16_OVFL(x)                                       (((x) >> 31) & 0x1)
#define   C_00B228_FP16_OVFL                                          0x7FFFFFFF
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
#define   S_00B22C_EXCP_EN(x)                                         (((unsigned)(x) & 0x1FF) << 7)
#define   G_00B22C_EXCP_EN(x)                                         (((x) >> 7) & 0x1FF)
#define   C_00B22C_EXCP_EN                                            0xFFFF007F
#define   S_00B22C_ES_VGPR_COMP_CNT(x)                                (((unsigned)(x) & 0x03) << 16)
#define   G_00B22C_ES_VGPR_COMP_CNT(x)                                (((x) >> 16) & 0x03)
#define   C_00B22C_ES_VGPR_COMP_CNT                                   0xFFFCFFFF
#define   S_00B22C_OC_LDS_EN(x)                                       (((unsigned)(x) & 0x1) << 18)
#define   G_00B22C_OC_LDS_EN(x)                                       (((x) >> 18) & 0x1)
#define   C_00B22C_OC_LDS_EN                                          0xFFFBFFFF
#define   S_00B22C_LDS_SIZE(x)                                        (((unsigned)(x) & 0xFF) << 19)
#define   G_00B22C_LDS_SIZE(x)                                        (((x) >> 19) & 0xFF)
#define   C_00B22C_LDS_SIZE                                           0xF807FFFF
#define   S_00B22C_SKIP_USGPR0(x)                                     (((unsigned)(x) & 0x1) << 27)
#define   G_00B22C_SKIP_USGPR0(x)                                     (((x) >> 27) & 0x1)
#define   C_00B22C_SKIP_USGPR0                                        0xF7FFFFFF
#define   S_00B22C_USER_SGPR_MSB(x)                                   (((unsigned)(x) & 0x1) << 28)
#define   G_00B22C_USER_SGPR_MSB(x)                                   (((x) >> 28) & 0x1)
#define   C_00B22C_USER_SGPR_MSB                                      0xEFFFFFFF
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
#define R_00B370_SPI_SHADER_USER_DATA_ES_16                             0x00B370
#define R_00B374_SPI_SHADER_USER_DATA_ES_17                             0x00B374
#define R_00B378_SPI_SHADER_USER_DATA_ES_18                             0x00B378
#define R_00B37C_SPI_SHADER_USER_DATA_ES_19                             0x00B37C
#define R_00B380_SPI_SHADER_USER_DATA_ES_20                             0x00B380
#define R_00B384_SPI_SHADER_USER_DATA_ES_21                             0x00B384
#define R_00B388_SPI_SHADER_USER_DATA_ES_22                             0x00B388
#define R_00B38C_SPI_SHADER_USER_DATA_ES_23                             0x00B38C
#define R_00B390_SPI_SHADER_USER_DATA_ES_24                             0x00B390
#define R_00B394_SPI_SHADER_USER_DATA_ES_25                             0x00B394
#define R_00B398_SPI_SHADER_USER_DATA_ES_26                             0x00B398
#define R_00B39C_SPI_SHADER_USER_DATA_ES_27                             0x00B39C
#define R_00B3A0_SPI_SHADER_USER_DATA_ES_28                             0x00B3A0
#define R_00B3A4_SPI_SHADER_USER_DATA_ES_29                             0x00B3A4
#define R_00B3A8_SPI_SHADER_USER_DATA_ES_30                             0x00B3A8
#define R_00B3AC_SPI_SHADER_USER_DATA_ES_31                             0x00B3AC
#define R_00B404_SPI_SHADER_PGM_RSRC4_HS                                0x00B404
#define   S_00B404_GROUP_FIFO_DEPTH(x)                                (((unsigned)(x) & 0x7F) << 0)
#define   G_00B404_GROUP_FIFO_DEPTH(x)                                (((x) >> 0) & 0x7F)
#define   C_00B404_GROUP_FIFO_DEPTH                                   0xFFFFFF80
#define R_00B408_SPI_SHADER_USER_DATA_ADDR_LO_HS                        0x00B408
#define R_00B40C_SPI_SHADER_USER_DATA_ADDR_HI_HS                        0x00B40C
#define R_00B410_SPI_SHADER_PGM_LO_LS                                   0x00B410
#define R_00B414_SPI_SHADER_PGM_HI_LS                                   0x00B414
#define   S_00B414_MEM_BASE(x)                                        (((unsigned)(x) & 0xFF) << 0)
#define   G_00B414_MEM_BASE(x)                                        (((x) >> 0) & 0xFF)
#define   C_00B414_MEM_BASE                                           0xFFFFFF00
#define R_00B41C_SPI_SHADER_PGM_RSRC3_HS                                0x00B41C
#define   S_00B41C_WAVE_LIMIT(x)                                      (((unsigned)(x) & 0x3F) << 0)
#define   G_00B41C_WAVE_LIMIT(x)                                      (((x) >> 0) & 0x3F)
#define   C_00B41C_WAVE_LIMIT                                         0xFFFFFFC0
#define   S_00B41C_LOCK_LOW_THRESHOLD(x)                              (((unsigned)(x) & 0x0F) << 6)
#define   G_00B41C_LOCK_LOW_THRESHOLD(x)                              (((x) >> 6) & 0x0F)
#define   C_00B41C_LOCK_LOW_THRESHOLD                                 0xFFFFFC3F
#define   S_00B41C_SIMD_DISABLE(x)                                    (((unsigned)(x) & 0x0F) << 10)
#define   G_00B41C_SIMD_DISABLE(x)                                    (((x) >> 10) & 0x0F)
#define   C_00B41C_SIMD_DISABLE                                       0xFFFFC3FF
#define   S_00B41C_CU_EN(x)                                           (((unsigned)(x) & 0xFFFF) << 16)
#define   G_00B41C_CU_EN(x)                                           (((x) >> 16) & 0xFFFF)
#define   C_00B41C_CU_EN                                              0x0000FFFF
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
#define   S_00B428_CDBG_USER(x)                                       (((unsigned)(x) & 0x1) << 27)
#define   G_00B428_CDBG_USER(x)                                       (((x) >> 27) & 0x1)
#define   C_00B428_CDBG_USER                                          0xF7FFFFFF
#define   S_00B428_LS_VGPR_COMP_CNT(x)                                (((unsigned)(x) & 0x03) << 28)
#define   G_00B428_LS_VGPR_COMP_CNT(x)                                (((x) >> 28) & 0x03)
#define   C_00B428_LS_VGPR_COMP_CNT                                   0xCFFFFFFF
#define   S_00B428_FP16_OVFL(x)                                       (((unsigned)(x) & 0x1) << 30)
#define   G_00B428_FP16_OVFL(x)                                       (((x) >> 30) & 0x1)
#define   C_00B428_FP16_OVFL                                          0xBFFFFFFF
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
#define   S_00B42C_EXCP_EN(x)                                         (((unsigned)(x) & 0x1FF) << 7)
#define   G_00B42C_EXCP_EN(x)                                         (((x) >> 7) & 0x1FF)
#define   C_00B42C_EXCP_EN                                            0xFFFF007F
#define   S_00B42C_LDS_SIZE(x)                                        (((unsigned)(x) & 0x1FF) << 16)
#define   G_00B42C_LDS_SIZE(x)                                        (((x) >> 16) & 0x1FF)
#define   C_00B42C_LDS_SIZE                                           0xFE00FFFF
#define   S_00B42C_SKIP_USGPR0(x)                                     (((unsigned)(x) & 0x1) << 27)
#define   G_00B42C_SKIP_USGPR0(x)                                     (((x) >> 27) & 0x1)
#define   C_00B42C_SKIP_USGPR0                                        0xF7FFFFFF
#define   S_00B42C_USER_SGPR_MSB(x)                                   (((unsigned)(x) & 0x1) << 28)
#define   G_00B42C_USER_SGPR_MSB(x)                                   (((x) >> 28) & 0x1)
#define   C_00B42C_USER_SGPR_MSB                                      0xEFFFFFFF
#define R_00B430_SPI_SHADER_USER_DATA_LS_0                              0x00B430
#define R_00B434_SPI_SHADER_USER_DATA_LS_1                              0x00B434
#define R_00B438_SPI_SHADER_USER_DATA_LS_2                              0x00B438
#define R_00B43C_SPI_SHADER_USER_DATA_LS_3                              0x00B43C
#define R_00B440_SPI_SHADER_USER_DATA_LS_4                              0x00B440
#define R_00B444_SPI_SHADER_USER_DATA_LS_5                              0x00B444
#define R_00B448_SPI_SHADER_USER_DATA_LS_6                              0x00B448
#define R_00B44C_SPI_SHADER_USER_DATA_LS_7                              0x00B44C
#define R_00B450_SPI_SHADER_USER_DATA_LS_8                              0x00B450
#define R_00B454_SPI_SHADER_USER_DATA_LS_9                              0x00B454
#define R_00B458_SPI_SHADER_USER_DATA_LS_10                             0x00B458
#define R_00B45C_SPI_SHADER_USER_DATA_LS_11                             0x00B45C
#define R_00B460_SPI_SHADER_USER_DATA_LS_12                             0x00B460
#define R_00B464_SPI_SHADER_USER_DATA_LS_13                             0x00B464
#define R_00B468_SPI_SHADER_USER_DATA_LS_14                             0x00B468
#define R_00B46C_SPI_SHADER_USER_DATA_LS_15                             0x00B46C
#define R_00B470_SPI_SHADER_USER_DATA_LS_16                             0x00B470
#define R_00B474_SPI_SHADER_USER_DATA_LS_17                             0x00B474
#define R_00B478_SPI_SHADER_USER_DATA_LS_18                             0x00B478
#define R_00B47C_SPI_SHADER_USER_DATA_LS_19                             0x00B47C
#define R_00B480_SPI_SHADER_USER_DATA_LS_20                             0x00B480
#define R_00B484_SPI_SHADER_USER_DATA_LS_21                             0x00B484
#define R_00B488_SPI_SHADER_USER_DATA_LS_22                             0x00B488
#define R_00B48C_SPI_SHADER_USER_DATA_LS_23                             0x00B48C
#define R_00B490_SPI_SHADER_USER_DATA_LS_24                             0x00B490
#define R_00B494_SPI_SHADER_USER_DATA_LS_25                             0x00B494
#define R_00B498_SPI_SHADER_USER_DATA_LS_26                             0x00B498
#define R_00B49C_SPI_SHADER_USER_DATA_LS_27                             0x00B49C
#define R_00B4A0_SPI_SHADER_USER_DATA_LS_28                             0x00B4A0
#define R_00B4A4_SPI_SHADER_USER_DATA_LS_29                             0x00B4A4
#define R_00B4A8_SPI_SHADER_USER_DATA_LS_30                             0x00B4A8
#define R_00B4AC_SPI_SHADER_USER_DATA_LS_31                             0x00B4AC
#define R_00B530_SPI_SHADER_USER_DATA_COMMON_0                          0x00B530
#define R_00B534_SPI_SHADER_USER_DATA_COMMON_1                          0x00B534
#define R_00B538_SPI_SHADER_USER_DATA_COMMON_2                          0x00B538
#define R_00B53C_SPI_SHADER_USER_DATA_COMMON_3                          0x00B53C
#define R_00B540_SPI_SHADER_USER_DATA_COMMON_4                          0x00B540
#define R_00B544_SPI_SHADER_USER_DATA_COMMON_5                          0x00B544
#define R_00B548_SPI_SHADER_USER_DATA_COMMON_6                          0x00B548
#define R_00B54C_SPI_SHADER_USER_DATA_COMMON_7                          0x00B54C
#define R_00B550_SPI_SHADER_USER_DATA_COMMON_8                          0x00B550
#define R_00B554_SPI_SHADER_USER_DATA_COMMON_9                          0x00B554
#define R_00B558_SPI_SHADER_USER_DATA_COMMON_10                         0x00B558
#define R_00B55C_SPI_SHADER_USER_DATA_COMMON_11                         0x00B55C
#define R_00B560_SPI_SHADER_USER_DATA_COMMON_12                         0x00B560
#define R_00B564_SPI_SHADER_USER_DATA_COMMON_13                         0x00B564
#define R_00B568_SPI_SHADER_USER_DATA_COMMON_14                         0x00B568
#define R_00B56C_SPI_SHADER_USER_DATA_COMMON_15                         0x00B56C
#define R_00B570_SPI_SHADER_USER_DATA_COMMON_16                         0x00B570
#define R_00B574_SPI_SHADER_USER_DATA_COMMON_17                         0x00B574
#define R_00B578_SPI_SHADER_USER_DATA_COMMON_18                         0x00B578
#define R_00B57C_SPI_SHADER_USER_DATA_COMMON_19                         0x00B57C
#define R_00B580_SPI_SHADER_USER_DATA_COMMON_20                         0x00B580
#define R_00B584_SPI_SHADER_USER_DATA_COMMON_21                         0x00B584
#define R_00B588_SPI_SHADER_USER_DATA_COMMON_22                         0x00B588
#define R_00B58C_SPI_SHADER_USER_DATA_COMMON_23                         0x00B58C
#define R_00B590_SPI_SHADER_USER_DATA_COMMON_24                         0x00B590
#define R_00B594_SPI_SHADER_USER_DATA_COMMON_25                         0x00B594
#define R_00B598_SPI_SHADER_USER_DATA_COMMON_26                         0x00B598
#define R_00B59C_SPI_SHADER_USER_DATA_COMMON_27                         0x00B59C
#define R_00B5A0_SPI_SHADER_USER_DATA_COMMON_28                         0x00B5A0
#define R_00B5A4_SPI_SHADER_USER_DATA_COMMON_29                         0x00B5A4
#define R_00B5A8_SPI_SHADER_USER_DATA_COMMON_30                         0x00B5A8
#define R_00B5AC_SPI_SHADER_USER_DATA_COMMON_31                         0x00B5AC
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
#define   S_00B800_ORDERED_APPEND_MODE(x)                             (((unsigned)(x) & 0x1) << 4)
#define   G_00B800_ORDERED_APPEND_MODE(x)                             (((x) >> 4) & 0x1)
#define   C_00B800_ORDERED_APPEND_MODE                                0xFFFFFFEF
#define   S_00B800_USE_THREAD_DIMENSIONS(x)                           (((unsigned)(x) & 0x1) << 5)
#define   G_00B800_USE_THREAD_DIMENSIONS(x)                           (((x) >> 5) & 0x1)
#define   C_00B800_USE_THREAD_DIMENSIONS                              0xFFFFFFDF
#define   S_00B800_ORDER_MODE(x)                                      (((unsigned)(x) & 0x1) << 6)
#define   G_00B800_ORDER_MODE(x)                                      (((x) >> 6) & 0x1)
#define   C_00B800_ORDER_MODE                                         0xFFFFFFBF
#define   S_00B800_SCALAR_L1_INV_VOL(x)                               (((unsigned)(x) & 0x1) << 10)
#define   G_00B800_SCALAR_L1_INV_VOL(x)                               (((x) >> 10) & 0x1)
#define   C_00B800_SCALAR_L1_INV_VOL                                  0xFFFFFBFF
#define   S_00B800_VECTOR_L1_INV_VOL(x)                               (((unsigned)(x) & 0x1) << 11)
#define   G_00B800_VECTOR_L1_INV_VOL(x)                               (((x) >> 11) & 0x1)
#define   C_00B800_VECTOR_L1_INV_VOL                                  0xFFFFF7FF
#define   S_00B800_RESERVED(x)                                        (((unsigned)(x) & 0x1) << 12)
#define   G_00B800_RESERVED(x)                                        (((x) >> 12) & 0x1)
#define   C_00B800_RESERVED                                           0xFFFFEFFF
#define   S_00B800_RESTORE(x)                                         (((unsigned)(x) & 0x1) << 14)
#define   G_00B800_RESTORE(x)                                         (((x) >> 14) & 0x1)
#define   C_00B800_RESTORE                                            0xFFFFBFFF
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
#define R_00B828_COMPUTE_PIPELINESTAT_ENABLE                            0x00B828
#define   S_00B828_PIPELINESTAT_ENABLE(x)                             (((unsigned)(x) & 0x1) << 0)
#define   G_00B828_PIPELINESTAT_ENABLE(x)                             (((x) >> 0) & 0x1)
#define   C_00B828_PIPELINESTAT_ENABLE                                0xFFFFFFFE
#define R_00B82C_COMPUTE_PERFCOUNT_ENABLE                               0x00B82C
#define   S_00B82C_PERFCOUNT_ENABLE(x)                                (((unsigned)(x) & 0x1) << 0)
#define   G_00B82C_PERFCOUNT_ENABLE(x)                                (((x) >> 0) & 0x1)
#define   C_00B82C_PERFCOUNT_ENABLE                                   0xFFFFFFFE
#define R_00B830_COMPUTE_PGM_LO                                         0x00B830
#define R_00B834_COMPUTE_PGM_HI                                         0x00B834
#define   S_00B834_DATA(x)                                            (((unsigned)(x) & 0xFF) << 0)
#define   G_00B834_DATA(x)                                            (((x) >> 0) & 0xFF)
#define   C_00B834_DATA                                               0xFFFFFF00
#define R_00B838_COMPUTE_DISPATCH_PKT_ADDR_LO                           0x00B838
#define R_00B83C_COMPUTE_DISPATCH_PKT_ADDR_HI                           0x00B83C
#define   S_00B83C_DATA(x)                                            (((unsigned)(x) & 0xFF) << 0)
#define   G_00B83C_DATA(x)                                            (((x) >> 0) & 0xFF)
#define   C_00B83C_DATA                                               0xFFFFFF00
#define R_00B840_COMPUTE_DISPATCH_SCRATCH_BASE_LO                       0x00B840
#define R_00B844_COMPUTE_DISPATCH_SCRATCH_BASE_HI                       0x00B844
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
#define   S_00B848_BULKY(x)                                           (((unsigned)(x) & 0x1) << 24)
#define   G_00B848_BULKY(x)                                           (((x) >> 24) & 0x1)
#define   C_00B848_BULKY                                              0xFEFFFFFF
#define   S_00B848_CDBG_USER(x)                                       (((unsigned)(x) & 0x1) << 25)
#define   G_00B848_CDBG_USER(x)                                       (((x) >> 25) & 0x1)
#define   C_00B848_CDBG_USER                                          0xFDFFFFFF
#define   S_00B848_FP16_OVFL(x)                                       (((unsigned)(x) & 0x1) << 26)
#define   G_00B848_FP16_OVFL(x)                                       (((x) >> 26) & 0x1)
#define   C_00B848_FP16_OVFL                                          0xFBFFFFFF
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
#define   S_00B84C_EXCP_EN_MSB(x)                                     (((unsigned)(x) & 0x03) << 13)
#define   G_00B84C_EXCP_EN_MSB(x)                                     (((x) >> 13) & 0x03)
#define   C_00B84C_EXCP_EN_MSB                                        0xFFFF9FFF
#define   S_00B84C_LDS_SIZE(x)                                        (((unsigned)(x) & 0x1FF) << 15)
#define   G_00B84C_LDS_SIZE(x)                                        (((x) >> 15) & 0x1FF)
#define   C_00B84C_LDS_SIZE                                           0xFF007FFF
#define   S_00B84C_EXCP_EN(x)                                         (((unsigned)(x) & 0x7F) << 24)
#define   G_00B84C_EXCP_EN(x)                                         (((x) >> 24) & 0x7F)
#define   C_00B84C_EXCP_EN                                            0x80FFFFFF
#define   S_00B84C_SKIP_USGPR0(x)                                     (((unsigned)(x) & 0x1) << 31)
#define   G_00B84C_SKIP_USGPR0(x)                                     (((x) >> 31) & 0x1)
#define   C_00B84C_SKIP_USGPR0                                        0x7FFFFFFF
#define R_00B850_COMPUTE_VMID                                           0x00B850
#define   S_00B850_DATA(x)                                            (((unsigned)(x) & 0x0F) << 0)
#define   G_00B850_DATA(x)                                            (((x) >> 0) & 0x0F)
#define   C_00B850_DATA                                               0xFFFFFFF0
#define R_00B854_COMPUTE_RESOURCE_LIMITS                                0x00B854
#define   S_00B854_WAVES_PER_SH(x)                                    (((unsigned)(x) & 0x3FF) << 0)
#define   G_00B854_WAVES_PER_SH(x)                                    (((x) >> 0) & 0x3FF)
#define   C_00B854_WAVES_PER_SH                                       0xFFFFFC00
#define   S_00B854_TG_PER_CU(x)                                       (((unsigned)(x) & 0x0F) << 12)
#define   G_00B854_TG_PER_CU(x)                                       (((x) >> 12) & 0x0F)
#define   C_00B854_TG_PER_CU                                          0xFFFF0FFF
#define   S_00B854_LOCK_THRESHOLD(x)                                  (((unsigned)(x) & 0x3F) << 16)
#define   G_00B854_LOCK_THRESHOLD(x)                                  (((x) >> 16) & 0x3F)
#define   C_00B854_LOCK_THRESHOLD                                     0xFFC0FFFF
#define   S_00B854_SIMD_DEST_CNTL(x)                                  (((unsigned)(x) & 0x1) << 22)
#define   G_00B854_SIMD_DEST_CNTL(x)                                  (((x) >> 22) & 0x1)
#define   C_00B854_SIMD_DEST_CNTL                                     0xFFBFFFFF
#define   S_00B854_FORCE_SIMD_DIST(x)                                 (((unsigned)(x) & 0x1) << 23)
#define   G_00B854_FORCE_SIMD_DIST(x)                                 (((x) >> 23) & 0x1)
#define   C_00B854_FORCE_SIMD_DIST                                    0xFF7FFFFF
#define   S_00B854_CU_GROUP_COUNT(x)                                  (((unsigned)(x) & 0x07) << 24)
#define   G_00B854_CU_GROUP_COUNT(x)                                  (((x) >> 24) & 0x07)
#define   C_00B854_CU_GROUP_COUNT                                     0xF8FFFFFF
#define   S_00B854_SIMD_DISABLE(x)                                    (((unsigned)(x) & 0x0F) << 27)
#define   G_00B854_SIMD_DISABLE(x)                                    (((x) >> 27) & 0x0F)
#define   C_00B854_SIMD_DISABLE                                       0x87FFFFFF
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
#define R_034030_CPF_LATENCY_STATS_DATA                                 0x034030
#define R_034034_CPG_LATENCY_STATS_DATA                                 0x034034
#define R_034038_CPC_LATENCY_STATS_DATA                                 0x034038
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
#define R_036000_CPG_PERFCOUNTER1_SELECT                                0x036000
#define R_036004_CPG_PERFCOUNTER0_SELECT1                               0x036004
#define   S_036004_CNTR_SEL2(x)                                       (((unsigned)(x) & 0x3FF) << 0)
#define   G_036004_CNTR_SEL2(x)                                       (((x) >> 0) & 0x3FF)
#define   C_036004_CNTR_SEL2                                          0xFFFFFC00
#define   S_036004_CNTR_SEL3(x)                                       (((unsigned)(x) & 0x3FF) << 10)
#define   G_036004_CNTR_SEL3(x)                                       (((x) >> 10) & 0x3FF)
#define   C_036004_CNTR_SEL3                                          0xFFF003FF
#define   S_036004_CNTR_MODE3(x)                                      (((unsigned)(x) & 0x0F) << 24)
#define   G_036004_CNTR_MODE3(x)                                      (((x) >> 24) & 0x0F)
#define   C_036004_CNTR_MODE3                                         0xF0FFFFFF
#define   S_036004_CNTR_MODE2(x)                                      (((unsigned)(x) & 0x0F) << 28)
#define   G_036004_CNTR_MODE2(x)                                      (((x) >> 28) & 0x0F)
#define   C_036004_CNTR_MODE2                                         0x0FFFFFFF
#define R_036008_CPG_PERFCOUNTER0_SELECT                                0x036008
#define   S_036008_CNTR_SEL0(x)                                       (((unsigned)(x) & 0x3FF) << 0)
#define   G_036008_CNTR_SEL0(x)                                       (((x) >> 0) & 0x3FF)
#define   C_036008_CNTR_SEL0                                          0xFFFFFC00
#define   S_036008_CNTR_SEL1(x)                                       (((unsigned)(x) & 0x3FF) << 10)
#define   G_036008_CNTR_SEL1(x)                                       (((x) >> 10) & 0x3FF)
#define   C_036008_CNTR_SEL1                                          0xFFF003FF
#define   S_036008_SPM_MODE(x)                                        (((unsigned)(x) & 0x0F) << 20)
#define   G_036008_SPM_MODE(x)                                        (((x) >> 20) & 0x0F)
#define   C_036008_SPM_MODE                                           0xFF0FFFFF
#define   S_036008_CNTR_MODE1(x)                                      (((unsigned)(x) & 0x0F) << 24)
#define   G_036008_CNTR_MODE1(x)                                      (((x) >> 24) & 0x0F)
#define   C_036008_CNTR_MODE1                                         0xF0FFFFFF
#define   S_036008_CNTR_MODE0(x)                                      (((unsigned)(x) & 0x0F) << 28)
#define   G_036008_CNTR_MODE0(x)                                      (((x) >> 28) & 0x0F)
#define   C_036008_CNTR_MODE0                                         0x0FFFFFFF
#define R_03600C_CPC_PERFCOUNTER1_SELECT                                0x03600C
#define R_036010_CPC_PERFCOUNTER0_SELECT1                               0x036010
#define   S_036010_CNTR_SEL2(x)                                       (((unsigned)(x) & 0x3FF) << 0)
#define   G_036010_CNTR_SEL2(x)                                       (((x) >> 0) & 0x3FF)
#define   C_036010_CNTR_SEL2                                          0xFFFFFC00
#define   S_036010_CNTR_SEL3(x)                                       (((unsigned)(x) & 0x3FF) << 10)
#define   G_036010_CNTR_SEL3(x)                                       (((x) >> 10) & 0x3FF)
#define   C_036010_CNTR_SEL3                                          0xFFF003FF
#define   S_036010_CNTR_MODE3(x)                                      (((unsigned)(x) & 0x0F) << 24)
#define   G_036010_CNTR_MODE3(x)                                      (((x) >> 24) & 0x0F)
#define   C_036010_CNTR_MODE3                                         0xF0FFFFFF
#define   S_036010_CNTR_MODE2(x)                                      (((unsigned)(x) & 0x0F) << 28)
#define   G_036010_CNTR_MODE2(x)                                      (((x) >> 28) & 0x0F)
#define   C_036010_CNTR_MODE2                                         0x0FFFFFFF
#define R_036014_CPF_PERFCOUNTER1_SELECT                                0x036014
#define R_036018_CPF_PERFCOUNTER0_SELECT1                               0x036018
#define   S_036018_CNTR_SEL2(x)                                       (((unsigned)(x) & 0x3FF) << 0)
#define   G_036018_CNTR_SEL2(x)                                       (((x) >> 0) & 0x3FF)
#define   C_036018_CNTR_SEL2                                          0xFFFFFC00
#define   S_036018_CNTR_SEL3(x)                                       (((unsigned)(x) & 0x3FF) << 10)
#define   G_036018_CNTR_SEL3(x)                                       (((x) >> 10) & 0x3FF)
#define   C_036018_CNTR_SEL3                                          0xFFF003FF
#define   S_036018_CNTR_MODE3(x)                                      (((unsigned)(x) & 0x0F) << 24)
#define   G_036018_CNTR_MODE3(x)                                      (((x) >> 24) & 0x0F)
#define   C_036018_CNTR_MODE3                                         0xF0FFFFFF
#define   S_036018_CNTR_MODE2(x)                                      (((unsigned)(x) & 0x0F) << 28)
#define   G_036018_CNTR_MODE2(x)                                      (((x) >> 28) & 0x0F)
#define   C_036018_CNTR_MODE2                                         0x0FFFFFFF
#define R_03601C_CPF_PERFCOUNTER0_SELECT                                0x03601C
#define   S_03601C_CNTR_SEL0(x)                                       (((unsigned)(x) & 0x3FF) << 0)
#define   G_03601C_CNTR_SEL0(x)                                       (((x) >> 0) & 0x3FF)
#define   C_03601C_CNTR_SEL0                                          0xFFFFFC00
#define   S_03601C_CNTR_SEL1(x)                                       (((unsigned)(x) & 0x3FF) << 10)
#define   G_03601C_CNTR_SEL1(x)                                       (((x) >> 10) & 0x3FF)
#define   C_03601C_CNTR_SEL1                                          0xFFF003FF
#define   S_03601C_SPM_MODE(x)                                        (((unsigned)(x) & 0x0F) << 20)
#define   G_03601C_SPM_MODE(x)                                        (((x) >> 20) & 0x0F)
#define   C_03601C_SPM_MODE                                           0xFF0FFFFF
#define   S_03601C_CNTR_MODE1(x)                                      (((unsigned)(x) & 0x0F) << 24)
#define   G_03601C_CNTR_MODE1(x)                                      (((x) >> 24) & 0x0F)
#define   C_03601C_CNTR_MODE1                                         0xF0FFFFFF
#define   S_03601C_CNTR_MODE0(x)                                      (((unsigned)(x) & 0x0F) << 28)
#define   G_03601C_CNTR_MODE0(x)                                      (((x) >> 28) & 0x0F)
#define   C_03601C_CNTR_MODE0                                         0x0FFFFFFF
#define R_036020_CP_PERFMON_CNTL                                        0x036020
#define   S_036020_PERFMON_STATE(x)                                   (((unsigned)(x) & 0x0F) << 0)
#define   G_036020_PERFMON_STATE(x)                                   (((x) >> 0) & 0x0F)
#define   C_036020_PERFMON_STATE                                      0xFFFFFFF0
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
#define   S_036024_CNTR_SEL0(x)                                       (((unsigned)(x) & 0x3FF) << 0)
#define   G_036024_CNTR_SEL0(x)                                       (((x) >> 0) & 0x3FF)
#define   C_036024_CNTR_SEL0                                          0xFFFFFC00
#define   S_036024_CNTR_SEL1(x)                                       (((unsigned)(x) & 0x3FF) << 10)
#define   G_036024_CNTR_SEL1(x)                                       (((x) >> 10) & 0x3FF)
#define   C_036024_CNTR_SEL1                                          0xFFF003FF
#define   S_036024_SPM_MODE(x)                                        (((unsigned)(x) & 0x0F) << 20)
#define   G_036024_SPM_MODE(x)                                        (((x) >> 20) & 0x0F)
#define   C_036024_SPM_MODE                                           0xFF0FFFFF
#define   S_036024_CNTR_MODE1(x)                                      (((unsigned)(x) & 0x0F) << 24)
#define   G_036024_CNTR_MODE1(x)                                      (((x) >> 24) & 0x0F)
#define   C_036024_CNTR_MODE1                                         0xF0FFFFFF
#define   S_036024_CNTR_MODE0(x)                                      (((unsigned)(x) & 0x0F) << 28)
#define   G_036024_CNTR_MODE0(x)                                      (((x) >> 28) & 0x0F)
#define   C_036024_CNTR_MODE0                                         0x0FFFFFFF
#define R_036028_CPF_TC_PERF_COUNTER_WINDOW_SELECT                      0x036028
#define   S_036028_INDEX(x)                                           (((unsigned)(x) & 0x07) << 0)
#define   G_036028_INDEX(x)                                           (((x) >> 0) & 0x07)
#define   C_036028_INDEX                                              0xFFFFFFF8
#define   S_036028_ALWAYS(x)                                          (((unsigned)(x) & 0x1) << 30)
#define   G_036028_ALWAYS(x)                                          (((x) >> 30) & 0x1)
#define   C_036028_ALWAYS                                             0xBFFFFFFF
#define   S_036028_ENABLE(x)                                          (((unsigned)(x) & 0x1) << 31)
#define   G_036028_ENABLE(x)                                          (((x) >> 31) & 0x1)
#define   C_036028_ENABLE                                             0x7FFFFFFF
#define R_03602C_CPG_TC_PERF_COUNTER_WINDOW_SELECT                      0x03602C
#define   S_03602C_INDEX(x)                                           (((unsigned)(x) & 0x1F) << 0)
#define   G_03602C_INDEX(x)                                           (((x) >> 0) & 0x1F)
#define   C_03602C_INDEX                                              0xFFFFFFE0
#define   S_03602C_ALWAYS(x)                                          (((unsigned)(x) & 0x1) << 30)
#define   G_03602C_ALWAYS(x)                                          (((x) >> 30) & 0x1)
#define   C_03602C_ALWAYS                                             0xBFFFFFFF
#define   S_03602C_ENABLE(x)                                          (((unsigned)(x) & 0x1) << 31)
#define   G_03602C_ENABLE(x)                                          (((x) >> 31) & 0x1)
#define   C_03602C_ENABLE                                             0x7FFFFFFF
#define R_036030_CPF_LATENCY_STATS_SELECT                               0x036030
#define   S_036030_INDEX(x)                                           (((unsigned)(x) & 0x0F) << 0)
#define   G_036030_INDEX(x)                                           (((x) >> 0) & 0x0F)
#define   C_036030_INDEX                                              0xFFFFFFF0
#define   S_036030_CLEAR(x)                                           (((unsigned)(x) & 0x1) << 30)
#define   G_036030_CLEAR(x)                                           (((x) >> 30) & 0x1)
#define   C_036030_CLEAR                                              0xBFFFFFFF
#define   S_036030_ENABLE(x)                                          (((unsigned)(x) & 0x1) << 31)
#define   G_036030_ENABLE(x)                                          (((x) >> 31) & 0x1)
#define   C_036030_ENABLE                                             0x7FFFFFFF
#define R_036034_CPG_LATENCY_STATS_SELECT                               0x036034
#define   S_036034_INDEX(x)                                           (((unsigned)(x) & 0x1F) << 0)
#define   G_036034_INDEX(x)                                           (((x) >> 0) & 0x1F)
#define   C_036034_INDEX                                              0xFFFFFFE0
#define   S_036034_CLEAR(x)                                           (((unsigned)(x) & 0x1) << 30)
#define   G_036034_CLEAR(x)                                           (((x) >> 30) & 0x1)
#define   C_036034_CLEAR                                              0xBFFFFFFF
#define   S_036034_ENABLE(x)                                          (((unsigned)(x) & 0x1) << 31)
#define   G_036034_ENABLE(x)                                          (((x) >> 31) & 0x1)
#define   C_036034_ENABLE                                             0x7FFFFFFF
#define R_036038_CPC_LATENCY_STATS_SELECT                               0x036038
#define   S_036038_INDEX(x)                                           (((unsigned)(x) & 0x07) << 0)
#define   G_036038_INDEX(x)                                           (((x) >> 0) & 0x07)
#define   C_036038_INDEX                                              0xFFFFFFF8
#define   S_036038_CLEAR(x)                                           (((unsigned)(x) & 0x1) << 30)
#define   G_036038_CLEAR(x)                                           (((x) >> 30) & 0x1)
#define   C_036038_CLEAR                                              0xBFFFFFFF
#define   S_036038_ENABLE(x)                                          (((unsigned)(x) & 0x1) << 31)
#define   G_036038_ENABLE(x)                                          (((x) >> 31) & 0x1)
#define   C_036038_ENABLE                                             0x7FFFFFFF
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
#define   S_036100_UTCL2_BUSY_USER_DEFINED_MASK(x)                    (((unsigned)(x) & 0x1) << 29)
#define   G_036100_UTCL2_BUSY_USER_DEFINED_MASK(x)                    (((x) >> 29) & 0x1)
#define   C_036100_UTCL2_BUSY_USER_DEFINED_MASK                       0xDFFFFFFF
#define   S_036100_EA_BUSY_USER_DEFINED_MASK(x)                       (((unsigned)(x) & 0x1) << 30)
#define   G_036100_EA_BUSY_USER_DEFINED_MASK(x)                       (((x) >> 30) & 0x1)
#define   C_036100_EA_BUSY_USER_DEFINED_MASK                          0xBFFFFFFF
#define   S_036100_RMI_BUSY_USER_DEFINED_MASK(x)                      (((unsigned)(x) & 0x1) << 31)
#define   G_036100_RMI_BUSY_USER_DEFINED_MASK(x)                      (((x) >> 31) & 0x1)
#define   C_036100_RMI_BUSY_USER_DEFINED_MASK                         0x7FFFFFFF
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
#define   S_036108_RMI_BUSY_USER_DEFINED_MASK(x)                      (((unsigned)(x) & 0x1) << 22)
#define   G_036108_RMI_BUSY_USER_DEFINED_MASK(x)                      (((x) >> 22) & 0x1)
#define   C_036108_RMI_BUSY_USER_DEFINED_MASK                         0xFFBFFFFF
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
#define   S_03610C_RMI_BUSY_USER_DEFINED_MASK(x)                      (((unsigned)(x) & 0x1) << 22)
#define   G_03610C_RMI_BUSY_USER_DEFINED_MASK(x)                      (((x) >> 22) & 0x1)
#define   C_03610C_RMI_BUSY_USER_DEFINED_MASK                         0xFFBFFFFF
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
#define   S_036110_RMI_BUSY_USER_DEFINED_MASK(x)                      (((unsigned)(x) & 0x1) << 22)
#define   G_036110_RMI_BUSY_USER_DEFINED_MASK(x)                      (((x) >> 22) & 0x1)
#define   C_036110_RMI_BUSY_USER_DEFINED_MASK                         0xFFBFFFFF
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
#define   S_036114_RMI_BUSY_USER_DEFINED_MASK(x)                      (((unsigned)(x) & 0x1) << 22)
#define   G_036114_RMI_BUSY_USER_DEFINED_MASK(x)                      (((x) >> 22) & 0x1)
#define   C_036114_RMI_BUSY_USER_DEFINED_MASK                         0xFFBFFFFF
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
#define   S_036600_PERF_MODE1(x)                                      (((unsigned)(x) & 0x0F) << 24)
#define   G_036600_PERF_MODE1(x)                                      (((x) >> 24) & 0x0F)
#define   C_036600_PERF_MODE1                                         0xF0FFFFFF
#define   S_036600_PERF_MODE(x)                                       (((unsigned)(x) & 0x0F) << 28)
#define   G_036600_PERF_MODE(x)                                       (((x) >> 28) & 0x0F)
#define   C_036600_PERF_MODE                                          0x0FFFFFFF
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
#define   S_036610_PERF_MODE3(x)                                      (((unsigned)(x) & 0x0F) << 24)
#define   G_036610_PERF_MODE3(x)                                      (((x) >> 24) & 0x0F)
#define   C_036610_PERF_MODE3                                         0xF0FFFFFF
#define   S_036610_PERF_MODE2(x)                                      (((unsigned)(x) & 0x0F) << 28)
#define   G_036610_PERF_MODE2(x)                                      (((x) >> 28) & 0x0F)
#define   C_036610_PERF_MODE2                                         0x0FFFFFFF
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
#define   S_028000_DECOMPRESS_ENABLE(x)                               (((unsigned)(x) & 0x1) << 12)
#define   G_028000_DECOMPRESS_ENABLE(x)                               (((x) >> 12) & 0x1)
#define   C_028000_DECOMPRESS_ENABLE                                  0xFFFFEFFF
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
#define   S_028008_MIPID(x)                                           (((unsigned)(x) & 0x0F) << 26)
#define   G_028008_MIPID(x)                                           (((x) >> 26) & 0x0F)
#define   C_028008_MIPID                                              0xC3FFFFFF
#define R_02800C_DB_RENDER_OVERRIDE                                     0x02800C
#define   S_02800C_FORCE_HIZ_ENABLE(x)                                (((unsigned)(x) & 0x03) << 0)
#define   G_02800C_FORCE_HIZ_ENABLE(x)                                (((x) >> 0) & 0x03)
#define   C_02800C_FORCE_HIZ_ENABLE                                   0xFFFFFFFC
#define   S_02800C_FORCE_HIS_ENABLE0(x)                               (((unsigned)(x) & 0x03) << 2)
#define   G_02800C_FORCE_HIS_ENABLE0(x)                               (((x) >> 2) & 0x03)
#define   C_02800C_FORCE_HIS_ENABLE0                                  0xFFFFFFF3
#define   S_02800C_FORCE_HIS_ENABLE1(x)                               (((unsigned)(x) & 0x03) << 4)
#define   G_02800C_FORCE_HIS_ENABLE1(x)                               (((x) >> 4) & 0x03)
#define   C_02800C_FORCE_HIS_ENABLE1                                  0xFFFFFFCF
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
#define   S_028010_ALLOW_PARTIAL_RES_HIER_KILL(x)                     (((unsigned)(x) & 0x1) << 25)
#define   G_028010_ALLOW_PARTIAL_RES_HIER_KILL(x)                     (((x) >> 25) & 0x1)
#define   C_028010_ALLOW_PARTIAL_RES_HIER_KILL                        0xFDFFFFFF
#define R_028014_DB_HTILE_DATA_BASE                                     0x028014
#define R_028018_DB_HTILE_DATA_BASE_HI                                  0x028018
#define   S_028018_BASE_HI(x)                                         (((unsigned)(x) & 0xFF) << 0)
#define   G_028018_BASE_HI(x)                                         (((x) >> 0) & 0xFF)
#define   C_028018_BASE_HI                                            0xFFFFFF00
#define R_02801C_DB_DEPTH_SIZE                                          0x02801C
#define   S_02801C_X_MAX(x)                                           (((unsigned)(x) & 0x3FFF) << 0)
#define   G_02801C_X_MAX(x)                                           (((x) >> 0) & 0x3FFF)
#define   C_02801C_X_MAX                                              0xFFFFC000
#define   S_02801C_Y_MAX(x)                                           (((unsigned)(x) & 0x3FFF) << 16)
#define   G_02801C_Y_MAX(x)                                           (((x) >> 16) & 0x3FFF)
#define   C_02801C_Y_MAX                                              0xC000FFFF
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
#define R_028038_DB_Z_INFO                                              0x028038
#define   S_028038_FORMAT(x)                                          (((unsigned)(x) & 0x03) << 0)
#define   G_028038_FORMAT(x)                                          (((x) >> 0) & 0x03)
#define   C_028038_FORMAT                                             0xFFFFFFFC
#define   S_028038_NUM_SAMPLES(x)                                     (((unsigned)(x) & 0x03) << 2)
#define   G_028038_NUM_SAMPLES(x)                                     (((x) >> 2) & 0x03)
#define   C_028038_NUM_SAMPLES                                        0xFFFFFFF3
#define   S_028038_SW_MODE(x)                                         (((unsigned)(x) & 0x1F) << 4)
#define   G_028038_SW_MODE(x)                                         (((x) >> 4) & 0x1F)
#define   C_028038_SW_MODE                                            0xFFFFFE0F
#define   S_028038_PARTIALLY_RESIDENT(x)                              (((unsigned)(x) & 0x1) << 12)
#define   G_028038_PARTIALLY_RESIDENT(x)                              (((x) >> 12) & 0x1)
#define   C_028038_PARTIALLY_RESIDENT                                 0xFFFFEFFF
#define   S_028038_FAULT_BEHAVIOR(x)                                  (((unsigned)(x) & 0x03) << 13)
#define   G_028038_FAULT_BEHAVIOR(x)                                  (((x) >> 13) & 0x03)
#define   C_028038_FAULT_BEHAVIOR                                     0xFFFF9FFF
#define   S_028038_ITERATE_FLUSH(x)                                   (((unsigned)(x) & 0x1) << 15)
#define   G_028038_ITERATE_FLUSH(x)                                   (((x) >> 15) & 0x1)
#define   C_028038_ITERATE_FLUSH                                      0xFFFF7FFF
#define   S_028038_MAXMIP(x)                                          (((unsigned)(x) & 0x0F) << 16)
#define   G_028038_MAXMIP(x)                                          (((x) >> 16) & 0x0F)
#define   C_028038_MAXMIP                                             0xFFF0FFFF
#define   S_028038_DECOMPRESS_ON_N_ZPLANES(x)                         (((unsigned)(x) & 0x0F) << 23)
#define   G_028038_DECOMPRESS_ON_N_ZPLANES(x)                         (((x) >> 23) & 0x0F)
#define   C_028038_DECOMPRESS_ON_N_ZPLANES                            0xF87FFFFF
#define   S_028038_ALLOW_EXPCLEAR(x)                                  (((unsigned)(x) & 0x1) << 27)
#define   G_028038_ALLOW_EXPCLEAR(x)                                  (((x) >> 27) & 0x1)
#define   C_028038_ALLOW_EXPCLEAR                                     0xF7FFFFFF
#define   S_028038_READ_SIZE(x)                                       (((unsigned)(x) & 0x1) << 28)
#define   G_028038_READ_SIZE(x)                                       (((x) >> 28) & 0x1)
#define   C_028038_READ_SIZE                                          0xEFFFFFFF
#define   S_028038_TILE_SURFACE_ENABLE(x)                             (((unsigned)(x) & 0x1) << 29)
#define   G_028038_TILE_SURFACE_ENABLE(x)                             (((x) >> 29) & 0x1)
#define   C_028038_TILE_SURFACE_ENABLE                                0xDFFFFFFF
#define   S_028038_CLEAR_DISALLOWED(x)                                (((unsigned)(x) & 0x1) << 30)
#define   G_028038_CLEAR_DISALLOWED(x)                                (((x) >> 30) & 0x1)
#define   C_028038_CLEAR_DISALLOWED                                   0xBFFFFFFF
#define   S_028038_ZRANGE_PRECISION(x)                                (((unsigned)(x) & 0x1) << 31)
#define   G_028038_ZRANGE_PRECISION(x)                                (((x) >> 31) & 0x1)
#define   C_028038_ZRANGE_PRECISION                                   0x7FFFFFFF
#define R_02803C_DB_STENCIL_INFO                                        0x02803C
#define   S_02803C_FORMAT(x)                                          (((unsigned)(x) & 0x1) << 0)
#define   G_02803C_FORMAT(x)                                          (((x) >> 0) & 0x1)
#define   C_02803C_FORMAT                                             0xFFFFFFFE
#define   S_02803C_SW_MODE(x)                                         (((unsigned)(x) & 0x1F) << 4)
#define   G_02803C_SW_MODE(x)                                         (((x) >> 4) & 0x1F)
#define   C_02803C_SW_MODE                                            0xFFFFFE0F
#define   S_02803C_PARTIALLY_RESIDENT(x)                              (((unsigned)(x) & 0x1) << 12)
#define   G_02803C_PARTIALLY_RESIDENT(x)                              (((x) >> 12) & 0x1)
#define   C_02803C_PARTIALLY_RESIDENT                                 0xFFFFEFFF
#define   S_02803C_FAULT_BEHAVIOR(x)                                  (((unsigned)(x) & 0x03) << 13)
#define   G_02803C_FAULT_BEHAVIOR(x)                                  (((x) >> 13) & 0x03)
#define   C_02803C_FAULT_BEHAVIOR                                     0xFFFF9FFF
#define   S_02803C_ITERATE_FLUSH(x)                                   (((unsigned)(x) & 0x1) << 15)
#define   G_02803C_ITERATE_FLUSH(x)                                   (((x) >> 15) & 0x1)
#define   C_02803C_ITERATE_FLUSH                                      0xFFFF7FFF
#define   S_02803C_ALLOW_EXPCLEAR(x)                                  (((unsigned)(x) & 0x1) << 27)
#define   G_02803C_ALLOW_EXPCLEAR(x)                                  (((x) >> 27) & 0x1)
#define   C_02803C_ALLOW_EXPCLEAR                                     0xF7FFFFFF
#define   S_02803C_TILE_STENCIL_DISABLE(x)                            (((unsigned)(x) & 0x1) << 29)
#define   G_02803C_TILE_STENCIL_DISABLE(x)                            (((x) >> 29) & 0x1)
#define   C_02803C_TILE_STENCIL_DISABLE                               0xDFFFFFFF
#define   S_02803C_CLEAR_DISALLOWED(x)                                (((unsigned)(x) & 0x1) << 30)
#define   G_02803C_CLEAR_DISALLOWED(x)                                (((x) >> 30) & 0x1)
#define   C_02803C_CLEAR_DISALLOWED                                   0xBFFFFFFF
#define R_028040_DB_Z_READ_BASE                                         0x028040
#define R_028044_DB_Z_READ_BASE_HI                                      0x028044
#define   S_028044_BASE_HI(x)                                         (((unsigned)(x) & 0xFF) << 0)
#define   G_028044_BASE_HI(x)                                         (((x) >> 0) & 0xFF)
#define   C_028044_BASE_HI                                            0xFFFFFF00
#define R_028048_DB_STENCIL_READ_BASE                                   0x028048
#define R_02804C_DB_STENCIL_READ_BASE_HI                                0x02804C
#define   S_02804C_BASE_HI(x)                                         (((unsigned)(x) & 0xFF) << 0)
#define   G_02804C_BASE_HI(x)                                         (((x) >> 0) & 0xFF)
#define   C_02804C_BASE_HI                                            0xFFFFFF00
#define R_028050_DB_Z_WRITE_BASE                                        0x028050
#define R_028054_DB_Z_WRITE_BASE_HI                                     0x028054
#define   S_028054_BASE_HI(x)                                         (((unsigned)(x) & 0xFF) << 0)
#define   G_028054_BASE_HI(x)                                         (((x) >> 0) & 0xFF)
#define   C_028054_BASE_HI                                            0xFFFFFF00
#define R_028058_DB_STENCIL_WRITE_BASE                                  0x028058
#define R_02805C_DB_STENCIL_WRITE_BASE_HI                               0x02805C
#define   S_02805C_BASE_HI(x)                                         (((unsigned)(x) & 0xFF) << 0)
#define   G_02805C_BASE_HI(x)                                         (((x) >> 0) & 0xFF)
#define   C_02805C_BASE_HI                                            0xFFFFFF00
#define R_028060_DB_DFSM_CONTROL                                        0x028060
#define   S_028060_PUNCHOUT_MODE(x)                                   (((unsigned)(x) & 0x03) << 0)
#define   G_028060_PUNCHOUT_MODE(x)                                   (((x) >> 0) & 0x03)
#define   C_028060_PUNCHOUT_MODE                                      0xFFFFFFFC
#define     V_028060_AUTO						0
#define     V_028060_FORCE_ON						1
#define     V_028060_FORCE_OFF						2
#define     V_028060_RESERVED						3
#define   S_028060_POPS_DRAIN_PS_ON_OVERLAP(x)                        (((unsigned)(x) & 0x1) << 2)
#define   G_028060_POPS_DRAIN_PS_ON_OVERLAP(x)                        (((x) >> 2) & 0x1)
#define   C_028060_POPS_DRAIN_PS_ON_OVERLAP                           0xFFFFFFFB
#define   S_028060_DISALLOW_OVERFLOW(x)                               (((unsigned)(x) & 0x1) << 3)
#define   G_028060_DISALLOW_OVERFLOW(x)                               (((x) >> 3) & 0x1)
#define   C_028060_DISALLOW_OVERFLOW                                  0xFFFFFFF7
#define R_028068_DB_Z_INFO2                                             0x028068
#define   S_028068_EPITCH(x)                                          (((unsigned)(x) & 0xFFFF) << 0)
#define   G_028068_EPITCH(x)                                          (((x) >> 0) & 0xFFFF)
#define   C_028068_EPITCH                                             0xFFFF0000
#define R_02806C_DB_STENCIL_INFO2                                       0x02806C
#define   S_02806C_EPITCH(x)                                          (((unsigned)(x) & 0xFFFF) << 0)
#define   G_02806C_EPITCH(x)                                          (((x) >> 0) & 0xFFFF)
#define   C_02806C_EPITCH                                             0xFFFF0000
#define R_028080_TA_BC_BASE_ADDR                                        0x028080
#define R_028084_TA_BC_BASE_ADDR_HI                                     0x028084
#define   S_028084_ADDRESS(x)                                         (((unsigned)(x) & 0xFF) << 0)
#define   G_028084_ADDRESS(x)                                         (((x) >> 0) & 0xFF)
#define   C_028084_ADDRESS                                            0xFFFFFF00
#define R_0281E8_COHER_DEST_BASE_HI_0                                   0x0281E8
#define   S_0281E8_DEST_BASE_HI_256B(x)                               (((unsigned)(x) & 0xFF) << 0)
#define   G_0281E8_DEST_BASE_HI_256B(x)                               (((x) >> 0) & 0xFF)
#define   C_0281E8_DEST_BASE_HI_256B                                  0xFFFFFF00
#define R_0281EC_COHER_DEST_BASE_HI_1                                   0x0281EC
#define   S_0281EC_DEST_BASE_HI_256B(x)                               (((unsigned)(x) & 0xFF) << 0)
#define   G_0281EC_DEST_BASE_HI_256B(x)                               (((x) >> 0) & 0xFF)
#define   C_0281EC_DEST_BASE_HI_256B                                  0xFFFFFF00
#define R_0281F0_COHER_DEST_BASE_HI_2                                   0x0281F0
#define   S_0281F0_DEST_BASE_HI_256B(x)                               (((unsigned)(x) & 0xFF) << 0)
#define   G_0281F0_DEST_BASE_HI_256B(x)                               (((x) >> 0) & 0xFF)
#define   C_0281F0_DEST_BASE_HI_256B                                  0xFFFFFF00
#define R_0281F4_COHER_DEST_BASE_HI_3                                   0x0281F4
#define   S_0281F4_DEST_BASE_HI_256B(x)                               (((unsigned)(x) & 0xFF) << 0)
#define   G_0281F4_DEST_BASE_HI_256B(x)                               (((x) >> 0) & 0xFF)
#define   C_0281F4_DEST_BASE_HI_256B                                  0xFFFFFF00
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
#define   S_028350_RB_MAP_PKR1(x)                                     (((unsigned)(x) & 0x03) << 2)
#define   G_028350_RB_MAP_PKR1(x)                                     (((x) >> 2) & 0x03)
#define   C_028350_RB_MAP_PKR1                                        0xFFFFFFF3
#define   S_028350_RB_XSEL2(x)                                        (((unsigned)(x) & 0x03) << 4)
#define   G_028350_RB_XSEL2(x)                                        (((x) >> 4) & 0x03)
#define   C_028350_RB_XSEL2                                           0xFFFFFFCF
#define   S_028350_RB_XSEL(x)                                         (((unsigned)(x) & 0x1) << 6)
#define   G_028350_RB_XSEL(x)                                         (((x) >> 6) & 0x1)
#define   C_028350_RB_XSEL                                            0xFFFFFFBF
#define   S_028350_RB_YSEL(x)                                         (((unsigned)(x) & 0x1) << 7)
#define   G_028350_RB_YSEL(x)                                         (((x) >> 7) & 0x1)
#define   C_028350_RB_YSEL                                            0xFFFFFF7F
#define   S_028350_PKR_MAP(x)                                         (((unsigned)(x) & 0x03) << 8)
#define   G_028350_PKR_MAP(x)                                         (((x) >> 8) & 0x03)
#define   C_028350_PKR_MAP                                            0xFFFFFCFF
#define   S_028350_PKR_XSEL(x)                                        (((unsigned)(x) & 0x03) << 10)
#define   G_028350_PKR_XSEL(x)                                        (((x) >> 10) & 0x03)
#define   C_028350_PKR_XSEL                                           0xFFFFF3FF
#define   S_028350_PKR_YSEL(x)                                        (((unsigned)(x) & 0x03) << 12)
#define   G_028350_PKR_YSEL(x)                                        (((x) >> 12) & 0x03)
#define   C_028350_PKR_YSEL                                           0xFFFFCFFF
#define   S_028350_PKR_XSEL2(x)                                       (((unsigned)(x) & 0x03) << 14)
#define   G_028350_PKR_XSEL2(x)                                       (((x) >> 14) & 0x03)
#define   C_028350_PKR_XSEL2                                          0xFFFF3FFF
#define   S_028350_SC_MAP(x)                                          (((unsigned)(x) & 0x03) << 16)
#define   G_028350_SC_MAP(x)                                          (((x) >> 16) & 0x03)
#define   C_028350_SC_MAP                                             0xFFFCFFFF
#define   S_028350_SC_XSEL(x)                                         (((unsigned)(x) & 0x03) << 18)
#define   G_028350_SC_XSEL(x)                                         (((x) >> 18) & 0x03)
#define   C_028350_SC_XSEL                                            0xFFF3FFFF
#define   S_028350_SC_YSEL(x)                                         (((unsigned)(x) & 0x03) << 20)
#define   G_028350_SC_YSEL(x)                                         (((x) >> 20) & 0x03)
#define   C_028350_SC_YSEL                                            0xFFCFFFFF
#define   S_028350_SE_MAP(x)                                          (((unsigned)(x) & 0x03) << 24)
#define   G_028350_SE_MAP(x)                                          (((x) >> 24) & 0x03)
#define   C_028350_SE_MAP                                             0xFCFFFFFF
#define   S_028350_SE_XSEL_GFX9(x)                                    (((unsigned)(x) & 0x07) << 26)
#define   G_028350_SE_XSEL_GFX9(x)                                    (((x) >> 26) & 0x07)
#define   C_028350_SE_XSEL_GFX9                                       0xE3FFFFFF
#define   S_028350_SE_YSEL_GFX9(x)                                    (((unsigned)(x) & 0x07) << 29)
#define   G_028350_SE_YSEL_GFX9(x)                                    (((x) >> 29) & 0x07)
#define   C_028350_SE_YSEL_GFX9                                       0x1FFFFFFF
#define R_028354_PA_SC_RASTER_CONFIG_1                                  0x028354
#define   S_028354_SE_PAIR_MAP(x)                                     (((unsigned)(x) & 0x03) << 0)
#define   G_028354_SE_PAIR_MAP(x)                                     (((x) >> 0) & 0x03)
#define   C_028354_SE_PAIR_MAP                                        0xFFFFFFFC
#define   S_028354_SE_PAIR_XSEL_GFX9(x)                               (((unsigned)(x) & 0x07) << 2)
#define   G_028354_SE_PAIR_XSEL_GFX9(x)                               (((x) >> 2) & 0x07)
#define   C_028354_SE_PAIR_XSEL_GFX9                                  0xFFFFFFE3
#define   S_028354_SE_PAIR_YSEL_GFX9(x)                               (((unsigned)(x) & 0x07) << 5)
#define   G_028354_SE_PAIR_YSEL_GFX9(x)                               (((x) >> 5) & 0x07)
#define   C_028354_SE_PAIR_YSEL_GFX9                                  0xFFFFFF1F
#define R_028358_PA_SC_SCREEN_EXTENT_CONTROL                            0x028358
#define   S_028358_SLICE_EVEN_ENABLE(x)                               (((unsigned)(x) & 0x03) << 0)
#define   G_028358_SLICE_EVEN_ENABLE(x)                               (((x) >> 0) & 0x03)
#define   C_028358_SLICE_EVEN_ENABLE                                  0xFFFFFFFC
#define   S_028358_SLICE_ODD_ENABLE(x)                                (((unsigned)(x) & 0x03) << 2)
#define   G_028358_SLICE_ODD_ENABLE(x)                                (((x) >> 2) & 0x03)
#define   C_028358_SLICE_ODD_ENABLE                                   0xFFFFFFF3
#define R_02835C_PA_SC_TILE_STEERING_OVERRIDE                           0x02835C
#define   S_02835C_ENABLE(x)                                          (((unsigned)(x) & 0x1) << 0)
#define   G_02835C_ENABLE(x)                                          (((x) >> 0) & 0x1)
#define   C_02835C_ENABLE                                             0xFFFFFFFE
#define   S_02835C_NUM_SE(x)                                          (((unsigned)(x) & 0x03) << 1)
#define   G_02835C_NUM_SE(x)                                          (((x) >> 1) & 0x03)
#define   C_02835C_NUM_SE                                             0xFFFFFFF9
#define   S_02835C_NUM_RB_PER_SE(x)                                   (((unsigned)(x) & 0x03) << 5)
#define   G_02835C_NUM_RB_PER_SE(x)                                   (((x) >> 5) & 0x03)
#define   C_02835C_NUM_RB_PER_SE                                      0xFFFFFF9F
#define R_028360_CP_PERFMON_CNTX_CNTL                                   0x028360
#define   S_028360_PERFMON_ENABLE(x)                                  (((unsigned)(x) & 0x1) << 31)
#define   G_028360_PERFMON_ENABLE(x)                                  (((x) >> 31) & 0x1)
#define   C_028360_PERFMON_ENABLE                                     0x7FFFFFFF
#define R_0283A0_PA_SC_RIGHT_VERT_GRID                                  0x0283A0
#define   S_0283A0_LEFT_QTR(x)                                        (((unsigned)(x) & 0xFF) << 0)
#define   G_0283A0_LEFT_QTR(x)                                        (((x) >> 0) & 0xFF)
#define   C_0283A0_LEFT_QTR                                           0xFFFFFF00
#define   S_0283A0_LEFT_HALF(x)                                       (((unsigned)(x) & 0xFF) << 8)
#define   G_0283A0_LEFT_HALF(x)                                       (((x) >> 8) & 0xFF)
#define   C_0283A0_LEFT_HALF                                          0xFFFF00FF
#define   S_0283A0_RIGHT_HALF(x)                                      (((unsigned)(x) & 0xFF) << 16)
#define   G_0283A0_RIGHT_HALF(x)                                      (((x) >> 16) & 0xFF)
#define   C_0283A0_RIGHT_HALF                                         0xFF00FFFF
#define   S_0283A0_RIGHT_QTR(x)                                       (((unsigned)(x) & 0xFF) << 24)
#define   G_0283A0_RIGHT_QTR(x)                                       (((x) >> 24) & 0xFF)
#define   C_0283A0_RIGHT_QTR                                          0x00FFFFFF
#define R_0283A4_PA_SC_LEFT_VERT_GRID                                   0x0283A4
#define   S_0283A4_LEFT_QTR(x)                                        (((unsigned)(x) & 0xFF) << 0)
#define   G_0283A4_LEFT_QTR(x)                                        (((x) >> 0) & 0xFF)
#define   C_0283A4_LEFT_QTR                                           0xFFFFFF00
#define   S_0283A4_LEFT_HALF(x)                                       (((unsigned)(x) & 0xFF) << 8)
#define   G_0283A4_LEFT_HALF(x)                                       (((x) >> 8) & 0xFF)
#define   C_0283A4_LEFT_HALF                                          0xFFFF00FF
#define   S_0283A4_RIGHT_HALF(x)                                      (((unsigned)(x) & 0xFF) << 16)
#define   G_0283A4_RIGHT_HALF(x)                                      (((x) >> 16) & 0xFF)
#define   C_0283A4_RIGHT_HALF                                         0xFF00FFFF
#define   S_0283A4_RIGHT_QTR(x)                                       (((unsigned)(x) & 0xFF) << 24)
#define   G_0283A4_RIGHT_QTR(x)                                       (((x) >> 24) & 0xFF)
#define   C_0283A4_RIGHT_QTR                                          0x00FFFFFF
#define R_0283A8_PA_SC_HORIZ_GRID                                       0x0283A8
#define   S_0283A8_TOP_QTR(x)                                         (((unsigned)(x) & 0xFF) << 0)
#define   G_0283A8_TOP_QTR(x)                                         (((x) >> 0) & 0xFF)
#define   C_0283A8_TOP_QTR                                            0xFFFFFF00
#define   S_0283A8_TOP_HALF(x)                                        (((unsigned)(x) & 0xFF) << 8)
#define   G_0283A8_TOP_HALF(x)                                        (((x) >> 8) & 0xFF)
#define   C_0283A8_TOP_HALF                                           0xFFFF00FF
#define   S_0283A8_BOT_HALF(x)                                        (((unsigned)(x) & 0xFF) << 16)
#define   G_0283A8_BOT_HALF(x)                                        (((x) >> 16) & 0xFF)
#define   C_0283A8_BOT_HALF                                           0xFF00FFFF
#define   S_0283A8_BOT_QTR(x)                                         (((unsigned)(x) & 0xFF) << 24)
#define   G_0283A8_BOT_QTR(x)                                         (((x) >> 24) & 0xFF)
#define   C_0283A8_BOT_QTR                                            0x00FFFFFF
#define R_02840C_VGT_MULTI_PRIM_IB_RESET_INDX                           0x02840C
#define R_028414_CB_BLEND_RED                                           0x028414
#define R_028418_CB_BLEND_GREEN                                         0x028418
#define R_02841C_CB_BLEND_BLUE                                          0x02841C
#define R_028420_CB_BLEND_ALPHA                                         0x028420
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
#define R_02842C_DB_STENCIL_CONTROL                                     0x02842C
#define   S_02842C_STENCILFAIL(x)                                     (((unsigned)(x) & 0x0F) << 0)
#define   G_02842C_STENCILFAIL(x)                                     (((x) >> 0) & 0x0F)
#define   C_02842C_STENCILFAIL                                        0xFFFFFFF0
#define   S_02842C_STENCILZPASS(x)                                    (((unsigned)(x) & 0x0F) << 4)
#define   G_02842C_STENCILZPASS(x)                                    (((x) >> 4) & 0x0F)
#define   C_02842C_STENCILZPASS                                       0xFFFFFF0F
#define   S_02842C_STENCILZFAIL(x)                                    (((unsigned)(x) & 0x0F) << 8)
#define   G_02842C_STENCILZFAIL(x)                                    (((x) >> 8) & 0x0F)
#define   C_02842C_STENCILZFAIL                                       0xFFFFF0FF
#define   S_02842C_STENCILFAIL_BF(x)                                  (((unsigned)(x) & 0x0F) << 12)
#define   G_02842C_STENCILFAIL_BF(x)                                  (((x) >> 12) & 0x0F)
#define   C_02842C_STENCILFAIL_BF                                     0xFFFF0FFF
#define   S_02842C_STENCILZPASS_BF(x)                                 (((unsigned)(x) & 0x0F) << 16)
#define   G_02842C_STENCILZPASS_BF(x)                                 (((x) >> 16) & 0x0F)
#define   C_02842C_STENCILZPASS_BF                                    0xFFF0FFFF
#define   S_02842C_STENCILZFAIL_BF(x)                                 (((unsigned)(x) & 0x0F) << 20)
#define   G_02842C_STENCILZFAIL_BF(x)                                 (((x) >> 20) & 0x0F)
#define   C_02842C_STENCILZFAIL_BF                                    0xFF0FFFFF
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
#define   S_028644_FLAT_SHADE(x)                                      (((unsigned)(x) & 0x1) << 10)
#define   G_028644_FLAT_SHADE(x)                                      (((x) >> 10) & 0x1)
#define   C_028644_FLAT_SHADE                                         0xFFFFFBFF
#define   S_028644_CYL_WRAP(x)                                        (((unsigned)(x) & 0x0F) << 13)
#define   G_028644_CYL_WRAP(x)                                        (((x) >> 13) & 0x0F)
#define   C_028644_CYL_WRAP                                           0xFFFE1FFF
#define   S_028644_PT_SPRITE_TEX(x)                                   (((unsigned)(x) & 0x1) << 17)
#define   G_028644_PT_SPRITE_TEX(x)                                   (((x) >> 17) & 0x1)
#define   C_028644_PT_SPRITE_TEX                                      0xFFFDFFFF
#define   S_028644_DUP(x)                                             (((unsigned)(x) & 0x1) << 18)
#define   G_028644_DUP(x)                                             (((x) >> 18) & 0x1)
#define   C_028644_DUP                                                0xFFFBFFFF
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
#define   S_0286D4_PNT_SPRITE_OVRD_Y(x)                               (((unsigned)(x) & 0x07) << 5)
#define   G_0286D4_PNT_SPRITE_OVRD_Y(x)                               (((x) >> 5) & 0x07)
#define   C_0286D4_PNT_SPRITE_OVRD_Y                                  0xFFFFFF1F
#define   S_0286D4_PNT_SPRITE_OVRD_Z(x)                               (((unsigned)(x) & 0x07) << 8)
#define   G_0286D4_PNT_SPRITE_OVRD_Z(x)                               (((x) >> 8) & 0x07)
#define   C_0286D4_PNT_SPRITE_OVRD_Z                                  0xFFFFF8FF
#define   S_0286D4_PNT_SPRITE_OVRD_W(x)                               (((unsigned)(x) & 0x07) << 11)
#define   G_0286D4_PNT_SPRITE_OVRD_W(x)                               (((x) >> 11) & 0x07)
#define   C_0286D4_PNT_SPRITE_OVRD_W                                  0xFFFFC7FF
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
#define   S_0286D8_OFFCHIP_PARAM_EN(x)                                (((unsigned)(x) & 0x1) << 7)
#define   G_0286D8_OFFCHIP_PARAM_EN(x)                                (((x) >> 7) & 0x1)
#define   C_0286D8_OFFCHIP_PARAM_EN                                   0xFFFFFF7F
#define   S_0286D8_LATE_PC_DEALLOC(x)                                 (((unsigned)(x) & 0x1) << 8)
#define   G_0286D8_LATE_PC_DEALLOC(x)                                 (((x) >> 8) & 0x1)
#define   C_0286D8_LATE_PC_DEALLOC                                    0xFFFFFEFF
#define   S_0286D8_BC_OPTIMIZE_DISABLE(x)                             (((unsigned)(x) & 0x1) << 14)
#define   G_0286D8_BC_OPTIMIZE_DISABLE(x)                             (((x) >> 14) & 0x1)
#define   C_0286D8_BC_OPTIMIZE_DISABLE                                0xFFFFBFFF
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
#define R_02870C_SPI_SHADER_POS_FORMAT                                  0x02870C
#define   S_02870C_POS0_EXPORT_FORMAT(x)                              (((unsigned)(x) & 0x0F) << 0)
#define   G_02870C_POS0_EXPORT_FORMAT(x)                              (((x) >> 0) & 0x0F)
#define   C_02870C_POS0_EXPORT_FORMAT                                 0xFFFFFFF0
#define   S_02870C_POS1_EXPORT_FORMAT(x)                              (((unsigned)(x) & 0x0F) << 4)
#define   G_02870C_POS1_EXPORT_FORMAT(x)                              (((x) >> 4) & 0x0F)
#define   C_02870C_POS1_EXPORT_FORMAT                                 0xFFFFFF0F
#define   S_02870C_POS2_EXPORT_FORMAT(x)                              (((unsigned)(x) & 0x0F) << 8)
#define   G_02870C_POS2_EXPORT_FORMAT(x)                              (((x) >> 8) & 0x0F)
#define   C_02870C_POS2_EXPORT_FORMAT                                 0xFFFFF0FF
#define   S_02870C_POS3_EXPORT_FORMAT(x)                              (((unsigned)(x) & 0x0F) << 12)
#define   G_02870C_POS3_EXPORT_FORMAT(x)                              (((x) >> 12) & 0x0F)
#define   C_02870C_POS3_EXPORT_FORMAT                                 0xFFFF0FFF
#define R_028710_SPI_SHADER_Z_FORMAT                                    0x028710
#define   S_028710_Z_EXPORT_FORMAT(x)                                 (((unsigned)(x) & 0x0F) << 0)
#define   G_028710_Z_EXPORT_FORMAT(x)                                 (((x) >> 0) & 0x0F)
#define   C_028710_Z_EXPORT_FORMAT                                    0xFFFFFFF0
#define R_028714_SPI_SHADER_COL_FORMAT                                  0x028714
#define   S_028714_COL0_EXPORT_FORMAT(x)                              (((unsigned)(x) & 0x0F) << 0)
#define   G_028714_COL0_EXPORT_FORMAT(x)                              (((x) >> 0) & 0x0F)
#define   C_028714_COL0_EXPORT_FORMAT                                 0xFFFFFFF0
#define   S_028714_COL1_EXPORT_FORMAT(x)                              (((unsigned)(x) & 0x0F) << 4)
#define   G_028714_COL1_EXPORT_FORMAT(x)                              (((x) >> 4) & 0x0F)
#define   C_028714_COL1_EXPORT_FORMAT                                 0xFFFFFF0F
#define   S_028714_COL2_EXPORT_FORMAT(x)                              (((unsigned)(x) & 0x0F) << 8)
#define   G_028714_COL2_EXPORT_FORMAT(x)                              (((x) >> 8) & 0x0F)
#define   C_028714_COL2_EXPORT_FORMAT                                 0xFFFFF0FF
#define   S_028714_COL3_EXPORT_FORMAT(x)                              (((unsigned)(x) & 0x0F) << 12)
#define   G_028714_COL3_EXPORT_FORMAT(x)                              (((x) >> 12) & 0x0F)
#define   C_028714_COL3_EXPORT_FORMAT                                 0xFFFF0FFF
#define   S_028714_COL4_EXPORT_FORMAT(x)                              (((unsigned)(x) & 0x0F) << 16)
#define   G_028714_COL4_EXPORT_FORMAT(x)                              (((x) >> 16) & 0x0F)
#define   C_028714_COL4_EXPORT_FORMAT                                 0xFFF0FFFF
#define   S_028714_COL5_EXPORT_FORMAT(x)                              (((unsigned)(x) & 0x0F) << 20)
#define   G_028714_COL5_EXPORT_FORMAT(x)                              (((x) >> 20) & 0x0F)
#define   C_028714_COL5_EXPORT_FORMAT                                 0xFF0FFFFF
#define   S_028714_COL6_EXPORT_FORMAT(x)                              (((unsigned)(x) & 0x0F) << 24)
#define   G_028714_COL6_EXPORT_FORMAT(x)                              (((x) >> 24) & 0x0F)
#define   C_028714_COL6_EXPORT_FORMAT                                 0xF0FFFFFF
#define   S_028714_COL7_EXPORT_FORMAT(x)                              (((unsigned)(x) & 0x0F) << 28)
#define   G_028714_COL7_EXPORT_FORMAT(x)                              (((x) >> 28) & 0x0F)
#define   C_028714_COL7_EXPORT_FORMAT                                 0x0FFFFFFF
#define R_028754_SX_PS_DOWNCONVERT                                      0x028754
#define   S_028754_MRT0(x)                                            (((unsigned)(x) & 0x0F) << 0)
#define   G_028754_MRT0(x)                                            (((x) >> 0) & 0x0F)
#define   C_028754_MRT0                                               0xFFFFFFF0
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
#define   S_028760_COLOR_DST_OPT(x)                                   (((unsigned)(x) & 0x07) << 4)
#define   G_028760_COLOR_DST_OPT(x)                                   (((x) >> 4) & 0x07)
#define   C_028760_COLOR_DST_OPT                                      0xFFFFFF8F
#define   S_028760_COLOR_COMB_FCN(x)                                  (((unsigned)(x) & 0x07) << 8)
#define   G_028760_COLOR_COMB_FCN(x)                                  (((x) >> 8) & 0x07)
#define   C_028760_COLOR_COMB_FCN                                     0xFFFFF8FF
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
#define R_028780_CB_BLEND0_CONTROL                                      0x028780
#define   S_028780_COLOR_SRCBLEND(x)                                  (((unsigned)(x) & 0x1F) << 0)
#define   G_028780_COLOR_SRCBLEND(x)                                  (((x) >> 0) & 0x1F)
#define   C_028780_COLOR_SRCBLEND                                     0xFFFFFFE0
#define   S_028780_COLOR_COMB_FCN(x)                                  (((unsigned)(x) & 0x07) << 5)
#define   G_028780_COLOR_COMB_FCN(x)                                  (((x) >> 5) & 0x07)
#define   C_028780_COLOR_COMB_FCN                                     0xFFFFFF1F
#define   S_028780_COLOR_DESTBLEND(x)                                 (((unsigned)(x) & 0x1F) << 8)
#define   G_028780_COLOR_DESTBLEND(x)                                 (((x) >> 8) & 0x1F)
#define   C_028780_COLOR_DESTBLEND                                    0xFFFFE0FF
#define   S_028780_ALPHA_SRCBLEND(x)                                  (((unsigned)(x) & 0x1F) << 16)
#define   G_028780_ALPHA_SRCBLEND(x)                                  (((x) >> 16) & 0x1F)
#define   C_028780_ALPHA_SRCBLEND                                     0xFFE0FFFF
#define   S_028780_ALPHA_COMB_FCN(x)                                  (((unsigned)(x) & 0x07) << 21)
#define   G_028780_ALPHA_COMB_FCN(x)                                  (((x) >> 21) & 0x07)
#define   C_028780_ALPHA_COMB_FCN                                     0xFF1FFFFF
#define   S_028780_ALPHA_DESTBLEND(x)                                 (((unsigned)(x) & 0x1F) << 24)
#define   G_028780_ALPHA_DESTBLEND(x)                                 (((x) >> 24) & 0x1F)
#define   C_028780_ALPHA_DESTBLEND                                    0xE0FFFFFF
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
#define R_0287A0_CB_MRT0_EPITCH                                         0x0287A0
#define   S_0287A0_EPITCH(x)                                          (((unsigned)(x) & 0xFFFF) << 0)
#define   G_0287A0_EPITCH(x)                                          (((x) >> 0) & 0xFFFF)
#define   C_0287A0_EPITCH                                             0xFFFF0000
#define R_0287A4_CB_MRT1_EPITCH                                         0x0287A4
#define   S_0287A4_EPITCH(x)                                          (((unsigned)(x) & 0xFFFF) << 0)
#define   G_0287A4_EPITCH(x)                                          (((x) >> 0) & 0xFFFF)
#define   C_0287A4_EPITCH                                             0xFFFF0000
#define R_0287A8_CB_MRT2_EPITCH                                         0x0287A8
#define   S_0287A8_EPITCH(x)                                          (((unsigned)(x) & 0xFFFF) << 0)
#define   G_0287A8_EPITCH(x)                                          (((x) >> 0) & 0xFFFF)
#define   C_0287A8_EPITCH                                             0xFFFF0000
#define R_0287AC_CB_MRT3_EPITCH                                         0x0287AC
#define   S_0287AC_EPITCH(x)                                          (((unsigned)(x) & 0xFFFF) << 0)
#define   G_0287AC_EPITCH(x)                                          (((x) >> 0) & 0xFFFF)
#define   C_0287AC_EPITCH                                             0xFFFF0000
#define R_0287B0_CB_MRT4_EPITCH                                         0x0287B0
#define   S_0287B0_EPITCH(x)                                          (((unsigned)(x) & 0xFFFF) << 0)
#define   G_0287B0_EPITCH(x)                                          (((x) >> 0) & 0xFFFF)
#define   C_0287B0_EPITCH                                             0xFFFF0000
#define R_0287B4_CB_MRT5_EPITCH                                         0x0287B4
#define   S_0287B4_EPITCH(x)                                          (((unsigned)(x) & 0xFFFF) << 0)
#define   G_0287B4_EPITCH(x)                                          (((x) >> 0) & 0xFFFF)
#define   C_0287B4_EPITCH                                             0xFFFF0000
#define R_0287B8_CB_MRT6_EPITCH                                         0x0287B8
#define   S_0287B8_EPITCH(x)                                          (((unsigned)(x) & 0xFFFF) << 0)
#define   G_0287B8_EPITCH(x)                                          (((x) >> 0) & 0xFFFF)
#define   C_0287B8_EPITCH                                             0xFFFF0000
#define R_0287BC_CB_MRT7_EPITCH                                         0x0287BC
#define   S_0287BC_EPITCH(x)                                          (((unsigned)(x) & 0xFFFF) << 0)
#define   G_0287BC_EPITCH(x)                                          (((x) >> 0) & 0xFFFF)
#define   C_0287BC_EPITCH                                             0xFFFF0000
#define R_0287CC_CS_COPY_STATE                                          0x0287CC
#define   S_0287CC_SRC_STATE_ID(x)                                    (((unsigned)(x) & 0x07) << 0)
#define   G_0287CC_SRC_STATE_ID(x)                                    (((x) >> 0) & 0x07)
#define   C_0287CC_SRC_STATE_ID                                       0xFFFFFFF8
#define R_0287D4_PA_CL_POINT_X_RAD                                      0x0287D4
#define R_0287D8_PA_CL_POINT_Y_RAD                                      0x0287D8
#define R_0287DC_PA_CL_POINT_SIZE                                       0x0287DC
#define R_0287E0_PA_CL_POINT_CULL_RAD                                   0x0287E0
#define R_0287E4_VGT_DMA_BASE_HI                                        0x0287E4
#define   S_0287E4_BASE_ADDR_GFX9(x)                                  (((unsigned)(x) & 0xFFFF) << 0)
#define   G_0287E4_BASE_ADDR_GFX9(x)                                  (((x) >> 0) & 0xFFFF)
#define   C_0287E4_BASE_ADDR_GFX9                                     0xFFFF0000
#define R_0287E8_VGT_DMA_BASE                                           0x0287E8
#define R_0287F0_VGT_DRAW_INITIATOR                                     0x0287F0
#define   S_0287F0_SOURCE_SELECT(x)                                   (((unsigned)(x) & 0x03) << 0)
#define   G_0287F0_SOURCE_SELECT(x)                                   (((x) >> 0) & 0x03)
#define   C_0287F0_SOURCE_SELECT                                      0xFFFFFFFC
#define   S_0287F0_MAJOR_MODE(x)                                      (((unsigned)(x) & 0x03) << 2)
#define   G_0287F0_MAJOR_MODE(x)                                      (((x) >> 2) & 0x03)
#define   C_0287F0_MAJOR_MODE                                         0xFFFFFFF3
#define   S_0287F0_SPRITE_EN_R6XX(x)                                  (((unsigned)(x) & 0x1) << 4)
#define   G_0287F0_SPRITE_EN_R6XX(x)                                  (((x) >> 4) & 0x1)
#define   C_0287F0_SPRITE_EN_R6XX                                     0xFFFFFFEF
#define   S_0287F0_NOT_EOP(x)                                         (((unsigned)(x) & 0x1) << 5)
#define   G_0287F0_NOT_EOP(x)                                         (((x) >> 5) & 0x1)
#define   C_0287F0_NOT_EOP                                            0xFFFFFFDF
#define   S_0287F0_USE_OPAQUE(x)                                      (((unsigned)(x) & 0x1) << 6)
#define   G_0287F0_USE_OPAQUE(x)                                      (((x) >> 6) & 0x1)
#define   C_0287F0_USE_OPAQUE                                         0xFFFFFFBF
#define   S_0287F0_UNROLLED_INST(x)                                   (((unsigned)(x) & 0x1) << 7)
#define   G_0287F0_UNROLLED_INST(x)                                   (((x) >> 7) & 0x1)
#define   C_0287F0_UNROLLED_INST                                      0xFFFFFF7F
#define   S_0287F0_GRBM_SKEW_NO_DEC(x)                                (((unsigned)(x) & 0x1) << 8)
#define   G_0287F0_GRBM_SKEW_NO_DEC(x)                                (((x) >> 8) & 0x1)
#define   C_0287F0_GRBM_SKEW_NO_DEC                                   0xFFFFFEFF
#define   S_0287F0_REG_RT_INDEX(x)                                    (((unsigned)(x) & 0x07) << 29)
#define   G_0287F0_REG_RT_INDEX(x)                                    (((x) >> 29) & 0x07)
#define   C_0287F0_REG_RT_INDEX                                       0x1FFFFFFF
#define R_0287F4_VGT_IMMED_DATA                                         0x0287F4
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
#define   S_028800_BACKFACE_ENABLE(x)                                 (((unsigned)(x) & 0x1) << 7)
#define   G_028800_BACKFACE_ENABLE(x)                                 (((x) >> 7) & 0x1)
#define   C_028800_BACKFACE_ENABLE                                    0xFFFFFF7F
#define   S_028800_STENCILFUNC(x)                                     (((unsigned)(x) & 0x07) << 8)
#define   G_028800_STENCILFUNC(x)                                     (((x) >> 8) & 0x07)
#define   C_028800_STENCILFUNC                                        0xFFFFF8FF
#define   S_028800_STENCILFUNC_BF(x)                                  (((unsigned)(x) & 0x07) << 20)
#define   G_028800_STENCILFUNC_BF(x)                                  (((x) >> 20) & 0x07)
#define   C_028800_STENCILFUNC_BF                                     0xFF8FFFFF
#define   S_028800_ENABLE_COLOR_WRITES_ON_DEPTH_FAIL(x)               (((unsigned)(x) & 0x1) << 30)
#define   G_028800_ENABLE_COLOR_WRITES_ON_DEPTH_FAIL(x)               (((x) >> 30) & 0x1)
#define   C_028800_ENABLE_COLOR_WRITES_ON_DEPTH_FAIL                  0xBFFFFFFF
#define   S_028800_DISABLE_COLOR_WRITES_ON_DEPTH_PASS(x)              (((unsigned)(x) & 0x1) << 31)
#define   G_028800_DISABLE_COLOR_WRITES_ON_DEPTH_PASS(x)              (((x) >> 31) & 0x1)
#define   C_028800_DISABLE_COLOR_WRITES_ON_DEPTH_PASS                 0x7FFFFFFF
#define R_028804_DB_EQAA                                                0x028804
#define   S_028804_MAX_ANCHOR_SAMPLES(x)                              (((unsigned)(x) & 0x07) << 0)
#define   G_028804_MAX_ANCHOR_SAMPLES(x)                              (((x) >> 0) & 0x07)
#define   C_028804_MAX_ANCHOR_SAMPLES                                 0xFFFFFFF8
#define   S_028804_PS_ITER_SAMPLES(x)                                 (((unsigned)(x) & 0x07) << 4)
#define   G_028804_PS_ITER_SAMPLES(x)                                 (((x) >> 4) & 0x07)
#define   C_028804_PS_ITER_SAMPLES                                    0xFFFFFF8F
#define   S_028804_MASK_EXPORT_NUM_SAMPLES(x)                         (((unsigned)(x) & 0x07) << 8)
#define   G_028804_MASK_EXPORT_NUM_SAMPLES(x)                         (((x) >> 8) & 0x07)
#define   C_028804_MASK_EXPORT_NUM_SAMPLES                            0xFFFFF8FF
#define   S_028804_ALPHA_TO_MASK_NUM_SAMPLES(x)                       (((unsigned)(x) & 0x07) << 12)
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
#define   S_028808_ROP3(x)                                            (((unsigned)(x) & 0xFF) << 16)
#define   G_028808_ROP3(x)                                            (((x) >> 16) & 0xFF)
#define   C_028808_ROP3                                               0xFF00FFFF
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
#define   S_02880C_CONSERVATIVE_Z_EXPORT(x)                           (((unsigned)(x) & 0x03) << 13)
#define   G_02880C_CONSERVATIVE_Z_EXPORT(x)                           (((x) >> 13) & 0x03)
#define   C_02880C_CONSERVATIVE_Z_EXPORT                              0xFFFF9FFF
#define   S_02880C_DUAL_QUAD_DISABLE(x)                               (((unsigned)(x) & 0x1) << 15)
#define   G_02880C_DUAL_QUAD_DISABLE(x)                               (((x) >> 15) & 0x1)
#define   C_02880C_DUAL_QUAD_DISABLE                                  0xFFFF7FFF
#define   S_02880C_PRIMITIVE_ORDERED_PIXEL_SHADER(x)                  (((unsigned)(x) & 0x1) << 16)
#define   G_02880C_PRIMITIVE_ORDERED_PIXEL_SHADER(x)                  (((x) >> 16) & 0x1)
#define   C_02880C_PRIMITIVE_ORDERED_PIXEL_SHADER                     0xFFFEFFFF
#define   S_02880C_EXEC_IF_OVERLAPPED(x)                              (((unsigned)(x) & 0x1) << 17)
#define   G_02880C_EXEC_IF_OVERLAPPED(x)                              (((x) >> 17) & 0x1)
#define   C_02880C_EXEC_IF_OVERLAPPED                                 0xFFFDFFFF
#define   S_02880C_POPS_OVERLAP_NUM_SAMPLES(x)                        (((unsigned)(x) & 0x07) << 20)
#define   G_02880C_POPS_OVERLAP_NUM_SAMPLES(x)                        (((x) >> 20) & 0x07)
#define   C_02880C_POPS_OVERLAP_NUM_SAMPLES                           0xFF8FFFFF
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
#define   S_028814_POLYMODE_FRONT_PTYPE(x)                            (((unsigned)(x) & 0x07) << 5)
#define   G_028814_POLYMODE_FRONT_PTYPE(x)                            (((x) >> 5) & 0x07)
#define   C_028814_POLYMODE_FRONT_PTYPE                               0xFFFFFF1F
#define   S_028814_POLYMODE_BACK_PTYPE(x)                             (((unsigned)(x) & 0x07) << 8)
#define   G_028814_POLYMODE_BACK_PTYPE(x)                             (((x) >> 8) & 0x07)
#define   C_028814_POLYMODE_BACK_PTYPE                                0xFFFFF8FF
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
#define   S_028814_RIGHT_TRIANGLE_ALTERNATE_GRADIENT_REF(x)           (((unsigned)(x) & 0x1) << 22)
#define   G_028814_RIGHT_TRIANGLE_ALTERNATE_GRADIENT_REF(x)           (((x) >> 22) & 0x1)
#define   C_028814_RIGHT_TRIANGLE_ALTERNATE_GRADIENT_REF              0xFFBFFFFF
#define   S_028814_NEW_QUAD_DECOMPOSITION(x)                          (((unsigned)(x) & 0x1) << 23)
#define   G_028814_NEW_QUAD_DECOMPOSITION(x)                          (((x) >> 23) & 0x1)
#define   C_028814_NEW_QUAD_DECOMPOSITION                             0xFF7FFFFF
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
#define   S_028818_PERFCOUNTER_REF(x)                                 (((unsigned)(x) & 0x1) << 11)
#define   G_028818_PERFCOUNTER_REF(x)                                 (((x) >> 11) & 0x1)
#define   C_028818_PERFCOUNTER_REF                                    0xFFFFF7FF
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
#define   S_02881C_USE_VTX_LINE_WIDTH(x)                              (((unsigned)(x) & 0x1) << 26)
#define   G_02881C_USE_VTX_LINE_WIDTH(x)                              (((x) >> 26) & 0x1)
#define   C_02881C_USE_VTX_LINE_WIDTH                                 0xFBFFFFFF
#define   S_02881C_USE_VTX_SHD_OBJPRIM_ID(x)                          (((unsigned)(x) & 0x1) << 27)
#define   G_02881C_USE_VTX_SHD_OBJPRIM_ID(x)                          (((x) >> 27) & 0x1)
#define   C_02881C_USE_VTX_SHD_OBJPRIM_ID                             0xF7FFFFFF
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
#define   S_02882C_XMAX_RIGHT_EXCLUSION(x)                            (((unsigned)(x) & 0x1) << 30)
#define   G_02882C_XMAX_RIGHT_EXCLUSION(x)                            (((x) >> 30) & 0x1)
#define   C_02882C_XMAX_RIGHT_EXCLUSION                               0xBFFFFFFF
#define   S_02882C_YMAX_BOTTOM_EXCLUSION(x)                           (((unsigned)(x) & 0x1) << 31)
#define   G_02882C_YMAX_BOTTOM_EXCLUSION(x)                           (((x) >> 31) & 0x1)
#define   C_02882C_YMAX_BOTTOM_EXCLUSION                              0x7FFFFFFF
#define R_028830_PA_SU_SMALL_PRIM_FILTER_CNTL                           0x028830
#define   S_028830_SMALL_PRIM_FILTER_ENABLE(x)                        (((unsigned)(x) & 0x1) << 0)
#define   G_028830_SMALL_PRIM_FILTER_ENABLE(x)                        (((x) >> 0) & 0x1)
#define   C_028830_SMALL_PRIM_FILTER_ENABLE                           0xFFFFFFFE
#define   S_028830_TRIANGLE_FILTER_DISABLE(x)                         (((unsigned)(x) & 0x1) << 1)
#define   G_028830_TRIANGLE_FILTER_DISABLE(x)                         (((x) >> 1) & 0x1)
#define   C_028830_TRIANGLE_FILTER_DISABLE                            0xFFFFFFFD
#define   S_028830_LINE_FILTER_DISABLE(x)                             (((unsigned)(x) & 0x1) << 2)
#define   G_028830_LINE_FILTER_DISABLE(x)                             (((x) >> 2) & 0x1)
#define   C_028830_LINE_FILTER_DISABLE                                0xFFFFFFFB
#define   S_028830_POINT_FILTER_DISABLE(x)                            (((unsigned)(x) & 0x1) << 3)
#define   G_028830_POINT_FILTER_DISABLE(x)                            (((x) >> 3) & 0x1)
#define   C_028830_POINT_FILTER_DISABLE                               0xFFFFFFF7
#define   S_028830_RECTANGLE_FILTER_DISABLE(x)                        (((unsigned)(x) & 0x1) << 4)
#define   G_028830_RECTANGLE_FILTER_DISABLE(x)                        (((x) >> 4) & 0x1)
#define   C_028830_RECTANGLE_FILTER_DISABLE                           0xFFFFFFEF
#define R_028834_PA_CL_OBJPRIM_ID_CNTL                                  0x028834
#define   S_028834_OBJ_ID_SEL(x)                                      (((unsigned)(x) & 0x1) << 0)
#define   G_028834_OBJ_ID_SEL(x)                                      (((x) >> 0) & 0x1)
#define   C_028834_OBJ_ID_SEL                                         0xFFFFFFFE
#define   S_028834_ADD_PIPED_PRIM_ID(x)                               (((unsigned)(x) & 0x1) << 1)
#define   G_028834_ADD_PIPED_PRIM_ID(x)                               (((x) >> 1) & 0x1)
#define   C_028834_ADD_PIPED_PRIM_ID                                  0xFFFFFFFD
#define   S_028834_EN_32BIT_OBJPRIMID(x)                              (((unsigned)(x) & 0x1) << 2)
#define   G_028834_EN_32BIT_OBJPRIMID(x)                              (((x) >> 2) & 0x1)
#define   C_028834_EN_32BIT_OBJPRIMID                                 0xFFFFFFFB
#define R_028838_PA_CL_NGG_CNTL                                         0x028838
#define   S_028838_VERTEX_REUSE_OFF(x)                                (((unsigned)(x) & 0x1) << 0)
#define   G_028838_VERTEX_REUSE_OFF(x)                                (((x) >> 0) & 0x1)
#define   C_028838_VERTEX_REUSE_OFF                                   0xFFFFFFFE
#define   S_028838_INDEX_BUF_EDGE_FLAG_ENA(x)                         (((unsigned)(x) & 0x1) << 1)
#define   G_028838_INDEX_BUF_EDGE_FLAG_ENA(x)                         (((x) >> 1) & 0x1)
#define   C_028838_INDEX_BUF_EDGE_FLAG_ENA                            0xFFFFFFFD
#define R_02883C_PA_SU_OVER_RASTERIZATION_CNTL                          0x02883C
#define   S_02883C_DISCARD_0_AREA_TRIANGLES(x)                        (((unsigned)(x) & 0x1) << 0)
#define   G_02883C_DISCARD_0_AREA_TRIANGLES(x)                        (((x) >> 0) & 0x1)
#define   C_02883C_DISCARD_0_AREA_TRIANGLES                           0xFFFFFFFE
#define   S_02883C_DISCARD_0_AREA_LINES(x)                            (((unsigned)(x) & 0x1) << 1)
#define   G_02883C_DISCARD_0_AREA_LINES(x)                            (((x) >> 1) & 0x1)
#define   C_02883C_DISCARD_0_AREA_LINES                               0xFFFFFFFD
#define   S_02883C_DISCARD_0_AREA_POINTS(x)                           (((unsigned)(x) & 0x1) << 2)
#define   G_02883C_DISCARD_0_AREA_POINTS(x)                           (((x) >> 2) & 0x1)
#define   C_02883C_DISCARD_0_AREA_POINTS                              0xFFFFFFFB
#define   S_02883C_DISCARD_0_AREA_RECTANGLES(x)                       (((unsigned)(x) & 0x1) << 3)
#define   G_02883C_DISCARD_0_AREA_RECTANGLES(x)                       (((x) >> 3) & 0x1)
#define   C_02883C_DISCARD_0_AREA_RECTANGLES                          0xFFFFFFF7
#define   S_02883C_USE_PROVOKING_ZW(x)                                (((unsigned)(x) & 0x1) << 4)
#define   G_02883C_USE_PROVOKING_ZW(x)                                (((x) >> 4) & 0x1)
#define   C_02883C_USE_PROVOKING_ZW                                   0xFFFFFFEF
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
#define   S_028A24_RETAIN_ORDER(x)                                    (((unsigned)(x) & 0x1) << 14)
#define   G_028A24_RETAIN_ORDER(x)                                    (((x) >> 14) & 0x1)
#define   C_028A24_RETAIN_ORDER                                       0xFFFFBFFF
#define   S_028A24_RETAIN_QUADS(x)                                    (((unsigned)(x) & 0x1) << 15)
#define   G_028A24_RETAIN_QUADS(x)                                    (((x) >> 15) & 0x1)
#define   C_028A24_RETAIN_QUADS                                       0xFFFF7FFF
#define   S_028A24_PRIM_ORDER(x)                                      (((unsigned)(x) & 0x07) << 16)
#define   G_028A24_PRIM_ORDER(x)                                      (((x) >> 16) & 0x07)
#define   C_028A24_PRIM_ORDER                                         0xFFF8FFFF
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
#define   S_028A38_X_OFFSET(x)                                        (((unsigned)(x) & 0x0F) << 4)
#define   G_028A38_X_OFFSET(x)                                        (((x) >> 4) & 0x0F)
#define   C_028A38_X_OFFSET                                           0xFFFFFF0F
#define   S_028A38_Y_CONV(x)                                          (((unsigned)(x) & 0x0F) << 8)
#define   G_028A38_Y_CONV(x)                                          (((x) >> 8) & 0x0F)
#define   C_028A38_Y_CONV                                             0xFFFFF0FF
#define   S_028A38_Y_OFFSET(x)                                        (((unsigned)(x) & 0x0F) << 12)
#define   G_028A38_Y_OFFSET(x)                                        (((x) >> 12) & 0x0F)
#define   C_028A38_Y_OFFSET                                           0xFFFF0FFF
#define   S_028A38_Z_CONV(x)                                          (((unsigned)(x) & 0x0F) << 16)
#define   G_028A38_Z_CONV(x)                                          (((x) >> 16) & 0x0F)
#define   C_028A38_Z_CONV                                             0xFFF0FFFF
#define   S_028A38_Z_OFFSET(x)                                        (((unsigned)(x) & 0x0F) << 20)
#define   G_028A38_Z_OFFSET(x)                                        (((x) >> 20) & 0x0F)
#define   C_028A38_Z_OFFSET                                           0xFF0FFFFF
#define   S_028A38_W_CONV(x)                                          (((unsigned)(x) & 0x0F) << 24)
#define   G_028A38_W_CONV(x)                                          (((x) >> 24) & 0x0F)
#define   C_028A38_W_CONV                                             0xF0FFFFFF
#define   S_028A38_W_OFFSET(x)                                        (((unsigned)(x) & 0x0F) << 28)
#define   G_028A38_W_OFFSET(x)                                        (((x) >> 28) & 0x0F)
#define   C_028A38_W_OFFSET                                           0x0FFFFFFF
#define R_028A3C_VGT_GROUP_VECT_1_FMT_CNTL                              0x028A3C
#define   S_028A3C_X_CONV(x)                                          (((unsigned)(x) & 0x0F) << 0)
#define   G_028A3C_X_CONV(x)                                          (((x) >> 0) & 0x0F)
#define   C_028A3C_X_CONV                                             0xFFFFFFF0
#define   S_028A3C_X_OFFSET(x)                                        (((unsigned)(x) & 0x0F) << 4)
#define   G_028A3C_X_OFFSET(x)                                        (((x) >> 4) & 0x0F)
#define   C_028A3C_X_OFFSET                                           0xFFFFFF0F
#define   S_028A3C_Y_CONV(x)                                          (((unsigned)(x) & 0x0F) << 8)
#define   G_028A3C_Y_CONV(x)                                          (((x) >> 8) & 0x0F)
#define   C_028A3C_Y_CONV                                             0xFFFFF0FF
#define   S_028A3C_Y_OFFSET(x)                                        (((unsigned)(x) & 0x0F) << 12)
#define   G_028A3C_Y_OFFSET(x)                                        (((x) >> 12) & 0x0F)
#define   C_028A3C_Y_OFFSET                                           0xFFFF0FFF
#define   S_028A3C_Z_CONV(x)                                          (((unsigned)(x) & 0x0F) << 16)
#define   G_028A3C_Z_CONV(x)                                          (((x) >> 16) & 0x0F)
#define   C_028A3C_Z_CONV                                             0xFFF0FFFF
#define   S_028A3C_Z_OFFSET(x)                                        (((unsigned)(x) & 0x0F) << 20)
#define   G_028A3C_Z_OFFSET(x)                                        (((x) >> 20) & 0x0F)
#define   C_028A3C_Z_OFFSET                                           0xFF0FFFFF
#define   S_028A3C_W_CONV(x)                                          (((unsigned)(x) & 0x0F) << 24)
#define   G_028A3C_W_CONV(x)                                          (((x) >> 24) & 0x0F)
#define   C_028A3C_W_CONV                                             0xF0FFFFFF
#define   S_028A3C_W_OFFSET(x)                                        (((unsigned)(x) & 0x0F) << 28)
#define   G_028A3C_W_OFFSET(x)                                        (((x) >> 28) & 0x0F)
#define   C_028A3C_W_OFFSET                                           0x0FFFFFFF
#define R_028A40_VGT_GS_MODE                                            0x028A40
#define   S_028A40_MODE(x)                                            (((unsigned)(x) & 0x07) << 0)
#define   G_028A40_MODE(x)                                            (((x) >> 0) & 0x07)
#define   C_028A40_MODE                                               0xFFFFFFF8
#define   S_028A40_RESERVED_0(x)                                      (((unsigned)(x) & 0x1) << 3)
#define   G_028A40_RESERVED_0(x)                                      (((x) >> 3) & 0x1)
#define   C_028A40_RESERVED_0                                         0xFFFFFFF7
#define   S_028A40_CUT_MODE(x)                                        (((unsigned)(x) & 0x03) << 4)
#define   G_028A40_CUT_MODE(x)                                        (((x) >> 4) & 0x03)
#define   C_028A40_CUT_MODE                                           0xFFFFFFCF
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
#define   S_028A40_RESERVED_3(x)                                      (((unsigned)(x) & 0x1) << 14)
#define   G_028A40_RESERVED_3(x)                                      (((x) >> 14) & 0x1)
#define   C_028A40_RESERVED_3                                         0xFFFFBFFF
#define   S_028A40_RESERVED_4(x)                                      (((unsigned)(x) & 0x1) << 15)
#define   G_028A40_RESERVED_4(x)                                      (((x) >> 15) & 0x1)
#define   C_028A40_RESERVED_4                                         0xFFFF7FFF
#define   S_028A40_RESERVED_5(x)                                      (((unsigned)(x) & 0x1) << 16)
#define   G_028A40_RESERVED_5(x)                                      (((x) >> 16) & 0x1)
#define   C_028A40_RESERVED_5                                         0xFFFEFFFF
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
#define   S_028A40_ONCHIP(x)                                          (((unsigned)(x) & 0x03) << 21)
#define   G_028A40_ONCHIP(x)                                          (((x) >> 21) & 0x03)
#define   C_028A40_ONCHIP                                             0xFF9FFFFF
#define R_028A44_VGT_GS_ONCHIP_CNTL                                     0x028A44
#define   S_028A44_ES_VERTS_PER_SUBGRP(x)                             (((unsigned)(x) & 0x7FF) << 0)
#define   G_028A44_ES_VERTS_PER_SUBGRP(x)                             (((x) >> 0) & 0x7FF)
#define   C_028A44_ES_VERTS_PER_SUBGRP                                0xFFFFF800
#define   S_028A44_GS_PRIMS_PER_SUBGRP(x)                             (((unsigned)(x) & 0x7FF) << 11)
#define   G_028A44_GS_PRIMS_PER_SUBGRP(x)                             (((x) >> 11) & 0x7FF)
#define   C_028A44_GS_PRIMS_PER_SUBGRP                                0xFFC007FF
#define   S_028A44_GS_INST_PRIMS_IN_SUBGRP(x)                         (((unsigned)(x) & 0x3FF) << 22)
#define   G_028A44_GS_INST_PRIMS_IN_SUBGRP(x)                         (((x) >> 22) & 0x3FF)
#define   C_028A44_GS_INST_PRIMS_IN_SUBGRP                            0x003FFFFF
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
#define   S_028A48_SCALE_LINE_WIDTH_PAD(x)                            (((unsigned)(x) & 0x1) << 4)
#define   G_028A48_SCALE_LINE_WIDTH_PAD(x)                            (((x) >> 4) & 0x1)
#define   C_028A48_SCALE_LINE_WIDTH_PAD                               0xFFFFFFEF
#define   S_028A48_ALTERNATE_RBS_PER_TILE(x)                          (((unsigned)(x) & 0x1) << 5)
#define   G_028A48_ALTERNATE_RBS_PER_TILE(x)                          (((x) >> 5) & 0x1)
#define   C_028A48_ALTERNATE_RBS_PER_TILE                             0xFFFFFFDF
#define   S_028A48_COARSE_TILE_STARTS_ON_EVEN_RB(x)                   (((unsigned)(x) & 0x1) << 6)
#define   G_028A48_COARSE_TILE_STARTS_ON_EVEN_RB(x)                   (((x) >> 6) & 0x1)
#define   C_028A48_COARSE_TILE_STARTS_ON_EVEN_RB                      0xFFFFFFBF
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
#define   S_028A7C_SWAP_MODE(x)                                       (((unsigned)(x) & 0x03) << 2)
#define   G_028A7C_SWAP_MODE(x)                                       (((x) >> 2) & 0x03)
#define   C_028A7C_SWAP_MODE                                          0xFFFFFFF3
#define   S_028A7C_BUF_TYPE(x)                                        (((unsigned)(x) & 0x03) << 4)
#define   G_028A7C_BUF_TYPE(x)                                        (((x) >> 4) & 0x03)
#define   C_028A7C_BUF_TYPE                                           0xFFFFFFCF
#define   S_028A7C_RDREQ_POLICY(x)                                    (((unsigned)(x) & 0x1) << 6)
#define   G_028A7C_RDREQ_POLICY(x)                                    (((x) >> 6) & 0x1)
#define   C_028A7C_RDREQ_POLICY                                       0xFFFFFFBF
#define   S_028A7C_PRIMGEN_EN(x)                                      (((unsigned)(x) & 0x1) << 8)
#define   G_028A7C_PRIMGEN_EN(x)                                      (((x) >> 8) & 0x1)
#define   C_028A7C_PRIMGEN_EN                                         0xFFFFFEFF
#define   S_028A7C_NOT_EOP(x)                                         (((unsigned)(x) & 0x1) << 9)
#define   G_028A7C_NOT_EOP(x)                                         (((x) >> 9) & 0x1)
#define   C_028A7C_NOT_EOP                                            0xFFFFFDFF
#define   S_028A7C_REQ_PATH(x)                                        (((unsigned)(x) & 0x1) << 10)
#define   G_028A7C_REQ_PATH(x)                                        (((x) >> 10) & 0x1)
#define   C_028A7C_REQ_PATH                                           0xFFFFFBFF
#define R_028A80_WD_ENHANCE                                             0x028A80
#define R_028A84_VGT_PRIMITIVEID_EN                                     0x028A84
#define   S_028A84_PRIMITIVEID_EN(x)                                  (((unsigned)(x) & 0x1) << 0)
#define   G_028A84_PRIMITIVEID_EN(x)                                  (((x) >> 0) & 0x1)
#define   C_028A84_PRIMITIVEID_EN                                     0xFFFFFFFE
#define   S_028A84_DISABLE_RESET_ON_EOI(x)                            (((unsigned)(x) & 0x1) << 1)
#define   G_028A84_DISABLE_RESET_ON_EOI(x)                            (((x) >> 1) & 0x1)
#define   C_028A84_DISABLE_RESET_ON_EOI                               0xFFFFFFFD
#define   S_028A84_NGG_DISABLE_PROVOK_REUSE(x)                        (((unsigned)(x) & 0x1) << 2)
#define   G_028A84_NGG_DISABLE_PROVOK_REUSE(x)                        (((x) >> 2) & 0x1)
#define   C_028A84_NGG_DISABLE_PROVOK_REUSE                           0xFFFFFFFB
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
#define     V_028A90_BREAK_BATCH                                    0x0E /* new */
#define     V_028A90_VS_PARTIAL_FLUSH                               0x0F
#define     V_028A90_PS_PARTIAL_FLUSH                               0x10
#define     V_028A90_FLUSH_HS_OUTPUT                                0x11
#define     V_028A90_FLUSH_DFSM                                     0x12 /* new */
#define     V_028A90_RESET_TO_LOWEST_VGT                            0x13 /* new */
#define     V_028A90_CACHE_FLUSH_AND_INV_TS_EVENT                   0x14
#define     V_028A90_ZPASS_DONE                                     0x15
#define     V_028A90_CACHE_FLUSH_AND_INV_EVENT                      0x16
#define     V_028A90_PERFCOUNTER_START                              0x17
#define     V_028A90_PERFCOUNTER_STOP                               0x18
#define     V_028A90_PIPELINESTAT_START                             0x19
#define     V_028A90_PIPELINESTAT_STOP                              0x1A
#define     V_028A90_PERFCOUNTER_SAMPLE                             0x1B
#define     V_028A90_SAMPLE_PIPELINESTAT                            0x1E
#define     V_028A90_SO_VGTSTREAMOUT_FLUSH                          0x1F
#define     V_028A90_SAMPLE_STREAMOUTSTATS                          0x20
#define     V_028A90_RESET_VTX_CNT                                  0x21
#define     V_028A90_BLOCK_CONTEXT_DONE                             0x22
#define     V_028A90_CS_CONTEXT_DONE                                0x23
#define     V_028A90_VGT_FLUSH                                      0x24
#define     V_028A90_TGID_ROLLOVER                                  0x25
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
#define     V_028A90_PIXEL_PIPE_STAT_CONTROL                        0x38
#define     V_028A90_PIXEL_PIPE_STAT_DUMP                           0x39
#define     V_028A90_PIXEL_PIPE_STAT_RESET                          0x3A
#define     V_028A90_ENABLE_NGG_PIPELINE                            0x3D /* new */
#define     V_028A90_ENABLE_LEGACY_PIPELINE                         0x3E /* new */
#define   S_028A90_ADDRESS_HI_GFX9(x)                                 (((unsigned)(x) & 0x1FFFF) << 10)
#define   G_028A90_ADDRESS_HI_GFX9(x)                                 (((x) >> 10) & 0x1FFFF)
#define   C_028A90_ADDRESS_HI_GFX9                                    0xF80003FF
#define   S_028A90_EXTENDED_EVENT(x)                                  (((unsigned)(x) & 0x1) << 27)
#define   G_028A90_EXTENDED_EVENT(x)                                  (((x) >> 27) & 0x1)
#define   C_028A90_EXTENDED_EVENT                                     0xF7FFFFFF
#define R_028A94_VGT_GS_MAX_PRIMS_PER_SUBGROUP                          0x028A94
#define   S_028A94_MAX_PRIMS_PER_SUBGROUP(x)                          (((unsigned)(x) & 0xFFFF) << 0)
#define   G_028A94_MAX_PRIMS_PER_SUBGROUP(x)                          (((x) >> 0) & 0xFFFF)
#define   C_028A94_MAX_PRIMS_PER_SUBGROUP                             0xFFFF0000
#define R_028A98_VGT_DRAW_PAYLOAD_CNTL                                  0x028A98
#define   S_028A98_OBJPRIM_ID_EN(x)                                   (((unsigned)(x) & 0x1) << 0)
#define   G_028A98_OBJPRIM_ID_EN(x)                                   (((x) >> 0) & 0x1)
#define   C_028A98_OBJPRIM_ID_EN                                      0xFFFFFFFE
#define   S_028A98_EN_REG_RT_INDEX(x)                                 (((unsigned)(x) & 0x1) << 1)
#define   G_028A98_EN_REG_RT_INDEX(x)                                 (((x) >> 1) & 0x1)
#define   C_028A98_EN_REG_RT_INDEX                                    0xFFFFFFFD
#define   S_028A98_EN_PIPELINE_PRIMID(x)                              (((unsigned)(x) & 0x1) << 2)
#define   G_028A98_EN_PIPELINE_PRIMID(x)                              (((x) >> 2) & 0x1)
#define   C_028A98_EN_PIPELINE_PRIMID                                 0xFFFFFFFB
#define   S_028A98_OBJECT_ID_INST_EN(x)                               (((unsigned)(x) & 0x1) << 3)
#define   G_028A98_OBJECT_ID_INST_EN(x)                               (((x) >> 3) & 0x1)
#define   C_028A98_OBJECT_ID_INST_EN                                  0xFFFFFFF7
#define R_028AA0_VGT_INSTANCE_STEP_RATE_0                               0x028AA0
#define R_028AA4_VGT_INSTANCE_STEP_RATE_1                               0x028AA4
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
#define   S_028ABC_PIPE_ALIGNED(x)                                    (((unsigned)(x) & 0x1) << 18)
#define   G_028ABC_PIPE_ALIGNED(x)                                    (((x) >> 18) & 0x1)
#define   C_028ABC_PIPE_ALIGNED                                       0xFFFBFFFF
#define   S_028ABC_RB_ALIGNED(x)                                      (((unsigned)(x) & 0x1) << 19)
#define   G_028ABC_RB_ALIGNED(x)                                      (((x) >> 19) & 0x1)
#define   C_028ABC_RB_ALIGNED                                         0xFFF7FFFF
#define R_028AC0_DB_SRESULTS_COMPARE_STATE0                             0x028AC0
#define   S_028AC0_COMPAREFUNC0(x)                                    (((unsigned)(x) & 0x07) << 0)
#define   G_028AC0_COMPAREFUNC0(x)                                    (((x) >> 0) & 0x07)
#define   C_028AC0_COMPAREFUNC0                                       0xFFFFFFF8
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
#define   S_028B50_TRAP_SPLIT(x)                                      (((unsigned)(x) & 0x07) << 29)
#define   G_028B50_TRAP_SPLIT(x)                                      (((x) >> 29) & 0x07)
#define   C_028B50_TRAP_SPLIT                                         0x1FFFFFFF
#define R_028B54_VGT_SHADER_STAGES_EN                                   0x028B54
#define   S_028B54_LS_EN(x)                                           (((unsigned)(x) & 0x03) << 0)
#define   G_028B54_LS_EN(x)                                           (((x) >> 0) & 0x03)
#define   C_028B54_LS_EN                                              0xFFFFFFFC
#define   S_028B54_HS_EN(x)                                           (((unsigned)(x) & 0x1) << 2)
#define   G_028B54_HS_EN(x)                                           (((x) >> 2) & 0x1)
#define   C_028B54_HS_EN                                              0xFFFFFFFB
#define   S_028B54_ES_EN(x)                                           (((unsigned)(x) & 0x03) << 3)
#define   G_028B54_ES_EN(x)                                           (((x) >> 3) & 0x03)
#define   C_028B54_ES_EN                                              0xFFFFFFE7
#define   S_028B54_GS_EN(x)                                           (((unsigned)(x) & 0x1) << 5)
#define   G_028B54_GS_EN(x)                                           (((x) >> 5) & 0x1)
#define   C_028B54_GS_EN                                              0xFFFFFFDF
#define   S_028B54_VS_EN(x)                                           (((unsigned)(x) & 0x03) << 6)
#define   G_028B54_VS_EN(x)                                           (((x) >> 6) & 0x03)
#define   C_028B54_VS_EN                                              0xFFFFFF3F
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
#define   S_028B54_PRIMGEN_EN(x)                                      (((unsigned)(x) & 0x1) << 13)
#define   G_028B54_PRIMGEN_EN(x)                                      (((x) >> 13) & 0x1)
#define   C_028B54_PRIMGEN_EN                                         0xFFFFDFFF
#define   S_028B54_ORDERED_ID_MODE(x)                                 (((unsigned)(x) & 0x1) << 14)
#define   G_028B54_ORDERED_ID_MODE(x)                                 (((x) >> 14) & 0x1)
#define   C_028B54_ORDERED_ID_MODE                                    0xFFFFBFFF
#define   S_028B54_MAX_PRIMGRP_IN_WAVE(x)                             (((unsigned)(x) & 0x0F) << 15)
#define   G_028B54_MAX_PRIMGRP_IN_WAVE(x)                             (((x) >> 15) & 0x0F)
#define   C_028B54_MAX_PRIMGRP_IN_WAVE                                0xFFF87FFF
#define   S_028B54_GS_FAST_LAUNCH(x)                                  (((unsigned)(x) & 0x1) << 19)
#define   G_028B54_GS_FAST_LAUNCH(x)                                  (((x) >> 19) & 0x1)
#define   C_028B54_GS_FAST_LAUNCH                                     0xFFF7FFFF
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
#define   S_028B6C_PARTITIONING(x)                                    (((unsigned)(x) & 0x07) << 2)
#define   G_028B6C_PARTITIONING(x)                                    (((x) >> 2) & 0x07)
#define   C_028B6C_PARTITIONING                                       0xFFFFFFE3
#define   S_028B6C_TOPOLOGY(x)                                        (((unsigned)(x) & 0x07) << 5)
#define   G_028B6C_TOPOLOGY(x)                                        (((x) >> 5) & 0x07)
#define   C_028B6C_TOPOLOGY                                           0xFFFFFF1F
#define   S_028B6C_RESERVED_REDUC_AXIS(x)                             (((unsigned)(x) & 0x1) << 8)
#define   G_028B6C_RESERVED_REDUC_AXIS(x)                             (((x) >> 8) & 0x1)
#define   C_028B6C_RESERVED_REDUC_AXIS                                0xFFFFFEFF
#define   S_028B6C_DEPRECATED(x)                                      (((unsigned)(x) & 0x1) << 9)
#define   G_028B6C_DEPRECATED(x)                                      (((x) >> 9) & 0x1)
#define   C_028B6C_DEPRECATED                                         0xFFFFFDFF
#define   S_028B6C_DISABLE_DONUTS(x)                                  (((unsigned)(x) & 0x1) << 14)
#define   G_028B6C_DISABLE_DONUTS(x)                                  (((x) >> 14) & 0x1)
#define   C_028B6C_DISABLE_DONUTS                                     0xFFFFBFFF
#define   S_028B6C_RDREQ_POLICY(x)                                    (((unsigned)(x) & 0x1) << 15)
#define   G_028B6C_RDREQ_POLICY(x)                                    (((x) >> 15) & 0x1)
#define   C_028B6C_RDREQ_POLICY                                       0xFFFF7FFF
#define   S_028B6C_DISTRIBUTION_MODE(x)                               (((unsigned)(x) & 0x03) << 17)
#define   G_028B6C_DISTRIBUTION_MODE(x)                               (((x) >> 17) & 0x03)
#define   C_028B6C_DISTRIBUTION_MODE                                  0xFFF9FFFF
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
#define R_028B74_VGT_DISPATCH_DRAW_INDEX                                0x028B74
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
#define   S_028B94_EN_PRIMS_NEEDED_CNT(x)                             (((unsigned)(x) & 0x1) << 7)
#define   G_028B94_EN_PRIMS_NEEDED_CNT(x)                             (((x) >> 7) & 0x1)
#define   C_028B94_EN_PRIMS_NEEDED_CNT                                0xFFFFFF7F
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
#define R_028B9C_VGT_DMA_EVENT_INITIATOR                                0x028B9C
#define   S_028B9C_EVENT_TYPE(x)                                      (((unsigned)(x) & 0x3F) << 0)
#define   G_028B9C_EVENT_TYPE(x)                                      (((x) >> 0) & 0x3F)
#define   C_028B9C_EVENT_TYPE                                         0xFFFFFFC0
#define   S_028B9C_ADDRESS_HI(x)                                      (((unsigned)(x) & 0x1FFFF) << 10)
#define   G_028B9C_ADDRESS_HI(x)                                      (((x) >> 10) & 0x1FFFF)
#define   C_028B9C_ADDRESS_HI                                         0xF80003FF
#define   S_028B9C_EXTENDED_EVENT(x)                                  (((unsigned)(x) & 0x1) << 27)
#define   G_028B9C_EXTENDED_EVENT(x)                                  (((x) >> 27) & 0x1)
#define   C_028B9C_EXTENDED_EVENT                                     0xF7FFFFFF
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
#define   S_028BE0_MSAA_NUM_SAMPLES(x)                                (((unsigned)(x) & 0x07) << 0)
#define   G_028BE0_MSAA_NUM_SAMPLES(x)                                (((x) >> 0) & 0x07)
#define   C_028BE0_MSAA_NUM_SAMPLES                                   0xFFFFFFF8
#define   S_028BE0_AA_MASK_CENTROID_DTMN(x)                           (((unsigned)(x) & 0x1) << 4)
#define   G_028BE0_AA_MASK_CENTROID_DTMN(x)                           (((x) >> 4) & 0x1)
#define   C_028BE0_AA_MASK_CENTROID_DTMN                              0xFFFFFFEF
#define   S_028BE0_MAX_SAMPLE_DIST(x)                                 (((unsigned)(x) & 0x0F) << 13)
#define   G_028BE0_MAX_SAMPLE_DIST(x)                                 (((x) >> 13) & 0x0F)
#define   C_028BE0_MAX_SAMPLE_DIST                                    0xFFFE1FFF
#define   S_028BE0_MSAA_EXPOSED_SAMPLES(x)                            (((unsigned)(x) & 0x07) << 20)
#define   G_028BE0_MSAA_EXPOSED_SAMPLES(x)                            (((x) >> 20) & 0x07)
#define   C_028BE0_MSAA_EXPOSED_SAMPLES                               0xFF8FFFFF
#define   S_028BE0_DETAIL_TO_EXPOSED_MODE(x)                          (((unsigned)(x) & 0x03) << 24)
#define   G_028BE0_DETAIL_TO_EXPOSED_MODE(x)                          (((x) >> 24) & 0x03)
#define   C_028BE0_DETAIL_TO_EXPOSED_MODE                             0xFCFFFFFF
#define   S_028BE0_COVERAGE_TO_SHADER_SELECT(x)                       (((unsigned)(x) & 0x03) << 26)
#define   G_028BE0_COVERAGE_TO_SHADER_SELECT(x)                       (((x) >> 26) & 0x03)
#define   C_028BE0_COVERAGE_TO_SHADER_SELECT                          0xF3FFFFFF
#define R_028BE4_PA_SU_VTX_CNTL                                         0x028BE4
#define   S_028BE4_PIX_CENTER(x)                                      (((unsigned)(x) & 0x1) << 0)
#define   G_028BE4_PIX_CENTER(x)                                      (((x) >> 0) & 0x1)
#define   C_028BE4_PIX_CENTER                                         0xFFFFFFFE
#define   S_028BE4_ROUND_MODE(x)                                      (((unsigned)(x) & 0x03) << 1)
#define   G_028BE4_ROUND_MODE(x)                                      (((x) >> 1) & 0x03)
#define   C_028BE4_ROUND_MODE                                         0xFFFFFFF9
#define   S_028BE4_QUANT_MODE(x)                                      (((unsigned)(x) & 0x07) << 3)
#define   G_028BE4_QUANT_MODE(x)                                      (((x) >> 3) & 0x07)
#define   C_028BE4_QUANT_MODE                                         0xFFFFFFC7
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
#define R_028C40_PA_SC_SHADER_CONTROL                                   0x028C40
#define   S_028C40_REALIGN_DQUADS_AFTER_N_WAVES(x)                    (((unsigned)(x) & 0x03) << 0)
#define   G_028C40_REALIGN_DQUADS_AFTER_N_WAVES(x)                    (((x) >> 0) & 0x03)
#define   C_028C40_REALIGN_DQUADS_AFTER_N_WAVES                       0xFFFFFFFC
#define   S_028C40_LOAD_COLLISION_WAVEID(x)                           (((unsigned)(x) & 0x1) << 2)
#define   G_028C40_LOAD_COLLISION_WAVEID(x)                           (((x) >> 2) & 0x1)
#define   C_028C40_LOAD_COLLISION_WAVEID                              0xFFFFFFFB
#define   S_028C40_LOAD_INTRAWAVE_COLLISION(x)                        (((unsigned)(x) & 0x1) << 3)
#define   G_028C40_LOAD_INTRAWAVE_COLLISION(x)                        (((x) >> 3) & 0x1)
#define   C_028C40_LOAD_INTRAWAVE_COLLISION                           0xFFFFFFF7
#define R_028C44_PA_SC_BINNER_CNTL_0                                    0x028C44
#define   S_028C44_BINNING_MODE(x)                                    (((unsigned)(x) & 0x03) << 0)
#define   G_028C44_BINNING_MODE(x)                                    (((x) >> 0) & 0x03)
#define   C_028C44_BINNING_MODE                                       0xFFFFFFFC
#define     V_028C44_BINNING_ALLOWED					0
#define     V_028C44_FORCE_BINNING_ON					1
#define     V_028C44_DISABLE_BINNING_USE_NEW_SC				2
#define     V_028C44_DISABLE_BINNING_USE_LEGACY_SC			3
#define   S_028C44_BIN_SIZE_X(x)                                      (((unsigned)(x) & 0x1) << 2)
#define   G_028C44_BIN_SIZE_X(x)                                      (((x) >> 2) & 0x1)
#define   C_028C44_BIN_SIZE_X                                         0xFFFFFFFB
#define   S_028C44_BIN_SIZE_Y(x)                                      (((unsigned)(x) & 0x1) << 3)
#define   G_028C44_BIN_SIZE_Y(x)                                      (((x) >> 3) & 0x1)
#define   C_028C44_BIN_SIZE_Y                                         0xFFFFFFF7
#define   S_028C44_BIN_SIZE_X_EXTEND(x)                               (((unsigned)(x) & 0x07) << 4)
#define   G_028C44_BIN_SIZE_X_EXTEND(x)                               (((x) >> 4) & 0x07)
#define   C_028C44_BIN_SIZE_X_EXTEND                                  0xFFFFFF8F
#define   S_028C44_BIN_SIZE_Y_EXTEND(x)                               (((unsigned)(x) & 0x07) << 7)
#define   G_028C44_BIN_SIZE_Y_EXTEND(x)                               (((x) >> 7) & 0x07)
#define   C_028C44_BIN_SIZE_Y_EXTEND                                  0xFFFFFC7F
#define   S_028C44_CONTEXT_STATES_PER_BIN(x)                          (((unsigned)(x) & 0x07) << 10)
#define   G_028C44_CONTEXT_STATES_PER_BIN(x)                          (((x) >> 10) & 0x07)
#define   C_028C44_CONTEXT_STATES_PER_BIN                             0xFFFFE3FF
#define   S_028C44_PERSISTENT_STATES_PER_BIN(x)                       (((unsigned)(x) & 0x1F) << 13)
#define   G_028C44_PERSISTENT_STATES_PER_BIN(x)                       (((x) >> 13) & 0x1F)
#define   C_028C44_PERSISTENT_STATES_PER_BIN                          0xFFFC1FFF
#define   S_028C44_DISABLE_START_OF_PRIM(x)                           (((unsigned)(x) & 0x1) << 18)
#define   G_028C44_DISABLE_START_OF_PRIM(x)                           (((x) >> 18) & 0x1)
#define   C_028C44_DISABLE_START_OF_PRIM                              0xFFFBFFFF
#define   S_028C44_FPOVS_PER_BATCH(x)                                 (((unsigned)(x) & 0xFF) << 19)
#define   G_028C44_FPOVS_PER_BATCH(x)                                 (((x) >> 19) & 0xFF)
#define   C_028C44_FPOVS_PER_BATCH                                    0xF807FFFF
#define   S_028C44_OPTIMAL_BIN_SELECTION(x)                           (((unsigned)(x) & 0x1) << 27)
#define   G_028C44_OPTIMAL_BIN_SELECTION(x)                           (((x) >> 27) & 0x1)
#define   C_028C44_OPTIMAL_BIN_SELECTION                              0xF7FFFFFF
#define R_028C48_PA_SC_BINNER_CNTL_1                                    0x028C48
#define   S_028C48_MAX_ALLOC_COUNT(x)                                 (((unsigned)(x) & 0xFFFF) << 0)
#define   G_028C48_MAX_ALLOC_COUNT(x)                                 (((x) >> 0) & 0xFFFF)
#define   C_028C48_MAX_ALLOC_COUNT                                    0xFFFF0000
#define   S_028C48_MAX_PRIM_PER_BATCH(x)                              (((unsigned)(x) & 0xFFFF) << 16)
#define   G_028C48_MAX_PRIM_PER_BATCH(x)                              (((x) >> 16) & 0xFFFF)
#define   C_028C48_MAX_PRIM_PER_BATCH                                 0x0000FFFF
#define R_028C4C_PA_SC_CONSERVATIVE_RASTERIZATION_CNTL                  0x028C4C
#define   S_028C4C_OVER_RAST_ENABLE(x)                                (((unsigned)(x) & 0x1) << 0)
#define   G_028C4C_OVER_RAST_ENABLE(x)                                (((x) >> 0) & 0x1)
#define   C_028C4C_OVER_RAST_ENABLE                                   0xFFFFFFFE
#define   S_028C4C_OVER_RAST_SAMPLE_SELECT(x)                         (((unsigned)(x) & 0x0F) << 1)
#define   G_028C4C_OVER_RAST_SAMPLE_SELECT(x)                         (((x) >> 1) & 0x0F)
#define   C_028C4C_OVER_RAST_SAMPLE_SELECT                            0xFFFFFFE1
#define   S_028C4C_UNDER_RAST_ENABLE(x)                               (((unsigned)(x) & 0x1) << 5)
#define   G_028C4C_UNDER_RAST_ENABLE(x)                               (((x) >> 5) & 0x1)
#define   C_028C4C_UNDER_RAST_ENABLE                                  0xFFFFFFDF
#define   S_028C4C_UNDER_RAST_SAMPLE_SELECT(x)                        (((unsigned)(x) & 0x0F) << 6)
#define   G_028C4C_UNDER_RAST_SAMPLE_SELECT(x)                        (((x) >> 6) & 0x0F)
#define   C_028C4C_UNDER_RAST_SAMPLE_SELECT                           0xFFFFFC3F
#define   S_028C4C_PBB_UNCERTAINTY_REGION_ENABLE(x)                   (((unsigned)(x) & 0x1) << 10)
#define   G_028C4C_PBB_UNCERTAINTY_REGION_ENABLE(x)                   (((x) >> 10) & 0x1)
#define   C_028C4C_PBB_UNCERTAINTY_REGION_ENABLE                      0xFFFFFBFF
#define   S_028C4C_ZMM_TRI_EXTENT(x)                                  (((unsigned)(x) & 0x1) << 11)
#define   G_028C4C_ZMM_TRI_EXTENT(x)                                  (((x) >> 11) & 0x1)
#define   C_028C4C_ZMM_TRI_EXTENT                                     0xFFFFF7FF
#define   S_028C4C_ZMM_TRI_OFFSET(x)                                  (((unsigned)(x) & 0x1) << 12)
#define   G_028C4C_ZMM_TRI_OFFSET(x)                                  (((x) >> 12) & 0x1)
#define   C_028C4C_ZMM_TRI_OFFSET                                     0xFFFFEFFF
#define   S_028C4C_OVERRIDE_OVER_RAST_INNER_TO_NORMAL(x)              (((unsigned)(x) & 0x1) << 13)
#define   G_028C4C_OVERRIDE_OVER_RAST_INNER_TO_NORMAL(x)              (((x) >> 13) & 0x1)
#define   C_028C4C_OVERRIDE_OVER_RAST_INNER_TO_NORMAL                 0xFFFFDFFF
#define   S_028C4C_OVERRIDE_UNDER_RAST_INNER_TO_NORMAL(x)             (((unsigned)(x) & 0x1) << 14)
#define   G_028C4C_OVERRIDE_UNDER_RAST_INNER_TO_NORMAL(x)             (((x) >> 14) & 0x1)
#define   C_028C4C_OVERRIDE_UNDER_RAST_INNER_TO_NORMAL                0xFFFFBFFF
#define   S_028C4C_DEGENERATE_OVERRIDE_INNER_TO_NORMAL_DISABLE(x)     (((unsigned)(x) & 0x1) << 15)
#define   G_028C4C_DEGENERATE_OVERRIDE_INNER_TO_NORMAL_DISABLE(x)     (((x) >> 15) & 0x1)
#define   C_028C4C_DEGENERATE_OVERRIDE_INNER_TO_NORMAL_DISABLE        0xFFFF7FFF
#define   S_028C4C_UNCERTAINTY_REGION_MODE(x)                         (((unsigned)(x) & 0x03) << 16)
#define   G_028C4C_UNCERTAINTY_REGION_MODE(x)                         (((x) >> 16) & 0x03)
#define   C_028C4C_UNCERTAINTY_REGION_MODE                            0xFFFCFFFF
#define   S_028C4C_OUTER_UNCERTAINTY_EDGERULE_OVERRIDE(x)             (((unsigned)(x) & 0x1) << 18)
#define   G_028C4C_OUTER_UNCERTAINTY_EDGERULE_OVERRIDE(x)             (((x) >> 18) & 0x1)
#define   C_028C4C_OUTER_UNCERTAINTY_EDGERULE_OVERRIDE                0xFFFBFFFF
#define   S_028C4C_INNER_UNCERTAINTY_EDGERULE_OVERRIDE(x)             (((unsigned)(x) & 0x1) << 19)
#define   G_028C4C_INNER_UNCERTAINTY_EDGERULE_OVERRIDE(x)             (((x) >> 19) & 0x1)
#define   C_028C4C_INNER_UNCERTAINTY_EDGERULE_OVERRIDE                0xFFF7FFFF
#define   S_028C4C_NULL_SQUAD_AA_MASK_ENABLE(x)                       (((unsigned)(x) & 0x1) << 20)
#define   G_028C4C_NULL_SQUAD_AA_MASK_ENABLE(x)                       (((x) >> 20) & 0x1)
#define   C_028C4C_NULL_SQUAD_AA_MASK_ENABLE                          0xFFEFFFFF
#define   S_028C4C_COVERAGE_AA_MASK_ENABLE(x)                         (((unsigned)(x) & 0x1) << 21)
#define   G_028C4C_COVERAGE_AA_MASK_ENABLE(x)                         (((x) >> 21) & 0x1)
#define   C_028C4C_COVERAGE_AA_MASK_ENABLE                            0xFFDFFFFF
#define   S_028C4C_PREZ_AA_MASK_ENABLE(x)                             (((unsigned)(x) & 0x1) << 22)
#define   G_028C4C_PREZ_AA_MASK_ENABLE(x)                             (((x) >> 22) & 0x1)
#define   C_028C4C_PREZ_AA_MASK_ENABLE                                0xFFBFFFFF
#define   S_028C4C_POSTZ_AA_MASK_ENABLE(x)                            (((unsigned)(x) & 0x1) << 23)
#define   G_028C4C_POSTZ_AA_MASK_ENABLE(x)                            (((x) >> 23) & 0x1)
#define   C_028C4C_POSTZ_AA_MASK_ENABLE                               0xFF7FFFFF
#define   S_028C4C_CENTROID_SAMPLE_OVERRIDE(x)                        (((unsigned)(x) & 0x1) << 24)
#define   G_028C4C_CENTROID_SAMPLE_OVERRIDE(x)                        (((x) >> 24) & 0x1)
#define   C_028C4C_CENTROID_SAMPLE_OVERRIDE                           0xFEFFFFFF
#define R_028C50_PA_SC_NGG_MODE_CNTL                                    0x028C50
#define   S_028C50_MAX_DEALLOCS_IN_WAVE(x)                            (((unsigned)(x) & 0x7FF) << 0)
#define   G_028C50_MAX_DEALLOCS_IN_WAVE(x)                            (((x) >> 0) & 0x7FF)
#define   C_028C50_MAX_DEALLOCS_IN_WAVE                               0xFFFFF800
#define R_028C58_VGT_VERTEX_REUSE_BLOCK_CNTL                            0x028C58
#define   S_028C58_VTX_REUSE_DEPTH(x)                                 (((unsigned)(x) & 0xFF) << 0)
#define   G_028C58_VTX_REUSE_DEPTH(x)                                 (((x) >> 0) & 0xFF)
#define   C_028C58_VTX_REUSE_DEPTH                                    0xFFFFFF00
#define R_028C5C_VGT_OUT_DEALLOC_CNTL                                   0x028C5C
#define   S_028C5C_DEALLOC_DIST(x)                                    (((unsigned)(x) & 0x7F) << 0)
#define   G_028C5C_DEALLOC_DIST(x)                                    (((x) >> 0) & 0x7F)
#define   C_028C5C_DEALLOC_DIST                                       0xFFFFFF80
#define R_028C60_CB_COLOR0_BASE                                         0x028C60
#define R_028C64_CB_COLOR0_BASE_EXT                                     0x028C64
#define   S_028C64_BASE_256B(x)                                       (((unsigned)(x) & 0xFF) << 0)
#define   G_028C64_BASE_256B(x)                                       (((x) >> 0) & 0xFF)
#define   C_028C64_BASE_256B                                          0xFFFFFF00
#define R_028C68_CB_COLOR0_ATTRIB2                                      0x028C68
#define   S_028C68_MIP0_HEIGHT(x)                                     (((unsigned)(x) & 0x3FFF) << 0)
#define   G_028C68_MIP0_HEIGHT(x)                                     (((x) >> 0) & 0x3FFF)
#define   C_028C68_MIP0_HEIGHT                                        0xFFFFC000
#define   S_028C68_MIP0_WIDTH(x)                                      (((unsigned)(x) & 0x3FFF) << 14)
#define   G_028C68_MIP0_WIDTH(x)                                      (((x) >> 14) & 0x3FFF)
#define   C_028C68_MIP0_WIDTH                                         0xF0003FFF
#define   S_028C68_MAX_MIP(x)                                         (((unsigned)(x) & 0x0F) << 28)
#define   G_028C68_MAX_MIP(x)                                         (((x) >> 28) & 0x0F)
#define   C_028C68_MAX_MIP                                            0x0FFFFFFF
#define R_028C6C_CB_COLOR0_VIEW                                         0x028C6C
#define   S_028C6C_SLICE_START(x)                                     (((unsigned)(x) & 0x7FF) << 0)
#define   G_028C6C_SLICE_START(x)                                     (((x) >> 0) & 0x7FF)
#define   C_028C6C_SLICE_START                                        0xFFFFF800
#define   S_028C6C_SLICE_MAX(x)                                       (((unsigned)(x) & 0x7FF) << 13)
#define   G_028C6C_SLICE_MAX(x)                                       (((x) >> 13) & 0x7FF)
#define   C_028C6C_SLICE_MAX                                          0xFF001FFF
#define   S_028C6C_MIP_LEVEL(x)                                       (((unsigned)(x) & 0x0F) << 24)
#define   G_028C6C_MIP_LEVEL(x)                                       (((x) >> 24) & 0x0F)
#define   C_028C6C_MIP_LEVEL                                          0xF0FFFFFF
#define R_028C70_CB_COLOR0_INFO                                         0x028C70
#define   S_028C70_ENDIAN(x)                                          (((unsigned)(x) & 0x03) << 0)
#define   G_028C70_ENDIAN(x)                                          (((x) >> 0) & 0x03)
#define   C_028C70_ENDIAN                                             0xFFFFFFFC
#define   S_028C70_FORMAT(x)                                          (((unsigned)(x) & 0x1F) << 2)
#define   G_028C70_FORMAT(x)                                          (((x) >> 2) & 0x1F)
#define   C_028C70_FORMAT                                             0xFFFFFF83
#define   S_028C70_NUMBER_TYPE(x)                                     (((unsigned)(x) & 0x07) << 8)
#define   G_028C70_NUMBER_TYPE(x)                                     (((x) >> 8) & 0x07)
#define   C_028C70_NUMBER_TYPE                                        0xFFFFF8FF
#define   S_028C70_COMP_SWAP(x)                                       (((unsigned)(x) & 0x03) << 11)
#define   G_028C70_COMP_SWAP(x)                                       (((x) >> 11) & 0x03)
#define   C_028C70_COMP_SWAP                                          0xFFFFE7FF
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
#define   S_028C70_BLEND_OPT_DONT_RD_DST(x)                           (((unsigned)(x) & 0x07) << 20)
#define   G_028C70_BLEND_OPT_DONT_RD_DST(x)                           (((x) >> 20) & 0x07)
#define   C_028C70_BLEND_OPT_DONT_RD_DST                              0xFF8FFFFF
#define   S_028C70_BLEND_OPT_DISCARD_PIXEL(x)                         (((unsigned)(x) & 0x07) << 23)
#define   G_028C70_BLEND_OPT_DISCARD_PIXEL(x)                         (((x) >> 23) & 0x07)
#define   C_028C70_BLEND_OPT_DISCARD_PIXEL                            0xFC7FFFFF
#define   S_028C70_FMASK_COMPRESSION_DISABLE(x)                       (((unsigned)(x) & 0x1) << 26)
#define   G_028C70_FMASK_COMPRESSION_DISABLE(x)                       (((x) >> 26) & 0x1)
#define   C_028C70_FMASK_COMPRESSION_DISABLE                          0xFBFFFFFF
#define   S_028C70_FMASK_COMPRESS_1FRAG_ONLY(x)                       (((unsigned)(x) & 0x1) << 27)
#define   G_028C70_FMASK_COMPRESS_1FRAG_ONLY(x)                       (((x) >> 27) & 0x1)
#define   C_028C70_FMASK_COMPRESS_1FRAG_ONLY                          0xF7FFFFFF
#define   S_028C70_DCC_ENABLE(x)                                      (((unsigned)(x) & 0x1) << 28)
#define   G_028C70_DCC_ENABLE(x)                                      (((x) >> 28) & 0x1)
#define   C_028C70_DCC_ENABLE                                         0xEFFFFFFF
#define   S_028C70_CMASK_ADDR_TYPE(x)                                 (((unsigned)(x) & 0x03) << 29)
#define   G_028C70_CMASK_ADDR_TYPE(x)                                 (((x) >> 29) & 0x03)
#define   C_028C70_CMASK_ADDR_TYPE                                    0x9FFFFFFF
#define R_028C74_CB_COLOR0_ATTRIB                                       0x028C74
#define   S_028C74_MIP0_DEPTH(x)                                      (((unsigned)(x) & 0x7FF) << 0)
#define   G_028C74_MIP0_DEPTH(x)                                      (((x) >> 0) & 0x7FF)
#define   C_028C74_MIP0_DEPTH                                         0xFFFFF800
#define   S_028C74_META_LINEAR(x)                                     (((unsigned)(x) & 0x1) << 11)
#define   G_028C74_META_LINEAR(x)                                     (((x) >> 11) & 0x1)
#define   C_028C74_META_LINEAR                                        0xFFFFF7FF
#define   S_028C74_NUM_SAMPLES(x)                                     (((unsigned)(x) & 0x07) << 12)
#define   G_028C74_NUM_SAMPLES(x)                                     (((x) >> 12) & 0x07)
#define   C_028C74_NUM_SAMPLES                                        0xFFFF8FFF
#define   S_028C74_NUM_FRAGMENTS(x)                                   (((unsigned)(x) & 0x03) << 15)
#define   G_028C74_NUM_FRAGMENTS(x)                                   (((x) >> 15) & 0x03)
#define   C_028C74_NUM_FRAGMENTS                                      0xFFFE7FFF
#define   S_028C74_FORCE_DST_ALPHA_1(x)                               (((unsigned)(x) & 0x1) << 17)
#define   G_028C74_FORCE_DST_ALPHA_1(x)                               (((x) >> 17) & 0x1)
#define   C_028C74_FORCE_DST_ALPHA_1                                  0xFFFDFFFF
#define   S_028C74_COLOR_SW_MODE(x)                                   (((unsigned)(x) & 0x1F) << 18)
#define   G_028C74_COLOR_SW_MODE(x)                                   (((x) >> 18) & 0x1F)
#define   C_028C74_COLOR_SW_MODE                                      0xFF83FFFF
#define   S_028C74_FMASK_SW_MODE(x)                                   (((unsigned)(x) & 0x1F) << 23)
#define   G_028C74_FMASK_SW_MODE(x)                                   (((x) >> 23) & 0x1F)
#define   C_028C74_FMASK_SW_MODE                                      0xF07FFFFF
#define   S_028C74_RESOURCE_TYPE(x)                                   (((unsigned)(x) & 0x03) << 28)
#define   G_028C74_RESOURCE_TYPE(x)                                   (((x) >> 28) & 0x03)
#define   C_028C74_RESOURCE_TYPE                                      0xCFFFFFFF
#define     V_028C74_1D                                             0
#define     V_028C74_2D                                             1
#define     V_028C74_3D                                             2
#define     V_028C74_RESERVED                                       3
#define   S_028C74_RB_ALIGNED(x)                                      (((unsigned)(x) & 0x1) << 30)
#define   G_028C74_RB_ALIGNED(x)                                      (((x) >> 30) & 0x1)
#define   C_028C74_RB_ALIGNED                                         0xBFFFFFFF
#define   S_028C74_PIPE_ALIGNED(x)                                    (((unsigned)(x) & 0x1) << 31)
#define   G_028C74_PIPE_ALIGNED(x)                                    (((x) >> 31) & 0x1)
#define   C_028C74_PIPE_ALIGNED                                       0x7FFFFFFF
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
#define R_028C7C_CB_COLOR0_CMASK                                        0x028C7C
#define R_028C80_CB_COLOR0_CMASK_BASE_EXT                               0x028C80
#define   S_028C80_BASE_256B(x)                                       (((unsigned)(x) & 0xFF) << 0)
#define   G_028C80_BASE_256B(x)                                       (((x) >> 0) & 0xFF)
#define   C_028C80_BASE_256B                                          0xFFFFFF00
#define R_028C84_CB_COLOR0_FMASK                                        0x028C84
#define R_028C88_CB_COLOR0_FMASK_BASE_EXT                               0x028C88
#define   S_028C88_BASE_256B(x)                                       (((unsigned)(x) & 0xFF) << 0)
#define   G_028C88_BASE_256B(x)                                       (((x) >> 0) & 0xFF)
#define   C_028C88_BASE_256B                                          0xFFFFFF00
#define R_028C8C_CB_COLOR0_CLEAR_WORD0                                  0x028C8C
#define R_028C90_CB_COLOR0_CLEAR_WORD1                                  0x028C90
#define R_028C94_CB_COLOR0_DCC_BASE                                     0x028C94
#define R_028C98_CB_COLOR0_DCC_BASE_EXT                                 0x028C98
#define   S_028C98_BASE_256B(x)                                       (((unsigned)(x) & 0xFF) << 0)
#define   G_028C98_BASE_256B(x)                                       (((x) >> 0) & 0xFF)
#define   C_028C98_BASE_256B                                          0xFFFFFF00
#define R_028C9C_CB_COLOR1_BASE                                         0x028C9C
#define R_028CA0_CB_COLOR1_BASE_EXT                                     0x028CA0
#define R_028CA4_CB_COLOR1_ATTRIB2                                      0x028CA4
#define R_028CA8_CB_COLOR1_VIEW                                         0x028CA8
#define R_028CAC_CB_COLOR1_INFO                                         0x028CAC
#define R_028CB0_CB_COLOR1_ATTRIB                                       0x028CB0
#define R_028CB4_CB_COLOR1_DCC_CONTROL                                  0x028CB4
#define R_028CB8_CB_COLOR1_CMASK                                        0x028CB8
#define R_028CBC_CB_COLOR1_CMASK_BASE_EXT                               0x028CBC
#define R_028CC0_CB_COLOR1_FMASK                                        0x028CC0
#define R_028CC4_CB_COLOR1_FMASK_BASE_EXT                               0x028CC4
#define R_028CC8_CB_COLOR1_CLEAR_WORD0                                  0x028CC8
#define R_028CCC_CB_COLOR1_CLEAR_WORD1                                  0x028CCC
#define R_028CD0_CB_COLOR1_DCC_BASE                                     0x028CD0
#define R_028CD4_CB_COLOR1_DCC_BASE_EXT                                 0x028CD4
#define R_028CD8_CB_COLOR2_BASE                                         0x028CD8
#define R_028CDC_CB_COLOR2_BASE_EXT                                     0x028CDC
#define R_028CE0_CB_COLOR2_ATTRIB2                                      0x028CE0
#define R_028CE4_CB_COLOR2_VIEW                                         0x028CE4
#define R_028CE8_CB_COLOR2_INFO                                         0x028CE8
#define R_028CEC_CB_COLOR2_ATTRIB                                       0x028CEC
#define R_028CF0_CB_COLOR2_DCC_CONTROL                                  0x028CF0
#define R_028CF4_CB_COLOR2_CMASK                                        0x028CF4
#define R_028CF8_CB_COLOR2_CMASK_BASE_EXT                               0x028CF8
#define R_028CFC_CB_COLOR2_FMASK                                        0x028CFC
#define R_028D00_CB_COLOR2_FMASK_BASE_EXT                               0x028D00
#define R_028D04_CB_COLOR2_CLEAR_WORD0                                  0x028D04
#define R_028D08_CB_COLOR2_CLEAR_WORD1                                  0x028D08
#define R_028D0C_CB_COLOR2_DCC_BASE                                     0x028D0C
#define R_028D10_CB_COLOR2_DCC_BASE_EXT                                 0x028D10
#define R_028D14_CB_COLOR3_BASE                                         0x028D14
#define R_028D18_CB_COLOR3_BASE_EXT                                     0x028D18
#define R_028D1C_CB_COLOR3_ATTRIB2                                      0x028D1C
#define R_028D20_CB_COLOR3_VIEW                                         0x028D20
#define R_028D24_CB_COLOR3_INFO                                         0x028D24
#define R_028D28_CB_COLOR3_ATTRIB                                       0x028D28
#define R_028D2C_CB_COLOR3_DCC_CONTROL                                  0x028D2C
#define R_028D30_CB_COLOR3_CMASK                                        0x028D30
#define R_028D34_CB_COLOR3_CMASK_BASE_EXT                               0x028D34
#define R_028D38_CB_COLOR3_FMASK                                        0x028D38
#define R_028D3C_CB_COLOR3_FMASK_BASE_EXT                               0x028D3C
#define R_028D40_CB_COLOR3_CLEAR_WORD0                                  0x028D40
#define R_028D44_CB_COLOR3_CLEAR_WORD1                                  0x028D44
#define R_028D48_CB_COLOR3_DCC_BASE                                     0x028D48
#define R_028D4C_CB_COLOR3_DCC_BASE_EXT                                 0x028D4C
#define R_028D50_CB_COLOR4_BASE                                         0x028D50
#define R_028D54_CB_COLOR4_BASE_EXT                                     0x028D54
#define R_028D58_CB_COLOR4_ATTRIB2                                      0x028D58
#define R_028D5C_CB_COLOR4_VIEW                                         0x028D5C
#define R_028D60_CB_COLOR4_INFO                                         0x028D60
#define R_028D64_CB_COLOR4_ATTRIB                                       0x028D64
#define R_028D68_CB_COLOR4_DCC_CONTROL                                  0x028D68
#define R_028D6C_CB_COLOR4_CMASK                                        0x028D6C
#define R_028D70_CB_COLOR4_CMASK_BASE_EXT                               0x028D70
#define R_028D74_CB_COLOR4_FMASK                                        0x028D74
#define R_028D78_CB_COLOR4_FMASK_BASE_EXT                               0x028D78
#define R_028D7C_CB_COLOR4_CLEAR_WORD0                                  0x028D7C
#define R_028D80_CB_COLOR4_CLEAR_WORD1                                  0x028D80
#define R_028D84_CB_COLOR4_DCC_BASE                                     0x028D84
#define R_028D88_CB_COLOR4_DCC_BASE_EXT                                 0x028D88
#define R_028D8C_CB_COLOR5_BASE                                         0x028D8C
#define R_028D90_CB_COLOR5_BASE_EXT                                     0x028D90
#define R_028D94_CB_COLOR5_ATTRIB2                                      0x028D94
#define R_028D98_CB_COLOR5_VIEW                                         0x028D98
#define R_028D9C_CB_COLOR5_INFO                                         0x028D9C
#define R_028DA0_CB_COLOR5_ATTRIB                                       0x028DA0
#define R_028DA4_CB_COLOR5_DCC_CONTROL                                  0x028DA4
#define R_028DA8_CB_COLOR5_CMASK                                        0x028DA8
#define R_028DAC_CB_COLOR5_CMASK_BASE_EXT                               0x028DAC
#define R_028DB0_CB_COLOR5_FMASK                                        0x028DB0
#define R_028DB4_CB_COLOR5_FMASK_BASE_EXT                               0x028DB4
#define R_028DB8_CB_COLOR5_CLEAR_WORD0                                  0x028DB8
#define R_028DBC_CB_COLOR5_CLEAR_WORD1                                  0x028DBC
#define R_028DC0_CB_COLOR5_DCC_BASE                                     0x028DC0
#define R_028DC4_CB_COLOR5_DCC_BASE_EXT                                 0x028DC4
#define R_028DC8_CB_COLOR6_BASE                                         0x028DC8
#define R_028DCC_CB_COLOR6_BASE_EXT                                     0x028DCC
#define R_028DD0_CB_COLOR6_ATTRIB2                                      0x028DD0
#define R_028DD4_CB_COLOR6_VIEW                                         0x028DD4
#define R_028DD8_CB_COLOR6_INFO                                         0x028DD8
#define R_028DDC_CB_COLOR6_ATTRIB                                       0x028DDC
#define R_028DE0_CB_COLOR6_DCC_CONTROL                                  0x028DE0
#define R_028DE4_CB_COLOR6_CMASK                                        0x028DE4
#define R_028DE8_CB_COLOR6_CMASK_BASE_EXT                               0x028DE8
#define R_028DEC_CB_COLOR6_FMASK                                        0x028DEC
#define R_028DF0_CB_COLOR6_FMASK_BASE_EXT                               0x028DF0
#define R_028DF4_CB_COLOR6_CLEAR_WORD0                                  0x028DF4
#define R_028DF8_CB_COLOR6_CLEAR_WORD1                                  0x028DF8
#define R_028DFC_CB_COLOR6_DCC_BASE                                     0x028DFC
#define R_028E00_CB_COLOR6_DCC_BASE_EXT                                 0x028E00
#define R_028E04_CB_COLOR7_BASE                                         0x028E04
#define R_028E08_CB_COLOR7_BASE_EXT                                     0x028E08
#define R_028E0C_CB_COLOR7_ATTRIB2                                      0x028E0C
#define R_028E10_CB_COLOR7_VIEW                                         0x028E10
#define R_028E14_CB_COLOR7_INFO                                         0x028E14
#define R_028E18_CB_COLOR7_ATTRIB                                       0x028E18
#define R_028E1C_CB_COLOR7_DCC_CONTROL                                  0x028E1C
#define R_028E20_CB_COLOR7_CMASK                                        0x028E20
#define R_028E24_CB_COLOR7_CMASK_BASE_EXT                               0x028E24
#define R_028E28_CB_COLOR7_FMASK                                        0x028E28
#define R_028E2C_CB_COLOR7_FMASK_BASE_EXT                               0x028E2C
#define R_028E30_CB_COLOR7_CLEAR_WORD0                                  0x028E30
#define R_028E34_CB_COLOR7_CLEAR_WORD1                                  0x028E34
#define R_028E38_CB_COLOR7_DCC_BASE                                     0x028E38
#define R_028E3C_CB_COLOR7_DCC_BASE_EXT                                 0x028E3C

#endif /* GFX9D_H */
