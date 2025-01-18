#!/usr/bin/env python3

"""
Query the XKB registry via xkbcommon and process the results, e.g. filter and
export (non-)Latin layouts.
"""

from __future__ import annotations

import argparse
import csv
import dataclasses
import logging
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Generator, Sequence

import yaml

# Add our internal xkbcommon test lib to the PATH
ROOT = Path(__file__).parent.parent
sys.path.append(str(ROOT / "tests"))

import xkbcommon

logger = logging.getLogger()
logging.basicConfig(
    stream=sys.stderr, level=logging.INFO, format="[%(levelname)s] %(message)s"
)


@dataclass
class Model:
    name: str


@dataclass(frozen=True, order=True)
class Layout:
    layout: str
    variant: str
    description: str
    extra: dict[str, Any]

    @classmethod
    def parse(cls, raw: dict[str, str]) -> Layout:
        """
        Parse YAML entry
        """
        return Layout(
            layout=raw.get("layout", ""),
            variant=raw.get("variant", ""),
            description=raw.get("description", ""),
            extra={},
        )


class Option:
    name: str


@dataclass
class Registry:
    """
    The XKB registry, i.e. a rules/*.xml file parsed.
    """

    models: tuple[Model, ...]
    layouts: tuple[Layout, ...]
    options: tuple[Option, ...]

    @classmethod
    def parse(cls, raw: dict[str, Sequence[Any]], skip_custom: bool = True) -> Registry:
        """
        Parse YAML entry
        """
        return cls(
            models=(),  # FIXME: process models
            layouts=tuple(
                l
                for l in map(Layout.parse, raw.get("layouts", ()))
                if not skip_custom or l.layout != "custom"
            ),
            options=(),  # FIXME: process options
        )

    @classmethod
    def load(cls, xkb_root: Path | None = None, rules: str | None = None) -> Registry:
        """
        Run xkbcli list and parse its YAML output.
        """
        args: tuple[str, ...] = ("xkbcli", "list", "--load-exotic")
        if xkb_root:
            # If no xkb config root is provided, we rely on the defaults that xkbcommon
            # will pick. It depends on its built-in defaults and on the environment.
            args += ("--skip-default-paths", str(xkb_root))
        if rules:
            # If no rules set is provided, we rely on the default one that xkbcommon
            # will pick. It depends on its built-in default and on the environment.
            args += ("--ruleset", rules)
        logger.info(f"Running: {' '.join(args)}")
        p = subprocess.run(args, encoding="utf-8", capture_output=True)
        raw = yaml.safe_load(p.stdout)
        return cls.parse(raw)


class Csv(csv.unix_dialect):
    """
    CSV dialect used to export results.
    """

    quoting = csv.QUOTE_NONE


LATIN_LETTERS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
"""
Required upper case characters for a layout to be considered Latin.
"""


def filter_latin_layouts(
    xkb_root: Path,
    registry: Registry,
    latin: bool,
    rules: str | None = None,
    debug: bool = False,
) -> Generator[Layout, None, None]:
    """
    Given a registry, filter all its layouts that are (non-)Latin by checking
    that each required characters are accessible at some key, group and level.
    """
    latin_letters = frozenset(LATIN_LETTERS + LATIN_LETTERS.lower())
    latin_keysyms: dict[int, str] = {
        xkbcommon.xkb_keysym_from_name(c): c for c in latin_letters
    }
    for layout in registry.layouts:
        try:
            with xkbcommon.ForeignKeymap(
                xkb_base=xkb_root,
                rules=rules,
                layout=layout.layout,
                variant=layout.variant,
            ) as keymap:
                found: set[str] = set()
                r: xkbcommon.KeyLevel
                for r in xkbcommon.ForeignKeymap.get_keys_levels(keymap):
                    for k in r.keysyms:
                        if (c := latin_keysyms.get(k)) is not None:
                            found.add(c)
        except ValueError as err:
            # Log error message and skip
            logger.error(err)
            continue
        missing = latin_letters.difference(found)
        if latin ^ bool(missing):
            if debug:
                # Add missing characters for debugging
                extra = dict(layout.extra)
                extra["missing"] = missing_str = "".join(sorted(missing))
                yield dataclasses.replace(layout, extra=extra)
            else:
                yield layout
        almost_latin = len(missing) / len(latin_letters) <= 0.10
        if debug and missing and almost_latin:
            logger.debug(
                f"Almost a Latin layout: {layout}; missing: {missing_str} ({len(missing)})"
            )


def process_layouts(xkb_root: Path, registry: Registry, args: argparse.Namespace):
    """
    Process layouts from a given registry, depending on the CLI arguments.
    """
    debug: bool = args.debug
    if args.latin or args.non_latin:
        # Filter (non-)Latin layouts
        layouts = tuple(
            filter_latin_layouts(
                xkb_root,
                registry,
                latin=args.latin,
                rules=args.rules,
                debug=args.debug,
            )
        )
    else:
        # Get all layouts
        layouts = registry.layouts
    if args.csv:
        # Output as CSV
        path: Path = args.csv
        with path.open("wt", encoding="utf-8", newline="") as fd:
            writer = csv.writer(fd, dialect=csv.unix_dialect if debug else Csv)
            fields: tuple[str, ...] = ("Layout", "Variant")
            if debug:
                fields += (
                    "Description",
                    "Missing Latin characters",
                )
            writer.writerow(fields)

            def get_fields(layout) -> Generator[str, None, None]:
                yield layout.layout
                yield layout.variant
                if debug:
                    yield layout.description
                    yield layout.extra.get("missing", "")

            for layout in sorted(layouts):
                writer.writerow(get_fields(layout))
    else:
        # Output as Python representation, for debugging
        for layout in layouts:
            print(layout)


def parse_cli_args() -> argparse.Namespace:
    """
    Create CLI parser and parse corresponding arguments.
    """
    parser = argparse.ArgumentParser()
    parser.add_argument("--xkb-root", type=Path, required=True)
    parser.add_argument("--debug", action="store_true")
    parser.add_argument("--rules", help="Rules set to use")
    subparsers = parser.add_subparsers(required=True)
    subparser = subparsers.add_parser("layouts", help="List layouts")
    subparser.set_defaults(run=process_layouts)
    subparser.add_argument("--csv", type=Path)
    group = subparser.add_mutually_exclusive_group()
    group.add_argument("--latin", action="store_true", help="List only Latin layouts")
    group.add_argument(
        "--non-latin", action="store_true", help="List only non-Latin layouts"
    )
    return parser.parse_args()


if __name__ == "__main__":
    args = parse_cli_args()
    if args.debug:
        logger.setLevel(logging.DEBUG)
    xkb_root: Path = args.xkb_root
    rules: str | None = args.rules
    registry = Registry.load(xkb_root, rules)
    args.run(xkb_root, registry, args)
