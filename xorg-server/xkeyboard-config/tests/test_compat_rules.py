# SPDX-License-Identifier: MIT

from __future__ import annotations

import functools
import itertools
import os
import re
from dataclasses import dataclass
from pathlib import Path
from typing import ClassVar, Generator, Optional, Union, TYPE_CHECKING

if TYPE_CHECKING:
    import builtins

import pytest
import xkbcommon


@dataclass
class RMLVO:
    rules: str
    model: Optional[str]
    layout: Optional[str]
    variant: Optional[str]
    options: Optional[str]


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
            elif self.variant and not other.variant:
                return True
            # Handle missing variant
            elif not self.variant and other.variant:
                return False
            else:
                return self.variant < other.variant
        else:
            return self.layout < other.layout

    @classmethod
    def read_file(cls, path: Path) -> Generator[tuple[Layout, Layout], None, None]:
        """Returns a list of two-layout tuples [(layout1, layout2), ...]"""

        with path.open("rt", encoding="utf-8") as fd:
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


def pytest_generate_tests(metafunc: pytest.Metafunc):
    if "mapping" in metafunc.fixturenames:
        if files := metafunc.config.getoption("layout_compat_config"):
            # Read all files
            mappings = tuple(
                itertools.chain.from_iterable(map(Layout.read_file, files)),
            )
        else:
            mappings = ()
        metafunc.parametrize("mapping", mappings)


@pytest.fixture(scope="session")
def xkb_base():
    """Get the xkeyboard-config directory from the environment."""
    path = os.environ.get("XKB_CONFIG_ROOT")
    if path:
        return Path(path)
    else:
        raise ValueError("XKB_CONFIG_ROOT environment variable is not defined")


def compile_keymap(
    xkb_base: Path,
    rules: str,
    ls: tuple[Union[Layout, "builtins.ellipsis"], ...],
    layout: Layout,
) -> tuple[str, str, str]:
    lsʹ: tuple[Layout, ...] = tuple(layout if x is Ellipsis else x for x in ls)
    alias_layout = ",".join(l.layout for l in lsʹ)
    alias_variant = ",".join(l.variant for l in lsʹ)
    km = xkbcommon.ForeignKeymap(
        xkb_base=xkb_base, rules=rules, layout=alias_layout, variant=alias_variant
    )
    return alias_layout, alias_variant, km.as_string()


@pytest.mark.parametrize("rules", ("base", "evdev"))
def test_compat_layout(xkb_base: Path, rules: str, mapping: tuple[Layout, Layout]):
    alias = mapping[0]
    target = mapping[1]
    us = Layout("us", "")
    fr = Layout("fr", "")
    es = Layout("es", "")
    configs = (
        (...,),
        (us, ...),
        (..., us),
        (us, fr, ...),
        (us, fr, es, ...),
    )
    for ls in configs:
        # Compile alias
        alias_layout, alias_variant, alias_string = compile_keymap(
            xkb_base, rules, ls, alias
        )
        assert alias_string != "", (rules, alias_layout, alias_variant)

        # Compile target
        target_layout, target_variant, target_string = compile_keymap(
            xkb_base, rules, ls, target
        )
        assert target_string != "", (rules, target_layout, target_variant)

        # [HACK]: fix keycodes aliases
        if alias.layout == "de" and target.layout != "de":
            alias_string = alias_string.replace(
                "<LatZ>         = <AD06>", "<LatY>         = <AD06>"
            )
            alias_string = alias_string.replace(
                "<LatY>         = <AB01>", "<LatZ>         = <AB01>"
            )

        # Keymap obtain using alias should be the same as if using its target directly
        assert alias_string == target_string, (
            rules,
            alias_layout,
            alias_variant,
            target_layout,
            target_variant,
        )
