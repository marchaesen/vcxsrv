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

import argparse
import sys
import struct
from valhall import instructions, enums, immediates, typesize

LINE = ''

class ParseError(Exception):
    def __init__(self, error):
        self.error = error

class FAUState:
    def __init__(self, mode, single_uniform_slot = True):
        self.mode = mode
        self.single_uniform_slot = single_uniform_slot
        self.uniform_slot = None
        self.special = None
        self.buffer = set()

    def push(self, s):
        self.buffer.add(s)
        die_if(len(self.buffer) > 2, "Overflowed FAU buffer")

    def push_special(self, s):
        die_if(self.special is not None and self.special != s,
                'Multiple special immediates')
        self.special = s
        self.push(s)

    def descriptor(self, s):
        die_if(self.mode != 'none', f'Expected no modifier with {s}')
        self.push_special(s)

    def uniform(self, v):
        slot = v >> 1

        die_if(self.mode != 'none',
                'Expected uniform with default immediate mode')
        die_if(self.uniform_slot is not None and self.uniform_slot != slot,
                'Overflowed uniform slots')

        if self.single_uniform_slot:
            self.uniform_slot = slot

        self.push(f'uniform{v}')

    def id(self, s):
        die_if(self.mode != 'id',
                'Expected .id modifier with thread storage pointer')

        self.push_special(f'id{s}')

    def ts(self, s):
        die_if(self.mode != 'ts',
                'Expected .ts modifier with thread pointer')
        self.push_special(f'ts{s}')

    def constant(self, cons):
        self.push(cons)

# When running standalone, exit with the error since we're dealing with a
# human. Otherwise raise a Python exception so the test harness can handle it.
def die(s):
    if __name__ == "__main__":
        print(LINE)
        print(s)
        sys.exit(1)
    else:
        raise ParseError(s)

def die_if(cond, s):
    if cond:
        die(s)

def parse_int(s, minimum, maximum):
    try:
        number = int(s, base = 0)
    except ValueError:
        die(f"Expected number {s}")

    if number > maximum or number < minimum:
        die(f"Range error on {s}")

    return number

def encode_source(op, fau):
    if op == 'atest_datum':
        fau.descriptor(op)
        return 0x2A | 0xC0
    elif op.startswith('blend_descriptor_'):
        fau.descriptor(op)
        fin = op[len('blend_descriptor_'):]
        die_if(len(fin) != 3, 'Bad syntax')
        die_if(fin[1] != '_', 'Bad syntax')
        die_if(fin[2] not in ['x', 'y'], 'Bad component')

        rt = parse_int(fin[0], 0, 7)
        hi = 1 if (fin[2] == 'y') else 0
        return (0x30 | (2*rt) + hi) | 0xC0
    elif op[0] == '`':
        die_if(op[1] != 'r', f"Expected register after discard {op}")
        return parse_int(op[2:], 0, 63) | 0x40
    elif op[0] == 'r':
        return parse_int(op[1:], 0, 63)
    elif op[0] == 'u':
        val = parse_int(op[1:], 0, 63)
        fau.uniform(val)
        return val | 0x80
    elif op[0] == 'i':
        return int(op[3:]) | 0xC0
    elif op in enums['thread_storage_pointers'].bare_values:
        fau.ts(op)
        idx = 32 + enums['thread_storage_pointers'].bare_values.index(op)
        return idx | 0xC0
    elif op in enums['thread_identification'].bare_values:
        fau.id(op)
        idx = 32 + enums['thread_identification'].bare_values.index(op)
        return idx | 0xC0
    elif op.startswith('0x'):
        try:
            val = int(op, base=0)
        except ValueError:
            die('Expected value')

        die_if(val not in immediates, 'Unexpected immediate value')
        fau.constant(val)
        return immediates.index(val) | 0xC0
    else:
        die('Invalid operand')

def encode_dest(op):
    die_if(op[0] != 'r', f"Expected register destination {op}")

    parts = op.split(".")
    reg = parts[0]

    # Default to writing in full
    wrmask = 0x3

    if len(parts) > 1:
        WMASKS = ["h0", "h1"]
        die_if(len(parts) > 2, "Too many modifiers")
        mask = parts[1];
        die_if(mask not in WMASKS, "Expected a write mask")
        wrmask = 1 << WMASKS.index(mask)

    return parse_int(reg[1:], 0, 63) | (wrmask << 6)

def parse_asm(line):
    global LINE
    LINE = line # For better errors
    encoded = 0

    # Figure out mnemonic
    head = line.split(" ")[0]
    opts = [ins for ins in instructions if head.startswith(ins.name)]
    opts = sorted(opts, key=lambda x: len(x.name), reverse=True)

    if len(opts) == 0:
        die(f"No known mnemonic for {head}")

    if len(opts) > 1 and len(opts[0].name) == len(opts[1].name):
        print(f"Ambiguous mnemonic for {head}")
        print(f"Options:")
        for ins in opts:
            print(f"  {ins}")
        sys.exit(1)

    ins = opts[0]

    # Split off modifiers
    if len(head) > len(ins.name) and head[len(ins.name)] != '.':
        die(f"Expected . after instruction in {head}")

    mods = head[len(ins.name) + 1:].split(".")
    modifier_map = {}
    immediate_mode = 'none'

    for mod in mods:
        if mod in enums['immediate_mode'].bare_values:
            die_if(immediate_mode != 'none', 'Multiple immediate modes specified')
            immediate_mode = mod

    tail = line[(len(head) + 1):]
    operands = [x.strip() for x in tail.split(",") if len(x.strip()) > 0]
    expected_op_count = len(ins.srcs) + len(ins.dests) + len(ins.immediates) + len(ins.staging)
    if len(operands) != expected_op_count:
        die(f"Wrong number of operands in {line}, expected {expected_op_count}, got {len(operands)} {operands}")

    # Encode each operand
    for i, (op, sr) in enumerate(zip(operands, ins.staging)):
        die_if(op[0] != '@', f'Expected staging register, got {op}')
        parts = op[1:].split(':')

        die_if(any([x[0] != 'r' for x in parts]), f'Expected registers, got {op}')
        regs = [parse_int(x[1:], 0, 63) for x in parts]

        sr_count = len(regs)
        die_if(sr_count < 1, f'Expected staging register, got {op}')
        die_if(sr_count > 7, f'Too many staging registers {sr_count}')

        base = regs[0]
        die_if(any([reg != (base + i) for i, reg in enumerate(regs)]),
                'Expected consecutive staging registers, got {op}')
        die_if(sr_count > 1 and (base % 2) != 0,
                'Consecutive staging registers must be aligned to a register pair')

        if sr.count == 0:
            modifier_map["staging_register_count"] = sr_count
        else:
            die_if(sr_count != sr.count, f"Expected 4 staging registers, got {sr_count}")

        encoded |= ((sr.encoded_flags | base) << sr.start)
    operands = operands[len(ins.staging):]

    for op, dest in zip(operands, ins.dests):
        encoded |= encode_dest(op) << 40
    operands = operands[len(ins.dests):]

    if len(ins.dests) == 0 and len(ins.staging) == 0:
        # Set a placeholder writemask to prevent encoding faults
        encoded |= (0xC0 << 40)

    # TODO: Determine which instructions can only have address a single uniform
    single_uniform_slot = not ins.name.startswith('LD_BUFFER')

    fau = FAUState(immediate_mode, single_uniform_slot = single_uniform_slot)

    for i, (op, src) in enumerate(zip(operands, ins.srcs)):
        parts = op.split('.')
        encoded |= encode_source(parts[0], fau) << (i * 8)

        # Has a swizzle been applied yet?
        swizzled = False

        for mod in parts[1:]:
            # Encode the modifier
            if mod in src.offset and src.bits[mod] == 1:
                encoded |= (1 << src.offset[mod])
            elif src.halfswizzle and mod in enums[f'half_swizzles_{src.size}_bit'].bare_values:
                die_if(swizzled, "Multiple swizzles specified")
                swizzled = True
                val = enums[f'half_swizzles_{src.size}_bit'].bare_values.index(mod)
                encoded |= (val << src.offset['widen'])
            elif mod in enums[f'swizzles_{src.size}_bit'].bare_values and (src.widen or src.lanes):
                die_if(swizzled, "Multiple swizzles specified")
                swizzled = True
                val = enums[f'swizzles_{src.size}_bit'].bare_values.index(mod)
                encoded |= (val << src.offset['widen'])
            elif src.lane and mod in enums[f'lane_{src.size}_bit'].bare_values:
                die_if(swizzled, "Multiple swizzles specified")
                swizzled = True
                val = enums[f'lane_{src.size}_bit'].bare_values.index(mod)
                encoded |= (val << src.offset['lane'])
            elif src.size == 32 and mod in enums['widen'].bare_values:
                die_if(not src.swizzle, "Instruction doesn't take widens")
                die_if(swizzled, "Multiple swizzles specified")
                swizzled = True
                val = enums['widen'].bare_values.index(mod)
                encoded |= (val << src.offset['swizzle'])
            elif src.size == 16 and mod in enums['swizzles_16_bit'].bare_values:
                die_if(not src.swizzle, "Instruction doesn't take swizzles")
                die_if(swizzled, "Multiple swizzles specified")
                swizzled = True
                val = enums['swizzles_16_bit'].bare_values.index(mod)
                encoded |= (val << src.offset['swizzle'])
            elif mod in enums['lane_8_bit'].bare_values:
                die_if(not src.lane, "Instruction doesn't take a lane")
                die_if(swizzled, "Multiple swizzles specified")
                swizzled = True
                val = enums['lane_8_bit'].bare_values.index(mod)
                encoded |= (val << src.lane)
            elif mod in enums['lanes_8_bit'].bare_values:
                die_if(not src.lanes, "Instruction doesn't take a lane")
                die_if(swizzled, "Multiple swizzles specified")
                swizzled = True
                val = enums['lanes_8_bit'].bare_values.index(mod)
                encoded |= (val << src.offset['widen'])
            else:
                die(f"Unknown modifier {mod}")

        # Encode the identity if a swizzle is required but not specified
        if src.swizzle and not swizzled and src.size == 16:
            mod = enums['swizzles_16_bit'].default
            val = enums['swizzles_16_bit'].bare_values.index(mod)
            encoded |= (val << src.offset['swizzle'])
        elif src.widen and not swizzled and src.size == 16:
            die_if(swizzled, "Multiple swizzles specified")
            mod = enums['swizzles_16_bit'].default
            val = enums['swizzles_16_bit'].bare_values.index(mod)
            encoded |= (val << src.offset['widen'])

    operands = operands[len(ins.srcs):]

    for i, (op, imm) in enumerate(zip(operands, ins.immediates)):
        if op[0] == '#':
            die_if(imm.name != 'constant', "Wrong syntax for immediate")
            parts = [imm.name, op[1:]]
        else:
            parts = op.split(':')
            die_if(len(parts) != 2, f"Wrong syntax for immediate, wrong number of colons in {op}")
            die_if(parts[0] != imm.name, f"Wrong immediate, expected {imm.name}, got {parts[0]}")

        if imm.signed:
            minimum = -(1 << (imm.size - 1))
            maximum = +(1 << (imm.size - 1)) - 1
        else:
            minimum = 0
            maximum = (1 << imm.size) - 1

        val = parse_int(parts[1], minimum, maximum)

        if val < 0:
            # Sign extends
            val = (1 << imm.size) + val

        encoded |= (val << imm.start)

    operands = operands[len(ins.immediates):]

    # Encode the operation itself
    encoded |= (ins.opcode << 48)
    encoded |= (ins.opcode2 << ins.secondary_shift)

    # Encode modifiers
    has_action = False
    for mod in mods:
        if len(mod) == 0:
            continue

        if mod in enums['action'].bare_values:
            die_if(has_action, "Multiple actions specified")
            has_action = True
            encoded |= (enums['action'].bare_values.index(mod) << 59)
            encoded |= (1 << 62) # Action, not wait
        elif mod.startswith('wait'):
            die_if(has_action, "Multiple actions specified")
            has_action = True

            slots = mod[len('wait'):]
            try:
                slots = set([int(x) for x in slots])
            except ValueError:
                die(f"Expected slots in {mod}")

            known_slots = set([0, 1, 2])
            die_if(not slots.issubset(known_slots), f"Unknown slots in {mod}")

            if 0 in slots:
                encoded |= (1 << 59)
            if 1 in slots:
                encoded |= (1 << 60)
            if 2 in slots:
                encoded |= (1 << 61)
        elif mod in enums['immediate_mode'].bare_values:
            pass # handled specially
        else:
            candidates = [c for c in ins.modifiers if mod in c.bare_values]

            die_if(len(candidates) == 0, f"Invalid modifier {mod} used")
            assert(len(candidates) == 1) # No ambiguous modifiers
            opts = candidates[0]

            value = opts.bare_values.index(mod)
            assert(value is not None)

            die_if(opts.name in modifier_map, f"{opts.name} specified twice")
            modifier_map[opts.name] = value

    for mod in ins.modifiers:
        value = modifier_map.get(mod.name, mod.default)
        die_if(value is None, f"Missing required modifier {mod.name}")

        assert(value < (1 << mod.size))
        encoded |= (value << mod.start)

    encoded |= (enums['immediate_mode'].bare_values.index(immediate_mode) << 57)
    return encoded

if __name__ == "__main__":
    # Provide commandline interface
    parser = argparse.ArgumentParser(description='Assemble Valhall shaders')
    parser.add_argument('infile', nargs='?', type=argparse.FileType('r'),
                        default=sys.stdin)
    parser.add_argument('outfile', type=argparse.FileType('wb'))
    args = parser.parse_args()

    lines = args.infile.read().strip().split('\n')
    lines = [l for l in lines if len(l) > 0 and l[0] != '#']

    packed = b''.join([struct.pack('<Q', parse_asm(ln)) for ln in lines])
    args.outfile.write(packed)
