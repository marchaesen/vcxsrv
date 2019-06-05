#
# Copyright 2017-2019 Advanced Micro Devices, Inc.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# on the rights to use, copy, modify, merge, publish, distribute, sub
# license, and/or sell copies of the Software, and to permit persons to whom
# the Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
# THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
# DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
# OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
# USE OR OTHER DEALINGS IN THE SOFTWARE.
#
"""
Helper script to merge register database JSON files.

  usage: python3 mergedbs.py [FILES...]

Will merge the given JSON files and output the result on stdout.
"""

from __future__ import absolute_import, division, print_function, unicode_literals

from collections import defaultdict
import json
import re
import sys

from regdb import RegisterDatabase, deduplicate_enums, deduplicate_register_types

def main():
    regdb = RegisterDatabase()
    for filename in sys.argv[1:]:
        with open(filename, 'r') as filp:
            regdb.update(RegisterDatabase.from_json(json.load(filp)))

    deduplicate_enums(regdb)
    deduplicate_register_types(regdb)

    print(regdb.encode_json_pretty())


if __name__ == '__main__':
    main()

# kate: space-indent on; indent-width 4; replace-tabs on;
