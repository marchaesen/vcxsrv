#!/usr/bin/env python3
# SPDX-License-Identifier: MIT

"""
Copy files from the source directory into the build directory.

Use to simplify running tests.
"""

import os
import shutil
import sys
from pathlib import Path

MESON_SOURCE_ROOT = Path(os.environ.get("MESON_SOURCE_ROOT", ""))
MESON_BUILD_ROOT = Path(os.environ.get("MESON_BUILD_ROOT", ""))
MESON_SUBDIR = os.environ.get("MESON_SUBDIR", "")

directory = Path(sys.argv[1])
excluded = tuple(str(directory / f) for f in sys.argv[2:])

source = MESON_SOURCE_ROOT / MESON_SUBDIR / directory
destination = MESON_BUILD_ROOT / MESON_SUBDIR / directory
destination.mkdir(parents=True, exist_ok=True)


# Callback for shutil.copytree
def ignore(directory: str, contents: list[str]) -> list[str]:
    return [f for f in contents if any(Path(directory, f).match(p) for p in excluded)]


shutil.copytree(source, destination, dirs_exist_ok=True, ignore=ignore)
