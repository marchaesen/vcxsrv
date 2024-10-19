#encoding=utf-8

# Copyright (C) 2021 Collabora, Ltd.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.

import sys
from valhall import valhall_parse_isa
from mako.template import Template
from mako import exceptions

(instructions, immediates, enums, typesize, safe_name) = valhall_parse_isa()

template = """
#include "disassemble.h"

#define BIT(b)         (1ull << (b))
#define MASK(count)    ((1ull << (count)) - 1)
#define SEXT(b, count) ((b ^ BIT(count - 1)) - BIT(count - 1))
#define UNUSED         __attribute__((unused))

#define VA_SRC_UNIFORM_TYPE 0x2
#define VA_SRC_IMM_TYPE     0x3

% for name, en in ENUMS.items():
UNUSED static const char *valhall_${name}[] = {
% for v in en.values:
    "${"" if v.default else "." + v.value}",
% endfor
};

% endfor
static const uint32_t va_immediates[32] = {
% for imm in IMMEDIATES:
    ${hex(imm)},
% endfor
};

static inline void
va_print_src(FILE *fp, uint8_t src, unsigned fau_page)
{
	unsigned type = (src >> 6);
	unsigned value = (src & 0x3F);

	if (type == VA_SRC_IMM_TYPE) {
        if (value >= 32) {
            if (fau_page == 0)
                fputs(valhall_fau_special_page_0[(value - 0x20) >> 1] + 1, fp);
            else if (fau_page == 1)
                fputs(valhall_fau_special_page_1[(value - 0x20) >> 1] + 1, fp);
            else if (fau_page == 3)
                fputs(valhall_fau_special_page_3[(value - 0x20) >> 1] + 1, fp);
            else
                fprintf(fp, "reserved_page2");

            fprintf(fp, ".w%u", value & 1);
        } else {
            fprintf(fp, "0x%X", va_immediates[value]);
        }
	} else if (type == VA_SRC_UNIFORM_TYPE) {
		fprintf(fp, "u%u", value | (fau_page << 6));
	} else {
		bool discard = (type & 1);
		fprintf(fp, "%sr%u", discard ? "^" : "", value);
	}
}

static inline void
va_print_float_src(FILE *fp, uint8_t src, unsigned fau_page, bool neg, bool abs)
{
	unsigned type = (src >> 6);
	unsigned value = (src & 0x3F);

	if (type == VA_SRC_IMM_TYPE) {
        assert(value < 32 && "overflow in LUT");
        fprintf(fp, "0x%X", va_immediates[value]);
	} else {
        va_print_src(fp, src, fau_page);
    }

	if (neg)
		fprintf(fp, ".neg");

	if (abs)
		fprintf(fp, ".abs");
}

static inline void
va_print_dest(FILE *fp, uint8_t dest, bool can_mask)
{
   unsigned mask = (dest >> 6);
   unsigned value = (dest & 0x3F);
   fprintf(fp, "r%u", value);

   /* Should write at least one component */
   //	assert(mask != 0);
   //	assert(mask == 0x3 || can_mask);

   if (mask != 0x3)
      fprintf(fp, ".h%u", (mask == 1) ? 0 : 1);
}

void
va_disasm_instr(FILE *fp, uint64_t instr)
{
   unsigned primary_opc = (instr >> 48) & MASK(9);
   unsigned fau_page = (instr >> 57) & MASK(2);
   unsigned secondary_opc = 0;

   switch (primary_opc) {
% for bucket in OPCODES:
    <%
        ops = OPCODES[bucket]
        ambiguous = (len(ops) > 1)
    %>
% if len(ops) > 0:
   case ${hex(bucket)}:
% if ambiguous:
	secondary_opc = (instr >> ${ops[0].secondary_shift}) & ${hex(ops[0].secondary_mask)};
% endif
% for op in ops:
<% no_comma = True %>
% if ambiguous:

        if (secondary_opc == ${op.opcode2}) { 
% endif
            fputs("${op.name}", fp);
% for mod in op.modifiers:
% if mod.name not in ["left", "memory_width", "descriptor_type", "staging_register_count", "staging_register_write_count"]:
% if mod.is_enum:
            fputs(valhall_${safe_name(mod.enum)}[(instr >> ${mod.start}) & ${hex((1 << mod.size) - 1)}], fp);
% else:
            if (instr & BIT(${mod.start})) fputs(".${mod.name}", fp);
% endif
% endif
% endfor
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);
% if len(op.dests) > 0:
<% no_comma = False %>
            va_print_dest(fp, (instr >> 40), true);
% endif
% for index, sr in enumerate(op.staging):
% if not no_comma:
            fputs(", ", fp);
% endif
<%
    no_comma = False

    if sr.count != 0:
        sr_count = sr.count
    elif "staging_register_write_count" in [x.name for x in op.modifiers] and sr.write:
        sr_count = "(((instr >> 36) & MASK(3)) + 1)"
    elif "staging_register_count" in [x.name for x in op.modifiers]:
        sr_count = "((instr >> 33) & MASK(3))"
    else:
        assert(0)
%>
//            assert(((instr >> ${sr.start}) & 0xC0) == ${sr.encoded_flags});
            fprintf(fp, "@");
            for (unsigned i = 0; i < ${sr_count}; ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> ${sr.start}) & 0x3F) + i));
            }
% endfor
% for i, src in enumerate(op.srcs):
% if not no_comma:
            fputs(", ", fp);
% endif
<% no_comma = False %>
% if src.absneg:
            va_print_float_src(fp, instr >> ${src.start}, fau_page,
                    instr & BIT(${src.offset['neg']}),
                    instr & BIT(${src.offset['abs']}));
% elif src.is_float:
            va_print_float_src(fp, instr >> ${src.start}, fau_page, false, false);
% else:
            va_print_src(fp, instr >> ${src.start}, fau_page);
% endif
% if src.swizzle:
% if src.size == 32:
            fputs(valhall_widen[(instr >> ${src.offset['swizzle']}) & 3], fp);
% else:
            fputs(valhall_swizzles_16_bit[(instr >> ${src.offset['swizzle']}) & 3], fp);
% endif
% endif
% if src.lanes:
            fputs(valhall_lanes_8_bit[(instr >> ${src.offset['widen']}) & 0xF], fp);
% elif src.halfswizzle:
            fputs(valhall_half_swizzles_8_bit[(instr >> ${src.offset['widen']}) & 0xF], fp);
% elif src.widen:
		    fputs(valhall_swizzles_${src.size}_bit[(instr >> ${src.offset['widen']}) & 0xF], fp);
% elif src.combine:
            fputs(valhall_combine[(instr >> ${src.offset['combine']}) & 0x7], fp);
% endif
% if src.lane:
            fputs(valhall_lane_${src.size}_bit[(instr >> ${src.lane}) & 0x3], fp);
% endif
% if 'not' in src.offset:
            if (instr & BIT(${src.offset['not']})) fputs(".not", fp);
% endif
% endfor
% for imm in op.immediates:
<%
    prefix = "#" if imm.name == "constant" else imm.name + ":"
    fmt = "%d" if imm.signed else "0x%X"
%>
            fprintf(fp, ", ${prefix}${fmt}", (uint32_t) ${"SEXT(" if imm.signed else ""}
                    ((instr >> ${imm.start}) & MASK(${imm.size})) ${f", {imm.size})" if imm.signed else ""});
% endfor
% if ambiguous:
        }
% endif
% endfor
     break;

% endif
% endfor
   }
}

void
disassemble_valhall(FILE *fp, const void *code, size_t size, bool verbose)
{
   assert((size & 7) == 0);

   const uint64_t *words = (const uint64_t *)code;

   /* Segment into 8-byte instructions */
   for (unsigned i = 0; i < (size / 8); ++i) {
      uint64_t instr = words[i];

      if (instr == 0) {
         fprintf(fp, "\\n");
         return;
      }

      if (verbose) {
         /* Print byte pattern */
         for (unsigned j = 0; j < 8; ++j)
            fprintf(fp, "%02x ", (uint8_t)(instr >> (j * 8)));

         fprintf(fp, "   ");
      } else {
         /* Print whitespace */
         fprintf(fp, "   ");
      }

      va_disasm_instr(fp, instr);
      fprintf(fp, "\\n");

      /* Detect branches */
      uint64_t opcode = (instr >> 48) & MASK(9);
      bool branchz = (opcode == 0x1F);
      bool branchzi = (opcode == 0x2F);

      /* Separate blocks visually by inserting whitespace after branches */
      if (branchz || branchzi)
         fprintf(fp, "\\n");
   }

   fprintf(fp, "\\n");
}
"""

# Bucket by opcode for hierarchical disassembly
OPCODE_BUCKETS = {}
for ins in instructions:
    opc = ins.opcode
    OPCODE_BUCKETS[opc] = OPCODE_BUCKETS.get(opc, []) + [ins]

# Check that each bucket may be disambiguated
for op in OPCODE_BUCKETS:
    bucket = OPCODE_BUCKETS[op]

    # Nothing to disambiguate
    if len(bucket) < 2:
        continue

    SECONDARY = {}
    for ins in bucket:
        # Number of sources determines opcode2 placement, must be consistent
        assert(len(ins.srcs) == len(bucket[0].srcs))

        # Must not repeat, else we're ambiguous
        assert(ins.opcode2 not in SECONDARY)
        SECONDARY[ins.opcode2] = ins

try:
    print(Template(template).render(OPCODES = OPCODE_BUCKETS, IMMEDIATES = immediates, ENUMS = enums, typesize = typesize, safe_name = safe_name))
except:
    print(exceptions.text_error_template().render())
