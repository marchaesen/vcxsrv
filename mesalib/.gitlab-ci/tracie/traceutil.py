# Copyright (c) 2019 Collabora Ltd
# Copyright © 2019-2020 Valve Corporation.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included
# in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
# OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
# OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
# ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
# OTHER DEALINGS IN THE SOFTWARE.
#
# SPDX-License-Identifier: MIT

import os
from pathlib import Path
from enum import Enum, auto

class TraceType(Enum):
    UNKNOWN = auto()
    APITRACE = auto()
    APITRACE_DXGI = auto()
    RENDERDOC = auto()
    GFXRECONSTRUCT = auto()
    TESTTRACE = auto()

_trace_type_info_map = {
    TraceType.APITRACE : ("apitrace", ".trace"),
    TraceType.APITRACE_DXGI : ("apitrace-dxgi", ".trace-dxgi"),
    TraceType.RENDERDOC : ("renderdoc", ".rdc"),
    TraceType.GFXRECONSTRUCT : ("gfxreconstruct", ".gfxr"),
    TraceType.TESTTRACE : ("testtrace", ".testtrace")
}

def all_trace_type_names():
    s = []
    for t,(name, ext) in _trace_type_info_map.items():
        if t != TraceType.UNKNOWN:
            s.append(name)
    return s

def trace_type_from_name(tt_name):
    for t,(name, ext) in _trace_type_info_map.items():
        if tt_name == name:
            return t

    return TraceType.UNKNOWN

def trace_type_from_filename(trace_file):
    for t,(name, ext) in _trace_type_info_map.items():
        if trace_file.endswith(ext):
            return t

    return TraceType.UNKNOWN
