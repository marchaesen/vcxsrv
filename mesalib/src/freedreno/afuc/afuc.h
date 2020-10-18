/*
 * Copyright (c) 2017 Rob Clark <robdclark@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _AFUC_H_
#define _AFUC_H_

/*
TODO kernel debugfs to inject packet into rb for easier experimentation.  It
should trigger reloading pfp/me and resetting gpu..

Actually maybe it should be flag on submit ioctl to be able to deal w/ relocs,
should be restricted to CAP_ADMIN and probably compile option too (default=n).
if flag set, copy cmdstream bo contents into RB instead of IB'ing to it from
RB.
 */

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#define PACKED __attribute__((__packed__))

/* The opcode is encoded variable length.  Opcodes less than 0x30
 * are encoded as 5 bits followed by (rep) flag.  Opcodes >= 0x30
 * (ie. top two bits are '11' are encoded as 6 bits.  See get_opc()
 */
typedef enum {
	OPC_NOP    = 0x00,

	OPC_ADD    = 0x01,  /* add immediate */
	OPC_ADDHI  = 0x02,  /* add immediate (hi 32b of 64b) */
	OPC_SUB    = 0x03,  /* subtract immediate */
	OPC_SUBHI  = 0x04,  /* subtract immediate (hi 32b of 64b) */
	OPC_AND    = 0x05,  /* AND immediate */
	OPC_OR     = 0x06,  /* OR immediate */
	OPC_XOR    = 0x07,  /* XOR immediate */
	OPC_NOT    = 0x08,  /* bitwise not of immed (src1 ignored) */
	OPC_SHL    = 0x09,  /* shift-left immediate */
	OPC_USHR   = 0x0a,  /* unsigned shift right by immediate */
	OPC_ISHR   = 0x0b,  /* signed shift right by immediate */
	OPC_ROT    = 0x0c,  /* rotate left (left shift with wrap-around) */
	OPC_MUL8   = 0x0d,  /* 8bit multiply by immediate */
	OPC_MIN    = 0x0e,
	OPC_MAX    = 0x0f,
	OPC_CMP    = 0x10,  /* compare src to immed */
	OPC_MOVI   = 0x11,  /* move immediate */

	/* Return the most-significant bit of src2, or 0 if src2 == 0 (the
	 * same as if src2 == 1). src1 is ignored. Note that this overlaps
	 * with STORE6, so it can only be used with the two-source encoding.
	 */
	OPC_MSB    = 0x14,


	OPC_ALU    = 0x13,  /* ALU instruction with two src registers */

	/* These seem something to do with setting some external state..
	 * doesn't seem to map *directly* to registers, but I guess that
	 * is where things end up.  For example, this sequence in the
	 * CP_INDIRECT_BUFFER handler:
	 *
	 *     mov $02, $data   ; low 32b of IB target address
	 *     mov $03, $data   ; high 32b of IB target
	 *     mov $04, $data   ; IB size in dwords
	 *     breq $04, 0x0, #l23 (#69, 04a2)
	 *     and $05, $18, 0x0003
	 *     shl $05, $05, 0x0002
	 *     cwrite $02, [$05 + 0x0b0], 0x8
	 *     cwrite $03, [$05 + 0x0b1], 0x8
	 *     cwrite $04, [$05 + 0x0b2], 0x8
	 *
	 * Note that CP_IB1/2_BASE_LO/HI/BUFSZ in 0x0b1f->0xb21 (IB1) and
	 * 0x0b22->0x0b24 (IB2).  Presumably $05 ends up w/ different value
	 * for RB->IB1 vs IB1->IB2.
	 */
	OPC_CWRITE5 = 0x15,
	OPC_CREAD5  = 0x16,

	/* A6xx shuffled around the cwrite/cread opcodes and added new opcodes
	 * that let you read/write directly to memory (and bypass the IOMMU?).
	 */
	OPC_STORE6  = 0x14,
	OPC_CWRITE6 = 0x15,
	OPC_LOAD6   = 0x16,
	OPC_CREAD6  = 0x17,

	OPC_BRNEI  = 0x30,  /* relative branch (if $src != immed) */
	OPC_BREQI  = 0x31,  /* relative branch (if $src == immed) */
	OPC_BRNEB  = 0x32,  /* relative branch (if bit not set) */
	OPC_BREQB  = 0x33,  /* relative branch (if bit is set) */
	OPC_RET    = 0x34,  /* return */
	OPC_CALL   = 0x35,  /* "function" call */
	OPC_WIN    = 0x36,  /* wait for input (ie. wait for WPTR to advance) */
	OPC_PREEMPTLEAVE6 = 0x38,  /* try to leave preemption */
	OPC_SETSECURE = 0x3b, /* switch secure mode on/off */
} afuc_opc;


typedef union PACKED {
	/* addi, subi, andi, ori, xori, etc: */
	struct PACKED {
		uint32_t uimm    : 16;
		uint32_t dst     : 5;
		uint32_t src     : 5;
		uint32_t hdr     : 6;
	} alui;
	struct PACKED {
		uint32_t uimm    : 16;
		uint32_t dst     : 5;
		uint32_t shift   : 5;
		uint32_t hdr     : 6;
	} movi;
	struct PACKED {
		uint32_t alu     : 5;
		uint32_t pad     : 4;
		uint32_t xmov    : 2; /* execute eXtra mov's based on $rem */
		uint32_t dst     : 5;
		uint32_t src2    : 5;
		uint32_t src1    : 5;
		uint32_t hdr     : 6;
	} alu;
	struct PACKED {
		uint32_t uimm    : 12;
		uint32_t flags   : 4;
		uint32_t src1    : 5;     /* dst (cread) or src (cwrite) register */
		uint32_t src2    : 5;     /* read or write address is src2+uimm */
		uint32_t hdr     : 6;
	} control;
	struct PACKED {
		int32_t  ioff    : 16;    /* relative offset */
		uint32_t bit_or_imm : 5;
		uint32_t src     : 5;
		uint32_t hdr     : 6;
	} br;
	struct PACKED {
		uint32_t uoff    : 26;    /* absolute (unsigned) offset */
		uint32_t hdr     : 6;
	} call;
	struct PACKED {
		uint32_t pad       : 25;
		uint32_t interrupt : 1; /* return from ctxt-switch interrupt handler */
		uint32_t hdr       : 6;
	} ret;
	struct PACKED {
		uint32_t pad     : 26;
		uint32_t hdr     : 6;
	} waitin;
	struct PACKED {
		uint32_t pad     : 26;
		uint32_t opc_r   : 6;
	};

} afuc_instr;

static inline void
afuc_get_opc(afuc_instr *ai, afuc_opc *opc, bool *rep)
{
	if (ai->opc_r < 0x30) {
		*opc = ai->opc_r >> 1;
		*rep = ai->opc_r & 0x1;
	} else {
		*opc = ai->opc_r;
		*rep = false;
	}
}

static inline void
afuc_set_opc(afuc_instr *ai, afuc_opc opc, bool rep)
{
	if (opc < 0x30) {
		ai->opc_r = opc << 1;
		ai->opc_r |= !!rep;
	} else {
		ai->opc_r = opc;
	}
}

#endif /* _AFUC_H_ */
