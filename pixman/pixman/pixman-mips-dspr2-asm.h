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

#ifndef PIXMAN_MIPS_DSPR2_ASM_H
#define PIXMAN_MIPS_DSPR2_ASM_H

#define zero $0
#define AT   $1
#define v0   $2
#define v1   $3
#define a0   $4
#define a1   $5
#define a2   $6
#define a3   $7
#define t0   $8
#define t1   $9
#define t2   $10
#define t3   $11
#define t4   $12
#define t5   $13
#define t6   $14
#define t7   $15
#define s0   $16
#define s1   $17
#define s2   $18
#define s3   $19
#define s4   $20
#define s5   $21
#define s6   $22
#define s7   $23
#define t8   $24
#define t9   $25
#define k0   $26
#define k1   $27
#define gp   $28
#define sp   $29
#define fp   $30
#define s8   $30
#define ra   $31

/*
 * LEAF_MIPS32R2 - declare leaf routine for MIPS32r2
 */
#define LEAF_MIPS32R2(symbol)                           \
                .globl  symbol;                         \
                .align  2;                              \
                .type   symbol, @function;              \
                .ent    symbol, 0;                      \
symbol:         .frame  sp, 0, ra;                      \
                .set    push;                           \
                .set    arch=mips32r2;                  \
                .set    noreorder;                      \
                .set    noat;

/*
 * LEAF_MIPS32R2 - declare leaf routine for MIPS DSPr2
 */
#define LEAF_MIPS_DSPR2(symbol)                         \
LEAF_MIPS32R2(symbol)                                   \
                .set    dspr2;

/*
 * END - mark end of function
 */
#define END(function)                                   \
                .set    pop;                            \
                .end    function;                       \
                .size   function,.-function

/*
 * Conversion of single r5g6b5 pixel (in_565) to single a8r8g8b8 pixel
 * returned in (out_8888) register. Requires two temporary registers
 * (scratch1 and scratch2).
 */
.macro CONVERT_1x0565_TO_1x8888 in_565,   \
                                out_8888, \
                                scratch1, scratch2
    lui     \out_8888, 0xff00
    sll     \scratch1, \in_565,   0x3
    andi    \scratch2, \scratch1, 0xff
    ext     \scratch1, \in_565,   0x2, 0x3
    or      \scratch1, \scratch2, \scratch1
    or      \out_8888, \out_8888, \scratch1

    sll     \scratch1, \in_565,   0x5
    andi    \scratch1, \scratch1, 0xfc00
    srl     \scratch2, \in_565,   0x1
    andi    \scratch2, \scratch2, 0x300
    or      \scratch2, \scratch1, \scratch2
    or      \out_8888, \out_8888, \scratch2

    andi    \scratch1, \in_565,   0xf800
    srl     \scratch2, \scratch1, 0x5
    andi    \scratch2, \scratch2, 0xff00
    or      \scratch1, \scratch1, \scratch2
    sll     \scratch1, \scratch1, 0x8
    or      \out_8888, \out_8888, \scratch1
.endm

/*
 * Conversion of two r5g6b5 pixels (in1_565 and in2_565) to two a8r8g8b8 pixels
 * returned in (out1_8888 and out2_8888) registers. Requires four scratch
 * registers (scratch1 ... scratch4). It also requires maskG and maskB for
 * color component extractions. These masks must have following values:
 *   li       maskG, 0x07e007e0
 *   li       maskB, 0x001F001F
 */
.macro CONVERT_2x0565_TO_2x8888 in1_565, in2_565,     \
                                out1_8888, out2_8888, \
                                maskG, maskB,         \
                                scratch1, scratch2, scratch3, scratch4
    sll               \scratch1,  \in1_565,   16
    or                \scratch1,  \scratch1,  \in2_565
    lui               \out2_8888, 0xff00
    ori               \out2_8888, \out2_8888, 0xff00
    shrl.ph           \scratch2,  \scratch1,  11
    and               \scratch3,  \scratch1,  \maskG
    shra.ph           \scratch4,  \scratch2,  2
    shll.ph           \scratch2,  \scratch2,  3
    shll.ph           \scratch3,  \scratch3,  5
    or                \scratch2,  \scratch2,  \scratch4
    shrl.qb           \scratch4,  \scratch3,  6
    or                \out2_8888, \out2_8888, \scratch2
    or                \scratch3,  \scratch3,  \scratch4
    and               \scratch1,  \scratch1,  \maskB
    shll.ph           \scratch2,  \scratch1,  3
    shra.ph           \scratch4,  \scratch1,  2
    or                \scratch2,  \scratch2,  \scratch4
    or                \scratch3,  \scratch2,  \scratch3
    precrq.ph.w       \out1_8888, \out2_8888, \scratch3
    precr_sra.ph.w    \out2_8888, \scratch3,  0
.endm

/*
 * Conversion of single a8r8g8b8 pixel (in_8888) to single r5g6b5 pixel
 * returned in (out_565) register. Requires two temporary registers
 * (scratch1 and scratch2).
 */
.macro CONVERT_1x8888_TO_1x0565 in_8888, \
                                out_565, \
                                scratch1, scratch2
    ext     \out_565,  \in_8888,  0x3, 0x5
    srl     \scratch1, \in_8888,  0x5
    andi    \scratch1, \scratch1, 0x07e0
    srl     \scratch2, \in_8888,  0x8
    andi    \scratch2, \scratch2, 0xf800
    or      \out_565,  \out_565,  \scratch1
    or      \out_565,  \out_565,  \scratch2
.endm

/*
 * Conversion of two a8r8g8b8 pixels (in1_8888 and in2_8888) to two r5g6b5
 * pixels returned in (out1_565 and out2_565) registers. Requires two temporary
 * registers (scratch1 and scratch2). It also requires maskR, maskG and maskB
 * for color component extractions. These masks must have following values:
 *   li       maskR, 0xf800f800
 *   li       maskG, 0x07e007e0
 *   li       maskB, 0x001F001F
 * Value of input register in2_8888 is lost.
 */
.macro CONVERT_2x8888_TO_2x0565 in1_8888, in2_8888,  \
                                out1_565, out2_565,  \
                                maskR, maskG, maskB, \
                                scratch1, scratch2
    precrq.ph.w       \scratch1, \in2_8888, \in1_8888
    precr_sra.ph.w    \in2_8888, \in1_8888, 0
    shll.ph           \scratch1, \scratch1, 8
    srl               \in2_8888, \in2_8888, 3
    and               \scratch2, \in2_8888, \maskB
    and               \scratch1, \scratch1, \maskR
    srl               \in2_8888, \in2_8888, 2
    and               \out2_565, \in2_8888, \maskG
    or                \out2_565, \out2_565, \scratch2
    or                \out1_565, \out2_565, \scratch1
    srl               \out2_565, \out1_565, 16
.endm

#endif //PIXMAN_MIPS_DSPR2_ASM_H
