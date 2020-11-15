#
# Copyright (C) 2020 Collabora, Ltd.
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
from isa_parse import parse_instructions, opname_to_c
from mako.template import Template

instructions = parse_instructions(sys.argv[1])

# Packs sources into an argument. Offset argument to work around a quirk of our
# compiler IR when dealing with staging registers (TODO: reorder in the IR to
# fix this)
def pack_sources(sources, body, pack_exprs, offset):
    for i, src in enumerate(sources):
        body.append('unsigned src{} = bi_get_src(ins, regs, {});'.format(i, i + offset))

        # Validate the source
        if src[1] != 0xFF:
            body.append('assert((1 << src{}) & {});'.format(i, hex(src[1])))

        # Sources are state-invariant
        for state in pack_exprs:
            state.append('(src{} << {})'.format(i, src[0]))

    body.append('')

# Gets the argument that the source modifier applies to from the name if
# applicable, otherwise defaults to the first argument

def mod_arg(mod):
    return int(mod[-1]) if mod[-1] in "0123" else 0

# Widen/lane/swz/swap/replicate modifiers conceptually act as a combined extend
# + swizzle.  We look at the size of the argument to determine if we apply
# them, and look at the swizzle to pick which one.

def pack_widen(mod, opts, body, pack_exprs):
    marg = mod_arg(mod)

    body.append('unsigned {}_sz = nir_alu_type_get_type_size(ins->src_types[{}]);'.format(mod, mod_arg(mod)))
    body.append('unsigned {}_temp = 0;'.format(mod))

    first = True
    for i, op in enumerate(opts):
        if op is None or op == 'reserved':
            continue

        t_else = 'else ' if not first else ''
        first = False

        if op in ['none', 'w0']:
            body.append('{}if ({}_sz == 32) {}_temp = {};'.format(t_else, mod, mod, i))
        elif op == 'd0':
            body.append('{}if ({}_sz == 64) {}_temp = {};'.format(t_else, mod, mod, i))
        else:
            assert(op[0] in ['h', 'b'])
            sz = 16 if op[0] == 'h' else 8

            # Condition on the swizzle
            conds = ['(ins->swizzle[{}][{}] % 4) == {}'.format(marg, idx, lane) for idx, lane in enumerate(op[1:])]
            cond = " && ".join(conds)

            body.append('{}if ({}_sz == {} && {}) {}_temp = {};'.format(t_else, mod, sz, cond, mod, i))
    body.append('else unreachable("Could not pattern match widen");')

    return mod + '_temp'

# abs/neg are stored in ins->src_{abs,neg}[src] arrays
def pack_absneg(mod, opts, body, pack_exprs):
    return 'ins->src_{}[{}]'.format(mod[0:-1] if mod[-1] in "0123" else mod, mod_arg(mod))

# ins->roundmode is the native format (RTE/RTP/RTN/RTZ) for most ops. But there
# are some others we might encounter that we don't support in the IR at this
# point, and there are a few that force a subset of round modes.

def pack_round(mod, opts, body, pack_exprs):
    if opts == ['none', 'rtz']:
        body.append('assert(ins->roundmode == BIFROST_RTE || ins->roundmode == BIFROST_RTZ);')
        return '(ins->roundmode == BIFROST_RTZ) ? 1 : 0'
    elif opts == ['rtn', 'rtp']:
        body.append('assert(ins->roundmode == BIFROST_RTN || ins->roundmode == BIFROST_RTP);')
        return '(ins->roundmode == BIFROST_RTP) ? 1 : 0'
    elif opts[0:4] == ['none', 'rtp', 'rtn', 'rtz']:
        return 'ins->roundmode'
    else:
        assert False

# Likewise, matches our native format

def pack_clamp(mod, opts, body, pack_exprs):
    if opts == ['none', 'clamp_0_inf', 'clamp_m1_1', 'clamp_0_1']:
        return 'ins->outmod'
    elif opts == ['none', 'clamp_0_1']:
        body.append('assert(ins->outmod == BIFROST_NONE || ins->outmod == BIFROST_SAT);')
        return '(ins->outmod == BIFROST_SAT) ? 1 : 0'
    else:
        assert False

# Our modifiers match up in name, but there is no shortage of orders. So just
# emit a table on the fly for it, since you won't get something much better.
# ENUM_BI_COND must be kept synced with `enum bi_cond` in compiler.h

ENUM_BI_COND = [
        "al",
        "lt",
        "le",
        "ge",
        "gt",
        "eq",
        "ne",
]

def pack_cmpf(mod, opts, body, pack_exprs):
    # Generate a table mapping ENUM_BI_COND to opts, or an invalid
    # sentintel if not used (which will then be asserted out in a debug build).
    table = [str(opts.index(x)) if x in opts else '~0' for x in ENUM_BI_COND]

    body.append('unsigned cmpf_table[] = {')
    body.append('    ' + ', '.join(table))
    body.append('};')

    return 'cmpf_table[ins->cond]'

# Since our IR is explicitly typed, we look at the size/sign to determine sign
# extension behaviour
def pack_extend(mod, opts, body, pack_exprs):
    body.append('ASSERTED bool {}_small = nir_alu_type_get_type_size(ins->src_types[{}]) <= 16;'.format(mod, mod_arg(mod)))
    body.append('bool {}_signed = nir_alu_type_get_base_type(ins->src_types[{}]) == nir_type_int;'.format(mod, mod_arg(mod)))
    
    if opts == ['none', 'sext', 'zext', 'reserved']:
        return '{}_small ? ({}_signed ? 1 : 2) : 0'.format(mod, mod)
    else:
        assert opts == ['zext', 'sext']
        body.append('assert({}_small);'.format(mod))
        return '{}_signed ? 1 : 0'.format(mod)

# Packs special varying loads. Our BIFROST_FRAGZ etc defines match the hw in
# the bottom two bits (TODO drop upper bits)
def pack_varying_name(mod, opts, body, pack_exprs):
    assert(opts[0] == 'point' and opts[2] == 'frag_w' and opts[3] == 'frag_z')
    return 'ins->constant.u64 & 0x3'

def pack_not_src1(mod, opts, body, pack_exprs):
    return 'ins->bitwise.src1_invert ? {} : {}'.format(opts.index('not'), opts.index('none'))

def pack_not_result(mod, opts, body, pack_exprs):
    return 'ins->bitwise.dest_invert ? {} : {}'.format(opts.index('not'), opts.index('none'))

REGISTER_FORMATS = {
    'f64': 'nir_type_float64',
    'f32': 'nir_type_float32',
    'f16': 'nir_type_float16',
    'u64': 'nir_type_uint64',
    'u32': 'nir_type_uint32',
    'u16': 'nir_type_uint16',
    'i64': 'nir_type_int64',
    's32': 'nir_type_int32',
    's16': 'nir_type_int16'
}

def pack_register_format(mod, opts, body, pack_exprs):
    body.append('unsigned {}_temp = 0;'.format(mod))

    first = True
    for i, op in enumerate(opts):
        if op is None or op == 'reserved':
            continue

        t_else = 'else ' if not first else ''
        first = False
        nir_type = REGISTER_FORMATS.get(op)

        if nir_type:
            body.append('{}if (ins->format == {}) {}_temp = {};'.format(t_else, nir_type, mod, i))

    assert not first
    body.append('else unreachable("Could not pattern match register format");')
    return mod + '_temp'

def pack_seg(mod, opts, body, pack_exprs):
    if len(opts) == 8:
        body.append('assert(ins->segment);')
        return 'ins->segment'
    elif opts == ['none', 'wgl']:
        body.append('assert(ins->segment == BI_SEGMENT_NONE || ins->segment == BI_SEGMENT_WLS);')
        return 'ins->segment == BI_SEGMENT_WLS ? 1 : 0'
    else:
        assert(False)

# TODO: Update modes (perf / slow) For now just force store, except for special
# varyings for which we force clobber
def pack_update(mod, opts, body, pack_exprs):
    if opts == ['store', 'retrieve', 'conditional', 'clobber']:
        return '(ins->constant.u64 >= 20) ? 3 : 0'
    else:
        assert(opts[0] == 'store')
        return '0'

# Processes modifiers. If used directly, emits a pack. Otherwise, just
# processes the value (grabbing it from the IR). This must sync with the IR.

modifier_map = {
        "widen": pack_widen,
        "widen0": pack_widen,
        "widen1": pack_widen,
        "lane": pack_widen,
        "lane0": pack_widen,
        "lane1": pack_widen,
        "lane2": pack_widen,
        "lane3": pack_widen,
        "lanes0": pack_widen,
        "lanes1": pack_widen,
        "lanes2": pack_widen,
        "swz": pack_widen,
        "swz0": pack_widen,
        "swz1": pack_widen,
        "swz2": pack_widen,
        "swap0": pack_widen,
        "swap1": pack_widen,
        "swap2": pack_widen,
        "replicate0": pack_widen,
        "replicate1": pack_widen,

        "abs": pack_absneg,
        "abs0": pack_absneg,
        "abs1": pack_absneg,
        "abs2": pack_absneg,
        "neg": pack_absneg,
        "neg0": pack_absneg,
        "neg1": pack_absneg,
        "neg2": pack_absneg,

        "extend": pack_extend,
        "extend0": pack_extend,
        "extend1": pack_extend,
        "extend2": pack_extend,
        "sign0": pack_extend,
        "sign1": pack_extend,

        "clamp": pack_clamp,
        "round": pack_round,
        "cmpf": pack_cmpf,
        "varying_name": pack_varying_name,
        "not1": pack_not_src1,
        "not_result": pack_not_result,
        "register_format": pack_register_format,
        "seg": pack_seg,
        "update": pack_update,

        # Just a minus one modifier
        "vecsize": lambda a,b,c,d: 'ins->vector_channels - 1',

        # 0: compute 1: zero
        "lod_mode": lambda a,b,c,d: '1 - ins->texture.compute_lod',
        "skip": lambda a,b,c,d: 'ins->skip',

        # Not much choice in the matter...
        "divzero": lambda a,b,c,d: '0',
        "sem": lambda a,b,c,d: '0', # IEEE 754 compliant NaN rules

        # For +ZS_EMIT, infer modifiers from specified sources
        "z": lambda a,b,c,d: '(ins->src[0] != 0)',
        "stencil": lambda a,b,c,d: '(ins->src[1] != 0)',

        # For +LD_VAR, infer sample from load_vary.interp_mode
        "sample": lambda a,b,c,d: 'ins->load_vary.interp_mode',

        # +CLPER
        "lane_op": lambda a,b,c,d: 'ins->special.clper.lane_op_mod',
        "inactive_result": lambda a,b,c,d: 'ins->special.clper.inactive_res',

        # +CLPER and +WMASK
        "subgroup": lambda a,b,c,d: 'ins->special.subgroup_sz',

        # We don't support these in the IR yet (TODO)
        "saturate": lambda a,b,c,d: '0', # clamp to min/max int
        "mask": lambda a,b,c,d: '0', # clz(~0) = ~0
        "result_type": lambda a,opts,c,d: str(opts.index('m1')), # #1, #1.0, ~0 for cmp
        "special": lambda a,b,c,d: '0', # none, which source wins..
        "offset": lambda a,b,c,d: '0', # sin/cos thing
        "adj": lambda a,b,c,d: '0', # sin/cos thing
        "sqrt": lambda a,b,c,d: '0', # sin/cos thing
        "log": lambda a,b,c,d: '1', # frexpe mode -- TODO: other transcendentals for g71
        "scale": lambda a,b,c,d: '0', # sin/cos thing
        "precision": lambda a,b,c,d: '0', # log thing
        "mode": lambda a,b,c,d: '0', # log thing
        "func": lambda a,b,c,d: '0', # pow special case thing
        "h": lambda a,b,c,d: '0', # VN_ASST1.f16
        "l": lambda a,b,c,d: '0', # VN_ASST1.f16
        "function": lambda a,b,c,d: '3', # LD_VAR_FLAT none
        "preserve_null": lambda a,b,c,d: '0', # SEG_ADD none
        "bytes2": lambda a,b,c,d: '0', # NIR shifts are in bits
        "result_word": lambda a,b,c,d: '0', # 32-bit only shifts for now (TODO)
        "source": lambda a,b,c,d: '7', # cycle_counter for LD_GCLK
        "threads": lambda a,b,c,d: '0', # IMULD odd
        "combine": lambda a,b,c,d: '0', # BRANCHC any
        "format": lambda a,b,c,d: '1', # LEA_TEX_IMM u32
        "test_mode": lambda a,b,c,d: '0', # JUMP_EX z
        "stack_mode": lambda a,b,c,d: '2', # JUMP_EX none
        "atom_opc": lambda a,b,c,d: '2', # ATOM_C aadd
        "mux": lambda a,b,c,d: '1', # MUX int_zero
}

def pack_modifier(mod, width, default, opts, body, pack_exprs):
    # Invoke the specific one
    fn = modifier_map.get(mod)

    if fn is None:
        return None

    expr = fn(mod, opts, body, pack_exprs)
    body.append('unsigned {} = {};'.format(mod, expr))

    # Validate we don't overflow
    try:
        assert(int(expr) < (1 << width))
    except:
        body.append('assert({} < {});'.format(mod, (1 << width)))

    body.append('')

    return True

# Compiles an S-expression (and/or/eq/neq, modifiers, `ordering`, immediates)
# into a C boolean expression suitable to stick in an if-statement. Takes an
# imm_map to map modifiers to immediate values, parametrized by the ctx that
# we're looking up in (the first, non-immediate argument of the equality)

SEXPR_BINARY = {
        "and": "&&",
        "or": "||",
        "eq": "==",
        "neq": "!="
}

def compile_s_expr(expr, imm_map, ctx):
    if expr[0] == 'alias':
        return compile_s_expr(expr[1], imm_map, ctx)
    elif expr == ['eq', 'ordering', '#gt']:
        return '(src0 > src1)'
    elif expr == ['neq', 'ordering', '#lt']:
        return '(src0 >= src1)'
    elif expr == ['neq', 'ordering', '#gt']:
        return '(src0 <= src1)'
    elif expr == ['eq', 'ordering', '#lt']:
        return '(src0 < src1)'
    elif expr == ['eq', 'ordering', '#eq']:
        return '(src0 == src1)'
    elif isinstance(expr, list):
        sep = " {} ".format(SEXPR_BINARY[expr[0]])
        return "(" + sep.join([compile_s_expr(s, imm_map, expr[1]) for s in expr[1:]]) + ")"
    elif expr[0] == '#':
        return str(imm_map[ctx][expr[1:]])
    else:
        return expr

# Packs a derived value. We just iterate through the possible choices and test
# whether the encoding matches, and if so we use it.

def pack_derived(pos, exprs, imm_map, body, pack_exprs):
    body.append('unsigned derived_{} = 0;'.format(pos))

    first = True
    for i, expr in enumerate(exprs):
        if expr is not None:
            cond = compile_s_expr(expr, imm_map, None)
            body.append('{}if {} derived_{} = {};'.format('' if first else 'else ', cond, pos, i))
            first = False

    assert (not first)
    body.append('else unreachable("No pattern match at pos {}");'.format(pos))
    body.append('')

    assert(pos is not None)
    pack_exprs.append('(derived_{} << {})'.format(pos, pos))

# Table mapping immediate names in the machine to expressions of `ins` to
# lookup the value in the IR, performing adjustments as needed

IMMEDIATE_TABLE = {
        'attribute_index': 'bi_get_immediate(ins, 0)',
        'varying_index': 'bi_get_immediate(ins, 0)',
        'index': 'bi_get_immediate(ins, 0)',
        'texture_index': 'ins->texture.texture_index',
        'sampler_index': 'ins->texture.sampler_index',
        'table': '63', # Bindless (flat addressing) mode for DTSEL_IMM

        # Not supported in the IR (TODO)
        'shift': '0',
        'fill': '0', # WMASK
}

# Generates a routine to pack a single variant of a single- instruction.
# Template applies the needed formatting and combine to OR together all the
# pack_exprs to avoid bit fields.
#
# Argument swapping is sensitive to the order of operations. Dependencies:
# sources (RW), modifiers (RW), derived values (W). Hence we emit sources and
# modifiers first, then perform a swap if necessary overwriting
# sources/modifiers, and last calculate derived values and pack.

variant_template = Template("""static inline unsigned
pan_pack_${name}(bi_clause *clause, bi_instruction *ins, bi_registers *regs)
{
${"\\n".join([("    " + x) for x in common_body])}
% if single_state:
% for (pack_exprs, s_body, _) in states:
${"\\n".join(["    " + x for x in s_body + ["return {};".format( " | ".join(pack_exprs))]])}
% endfor
% else:
% for i, (pack_exprs, s_body, cond) in enumerate(states):
    ${'} else ' if i > 0 else ''}if ${cond} {
${"\\n".join(["        " + x for x in s_body + ["return {};".format(" | ".join(pack_exprs))]])}
% endfor
    } else {
        unreachable("No matching state found in ${name}");
    }
% endif
}
""")

def pack_variant(opname, states):
    # Expressions to be ORed together for the final pack, an array per state
    pack_exprs = [[hex(state[1]["exact"][1])] for state in states]

    # Computations which need to be done to encode first, across states
    common_body = []

    # Map from modifier names to a map from modifier values to encoded values
    # String -> { String -> Uint }. This can be shared across states since
    # modifiers are (except the pos values) constant across state.
    imm_map = {}

    # Pack sources. Offset over to deal with staging/immediate weirdness in our
    # IR (TODO: reorder sources upstream so this goes away). Note sources are
    # constant across states.
    staging = states[0][1].get("staging", "")
    offset = 0
    if staging in ["r", "rw"]:
        offset += 1

    offset += len(set(["attribute_index", "varying_index", "index"]) & set([x[0] for x in states[0][1].get("immediates", [])]))

    if opname == '+LD_VAR_SPECIAL':
        offset += 1

    pack_sources(states[0][1].get("srcs", []), common_body, pack_exprs, offset)

    modifiers_handled = []
    for st in states:
        for ((mod, _, width), default, opts) in st[1].get("modifiers", []):
            if mod in modifiers_handled:
                continue

            modifiers_handled.append(mod)

            if pack_modifier(mod, width, default, opts, common_body, pack_exprs) is None:
                return None

            imm_map[mod] = { x: y for y, x in enumerate(opts) }

    for i, st in enumerate(states):
        for ((mod, pos, width), default, opts) in st[1].get("modifiers", []):
            if pos is not None:
                pack_exprs[i].append('({} << {})'.format(mod, pos))

    for ((src_a, src_b), cond, remap) in st[1].get("swaps", []):
        # Figure out which vars to swap, in order to swap the arguments. This
        # always includes the sources themselves, and may include source
        # modifiers (with the same source indices). We swap based on which
        # matches A, this is arbitrary but if we swapped both nothing would end
        # up swapping at all since it would swap back.

        vars_to_swap = ['src']
        for ((mod, _, width), default, opts) in st[1].get("modifiers", []):
            if mod[-1] in str(src_a):
                vars_to_swap.append(mod[0:-1])

        common_body.append('if {}'.format(compile_s_expr(cond, imm_map, None)) + ' {')

        # Emit the swaps. We use a temp, and wrap in a block to avoid naming
        # collisions with multiple swaps. {{Doubling}} to escape the format.

        for v in vars_to_swap:
            common_body.append('    {{ unsigned temp = {}{}; {}{} = {}{}; {}{} = temp; }}'.format(v, src_a, v, src_a, v, src_b, v, src_b))

        # Also, remap. Bidrectional swaps are explicit in the XML.
        for v in remap:
            maps = remap[v]
            imm = imm_map[v]

            for i, l in enumerate(maps):
                common_body.append('    {}if ({} == {}) {} = {};'.format('' if i == 0 else 'else ', v, imm[l], v, imm[maps[l]]))

        common_body.append('}')
        common_body.append('')

    for (name, pos, width) in st[1].get("immediates", []):
        if name not in IMMEDIATE_TABLE:
            return None

        common_body.append('unsigned {} = {};'.format(name, IMMEDIATE_TABLE[name]))

        for st in pack_exprs:
            st.append('({} << {})'.format(name, pos))

    if staging == 'r':
        common_body.append('bi_read_staging_register(clause, ins);')
    elif staging == 'w':
        common_body.append('bi_write_staging_register(clause, ins);')
    elif staging == '':
        pass
    else:
        assert staging == 'rw'
        # XXX: register allocation requirement (!)
        common_body.append('bi_read_staging_register(clause, ins);')
        common_body.append('assert(ins->src[0] == ins->dest);')

    # After this, we have to branch off, since deriveds *do* vary based on state.
    state_body = [[] for s in states]

    for i, (_, st) in enumerate(states):
        for ((pos, width), exprs) in st.get("derived", []):
            pack_derived(pos, exprs, imm_map, state_body[i], pack_exprs[i])

    # How do we pick a state? Accumulate the conditions
    state_conds = [compile_s_expr(st[0], imm_map, None) for st in states] if len(states) > 1 else [None]

    if state_conds == None:
        assert (states[0][0] == None)

    # Finally, we'll collect everything together
    return variant_template.render(name = opname_to_c(opname), states = zip(pack_exprs, state_body, state_conds), common_body = common_body, single_state = (len(states) == 1))

HEADER = """/*
 * Copyright (C) 2020 Collabora, Ltd.
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

/* Autogenerated file, do not edit */

#ifndef _BI_GENERATED_PACK_H
#define _BI_GENERATED_PACK_H

#include "compiler.h"
#include "bi_pack_helpers.h"
"""

print(HEADER)

packs = [pack_variant(e, instructions[e]) for e in instructions]
for p in packs:
    print(p)

print("#endif")
