#! /usr/bin/env python3
# Copyright (C) 2024 fontconfig Authors
# SPDX-License-Identifier: HPND

import os
import pytest
import re
import requests
import shutil
import subprocess
from pathlib import Path


def test_issue431(tmp_path):
    req = requests.get('https://github.com/googlefonts/roboto-flex/releases/download/3.100/roboto-flex-fonts.zip',
                       allow_redirects=True)
    with open(tmp_path / 'roboto-flex-fonts.zip', 'wb') as f:
        f.write(req.content)
    shutil.unpack_archive(tmp_path / 'roboto-flex-fonts.zip', tmp_path)
    builddir = Path(os.environ.get('builddir', Path(__file__).parent)).parent
    result = subprocess.run([builddir / 'fc-query' / 'fc-query', '-f', '%{family[0]}:%{index}:%{style[0]}:%{postscriptname}\n', tmp_path / 'roboto-flex-fonts/fonts/variable/RobotoFlex[GRAD,XOPQ,XTRA,YOPQ,YTAS,YTDE,YTFI,YTLC,YTUC,opsz,slnt,wdth,wght].ttf'], stdout=subprocess.PIPE)

    for line in result.stdout.decode('utf-8').splitlines():
        family, index, style, psname = line.split(':')
        normstyle = re.sub('[\x04\\(\\)/<>\\[\\]{}\t\f\r\n ]', '', style)
        assert psname.split('-')[-1] == normstyle, \
            f'postscriptname `{psname}\' does not contain style name `{normstyle}\': index {index}'
