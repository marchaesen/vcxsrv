# Copyright (C) 2014-2018 Intel Corporation.   All Rights Reserved.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.

# Python source
from __future__ import print_function
import os
import sys
import knob_defs
from gen_common import *

def main(args=sys.argv[1:]):

    # parse args
    parser = ArgumentParser()
    parser.add_argument("--output", "-o", help="Path to output file", required=True)
    parser.add_argument("--gen_h", "-gen_h", help="Generate gen_knobs.h", action="store_true", default=False)
    parser.add_argument("--gen_cpp", "-gen_cpp", help="Generate gen_knobs.cpp", action="store_true", required=False)

    args = parser.parse_args()

    cur_dir = os.path.dirname(os.path.abspath(__file__))
    template_cpp = os.path.join(cur_dir, 'templates', 'gen_knobs.cpp')
    template_h = os.path.join(cur_dir, 'templates', 'gen_knobs.h')

    output_filename = os.path.basename(args.output)
    output_dir = MakeTmpDir('_codegen')

    output_file = os.path.join(output_dir, output_filename)

    rval = 0

    try:
        if args.gen_h:
            MakoTemplateWriter.to_file(
                template_h,
                output_file,
                cmdline=sys.argv,
                filename='gen_knobs',
                knobs=knob_defs.KNOBS)

        if args.gen_cpp:
            MakoTemplateWriter.to_file(
                template_cpp,
                output_file,
                cmdline=sys.argv,
                filename='gen_knobs',
                knobs=knob_defs.KNOBS,
                includes=['core/knobs_init.h', 'common/os.h', 'sstream', 'iomanip'])

        rval = CopyFileIfDifferent(output_file, args.output)

    except:
        rval = 1

    finally:
        # ignore errors from delete of tmp directory
        DeleteDirTree(output_dir)

    return 0

if __name__ == '__main__':
    sys.exit(main())

