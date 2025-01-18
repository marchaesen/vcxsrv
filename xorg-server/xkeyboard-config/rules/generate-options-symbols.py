#!/usr/bin/env python3
#
# This file is formatted with python black
#
# This file parses the base.xml and base.extras.xml file and prints out the option->symbols
# mapping compatible with the rules format. See the meson.build file for how this is used.

from __future__ import annotations
import argparse
from enum import unique
import sys
import xml.etree.ElementTree as ET
from typing import Generator, Iterable
from dataclasses import dataclass
from pathlib import Path

try:
    # Available from Python 3.11
    from enum import StrEnum
except ImportError:
    # Fallback to external package
    from strenum import StrEnum


def error(msg):
    print(f"ERROR: {msg}", file=sys.stderr)
    print("Aborting now")
    sys.exit(1)


@unique
class Section(StrEnum):
    """
    XKB sections.
    Name correspond to the header (`xkb_XXX`), value to the subdir/rules header.
    """

    keycodes = "keycodes"
    compatibility = "compat"
    geometry = "geometry"
    symbols = "symbols"
    types = "types"

    @classmethod
    def parse(cls, raw: str) -> Section:
        # Note: in order to display a nice message, argparse requires the error
        # to be one of: ArgumentTypeError, TypeError, or ValueError
        # See: https://docs.python.org/3/library/argparse.html#type
        try:
            return cls[raw]
        except KeyError:
            raise ValueError(raw)


@dataclass
class Directive:
    option: Option
    filename: str
    section: str

    @property
    def name(self) -> str:
        return self.option.name

    def __str__(self) -> str:
        return f"{self.filename}({self.section})"


@dataclass
class DirectiveSet:
    option: Option
    keycodes: Directive | None
    compatibility: Directive | None
    geometry: Directive | None
    symbols: Directive | None
    types: Directive | None

    @property
    def is_empty(self) -> bool:
        return all(
            x is None
            for x in (
                self.keycodes,
                self.compatibility,
                self.geometry,
                self.symbols,
                self.types,
            )
        )


@dataclass
class Option:
    """
    Wrapper around a single option -> symbols rules file entry. Has the properties
    name and directive where the directive consists of the XKB symbols file name
    and corresponding section, usually composed in the rules file as:
        name = +directive
    """

    name: str

    def __lt__(self, other) -> bool:
        return self.name < other.name

    @property
    def directive(self) -> Directive:
        f, s = self.name.split(":")
        return Directive(self, f, s)


def resolve_option(xkb_root: Path, option: Option) -> DirectiveSet:
    directives: dict[Section, Directive | None] = {s: None for s in Section}
    directive = option.directive
    filename, section_name = directive.filename, directive.section
    for section in Section:
        subdir = xkb_root / section
        if not (subdir / filename).exists():
            # Some of our foo:bar entries map to a baz_vndr/foo file
            for vndr in subdir.glob("*_vndr"):
                vndr_path = vndr / filename
                if vndr_path.exists():
                    filename = vndr_path.relative_to(subdir).as_posix()
                    break
            else:
                continue

        if (subdir / filename).is_symlink():
            resolved_filename = (subdir / filename).resolve().name
            assert (subdir / filename).exists()
        else:
            resolved_filename = filename

        # Now check if the target file actually has that section
        f = subdir / resolved_filename
        with f.open("rt", encoding="utf-8") as fd:
            section_header = f'xkb_{section.name} "{section_name}"'
            if any(section_header in line for line in fd):
                directives[section] = Directive(option, resolved_filename, section_name)

    return DirectiveSet(
        option=option,
        keycodes=directives[Section.keycodes],
        compatibility=directives[Section.compatibility],
        geometry=directives[Section.geometry],
        symbols=directives[Section.symbols],
        types=directives[Section.types],
    )


def options(rules_xml: Path) -> Iterable[Option]:
    """
    Yields all Options from the given XML file
    """
    tree = ET.parse(rules_xml)
    root = tree.getroot()

    def fetch_subelement(parent, name):
        sub_element = parent.findall(name)
        if sub_element is not None and len(sub_element) == 1:
            return sub_element[0]
        return None

    def fetch_text(parent, name):
        sub_element = fetch_subelement(parent, name)
        if sub_element is None:
            return None
        return sub_element.text

    def fetch_name(elem):
        try:
            ci_element = (
                elem
                if elem.tag == "configItem"
                else fetch_subelement(elem, "configItem")
            )
            name = fetch_text(ci_element, "name")
            assert name is not None
            return name
        except AssertionError as e:
            endl = "\n"  # f{} cannot contain backslashes
            e.args = (
                f"\nFor element {ET.tostring(elem).decode('utf-8')}\n{endl.join(e.args)}",
            )
            raise

    for option in root.iter("option"):
        yield Option(fetch_name(option))


def find_options_to_skip(xkb_root: Path) -> Generator[str, None, None]:
    """
    Find options to skip

    Theses are the “option” rules defined explicitly in partial rules files *.part
    """
    rules_dir = xkb_root / "rules"
    for f in rules_dir.glob("*.part"):
        filename = f.stem
        if "option" not in filename:
            # Skip files that do not match an option
            continue
        option_index = None
        # Parse rule file to get options to skip
        with f.open("rt", encoding="utf-8") as fp:
            for line in fp:
                if line.startswith("//") or "=" not in line:
                    continue
                elif line.startswith("!"):
                    # Header
                    if option_index is not None or "$" in line:
                        # Index already defined or definition of an alias
                        continue
                    else:
                        option_index = line.split()[1:].index("option")
                    continue
                else:
                    assert option_index is not None
                    yield line.split()[option_index]


def main():
    parser = argparse.ArgumentParser(description="Generate the evdev keycode lists.")
    parser.add_argument(
        "--xkb-config-root",
        help="The XKB base directory",
        default=Path("."),
        type=Path,
    )
    parser.add_argument(
        "--rules-section",
        type=Section.parse,
        choices=(Section.symbols, Section.compatibility, Section.types),
        help="The rules section to generate",
        default=Section.symbols,
    )
    parser.add_argument(
        "files", nargs="+", help="The base.xml and base.extras.xml files", type=Path
    )
    ns = parser.parse_args()
    rules_section: Section = ns.rules_section

    all_options = (opt for f in ns.files for opt in options(f))

    skip = frozenset(find_options_to_skip(ns.xkb_config_root))

    directives = (
        resolve_option(ns.xkb_config_root, o)
        for o in sorted(all_options)
        if o.name not in skip and not o.name.startswith("custom:")
    )

    def check_and_map(directive: DirectiveSet) -> Directive:
        assert (
            not directive.is_empty
        ), f"Option {directive.option} does not resolve to any section"

        return getattr(directive, rules_section.name)

    filtered = filter(
        lambda y: y is not None,
        map(check_and_map, directives),
    )

    print(f"! option                         = {rules_section}")
    for d in filtered:
        assert d is not None
        print(f"  {d.name:30s} = +{d}")

    if rules_section is Section.types:
        print(f"  {'custom:types':30s} = +custom")


if __name__ == "__main__":
    main()
