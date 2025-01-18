/* Copyright 2024 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */
#pragma once

#include "cdc.h"
#include "reg_helper.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VPE10_CDC_VUPDATE_OFFSET_DEFAULT (21)
#define VPE10_CDC_VUPDATE_WIDTH_DEFAULT  (60)
#define VPE10_CDC_VREADY_OFFSET_DEFAULT  (150)

/* macros for filing variable or field list
   SRI, SFRB should be defined in the resource file */
#define CDC_BE_REG_LIST_VPE10(id)                                                                  \
    SRIDFVL(VPCDC_BE0_P2B_CONFIG, CDC, id), SRIDFVL(VPCDC_BE0_GLOBAL_SYNC_CONFIG, CDC, id)

#define CDC_BE_FIELD_LIST_VPE10(post_fix)                                                          \
    SFRB(VPCDC_BE0_P2B_XBAR_SEL0, VPCDC_BE0_P2B_CONFIG, post_fix),                                 \
        SFRB(VPCDC_BE0_P2B_XBAR_SEL1, VPCDC_BE0_P2B_CONFIG, post_fix),                             \
        SFRB(VPCDC_BE0_P2B_XBAR_SEL2, VPCDC_BE0_P2B_CONFIG, post_fix),                             \
        SFRB(VPCDC_BE0_P2B_XBAR_SEL3, VPCDC_BE0_P2B_CONFIG, post_fix),                             \
        SFRB(VPCDC_BE0_P2B_FORMAT_SEL, VPCDC_BE0_P2B_CONFIG, post_fix),                            \
        SFRB(BE0_VUPDATE_OFFSET, VPCDC_BE0_GLOBAL_SYNC_CONFIG, post_fix),                          \
        SFRB(BE0_VUPDATE_WIDTH, VPCDC_BE0_GLOBAL_SYNC_CONFIG, post_fix),                           \
        SFRB(BE0_VREADY_OFFSET, VPCDC_BE0_GLOBAL_SYNC_CONFIG, post_fix)
/* define all structure register variables below */
#define CDC_BE_REG_VARIABLE_LIST_VPE10                                                             \
    reg_id_val VPCDC_BE0_P2B_CONFIG;                                                               \
    reg_id_val VPCDC_BE0_GLOBAL_SYNC_CONFIG;

#define CDC_BE_FIELD_VARIABLE_LIST_VPE10(type)                                                     \
    type VPCDC_BE0_P2B_XBAR_SEL0;                                                                  \
    type VPCDC_BE0_P2B_XBAR_SEL1;                                                                  \
    type VPCDC_BE0_P2B_XBAR_SEL2;                                                                  \
    type VPCDC_BE0_P2B_XBAR_SEL3;                                                                  \
    type VPCDC_BE0_P2B_FORMAT_SEL;                                                                 \
    type BE0_VUPDATE_OFFSET;                                                                       \
    type BE0_VUPDATE_WIDTH;                                                                        \
    type BE0_VREADY_OFFSET;

struct vpe10_cdc_be_registers {
    CDC_BE_REG_VARIABLE_LIST_VPE10
};

struct vpe10_cdc_be_shift {
    CDC_BE_FIELD_VARIABLE_LIST_VPE10(uint8_t)
};

struct vpe10_cdc_be_mask {
    CDC_BE_FIELD_VARIABLE_LIST_VPE10(uint32_t)
};

struct vpe10_cdc_be {
    struct cdc_be                    base; // base class, must be the first field
    struct vpe10_cdc_be_registers   *regs;
    const struct vpe10_cdc_be_shift *shift;
    const struct vpe10_cdc_be_mask  *mask;
};

void vpe10_construct_cdc_be(struct vpe_priv *vpe_priv, struct cdc_be *cdc_be);

bool vpe10_cdc_check_output_format(struct cdc_be *cdc_be, enum vpe_surface_pixel_format format);

void vpe10_cdc_program_global_sync(
    struct cdc_be *cdc_be, uint32_t vupdate_offset, uint32_t vupdate_width, uint32_t vready_offset);

void vpe10_cdc_program_p2b_config(struct cdc_be *cdc_be, enum vpe_surface_pixel_format format,
    enum vpe_swizzle_mode_values swizzle, const struct vpe_rect *viewport,
    const struct vpe_rect *viewport_c);

#ifdef __cplusplus
}
#endif
