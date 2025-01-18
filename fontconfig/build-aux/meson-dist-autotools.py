#! /usr/bin/env python3
# Copyright (C) 2024 fontconfig Authors
# SPDX-License-Identifier: HPND

import os
import shutil
import subprocess
import sys
from pathlib import Path

sourcedir = os.environ.get('MESON_SOURCE_ROOT')
builddir = os.environ.get('MESON_BUILD_ROOT')
distdir = os.environ.get('MESON_DIST_ROOT')

if shutil.which('autoreconf'):
    print('no autoreconf installed', file=sys.stderr)

subprocess.run(['autoreconf', '-i'], cwd=distdir)

# Copy files for compatibility
for f in (Path(builddir) / 'doc').glob('*.1'):
    print(f'Copying {f.name}')
    shutil.copy2(f, Path(distdir) / f.stem)

# Remove autom4te.cache
shutil.rmtree(Path(distdir) / 'autom4te.cache')
