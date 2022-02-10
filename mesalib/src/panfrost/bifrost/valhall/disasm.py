from valhall import instructions, immediates, enums, typesize, safe_name
from mako.template import Template
from mako import exceptions

template = """
#include "disassemble.h"

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

/* Byte 7 has instruction metadata, analogous to Bifrost's clause header */
struct va_metadata {
	bool opcode_high : 1;
    unsigned immediate_mode : 2;
    unsigned action : 3;
	bool do_action : 1;
	bool unk3 : 1;
} __attribute__((packed));

static inline void
va_print_metadata(FILE *fp, uint8_t meta)
{
	struct va_metadata m;
	memcpy(&m, &meta, 1);

    fputs(valhall_immediate_mode[m.immediate_mode], fp);

	if (m.do_action) {
        fputs(valhall_action[m.action], fp);
	} else if (m.action) {
		fprintf(fp, ".wait%s%s%s",
				m.action & (1 << 0) ? "0" : "",
				m.action & (1 << 1) ? "1" : "",
				m.action & (1 << 2) ? "2" : "");
	}

	if (m.unk3)
		fprintf(fp, ".unk3");
}

static inline void
va_print_src(FILE *fp, uint8_t src, unsigned imm_mode)
{
	unsigned type = (src >> 6);
	unsigned value = (src & 0x3F);

	if (type == VA_SRC_IMM_TYPE) {
        if (value >= 32) {
            if (imm_mode == 0) {
                if (value >= 0x30)
                    fprintf(fp, "blend_descriptor_%u_%c", (value - 0x30) >> 1, value & 1 ? 'y' : 'x');
                else if (value == 0x2A)
                    fprintf(fp, "atest_datum");
                else
                    fprintf(fp, "unk:%X", value);
            } else if (imm_mode == 1) {
                if (value < 0x28)
                    fputs(valhall_thread_storage_pointers[value - 0x20] + 1, fp);
                else
                    fprintf(fp, "unk:%X", value);
            } else if (imm_mode == 3) {
                if (value < 0x40)
                    fputs(valhall_thread_identification[value - 0x20] + 1, fp);
                else
                    fprintf(fp, "unk:%X", value);
            } else {
                    fprintf(fp, "unk:%X", value);
            }
        } else {
            fprintf(fp, "0x%X", va_immediates[value]);
        }
	} else if (type == VA_SRC_UNIFORM_TYPE) {
		fprintf(fp, "u%u", value);
	} else {
		bool discard = (type & 1);
		fprintf(fp, "%sr%u", discard ? "`" : "", value);
	}
}

static inline void
va_print_float_src(FILE *fp, uint8_t src, unsigned imm_mode, bool neg, bool abs)
{
	unsigned type = (src >> 6);
	unsigned value = (src & 0x3F);

	if (type == VA_SRC_IMM_TYPE) {
        assert(value < 32 && "overflow in LUT");
        fprintf(fp, "0x%X", va_immediates[value]);
	} else {
        va_print_src(fp, src, imm_mode);
    }

	if (neg)
		fprintf(fp, ".neg");

	if (abs)
		fprintf(fp, ".abs");
}

void
va_disasm_instr(FILE *fp, uint64_t instr)
{
   unsigned primary_opc = (instr >> 48) & MASK(9);
   unsigned imm_mode = (instr >> 57) & MASK(2);
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
% if mod.name not in ["left", "staging_register_count"]:
% if mod.size == 1:
            if (instr & BIT(${mod.start})) fputs(".${mod.name}", fp);
% else:
            fputs(valhall_${safe_name(mod.name)}[(instr >> ${mod.start}) & ${hex((1 << mod.size) - 1)}], fp);
% endif
% endif
% endfor
            va_print_metadata(fp, instr >> 56);
            fputs(" ", fp);
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
    sr_count = "((instr >> 33) & MASK(3))" if sr.count == 0 else sr.count
%>
//            assert(((instr >> ${sr.start}) & 0xC0) == ${sr.encoded_flags});
            for (unsigned i = 0; i < ${sr_count}; ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "@" : ":",
                        (uint32_t) (((instr >> ${sr.start}) & 0x3F) + i));
            }
% endfor
% for i, src in enumerate(op.srcs):
% if not no_comma:
            fputs(", ", fp);
% endif
<% no_comma = False %>
% if src.absneg:
            va_print_float_src(fp, instr >> ${8 * i}, imm_mode,
                    instr & BIT(${src.offset['neg']}),
                    instr & BIT(${src.offset['abs']}));
% elif src.is_float:
            va_print_float_src(fp, instr >> ${8 * i}, imm_mode, false, false);
% else:
            va_print_src(fp, instr >> ${8 * i}, imm_mode);
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
