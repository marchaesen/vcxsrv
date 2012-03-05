/*
 * Copyright (c) 2012
 *      MIPS Technologies, Inc., California.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the MIPS Technologies, Inc., nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE MIPS TECHNOLOGIES, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE MIPS TECHNOLOGIES, INC. BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Author:  Nemanja Lukic (nlukic@mips.com)
 */

#include "pixman-mips-dspr2-asm.h"

LEAF_MIPS_DSPR2(pixman_fill_buff16_mips)
/*
 * a0 - *dest
 * a1 - count (bytes)
 * a2 - value to fill buffer with
 */

    beqz     a1, 3f
     andi    t1, a0, 0x0002
    beqz     t1, 0f          /* check if address is 4-byte aligned */
     nop
    sh       a2, 0(a0)
    addiu    a0, a0, 2
    addiu    a1, a1, -2
0:
    srl      t1, a1, 5       /* t1 how many multiples of 32 bytes */
    replv.ph a2, a2          /* replicate fill value (16bit) in a2 */
    beqz     t1, 2f
     nop
1:
    addiu    t1, t1, -1
    beqz     t1, 11f
     addiu   a1, a1, -32
    pref     30, 32(a0)
    sw       a2, 0(a0)
    sw       a2, 4(a0)
    sw       a2, 8(a0)
    sw       a2, 12(a0)
    sw       a2, 16(a0)
    sw       a2, 20(a0)
    sw       a2, 24(a0)
    sw       a2, 28(a0)
    b        1b
     addiu   a0, a0, 32
11:
    sw       a2, 0(a0)
    sw       a2, 4(a0)
    sw       a2, 8(a0)
    sw       a2, 12(a0)
    sw       a2, 16(a0)
    sw       a2, 20(a0)
    sw       a2, 24(a0)
    sw       a2, 28(a0)
    addiu    a0, a0, 32
2:
    blez     a1, 3f
     addiu   a1, a1, -2
    sh       a2, 0(a0)
    b        2b
     addiu   a0, a0, 2
3:
    jr       ra
     nop

END(pixman_fill_buff16_mips)

LEAF_MIPS32R2(pixman_fill_buff32_mips)
/*
 * a0 - *dest
 * a1 - count (bytes)
 * a2 - value to fill buffer with
 */

    beqz     a1, 3f
     nop
    srl      t1, a1, 5 /* t1 how many multiples of 32 bytes */
    beqz     t1, 2f
     nop
1:
    addiu    t1, t1, -1
    beqz     t1, 11f
     addiu   a1, a1, -32
    pref     30, 32(a0)
    sw       a2, 0(a0)
    sw       a2, 4(a0)
    sw       a2, 8(a0)
    sw       a2, 12(a0)
    sw       a2, 16(a0)
    sw       a2, 20(a0)
    sw       a2, 24(a0)
    sw       a2, 28(a0)
    b        1b
     addiu   a0, a0, 32
11:
    sw       a2, 0(a0)
    sw       a2, 4(a0)
    sw       a2, 8(a0)
    sw       a2, 12(a0)
    sw       a2, 16(a0)
    sw       a2, 20(a0)
    sw       a2, 24(a0)
    sw       a2, 28(a0)
    addiu    a0, a0, 32
2:
    blez     a1, 3f
     addiu   a1, a1, -4
    sw       a2, 0(a0)
    b        2b
     addiu   a0, a0, 4
3:
    jr       ra
     nop

END(pixman_fill_buff32_mips)

LEAF_MIPS_DSPR2(pixman_composite_src_8888_0565_asm_mips)
/*
 * a0 - dst (r5g6b5)
 * a1 - src (a8r8g8b8)
 * a2 - w
 */

    beqz     a2, 3f
     nop
    addiu    t1, a2, -1
    beqz     t1, 2f
     nop
    li       t4, 0xf800f800
    li       t5, 0x07e007e0
    li       t6, 0x001f001f
1:
    lw       t0, 0(a1)
    lw       t1, 4(a1)
    addiu    a1, a1, 8
    addiu    a2, a2, -2

    CONVERT_2x8888_TO_2x0565 t0, t1, t2, t3, t4, t5, t6, t7, t8

    sh       t2, 0(a0)
    sh       t3, 2(a0)

    addiu    t2, a2, -1
    bgtz     t2, 1b
     addiu   a0, a0, 4
2:
    beqz     a2, 3f
     nop
    lw       t0, 0(a1)

    CONVERT_1x8888_TO_1x0565 t0, t1, t2, t3

    sh       t1, 0(a0)
3:
    j        ra
     nop

END(pixman_composite_src_8888_0565_asm_mips)

LEAF_MIPS_DSPR2(pixman_composite_src_0565_8888_asm_mips)
/*
 * a0 - dst (a8r8g8b8)
 * a1 - src (r5g6b5)
 * a2 - w
 */

    beqz     a2, 3f
     nop
    addiu    t1, a2, -1
    beqz     t1, 2f
     nop
    li       t4, 0x07e007e0
    li       t5, 0x001F001F
1:
    lhu      t0, 0(a1)
    lhu      t1, 2(a1)
    addiu    a1, a1, 4
    addiu    a2, a2, -2

    CONVERT_2x0565_TO_2x8888 t0, t1, t2, t3, t4, t5, t6, t7, t8, t9

    sw       t2, 0(a0)
    sw       t3, 4(a0)

    addiu    t2, a2, -1
    bgtz     t2, 1b
     addiu   a0, a0, 8
2:
    beqz     a2, 3f
     nop
    lhu      t0, 0(a1)

    CONVERT_1x0565_TO_1x8888 t0, t1, t2, t3

    sw       t1, 0(a0)
3:
    j        ra
     nop

END(pixman_composite_src_0565_8888_asm_mips)

LEAF_MIPS_DSPR2(pixman_composite_src_x888_8888_asm_mips)
/*
 * a0 - dst (a8r8g8b8)
 * a1 - src (x8r8g8b8)
 * a2 - w
 */

    beqz     a2, 4f
     nop
    li       t9, 0xff000000
    srl      t8, a2, 3    /* t1 = how many multiples of 8 src pixels */
    beqz     t8, 3f       /* branch if less than 8 src pixels */
     nop
1:
    addiu    t8, t8, -1
    beqz     t8, 2f
     addiu   a2, a2, -8
    pref     0, 32(a1)
    lw       t0, 0(a1)
    lw       t1, 4(a1)
    lw       t2, 8(a1)
    lw       t3, 12(a1)
    lw       t4, 16(a1)
    lw       t5, 20(a1)
    lw       t6, 24(a1)
    lw       t7, 28(a1)
    addiu    a1, a1, 32
    or       t0, t0, t9
    or       t1, t1, t9
    or       t2, t2, t9
    or       t3, t3, t9
    or       t4, t4, t9
    or       t5, t5, t9
    or       t6, t6, t9
    or       t7, t7, t9
    pref     30, 32(a0)
    sw       t0, 0(a0)
    sw       t1, 4(a0)
    sw       t2, 8(a0)
    sw       t3, 12(a0)
    sw       t4, 16(a0)
    sw       t5, 20(a0)
    sw       t6, 24(a0)
    sw       t7, 28(a0)
    b        1b
     addiu   a0, a0, 32
2:
    lw       t0, 0(a1)
    lw       t1, 4(a1)
    lw       t2, 8(a1)
    lw       t3, 12(a1)
    lw       t4, 16(a1)
    lw       t5, 20(a1)
    lw       t6, 24(a1)
    lw       t7, 28(a1)
    addiu    a1, a1, 32
    or       t0, t0, t9
    or       t1, t1, t9
    or       t2, t2, t9
    or       t3, t3, t9
    or       t4, t4, t9
    or       t5, t5, t9
    or       t6, t6, t9
    or       t7, t7, t9
    sw       t0, 0(a0)
    sw       t1, 4(a0)
    sw       t2, 8(a0)
    sw       t3, 12(a0)
    sw       t4, 16(a0)
    sw       t5, 20(a0)
    sw       t6, 24(a0)
    sw       t7, 28(a0)
    beqz     a2, 4f
     addiu   a0, a0, 32
3:
    lw       t0, 0(a1)
    addiu    a1, a1, 4
    addiu    a2, a2, -1
    or       t1, t0, t9
    sw       t1, 0(a0)
    bnez     a2, 3b
     addiu   a0, a0, 4
4:
    jr       ra
     nop

END(pixman_composite_src_x888_8888_asm_mips)
