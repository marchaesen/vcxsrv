#
# Copyright 2024 Autodesk, Inc.
#
# SPDX-License-Identifier: MIT
#

import argparse
from bisect import bisect_left, bisect_right
from dataclasses import dataclass
from pathlib import Path
import re


@dataclass
class Instruction:
    address: int
    assembly: str
    samples: int = 0


def mapping_address_key(mapping: tuple[int, int, str]):
    return mapping[0]


def instruction_address_key(instruction: Instruction):
    return instruction.address


def parse_mappings(map_file_path: Path):
    mappings: list[tuple[int, int, str]] = []
    with open(map_file_path) as map_file:
        for mapping in map_file:
            address_hex, size_hex, name = mapping.split(' ')
            address = int(address_hex, base=16)
            mappings.append((address, address + int(size_hex, base=16), name.strip()))

    mappings.sort(key=mapping_address_key)
    return mappings


def parse_traces(trace_file_path: Path):
    pattern = re.compile(r'((?:[^;]+;)*?[^;]+) (\d+)\n')

    traces: list[tuple[list[str], int]] = []
    with open(trace_file_path) as trace_file:
        for trace in trace_file:
            match = pattern.fullmatch(trace)
            traces.append((match.group(1).split(';'), int(match.group(2))))

    return traces


def parse_asm(asm_file_path: Path):
    symbol_pattern = re.compile(r'(\w+) ([0-9a-fA-F]+):\n')
    instruction_pattern = re.compile(r' *([0-9a-fA-F]+):\t(.*?)\n')

    asm: dict[tuple[int, str], list[Instruction]] = {}
    with open(asm_file_path) as asm_file:
        current_instructions = None
        for line in asm_file:
            if match := symbol_pattern.fullmatch(line):
                symbol = (int(match.group(2), base=16), match.group(1))
                current_instructions = asm[symbol] = []
            elif match := instruction_pattern.fullmatch(line):
                current_instructions.append(Instruction(int(match.group(1), base=16), match.group(2)))

    return asm


def main():
    parser = argparse.ArgumentParser(description='Map LLVMPipe JIT addresses in FlameGraph style '
                                     'collapsed stack traces to their symbol name. Also optionally '
                                     'annotate JIT assembly dumps with sample counts.')
    parser.add_argument('jit_symbol_map', type=Path, help='JIT symbol map from LLVMPipe')
    parser.add_argument('collapsed_traces', type=Path)
    parser.add_argument('-a', '--asm', type=Path, nargs='?', const='', metavar='asm_path',
                        help='JIT assembly dump from LLVMPipe. Defaults to "<jit_symbol_map>.asm"')
    parser.add_argument('-o', '--out', type=Path, metavar='out_path')
    arguments = parser.parse_args()

    mappings = parse_mappings(arguments.jit_symbol_map)
    traces = parse_traces(arguments.collapsed_traces)

    asm = {}
    asm_file_path: Path | None = arguments.asm
    if asm_file_path:
        if len(asm_file_path.parts) <= 0:
            asm_file_path = Path(str(arguments.jit_symbol_map) + '.asm')
            if asm_file_path.exists():
                asm = parse_asm(asm_file_path)
        else:
            asm = parse_asm(asm_file_path)

    merged_traces: dict[str, int] = {}
    for stack, count in traces:
        for i, function in enumerate(stack):
            if not function.startswith('0x'):
                continue

            address = int(function, base=16)
            mapping = mappings[bisect_right(mappings, address, key=mapping_address_key) - 1]
            if address < mapping[0] or address >= mapping[1]:
                continue

            stack[i] = f'lp`{mapping[2]}@{mapping[0]:x}'

            symbol = (mapping[0], mapping[2])
            if symbol in asm:
                instructions = asm[symbol]
                instruction_address = address - symbol[0]
                index = bisect_left(instructions, instruction_address, key=instruction_address_key)
                if index < len(instructions) and instructions[index].address == instruction_address:
                    instructions[index].samples += count

        stack_key = ';'.join(stack)
        if stack_key in merged_traces:
            merged_traces[stack_key] += count
        else:
            merged_traces[stack_key] = count

    out_file_path: Path | None = arguments.out
    if not out_file_path:
        out_file_path = arguments.collapsed_traces.with_stem(f'{arguments.collapsed_traces.stem}_mapped')
    with open(out_file_path, 'w') as out:
        for t, c in merged_traces.items():
            print(f'{t} {c}', file=out)

    if asm:
        annotated_asm_file_path = asm_file_path.with_stem(f'{asm_file_path.stem}_annotated')
        with open(annotated_asm_file_path, 'w') as out:
            for symbol, instructions in asm.items():
                print(f'{symbol[1]}: ;{symbol[0]:x}', file=out)
                for instruction in instructions:
                    print(f'\t{instruction.assembly}', end='', file=out)
                    if instruction.samples:
                        print(f' ;s {instruction.samples}', file=out)
                    else:
                        print(file=out)
                print(file=out)

if __name__ == '__main__':
    main()
