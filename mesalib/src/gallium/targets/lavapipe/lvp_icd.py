# Copyright 2017 Intel Corporation
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

import json
import os.path

import argparse

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--out', help='Output json file.', required=True)
    parser.add_argument('--lib-path', help='Path to libvulkan_lvp.*')
    parser.add_argument('--suffix', help='Extension of libvulkan_lvp.*')
    args = parser.parse_args()

    path = 'libvulkan_lvp.' + args.suffix
    if args.lib_path:
        path = os.path.join(args.lib_path, path)

    json_data = {
        'file_format_version': '1.0.0',
        'ICD': {
            'library_path': path,
            'api_version': str('1.1.107'),
        },
    }

    with open(args.out, 'w') as f:
        json.dump(json_data, f, indent = 4, sort_keys=True, separators=(',', ': '))
