#!/usr/bin/env python3
#
# Copyright © 2021 Intel Corporation
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the
# "Software"), to deal in the Software without restriction, including
# without limitation the rights to use, copy, modify, merge, publish,
# distribute, sub license, and/or sell copies of the Software, and to
# permit persons to whom the Software is furnished to do so, subject to
# the following conditions:
#
# The above copyright notice and this permission notice (including the
# next paragraph) shall be included in all copies or substantial portions
# of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
# OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
# IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
# ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
# TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
# SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

import argparse
from mako.template import Template
import os
import subprocess
import tempfile

INPUT_PATHS = [
    'src/compiler/nir/nir.h',
    'src/intel/isl',
]

TEMPLATE_DOXYFILE = Template("""
# Doxyfile 1.9.1
DOXYFILE_ENCODING = UTF-8
PROJECT_NAME = "Mesa"

INPUT = ${' '.join(input_files)}
XML_OUTPUT = ${output_xml}

# Only generate XML
GENERATE_HTML = NO
GENERATE_LATEX = NO
GENERATE_XML = YES

# Add aliases for easily writing reStructuredText in comments
ALIASES  = "rst=\\verbatim embed:rst:leading-asterisk"
ALIASES += "endrst=\endverbatim"

ENABLE_PREPROCESSING = YES
MACRO_EXPANSION = YES
EXPAND_ONLY_PREDEF = YES

# Defines required to keep doxygen from tripping on our attribute macros
PREDEFINED  = PACKED=
PREDEFINED += ATTRIBUTE_CONST=
""")

def run_doxygen(output_path, input_paths=[]):
    doxyfile = tempfile.NamedTemporaryFile(mode='w', delete=False)
    try:
        doxyfile.write(TEMPLATE_DOXYFILE.render(
            input_files=[ os.path.abspath(i) for i in input_paths ],
            output_xml=os.path.abspath(output_path),
        ))
        doxyfile.close()

        subprocess.run(['doxygen', doxyfile.name])

    finally:
        doxyfile.close()
        os.unlink(doxyfile.name)

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--out-dir',
                        help='Output XML directory.',
                        required=True)
    args = parser.parse_args()

    this_dir = os.path.dirname(os.path.abspath(__file__))
    mesa_dir = os.path.join(this_dir, '..')
    def fixpath(p):
        if os.path.isabs(p):
            return p
        return os.path.join(mesa_dir, p)

    input_paths = [ fixpath(p) for p in INPUT_PATHS ]

    run_doxygen(args.out_dir, input_paths)
