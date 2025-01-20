# Copyright © 2024 Tomeu Vizoso <tomeu.vizoso@tomeuvizoso.net>
# SPDX-License-Identifier: MIT

"""Parse the coefficients data blob and print something more readable."""

import math
import os
import sys

DEBUG = False

VERSION = os.environ.get("VSIMULATOR_CONFIG", "VIPPICO_V3_PID0X99")

weights_width = 2
weights_height = weights_width
input_channels = 8
output_channels = 0

def assertEqual(actual, expected):
    assert abs(actual - expected) <= 1, "Expecting 0x%02x, got 0x%02x instead" % (expected, actual)
    if actual != expected:
        print("Warning: expecting 0x%02x, got 0x%02x instead" % (expected, actual))

def dbg(*args, **kwargs):
    if DEBUG:
        print(" ".join(map(str, args)), **kwargs)

class BitStream():
    def __init__(self, bytes):
      self._bytes = bytes
      self._buffer = 0
      self._bits_in_buffer = 0

    def read(self, bits):
        if bits == 0:
            dbg("read %d bits: %d" % (bits, 0))
            return 0

        while bits > self._bits_in_buffer:
            self._buffer |= self._bytes.pop(0) << self._bits_in_buffer
            self._bits_in_buffer += 8

        temp = 0
        for i in range(0, bits):
            temp |= self._buffer & (1 << i)
            self._bits_in_buffer -= 1
        self._buffer >>= bits

        dbg("read %d bits: %d" % (bits, temp))
        return temp

    def read32(self):
        val = self.read(32)
        return val

    def read16(self):
        val = self.read(16)
        return val

    def reset(self):
        self._buffer = 0
        self._bits_in_buffer = 0

    def read_bytes(self, length):
        assert(self._bits_in_buffer == 0)

        new = BitStream(self._bytes[:length])
        self._bytes = self._bytes[length:]
        return new

bytes = []
content = sys.stdin.read().strip()
content = content.replace("\n", "")
bytes = [int(content[i:i + 2], 16) for i in range(0, len(content), 2)]

bytes = BitStream(bytes)

precode = bytes.read(1)
bit16 = bytes.read(1)
fp16 = bytes.read(1)
reserved1 = bytes.read(1)
version = bytes.read(4)

run_length_table_size = bytes.read(8)

run_length_table = []
for i in range(0, 18):
    run_length_table.append(bytes.read(8))

symbol_map = []
for i in range(0, 8):
    symbol_map.append(bytes.read(4))

avg_bias = bytes.read(16)
reserved2 = bytes.read(16)

stream_sizes = []
for i in range(0, 8):
    stream_sizes.append(bytes.read32())

padding = bytes.read32()

print("Precode: %d" % precode)
print("Bit16: %d" % bit16)
print("FP16: %d" % fp16)
print("Reserved 1: %d" % reserved1)
print("Version: %d" % version)
print("Run length table size: %d" % run_length_table_size)
print("Run length table: %r" % run_length_table)
print("Symbol map: %r" % symbol_map)
print("Avg bias: %d" % avg_bias)
print("Reserved 2: %d" % reserved2)
print("Stream sizes: %r" % stream_sizes)
print("Padding: %d" % padding)

def get_symbol(part0, part1):
    dbg("get_symbol part0 %d part1 %d" % (part0, part1))
    if part0 == 0:
        return 0, part0 >> 2
    elif part0 == 1:
        return 1, part0 >> 2
    elif part0 == 2:
        if part1 == 1 or part1 == 3:
            return 5, part1 >> 1
        elif part1 == 0:
            return 7, -1
        elif part1 == 2:
            return 6, -1
        else:
            assert False
    elif part0 == 3:
        return 3, -1
    elif part0 == 4:
        return 0, part0 >> 2
    elif part0 == 5:
        return 1, part0 >> 2
    elif part0 == 6:
        return 4, -1
    elif part0 == 7:
        return 2, -1
    else:
        assert False

class Code:
    def __init__(self):
        self.reset()

    def reset(self):
        self.part0 = 0
        self.part1 = 0
        self.part2 = 0
        self.part1_len = 0
        self.part2_len = 0
        self.bit_len = 0

RING_BUFFER_SIZE = 6
ring_buffer = []
for i in range(0, RING_BUFFER_SIZE):
    ring_buffer.append(Code())

weights = []
total_read = 1

def decode_one_char(part2, bit_length, unk1, zero):
    if (unk1 == -1):
        unk1 = part2 & 1
        part2 = part2 >> 1

    if (bit_length != 0):
        part2 = part2 | 1 << (bit_length - 1 & 0x1f)

    if (unk1 != 0):
        part2 = (part2 ^ 0xff) + zero

    return part2

def uint8(val):
    if val > 255:
        return val - 256
    else:
        return val

def read_pair(bytes):
    global total_read

    if avg_bias > 0:
        zero_point = avg_bias
    else:
        zero_point = 0x80

    dbg(">>>>>>>>>> Stage 1: total_read %d" % total_read)
    for i in range(1, -1, -1):
        code = ring_buffer[(total_read - i) % RING_BUFFER_SIZE]
        code.reset()
        code.part0 = bytes.read(3)
        code.part1_len = 2 if code.part0 == 2 else 0
        dbg("code at %d has part0 %d part1_len %d" % ((total_read - i) % RING_BUFFER_SIZE, code.part0, code.part1_len))

    if total_read >= 2:
        dbg(">>>>>>>>>> Stage 2")
        for i in range(3, 1, -1):
            code = ring_buffer[(total_read - i) % RING_BUFFER_SIZE]
            code.part1 = bytes.read(code.part1_len)
            symbol, code.unk1 = get_symbol(code.part0, code.part1)
            #dbg("symbol %d code.unk1 %d" % (symbol, code.unk1))
            code.bit_len = symbol_map[symbol]
            if run_length_table_size == 0:
                code.part2_len = max(code.bit_len, 1)
            else:
                if run_length_table_size <= 4:
                   code.part2_len = 1
                elif run_length_table_size <= 6:
                   code.part2_len = 2
                elif run_length_table_size <= 10:
                   code.part2_len = 3
                else:
                   code.part2_len = 4

            if code.unk1 != -1:
                code.part2_len -= 1

            dbg("part1 %d bit_len %d part2_len %d" % (code.part1, code.bit_len, code.part2_len))

    if total_read >= 4:
        dbg(">>>>>>>>>> Stage 3")
        for i in range(5, 3, -1):
            code = ring_buffer[(total_read - i) % RING_BUFFER_SIZE]
            code.part2 = bytes.read(code.part2_len)

            if run_length_table_size == 0:
                char = decode_one_char(code.part2, code.bit_len, code.unk1, 0)
                weights.append(uint8(char + avg_bias))
                dbg("run_length_table_size == 0: uint8(char + avg_bias) %d" % uint8(char + avg_bias))
            else:
                if code.bit_len == 7:
                    if code.unk1 == -1:
                        char = code.part2
                    else:
                        char = code.unk1 + code.part2 * 2
                    weights.append(uint8(char + avg_bias))
                    dbg("7: char %d uint8(char + avg_bias) %d" % (char, uint8(char + avg_bias)))
                elif code.bit_len == 8:
                    if code.unk1 == -1:
                        index = code.part2 + 2
                    else:
                        index = code.part2 * 2 + code.unk1
                    char = run_length_table[index] + 1
                    weights.extend([0x0 + avg_bias] * char)
                    dbg("8: [0x0 + avg_bias] * char %r" % [0x0 + avg_bias] * char)
                elif code.bit_len == 0:
                    if code.unk1 == -1:
                        symbol = code.part2
                    else:
                        symbol = code.unk1
                    char = run_length_table[symbol] + 1
                    if VERSION == "VIPPICO_V3_PID0X99":
                        weights.extend([zero_point] * char)
                    else:
                        weights.extend([0x0] * char)
                    dbg("0: [zero_point] * char %r" % [zero_point] * char)
                else:
                    char = decode_one_char(code.part2, code.bit_len, code.unk1, 0)
                    weights.append(uint8(char + avg_bias))
                    dbg("else: uint8(char + avg_bias) %d" % uint8(char + avg_bias))

            dbg("bit_len %d part2_len %d part2 %d char %02x" % (code.bit_len, code.part2_len, code.part2, char))

    dbg()
    total_read += 2

def align(num, alignment):
    if num % alignment == 0:
        return num
    return num + alignment - (num % alignment)

def pop_int32(weights):
    val1 = weights.pop(0)

    val2 = weights.pop(0)
    if val2 > 0:
        val2 += 1

    val3 = weights.pop(0)
    if val3 > 0:
        val3 += 1

    val4 = weights.pop(0)
    if val4 > 0:
        val4 += 1

    return val1 | (val2 << 8) | (val3 << 16) | (val4 << 24)

core = 0
for stream_size in stream_sizes:
    if stream_size == 0:
        break
    aligned_size = int(align(math.ceil(stream_size / 8.0), 64))
    core_bytes = bytes.read_bytes(aligned_size)
    while len(core_bytes._bytes) > aligned_size - math.ceil(stream_size / 8.0):
        read_pair(core_bytes)

    print()
    print("Raw data for core %d: %r" % (core, weights))

    vz1 = weights.pop(0)
    vz2 = weights.pop(0)
    kernels_per_core = vz1 | (vz2 << 8)
    output_channels += kernels_per_core
    print("Kernels per core: %r" % (kernels_per_core))

    for ic in range(0, input_channels):
        for kernel in range(0, kernels_per_core):
            if VERSION == "VIPPICO_V3_PID0X99" and ic == 0:
                bias = pop_int32(weights)
                print("Bias: 0x%x" % (bias))

            kernel_size = weights_width * weights_height
            channel_weights = weights[:kernel_size]
            weights = weights[kernel_size:]

            if VERSION == "VIP8000NANOSI_PLUS_PID0X9F":
                converted = []
                for weight in channel_weights:
                    unsigned = weight + 0x80
                    if unsigned > 255:
                        unsigned = unsigned & 0xFF
                    converted.append(unsigned)
                channel_weights = converted

            print("Weights: %r" % channel_weights)

            if ic == input_channels - 1:
                if VERSION == "VIPPICO_V3_PID0X99":
                    out_offset = pop_int32(weights)
                    print("Output offset: %r" % out_offset)

    weights.clear()

    total_read = 0

    core += 1

for oc in range(0, output_channels):
   print("%x" % bytes.read32())
