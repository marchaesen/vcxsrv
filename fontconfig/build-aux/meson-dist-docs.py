#! /usr/bin/env python3
# Copyright (C) 2024 fontconfig Authors
# SPDX-License-Identifier: HPND

import os
import shutil
from pathlib import Path

sourcedir = os.environ.get('MESON_SOURCE_ROOT')
builddir = os.environ.get('MESON_BUILD_ROOT')
distdir = os.environ.get('MESON_DIST_ROOT')

# Copy manpages
docdir = Path(distdir) / 'doc'
for f in (Path(builddir) / 'doc').glob('*.[135]'):
    print(f'Copying {f.name}')
    shutil.copy2(f, docdir)

# Copy config file
confdir = Path(distdir) / 'conf.d'
shutil.copy2(Path(builddir) / 'conf.d' / '35-lang-normalize.conf', confdir)

# Documentation
shutil.copy2(Path(builddir) / 'doc' / 'fontconfig-devel.html', docdir)
shutil.copy2(Path(builddir) / 'doc' / 'fontconfig-devel.pdf', docdir)
shutil.copy2(Path(builddir) / 'doc' / 'fontconfig-devel.txt', docdir)
shutil.copy2(Path(builddir) / 'doc' / 'fontconfig-user.html', docdir)
shutil.copy2(Path(builddir) / 'doc' / 'fontconfig-user.pdf', docdir)
shutil.copy2(Path(builddir) / 'doc' / 'fontconfig-user.txt', docdir)
