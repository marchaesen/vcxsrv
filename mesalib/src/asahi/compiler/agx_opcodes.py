"""
Copyright (C) 2021 Alyssa Rosenzweig <alyssa@rosenzweig.io>

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice (including the next
paragraph) shall be included in all copies or substantial portions of the
Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
"""

opcodes = {}
immediates = {}
enums = {}

class Opcode(object):
   def __init__(self, name, dests, srcs, imms, is_float, can_eliminate, encoding_16, encoding_32):
      self.name = name
      self.dests = dests
      self.srcs = srcs
      self.imms = imms
      self.is_float = is_float
      self.can_eliminate = can_eliminate
      self.encoding_16 = encoding_16
      self.encoding_32 = encoding_32

class Immediate(object):
   def __init__(self, name, ctype):
      self.name = name
      self.ctype = ctype

class Encoding(object):
   def __init__(self, description):
      (exact, mask, length_short, length_long) = description

      # Convenience
      if length_long is None:
         length_long = length_short

      self.exact = exact
      self.mask = mask
      self.length_short = length_short
      self.extensible = length_short != length_long

      if self.extensible:
         assert(length_long == length_short + (4 if length_short > 8 else 2))

def op(name, encoding_32, dests = 1, srcs = 0, imms = [], is_float = False, can_eliminate = True, encoding_16 = None):
   encoding_16 = Encoding(encoding_16) if encoding_16 is not None else None
   encoding_32 = Encoding(encoding_32) if encoding_32 is not None else None

   opcodes[name] = Opcode(name, dests, srcs, imms, is_float, can_eliminate, encoding_16, encoding_32)

def immediate(name, ctype = "uint32_t"):
   imm = Immediate(name, ctype)
   immediates[name] = imm
   return imm

def enum(name, value_dict):
   enums[name] = value_dict
   return immediate(name, "enum agx_" + name)

L = (1 << 15)
_ = None

FORMAT = immediate("format", "enum agx_format")
IMM = immediate("imm")
WRITEOUT = immediate("writeout")
INDEX = immediate("index")
COMPONENT = immediate("component")
CHANNELS = immediate("channels")
TRUTH_TABLE = immediate("truth_table")
ROUND = immediate("round")
SHIFT = immediate("shift")
MASK = immediate("mask")
BFI_MASK = immediate("bfi_mask")
LOD_MODE = immediate("lod_mode", "enum agx_lod_mode")
DIM = immediate("dim", "enum agx_dim")
SCOREBOARD = immediate("scoreboard")
ICOND = immediate("icond")
FCOND = immediate("fcond")
NEST = immediate("nest")
INVERT_COND = immediate("invert_cond")
NEST = immediate("nest")
TARGET = immediate("target", "agx_block *")
PERSPECTIVE = immediate("perspective", "bool")
SR = enum("sr", {
   0:  'threadgroup_position_in_grid.x',
   1:  'threadgroup_position_in_grid.y',
   2:  'threadgroup_position_in_grid.z',
   4:  'threads_per_threadgroup.x',
   5:  'threads_per_threadgroup.y',
   6:  'threads_per_threadgroup.z',
   8:  'dispatch_threads_per_threadgroup.x',
   9:  'dispatch_threads_per_threadgroup.y',
   10: 'dispatch_threads_per_threadgroup.z',
   48: 'thread_position_in_threadgroup.x',
   49: 'thread_position_in_threadgroup.y',
   50: 'thread_position_in_threadgroup.z',
   51: 'thread_index_in_threadgroup',
   52: 'thread_index_in_subgroup',
   53: 'subgroup_index_in_threadgroup',
   56: 'active_thread_index_in_quad',
   58: 'active_thread_index_in_subgroup',
   62: 'backfacing',
   80: 'thread_position_in_grid.x',
   81: 'thread_position_in_grid.y',
   82: 'thread_position_in_grid.z',
})

FUNOP = lambda x: (x << 28)
FUNOP_MASK = FUNOP((1 << 14) - 1)

def funop(name, opcode):
   op(name, (0x0A | L | (opcode << 28),
      0x3F | L | (((1 << 14) - 1) << 28), 6, _),
      srcs = 1, is_float = True)

# Listing of opcodes
funop("floor",     0b000000)
funop("srsqrt",    0b000001)
funop("dfdx",      0b000100)
funop("dfdy",      0b000110)
funop("rcp",       0b001000)
funop("rsqrt",     0b001001)
funop("sin_pt_1",  0b001010)
funop("log2",      0b001100)
funop("exp2",      0b001101)
funop("sin_pt_2",  0b001110)
funop("ceil",      0b010000)
funop("trunc",     0b100000)
funop("roundeven", 0b110000)

op("fadd",
      encoding_16 = (0x26 | L, 0x3F | L, 6, _),
      encoding_32 = (0x2A | L, 0x3F | L, 6, _),
      srcs = 2, is_float = True)

op("fma",
      encoding_16 = (0x36, 0x3F, 6, 8),
      encoding_32 = (0x3A, 0x3F, 6, 8),
      srcs = 3, is_float = True)

op("fmul",
      encoding_16 = ((0x16 | L), (0x3F | L), 6, _),
      encoding_32 = ((0x1A | L), (0x3F | L), 6, _),
      srcs = 2, is_float = True)

op("mov_imm",
      encoding_32 = (0x62, 0xFF, 6, 8),
      encoding_16 = (0x62, 0xFF, 4, 6),
      imms = [IMM])

op("iadd",
      encoding_32 = (0x0E, 0x3F | L, 8, _),
      srcs = 2, imms = [SHIFT])

op("imad",
      encoding_32 = (0x1E, 0x3F | L, 8, _),
      srcs = 3, imms = [SHIFT])

op("bfi",
      encoding_32 = (0x2E, 0x7F | (0x3 << 26), 8, _),
      srcs = 3, imms = [BFI_MASK])

op("bfeil",
      encoding_32 = (0x2E | L, 0x7F | L | (0x3 << 26), 8, _),
      srcs = 3, imms = [BFI_MASK])

op("asr",
      encoding_32 = (0x2E | L | (0x1 << 26), 0x7F | L | (0x3 << 26), 8, _),
      srcs = 2)

op("icmpsel",
      encoding_32 = (0x12, 0x7F, 8, 10),
      srcs = 4, imms = [ICOND])

op("fcmpsel",
      encoding_32 = (0x02, 0x7F, 8, 10),
      srcs = 4, imms = [FCOND])

# sources are coordinates, LOD, texture, sampler, offset
# TODO: anything else?
op("texture_sample",
      encoding_32 = (0x32, 0x7F, 8, 10), # XXX WRONG SIZE
      srcs = 5, imms = [DIM, LOD_MODE, MASK, SCOREBOARD])

# sources are base, index
op("device_load",
      encoding_32 = (0x05, 0x7F, 6, 8),
      srcs = 2, imms = [FORMAT, MASK, SCOREBOARD])

op("wait", (0x38, 0xFF, 2, _), dests = 0,
      can_eliminate = False, imms = [SCOREBOARD])

op("get_sr", (0x72, 0x7F | L, 4, _), dests = 1, imms = [SR])

op("sample_mask", (0x7fc1, 0xffff, 6, _), dests = 0, srcs = 1, can_eliminate = False)

# Essentially same encoding
op("ld_tile", (0x49, 0x7F, 8, _), dests = 1, srcs = 0,
      can_eliminate = False, imms = [FORMAT])

op("st_tile", (0x09, 0x7F, 8, _), dests = 0, srcs = 1,
      can_eliminate = False, imms = [FORMAT])

for (name, exact) in [("any", 0xC000), ("none", 0xC200)]:
   op("jmp_exec_" + name, (exact, (1 << 16) - 1, 6, _), dests = 0, srcs = 0,
         can_eliminate = False, imms = [TARGET])

# TODO: model implicit r0l destinations
op("pop_exec", (0x52 | (0x3 << 9), ((1 << 48) - 1) ^ (0x3 << 7) ^ (0x3 << 11), 6, _),
      dests = 0, srcs = 0, can_eliminate = False, imms = [NEST])

for is_float in [False, True]:
   mod_mask = 0 if is_float else (0x3 << 26) | (0x3 << 38)

   for (cf, cf_op) in [("if", 0), ("else", 1), ("while", 2)]:
      name = "{}_{}cmp".format(cf, "f" if is_float else "i")
      exact = 0x42 | (0x0 if is_float else 0x10) | (cf_op << 9)
      mask = 0x7F | (0x3 << 9) | mod_mask | (0x3 << 44)
      imms = [NEST, FCOND if is_float else ICOND, INVERT_COND]

      op(name, (exact, mask, 6, _), dests = 0, srcs = 2, can_eliminate = False,
            imms = imms, is_float = is_float)

op("bitop", (0x7E, 0x7F, 6, _), srcs = 2, imms = [TRUTH_TABLE])
op("convert", (0x3E | L, 0x7F | L | (0x3 << 38), 6, _), srcs = 2, imms = [ROUND]) 
op("ld_vary", (0x21, 0xBF, 8, _), srcs = 1, imms = [CHANNELS, PERSPECTIVE])
op("ld_vary_flat", (0xA1, 0xBF, 8, _), srcs = 1, imms = [CHANNELS])
op("st_vary", None, dests = 0, srcs = 2, can_eliminate = False)
op("stop", (0x88, 0xFFFF, 2, _), dests = 0, can_eliminate = False)
op("trap", (0x08, 0xFFFF, 2, _), dests = 0, can_eliminate = False)
op("writeout", (0x48, 0xFF, 4, _), dests = 0, imms = [WRITEOUT], can_eliminate = False)

op("p_combine", _, srcs = 4)
op("p_extract", _, srcs = 1, imms = [COMPONENT])
