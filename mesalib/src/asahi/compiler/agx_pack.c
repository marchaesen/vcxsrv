/*
 * Copyright (C) 2021 Alyssa Rosenzweig <alyssa@rosenzweig.io>
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

#include "agx_compiler.h"

/* Binary patches needed for branch offsets */
struct agx_branch_fixup {
   /* Offset into the binary to patch */
   off_t offset;

   /* Value to patch with will be block->offset */
   agx_block *block;
};

/* Texturing has its own operands */
static unsigned
agx_pack_sample_coords(agx_index index, bool *flag)
{
   /* TODO: how to encode 16-bit coords? */
   assert(index.size == AGX_SIZE_32);
   assert(index.value < 0x100);

   *flag = index.discard;
   return index.value;
}

static unsigned
agx_pack_texture(agx_index index, unsigned *flag)
{
   /* TODO: indirection */
   assert(index.type == AGX_INDEX_IMMEDIATE);
   *flag = 0;
   return index.value;
}

static unsigned
agx_pack_sampler(agx_index index, bool *flag)
{
   /* TODO: indirection */
   assert(index.type == AGX_INDEX_IMMEDIATE);
   *flag = 0;
   return index.value;
}

static unsigned
agx_pack_sample_offset(agx_index index, bool *flag)
{
   /* TODO: offsets */
   assert(index.type == AGX_INDEX_NULL);
   *flag = 0;
   return 0;
}

static unsigned
agx_pack_lod(agx_index index)
{
   /* Immediate zero */
   if (index.type == AGX_INDEX_IMMEDIATE && index.value == 0)
      return 0;

   /* Otherwise must be a 16-bit float immediate */
   assert(index.type == AGX_INDEX_REGISTER);
   assert(index.size == AGX_SIZE_16);
   assert(index.value < 0x100);

   return index.value;
}

/* Load/stores have their own operands */

static unsigned
agx_pack_memory_reg(agx_index index, bool *flag)
{
   assert(index.size == AGX_SIZE_16 || index.size == AGX_SIZE_32);
   assert(index.size == AGX_SIZE_16 || (index.value & 1) == 0);
   assert(index.value < 0x100);

   *flag = (index.size == AGX_SIZE_32);
   return index.value;
}

static unsigned
agx_pack_memory_base(agx_index index, bool *flag)
{
   assert(index.size == AGX_SIZE_64);
   assert((index.value & 1) == 0);

   if (index.type == AGX_INDEX_UNIFORM) {
      assert(index.value < 0x200);
      *flag = 1;
      return index.value;
   } else {
      assert(index.value < 0x100);
      *flag = 0;
      return index.value;
   }
}

static unsigned
agx_pack_memory_index(agx_index index, bool *flag)
{
   if (index.type == AGX_INDEX_IMMEDIATE) {
      assert(index.value < 0x10000);
      *flag = 1;

      return index.value;
   } else {
      assert(index.type == AGX_INDEX_REGISTER);
      assert((index.value & 1) == 0);
      assert(index.value < 0x100);

      *flag = 0;
      return index.value;
   }
}

/* ALU goes through a common path */

static unsigned
agx_pack_alu_dst(agx_index dest)
{
   assert(dest.type == AGX_INDEX_REGISTER);
   unsigned reg = dest.value;
   enum agx_size size = dest.size;
   assert(reg < 0x100);

   /* RA invariant: alignment of half-reg */
   if (size >= AGX_SIZE_32)
      assert((reg & 1) == 0);

   return
      (dest.cache ? (1 << 0) : 0) |
      ((size >= AGX_SIZE_32) ? (1 << 1) : 0) |
      ((size == AGX_SIZE_64) ? (1 << 2) : 0) |
      ((reg << 2));
}

static unsigned
agx_pack_alu_src(agx_index src)
{
   unsigned value = src.value;
   enum agx_size size = src.size;

   if (src.type == AGX_INDEX_IMMEDIATE) {
      /* Flags 0 for an 8-bit immediate */
      assert(value < 0x100);

      return
         (value & BITFIELD_MASK(6)) |
         ((value >> 6) << 10);
   } else if (src.type == AGX_INDEX_UNIFORM) {
      assert(size == AGX_SIZE_16 || size == AGX_SIZE_32);
      assert(value < 0x200);

      return
         (value & BITFIELD_MASK(6)) |
         ((value >> 8) << 6) |
         ((size == AGX_SIZE_32) ? (1 << 7) : 0) |
         (0x1 << 8) |
         (((value >> 6) & BITFIELD_MASK(2)) << 10);
   } else {
      assert(src.type == AGX_INDEX_REGISTER);
      assert(!(src.cache && src.discard));

      unsigned hint = src.discard ? 0x3 : src.cache ? 0x2 : 0x1;
      unsigned size_flag =
         (size == AGX_SIZE_64) ? 0x3 :
         (size == AGX_SIZE_32) ? 0x2 :
         (size == AGX_SIZE_16) ? 0x0 : 0x0;

      return
         (value & BITFIELD_MASK(6)) |
         (hint << 6) |
         (size_flag << 8) |
         (((value >> 6) & BITFIELD_MASK(2)) << 10);
   }
}

static unsigned
agx_pack_cmpsel_src(agx_index src, enum agx_size dest_size)
{
   unsigned value = src.value;
   ASSERTED enum agx_size size = src.size;

   if (src.type == AGX_INDEX_IMMEDIATE) {
      /* Flags 0x4 for an 8-bit immediate */
      assert(value < 0x100);

      return
         (value & BITFIELD_MASK(6)) |
         (0x4 << 6) |
         ((value >> 6) << 10);
   } else if (src.type == AGX_INDEX_UNIFORM) {
      assert(size == AGX_SIZE_16 || size == AGX_SIZE_32);
      assert(size == dest_size);
      assert(value < 0x200);

      return
         (value & BITFIELD_MASK(6)) |
         ((value >> 8) << 6) |
         (0x3 << 7) |
         (((value >> 6) & BITFIELD_MASK(2)) << 10);
   } else {
      assert(src.type == AGX_INDEX_REGISTER);
      assert(!(src.cache && src.discard));
      assert(size == AGX_SIZE_16 || size == AGX_SIZE_32);
      assert(size == dest_size);

      unsigned hint = src.discard ? 0x3 : src.cache ? 0x2 : 0x1;

      return
         (value & BITFIELD_MASK(6)) |
         (hint << 6) |
         (((value >> 6) & BITFIELD_MASK(2)) << 10);
   }
}

static unsigned
agx_pack_float_mod(agx_index src)
{
   return (src.abs ? (1 << 0) : 0)
        | (src.neg ? (1 << 1) : 0);
}

static bool
agx_all_16(agx_instr *I)
{
   agx_foreach_dest(I, d) {
      if (!agx_is_null(I->dest[d]) && I->dest[d].size != AGX_SIZE_16)
         return false;
   }

   agx_foreach_src(I, s) {
      if (!agx_is_null(I->src[s]) && I->src[s].size != AGX_SIZE_16)
         return false;
   }

   return true;
}

/* Generic pack for ALU instructions, which are quite regular */

static void
agx_pack_alu(struct util_dynarray *emission, agx_instr *I)
{
   struct agx_opcode_info info = agx_opcodes_info[I->op];
   bool is_16 = agx_all_16(I) && info.encoding_16.exact;
   struct agx_encoding encoding = is_16 ?
                                     info.encoding_16 : info.encoding;

   assert(encoding.exact && "invalid encoding");

   uint64_t raw = encoding.exact;
   uint16_t extend = 0;

   // TODO: assert saturable
   if (I->saturate)
      raw |= (1 << 6);

   if (info.nr_dests) {
      assert(info.nr_dests == 1);
      unsigned D = agx_pack_alu_dst(I->dest[0]);
      unsigned extend_offset = (sizeof(extend)*8) - 4;

      raw |= (D & BITFIELD_MASK(8)) << 7;
      extend |= ((D >> 8) << extend_offset);
   } else if (info.immediates & AGX_IMMEDIATE_NEST) {
      raw |= (I->invert_cond << 8);
      raw |= (I->nest << 11);
      raw |= (I->icond << 13);
   }

   for (unsigned s = 0; s < info.nr_srcs; ++s) {
      bool is_cmpsel = (s >= 2) &&
         (I->op == AGX_OPCODE_ICMPSEL || I->op == AGX_OPCODE_FCMPSEL);

      unsigned src = is_cmpsel ?
         agx_pack_cmpsel_src(I->src[s], I->dest[0].size) :
         agx_pack_alu_src(I->src[s]);

      unsigned src_short = (src & BITFIELD_MASK(10));
      unsigned src_extend = (src >> 10);

      /* Size bit always zero and so omitted for 16-bit */
      if (is_16 && !is_cmpsel)
         assert((src_short & (1 << 9)) == 0);

      if (info.is_float) {
         unsigned fmod = agx_pack_float_mod(I->src[s]);
         unsigned fmod_offset = is_16 ? 9 : 10;
         src_short |= (fmod << fmod_offset);
      } else if (I->op == AGX_OPCODE_IMAD || I->op == AGX_OPCODE_IADD) {
         bool zext = I->src[s].abs;
         bool extends = I->src[s].size < AGX_SIZE_64;

         unsigned sxt = (extends && !zext) ? (1 << 10) : 0;

         assert(!I->src[s].neg || s == 1);
         src_short |= sxt;
      }

      /* Sources come at predictable offsets */
      unsigned offset = 16 + (12 * s);
      raw |= (((uint64_t) src_short) << offset);

      /* Destination and each source get extended in reverse order */
      unsigned extend_offset = (sizeof(extend)*8) - ((s + 3) * 2);
      extend |= (src_extend << extend_offset);
   }

   if ((I->op == AGX_OPCODE_IMAD || I->op == AGX_OPCODE_IADD) && I->src[1].neg)
      raw |= (1 << 27);

   if (info.immediates & AGX_IMMEDIATE_TRUTH_TABLE) {
      raw |= (I->truth_table & 0x3) << 26;
      raw |= (uint64_t) (I->truth_table >> 2)  << 38;
   } else if (info.immediates & AGX_IMMEDIATE_SHIFT) {
      raw |= (uint64_t) (I->shift & 1) << 39;
      raw |= (uint64_t) (I->shift >> 2) << 52;
   } else if (info.immediates & AGX_IMMEDIATE_BFI_MASK) {
      raw |= (uint64_t) (I->mask & 0x3) << 38;
      raw |= (uint64_t) ((I->mask >> 2) & 0x3) << 50;
      raw |= (uint64_t) ((I->mask >> 4) & 0x1) << 63;
   } else if (info.immediates & AGX_IMMEDIATE_SR) {
      raw |= (uint64_t) (I->sr & 0x3F) << 16;
      raw |= (uint64_t) (I->sr >> 6) << 26;
   } else if (info.immediates & AGX_IMMEDIATE_WRITEOUT)
      raw |= (uint64_t) (I->imm) << 8;
   else if (info.immediates & AGX_IMMEDIATE_IMM)
      raw |= (uint64_t) (I->imm) << 16;
   else if (info.immediates & AGX_IMMEDIATE_ROUND)
      raw |= (uint64_t) (I->imm) << 26;
   else if (info.immediates & (AGX_IMMEDIATE_FCOND | AGX_IMMEDIATE_ICOND))
      raw |= (uint64_t) (I->fcond) << 61;

   /* Determine length bit */
   unsigned length = encoding.length_short;
   unsigned short_mask = (1 << length) - 1;
   bool length_bit = (extend || (raw & ~short_mask));

   if (encoding.extensible && length_bit) {
      raw |= (1 << 15);
      length += (length > 8) ? 4 : 2;
   }

   /* Pack! */
   if (length <= sizeof(uint64_t)) {
      unsigned extend_offset = ((length - sizeof(extend)) * 8);

      /* XXX: This is a weird special case */
      if (I->op == AGX_OPCODE_IADD)
         extend_offset -= 16;

      raw |= (uint64_t) extend << extend_offset;
      memcpy(util_dynarray_grow_bytes(emission, 1, length), &raw, length);
   } else {
      /* So far, >8 byte ALU is only to store the extend bits */
      unsigned extend_offset = (((length - sizeof(extend)) * 8) - 64);
      unsigned hi = ((uint64_t) extend) << extend_offset;

      memcpy(util_dynarray_grow_bytes(emission, 1, 8), &raw, 8);
      memcpy(util_dynarray_grow_bytes(emission, 1, length - 8), &hi, length - 8);
   }
}

static void
agx_pack_instr(struct util_dynarray *emission, struct util_dynarray *fixups, agx_instr *I)
{
   switch (I->op) {
   case AGX_OPCODE_LD_TILE:
   case AGX_OPCODE_ST_TILE:
   {
      bool load = (I->op == AGX_OPCODE_LD_TILE);
      unsigned D = agx_pack_alu_dst(load ? I->dest[0] : I->src[0]);
      unsigned rt = 0; /* TODO */
      unsigned mask = I->mask ?: 0xF;
      assert(mask < 0x10);

      uint64_t raw =
         0x09 |
         (load ? (1 << 6) : 0) |
         ((uint64_t) (D & BITFIELD_MASK(8)) << 7) |
         ((uint64_t) (I->format) << 24) |
         ((uint64_t) (rt) << 32) |
         (load ? (1ull << 35) : 0) |
         ((uint64_t) (mask) << 36) |
         ((uint64_t) 0x0380FC << 40) |
         (((uint64_t) (D >> 8)) << 60);

      unsigned size = 8;
      memcpy(util_dynarray_grow_bytes(emission, 1, size), &raw, size);
      break;
   }

   case AGX_OPCODE_LD_VARY:
   case AGX_OPCODE_LD_VARY_FLAT:
   {
      bool flat = (I->op == AGX_OPCODE_LD_VARY_FLAT);
      unsigned D = agx_pack_alu_dst(I->dest[0]);
      unsigned channels = (I->channels & 0x3);
      assert(I->mask < 0xF); /* 0 indicates full mask */
      agx_index index_src = I->src[0];
      assert(index_src.type == AGX_INDEX_IMMEDIATE);
      assert(!(flat && I->perspective));
      unsigned index = index_src.value;

      uint64_t raw =
            0x21 | (flat ? (1 << 7) : 0) |
            (I->perspective ? (1 << 6) : 0) |
            ((D & 0xFF) << 7) |
            (1ull << 15) | /* XXX */
            (((uint64_t) index) << 16) |
            (((uint64_t) channels) << 30) |
            (!flat ? (1ull << 46) : 0) | /* XXX */
            (!flat ? (1ull << 52) : 0) | /* XXX */
            (((uint64_t) (D >> 8)) << 56);

      unsigned size = 8;
      memcpy(util_dynarray_grow_bytes(emission, 1, size), &raw, size);
      break;
   }

   case AGX_OPCODE_ST_VARY:
   {
      agx_index index_src = I->src[0];
      agx_index value = I->src[1];

      assert(index_src.type == AGX_INDEX_IMMEDIATE);
      assert(value.type == AGX_INDEX_REGISTER);
      assert(value.size == AGX_SIZE_32);

      uint64_t raw =
            0x11 |
            (I->last ? (1 << 7) : 0) |
            ((value.value & 0x3F) << 9) |
            (((uint64_t) index_src.value) << 16) |
            (0x80 << 16) | /* XXX */
            ((value.value >> 6) << 24) |
            (0x8 << 28); /* XXX */

      unsigned size = 4;
      memcpy(util_dynarray_grow_bytes(emission, 1, size), &raw, size);
      break;
   }

   case AGX_OPCODE_DEVICE_LOAD:
   {
      assert(I->mask != 0);
      assert(I->format <= 0x10);

      bool Rt, At, Ot;
      unsigned R = agx_pack_memory_reg(I->dest[0], &Rt);
      unsigned A = agx_pack_memory_base(I->src[0], &At);
      unsigned O = agx_pack_memory_index(I->src[1], &Ot);
      unsigned u1 = 1; // XXX
      unsigned u3 = 0;
      unsigned u4 = 4; // XXX
      unsigned u5 = 0;
      bool L = true; /* TODO: when would you want short? */

      uint64_t raw =
            0x05 |
            ((I->format & BITFIELD_MASK(3)) << 7) |
            ((R & BITFIELD_MASK(6)) << 10) |
            ((A & BITFIELD_MASK(4)) << 16) |
            ((O & BITFIELD_MASK(4)) << 20) |
            (Ot ? (1 << 24) : 0) |
            (I->src[1].abs ? (1 << 25) : 0) |
            (u1 << 26) |
            (At << 27) |
            (u3 << 28) |
            (I->scoreboard << 30) |
            (((uint64_t) ((O >> 4) & BITFIELD_MASK(4))) << 32) |
            (((uint64_t) ((A >> 4) & BITFIELD_MASK(4))) << 36) |
            (((uint64_t) ((R >> 6) & BITFIELD_MASK(2))) << 40) |
            (((uint64_t) I->shift) << 42) |
            (((uint64_t) u4) << 44) |
            (L ? (1ull << 47) : 0) |
            (((uint64_t) (I->format >> 3)) << 48) |
            (((uint64_t) Rt) << 49) |
            (((uint64_t) u5) << 50) |
            (((uint64_t) I->mask) << 52) |
            (((uint64_t) (O >> 8)) << 56);

      unsigned size = L ? 8 : 6;
      memcpy(util_dynarray_grow_bytes(emission, 1, size), &raw, size);
      break;
   }

   case AGX_OPCODE_TEXTURE_SAMPLE:
   {
      assert(I->mask != 0);
      assert(I->format <= 0x10);

      bool Rt, Ot, Ct, St;
      unsigned Tt;

      unsigned R = agx_pack_memory_reg(I->dest[0], &Rt);
      unsigned C = agx_pack_sample_coords(I->src[0], &Ct);
      unsigned T = agx_pack_texture(I->src[2], &Tt);
      unsigned S = agx_pack_sampler(I->src[3], &St);
      unsigned O = agx_pack_sample_offset(I->src[4], &Ot);
      unsigned D = agx_pack_lod(I->src[1]);

      unsigned U = 0; // TODO: what is sampler ureg?
      unsigned q1 = 0; // XXX
      unsigned q2 = 0; // XXX
      unsigned q3 = 12; // XXX
      unsigned kill = 0; // helper invocation kill bit
      unsigned q5 = 0; // XXX
      unsigned q6 = 0; // XXX

      uint32_t extend =
            ((U & BITFIELD_MASK(5)) << 0) |
            (kill << 5) |
            ((R >> 6) << 8) |
            ((C >> 6) << 10) |
            ((D >> 6) << 12) |
            ((T >> 6) << 14) |
            ((O & BITFIELD_MASK(6)) << 16) |
            (q6 << 22) |
            (Ot << 27) |
            ((S >> 6) << 28) |
            ((O >> 6) << 30);

      bool L = (extend != 0);
      assert(I->scoreboard == 0 && "todo");

      uint64_t raw =
            0x31 |
            (Rt ? (1 << 8) : 0) |
            ((R & BITFIELD_MASK(6)) << 9) |
            (L ? (1 << 15) : 0) |
            ((C & BITFIELD_MASK(6)) << 16) |
            (Ct ? (1 << 22) : 0) |
            (q1 << 23) |
            ((D & BITFIELD_MASK(6)) << 24) |
            (q2 << 30) |
            (((uint64_t) (T & BITFIELD_MASK(6))) << 32) |
            (((uint64_t) Tt) << 38) |
            (((uint64_t) I->dim) << 40) |
            (((uint64_t) q3) << 43) |
            (((uint64_t) I->mask) << 48) |
            (((uint64_t) I->lod_mode) << 52) |
            (((uint64_t) (S & BITFIELD_MASK(6))) << 32) |
            (((uint64_t) St) << 62) |
            (((uint64_t) q5) << 63);

      memcpy(util_dynarray_grow_bytes(emission, 1, 8), &raw, 8);
      if (L)
         memcpy(util_dynarray_grow_bytes(emission, 1, 4), &extend, 4);

      break;
   }

   case AGX_OPCODE_JMP_EXEC_ANY:
   case AGX_OPCODE_JMP_EXEC_NONE:
   {
      /* We don't implement indirect branches */
      assert(I->target != NULL);

      /* We'll fix the offset later. */
      struct agx_branch_fixup fixup = {
         .block = I->target,
         .offset = emission->size
      };

      util_dynarray_append(fixups, struct agx_branch_fixup, fixup);

      /* The rest of the instruction is fixed */
      struct agx_opcode_info info = agx_opcodes_info[I->op];
      uint64_t raw = info.encoding.exact;
      memcpy(util_dynarray_grow_bytes(emission, 1, 6), &raw, 6);
      break;
   }

   default:
      agx_pack_alu(emission, I);
      return;
   }
}

/* Relative branches may be emitted before their targets, so we patch the
 * binary to fix up the branch offsets after the main emit */

static void
agx_fixup_branch(struct util_dynarray *emission, struct agx_branch_fixup fix)
{
   /* Branch offset is 2 bytes into the jump instruction */
   uint8_t *location = ((uint8_t *) emission->data) + fix.offset + 2;

   /* Offsets are relative to the jump instruction */
   int32_t patch = (int32_t) fix.block->offset - (int32_t) fix.offset;

   /* Patch the binary */
   memcpy(location, &patch, sizeof(patch));
}

void
agx_pack_binary(agx_context *ctx, struct util_dynarray *emission)
{
   struct util_dynarray fixups;
   util_dynarray_init(&fixups, ctx);

   agx_foreach_block(ctx, block) {
      /* Relative to the start of the binary, the block begins at the current
       * number of bytes emitted */
      block->offset = emission->size;

      agx_foreach_instr_in_block(block, ins) {
         agx_pack_instr(emission, &fixups, ins);
      }
   }

   util_dynarray_foreach(&fixups, struct agx_branch_fixup, fixup)
      agx_fixup_branch(emission, *fixup);

   util_dynarray_fini(&fixups);
}
