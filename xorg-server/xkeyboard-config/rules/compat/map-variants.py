#!/usr/bin/env python3
# SPDX-License-Identifier: MIT

from __future__ import annotations

import argparse
import functools
import itertools
import re
import sys
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
from string import Template
from typing import ClassVar, Generator

ROOT = Path(__file__).parent.parent.parent
RULES = ROOT / "rules"
SYMBOLS = ROOT / "symbols"
# Some checks in case we move this script
assert RULES.is_dir()
assert SYMBOLS.is_dir()


@functools.total_ordering
@dataclass(frozen=True, order=False)
class Layout:
    PATTERN: ClassVar[re.Pattern[str]] = re.compile(
        r"(?P<layout>[^(]+)\((?P<variant>[^)]+)\)"
    )

    layout: str
    variant: str

    @classmethod
    def parse(cls, layout: str, variant: str = ""):
        # parse a layout(variant) string
        if match := cls.PATTERN.match(layout):
            assert not variant
            layout = match.group("layout")
            variant = match.group("variant")
        return cls(layout, variant)

    def __str__(self):
        if self.variant:
            return "{}({})".format(self.layout, self.variant)
        else:
            return "{}".format(self.layout)

    def __lt__(self, other):
        """
        Custom compare function in order to deal with missing variant.
        """
        if not isinstance(other, self.__class__):
            return NotImplemented
        elif self.layout == other.layout:
            if self.variant == other.variant:
                return False
            # Handle missing variant
            elif self.variant and (not other.variant or other.variant == "*"):
                return True
            # Handle missing variant
            elif (not self.variant or self.variant == "*") and other.variant:
                return False
            else:
                return self.variant < other.variant
        else:
            return self.layout < other.layout

    @classmethod
    def read_file(cls, path: str) -> Generator[tuple[Layout, Layout], None, None]:
        """Returns a list of two-layout tuples [(layout1, layout2), ...]"""

        with open(path, "rt", encoding="utf-8") as fd:
            for line in fd:
                # Remove optional comment
                line = line.split("//")[0]
                # Split on whitespaces
                groups = tuple(line.split())
                length = len(groups)
                if length == 2:
                    l1 = Layout.parse(groups[0])
                    l2 = Layout.parse(groups[1])
                elif length == 4:
                    l1 = Layout.parse(groups[0], groups[1])
                    l2 = Layout.parse(groups[2], groups[3])
                else:
                    raise ValueError(f"Invalid line: {line}")
                yield (l1, l2)


def write_rules(
    dest,
    mappings: list[tuple[Layout, Layout]],
    number: int,
    expect_variant: bool,
    write_header: bool,
):
    index = f"[{number}]" if number > 0 else ""
    if write_header:
        variant = f"\t\tvariant{index}" if expect_variant else "\t\t"
        dest.write(f"! model		layout{index}{variant}	=	symbols\n")

    # symbols is
    #   +layout(variant):2
    # and where the number is 1, we have a base and drop the suffix, i.e.
    # the above becomes
    #   pc+layout(variant)
    # This part is only executed for the variantMappings.lst

    base = "pc" if number <= 1 else ""
    suffix = "" if number <= 1 else ":{}".format(number)

    for l1, l2 in mappings:
        if expect_variant ^ bool(l1.variant):
            expectation = "Expected" if expect_variant else "Unexpected"
            raise ValueError(f"{expectation} variant: {l1}")
        variant = f"\t\t{l1.variant}" if expect_variant else "\t\t"
        if l2.variant == "*":
            second_layout = l2.layout
        else:
            second_layout = str(l2) if l2.variant else f"{l2.layout}%(v{index})"
        dest.write(
            "  *		{}{}	=	{}+{}{}\n".format(
                l1.layout, variant, base, second_layout, suffix
            )
        )


SYMBOLS_TEMPLATE = Template("""
// Compatibility mapping
partial xkb_symbols "${alias}" {
    include "${target}"
};
""")


def write_symbols(
    dest: Path,
    mappings: list[tuple[Layout, Layout]],
    expect_variant: bool,
    dry_run: bool,
):
    """
    Append xkb_symbols entries
    """
    # Group by alias symbol file
    files = defaultdict(list)
    for mapping in mappings:
        files[mapping[0].layout].append(mapping)

    for filename, mappings in files.items():
        src_path: Path = SYMBOLS / filename
        # Get original file content
        with src_path.open("rt", encoding="utf-8") as fd:
            content = fd.read()
        # Check that there is no clash with existing sections
        new_sections = "|".join(re.escape(l1.variant) for l1, _ in mappings)
        pattern = re.compile(rf'xkb_symbols\s+"(?P<section>{new_sections})"')
        for line_number, line in enumerate(content.splitlines(), start=1):
            # Drop comments
            line = line.split("//")[0]
            # Check for clashing section definition
            if m := pattern.search(line):
                l1 = mappings[0][0]
                section = m.group("section")
                raise ValueError(
                    f'Cannot add compatibility section in symbols/{l1.layout}: "{section}" already exists at line {line_number}'
                )
        # Add compat sections
        dest_path = dest / filename
        # Print path to stdout, for path collection in meson
        print(dest_path.name)
        if dry_run:
            continue
        with dest_path.open("wt", encoding="utf-8") as fd:
            fd.write(content)
            for l1, l2 in mappings:
                if expect_variant ^ bool(l1.variant):
                    expectation = "Expected" if expect_variant else "Unexpected"
                    raise ValueError(f"{expectation} variant: {l1}")
                content = SYMBOLS_TEMPLATE.substitute(alias=l1.variant, target=l2)
                fd.write(content)


def check_symbols(filename: str) -> bool:
    """
    Check if symbols file exists
    """
    return (SYMBOLS / filename).is_file()


def check_mapping(symbols: bool, mapping: tuple[Layout, Layout]) -> bool:
    """
    Check whether a mapping should be kept
    """
    return symbols == check_symbols(mapping[0].layout)


def run(
    dest: str, files, dry_run: bool, symbols: bool, expect_variant: bool, number: int
):
    check = functools.partial(check_mapping, symbols)
    # Read all files and sort their entry on the aliased layout
    mappings = sorted(
        filter(check, itertools.chain.from_iterable(map(Layout.read_file, files))),
        key=lambda ls: ls[0],
    )
    if symbols:
        write_symbols(Path(dest), mappings, expect_variant, dry_run)
    elif not dry_run:
        fd = None
        if dest == "-":
            fd = sys.stdout
        with fd or open(ns.dest, "w") as fd:
            write_rules(fd, mappings, number, expect_variant, True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser("variant mapping script")
    parser.add_argument("--number", type=int, default=0)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--has-variant", action="store_true")
    parser.add_argument("--symbols", action="store_true", help="Write symbols files")

    parser.add_argument("dest", type=str)
    parser.add_argument("files", nargs="+", type=str)
    ns = parser.parse_args()

    run(ns.dest, ns.files, ns.dry_run, ns.symbols, ns.has_variant, ns.number)
