#
# Copyright 2019 Advanced Micro Devices, Inc.
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
Helper script that was used during the generation of the JSON data.

  usage: python3 canonicalize.py FILE

Reads the register database from FILE, performs canonicalization
(de-duplication of enums and register types, implicitly sorting JSON by name)
and attempts to deduce missing register types.

Notes about deduced register types as well as the output JSON are printed on
stdout.
"""

from __future__ import absolute_import, division, print_function, unicode_literals

from collections import defaultdict
import json
import re
import sys

from regdb import RegisterDatabase, deduplicate_enums, deduplicate_register_types

RE_number = re.compile('[0-9]+')

def deduce_missing_register_types(regdb):
    """
    This is a heuristic for filling in missing register types based on
    sequentially named registers.
    """
    buckets = defaultdict(list)
    for regmap in regdb.register_mappings():
        buckets[RE_number.sub('0', regmap.name)].append(regmap)

    for bucket in buckets.values():
        if len(bucket) <= 1:
            continue

        regtypenames = set(
            regmap.type_ref for regmap in bucket if hasattr(regmap, 'type_ref')
        )
        if len(regtypenames) == 1:
            regtypename = regtypenames.pop()
            for regmap in bucket:
                if not hasattr(regmap, 'type_ref'):
                    print('Deducing {0} -> {1}'.format(regmap.name, regtypename), file=sys.stderr)
                regmap.type_ref = regtypename


def json_canonicalize(filp, chips = None):
    regdb = RegisterDatabase.from_json(json.load(filp))

    if chips is not None:
        for regmap in regdb.register_mappings():
            assert not hasattr(regmap, 'chips')
            regmap.chips = [chips]

    deduplicate_enums(regdb)
    deduplicate_register_types(regdb)
    deduce_missing_register_types(regdb)
    regdb.garbage_collect()

    return regdb.encode_json_pretty()


def main():
    print(json_canonicalize(open(sys.argv[1], 'r'), sys.argv[2]))

if __name__ == '__main__':
    main()

# kate: space-indent on; indent-width 4; replace-tabs on;
