#!/usr/bin/env python3
#
# Builds a tree view of a symbols file (showing all includes)
#
# This file is formatted with Python Black

import argparse
import dataclasses
import os
from pathlib import Path

from pyparsing import (
    And,
    LineEnd,
    Literal,
    OneOrMore,
    Optional,
    Or,
    ParseException,
    QuotedString,
    Regex,
    Word,
    alphanums,
    cppStyleComment,
    oneOf,
)

xkb_basedir = None


@dataclasses.dataclass
class XkbSymbols:
    file: Path  # Path to the file this section came from
    name: str
    includes: list[str] = dataclasses.field(default_factory=list)

    @property
    def layout(self) -> str:
        return self.file.name  # XKb - filename is the layout name

    def __str__(self):
        return f"{self.layout}({self.name}): {self.includes}"


class XkbLoader:
    """
    Wrapper class to avoid loading the same symbols file over and over
    again.
    """

    class XkbParserException(Exception):
        pass

    _instance = None

    def __init__(self, xkb_basedir: Path):
        self.xkb_basedir = xkb_basedir
        self.loaded: dict[Path, list[XkbSymbols]] = {}

    @classmethod
    def create(cls, xkb_basedir: Path):
        assert cls._instance is None
        cls._instance = XkbLoader(xkb_basedir)

    @classmethod
    def instance(cls):
        assert cls._instance is not None
        return cls._instance

    @classmethod
    def load_symbols(cls, file: Path):
        return cls.instance().load_symbols_file(file)

    def load_symbols_file(self, file: Path) -> list[XkbSymbols]:
        file = self.xkb_basedir / file
        try:
            return self.loaded[file]
        except KeyError:
            pass

        sections = []

        def quoted(name):
            return QuotedString(quoteChar='"', unquoteResults=True)

        # Callback, toks[0] is "foo" for xkb_symbols "foo"
        def new_symbols_section(name, loc, toks):
            assert len(toks) == 1
            sections.append(XkbSymbols(file, toks[0]))

        # Callback, toks[0] is "foo(bar)" for include "foo(bar)"
        def append_includes(name, loc, toks):
            assert len(toks) == 1
            sections[-1].includes.append(toks[0])

        EOL = LineEnd().suppress()
        SECTIONTYPE = (
            "default",
            "partial",
            "hidden",
            "alphanumeric_keys",
            "modifier_keys",
            "keypad_keys",
            "function_keys",
            "alternate_group",
        )
        NAME = quoted("name").setParseAction(new_symbols_section)
        INCLUDE = (
            lit("include") + quoted("include").setParseAction(append_includes) + EOL
        )
        # We only care about includes
        OTHERLINE = And([~lit("};"), ~lit("include") + Regex(".*")]) + EOL

        with open(file) as fd:
            types = OneOrMore(oneOf(SECTIONTYPE)).suppress()
            include_or_other = Or([INCLUDE, OTHERLINE.suppress()])
            section = (
                types
                + lit("xkb_symbols")
                + NAME
                + lit("{")
                + OneOrMore(include_or_other)
                + lit("};")
            )
            grammar = OneOrMore(section)
            grammar.ignore(cppStyleComment)
            try:
                grammar.parseFile(fd)
            except ParseException as e:
                raise XkbLoader.XkbParserException(str(e))

        self.loaded[file] = sections

        return sections


def lit(string):
    return Literal(string).suppress()


def print_section(
    root: Path, s: XkbSymbols, filter_section: str | None = None, indent=0
):
    if filter_section and s.name != filter_section:
        return

    layout = Word(alphanums + "_/").setResultsName("layout")
    variant = Optional(
        lit("(") + Word(alphanums + "_").setResultsName("variant") + lit(")")
    )
    grammar = layout + variant

    prefix = ""
    if indent > 0:
        prefix = " " * (indent - 2) + "|-> "
    print(f"{prefix}{s.file.relative_to(root)}({s.name})")
    for include in s.includes:
        result = grammar.parseString(include)
        # Should really find the "default" section but for this script
        # hardcoding "basic" is good enough
        layout, variant = result.layout, result.variant or "basic"

        include_sections = XkbLoader.load_symbols(layout)
        for include_section in include_sections:
            print_section(
                root, include_section, filter_section=variant, indent=indent + 4
            )


def list_sections(
    root: Path, sections: list[XkbSymbols], filter_section: str | None = None
):
    for section in sections:
        print_section(root, section, filter_section)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="""
            XKB symbol tree viewer.

            This tool takes a symbols file and optionally a section in that
            file and recursively walks the include directives in that section.
            The resulting tree may be useful for checking which files
            are affected when a single section is modified.
            """
    )
    parser.add_argument(
        "--xkb-root",
        type=Path,
        help="The XKB root directory",
    )
    parser.add_argument(
        "file",
        metavar="file-or-directory",
        type=Path,
        help="The XKB symbols file or directory",
    )
    parser.add_argument(
        "section", type=str, default=None, nargs="?", help="The section (optional)"
    )
    ns = parser.parse_args()
    if ns.xkb_root is not None:
        ns.file = ns.xkb_root / "symbols" / ns.file

    if ns.file.is_dir():
        if ns.xkb_root is None:
            xkb_basedir: Path = ns.file.resolve()
        else:
            xkb_basedir = (ns.xkb_root / "symbols").resolve()
        files: list[Path] = sorted(
            Path(d) / f for d, _, fs in os.walk(ns.file.resolve()) for f in fs
        )
    else:
        if ns.xkb_root is None:
            # Note: this requires that the file given on the cmdline is not one of
            # the sun_vdr/de or others inside a subdirectory. meh.
            xkb_basedir = ns.file.parent.resolve()
        else:
            xkb_basedir = (ns.xkb_root / "symbols").resolve()
        files = [ns.file]

    XkbLoader.create(xkb_basedir)

    try:
        for file in files:
            try:
                sections = XkbLoader.load_symbols(file.resolve())
                list_sections(xkb_basedir, sections, filter_section=ns.section)
            except XkbLoader.XkbParserException:
                pass
    except KeyboardInterrupt:
        pass
