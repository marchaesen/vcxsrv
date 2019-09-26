#
# Copyright 2017 Advanced Micro Devices, Inc.
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
Script that generates the mapping from Vulkan VK_FORMAT_xxx to gfx10
IMG_FORMAT_xxx enums.
"""

from __future__ import absolute_import, division, print_function, unicode_literals

import json
import mako.template
import os
import re
import sys

AMD_REGISTERS = os.path.abspath(os.path.join(os.path.dirname(sys.argv[0]), "../registers"))
sys.path.extend([AMD_REGISTERS])

from regdb import Object, RegisterDatabase
from vk_format_parse import *

# ----------------------------------------------------------------------------
# Hard-coded mappings

def hardcoded_format(hw_enum):
    return Object(img_format=hw_enum, flags=[])

HARDCODED = {
    'VK_FORMAT_E5B9G9R9_UFLOAT_PACK32': hardcoded_format('5_9_9_9_FLOAT'),
    'VK_FORMAT_B10G11R11_UFLOAT_PACK32': hardcoded_format('10_11_11_FLOAT'), # NOTE: full set of int/unorm/etc. exists

    # BC
    'VK_FORMAT_BC1_RGB_UNORM_BLOCK': hardcoded_format('BC1_UNORM'),
    'VK_FORMAT_BC1_RGBA_UNORM_BLOCK': hardcoded_format('BC1_UNORM'),
    'VK_FORMAT_BC1_RGB_SRGB_BLOCK': hardcoded_format('BC1_SRGB'),
    'VK_FORMAT_BC1_RGBA_SRGB_BLOCK': hardcoded_format('BC1_SRGB'),
    'VK_FORMAT_BC2_UNORM_BLOCK': hardcoded_format('BC2_UNORM'),
    'VK_FORMAT_BC2_SRGB_BLOCK': hardcoded_format('BC2_SRGB'),
    'VK_FORMAT_BC3_UNORM_BLOCK': hardcoded_format('BC3_UNORM'),
    'VK_FORMAT_BC3_SRGB_BLOCK': hardcoded_format('BC3_SRGB'),
    'VK_FORMAT_BC4_UNORM_BLOCK': hardcoded_format('BC4_UNORM'),
    'VK_FORMAT_BC4_SNORM_BLOCK': hardcoded_format('BC4_SNORM'),
    'VK_FORMAT_BC5_UNORM_BLOCK': hardcoded_format('BC5_UNORM'),
    'VK_FORMAT_BC5_SNORM_BLOCK': hardcoded_format('BC5_SNORM'),
    'VK_FORMAT_BC6H_UFLOAT_BLOCK': hardcoded_format('BC6_UFLOAT'),
    'VK_FORMAT_BC6H_SFLOAT_BLOCK': hardcoded_format('BC6_SFLOAT'),
    'VK_FORMAT_BC7_UNORM_BLOCK': hardcoded_format('BC7_UNORM'),
    'VK_FORMAT_BC7_SRGB_BLOCK': hardcoded_format('BC7_SRGB'),

    # DS
    'VK_FORMAT_D16_UNORM_S8_UINT': hardcoded_format('INVALID'),
    'VK_FORMAT_D24_UNORM_S8_UINT': hardcoded_format('8_24_UNORM'),
    'VK_FORMAT_D32_SFLOAT_S8_UINT': hardcoded_format('X24_8_32_FLOAT'),
}


# ----------------------------------------------------------------------------
# Main script

header_template = mako.template.Template("""\
// DO NOT EDIT -- AUTOMATICALLY GENERATED

#define FMT(_img_format, ...) \
   { .img_format = V_008F0C_IMG_FORMAT_##_img_format, \
     ##__VA_ARGS__ }

static const struct gfx10_format gfx10_format_table[VK_FORMAT_RANGE_SIZE] = {
% for vk_format, args in formats:
 % if args is not None:
  [${vk_format}] = FMT(${args}),
 % else:
/* ${vk_format} is not supported */
 % endif
% endfor
};
""")

class Gfx10Format(object):
    RE_plain_channel = re.compile(r'X?([0-9]+)')

    def __init__(self, enum_entry):
        self.img_format = enum_entry.name[11:]
        self.flags = getattr(enum_entry, 'flags', [])

        code = self.img_format.split('_')

        self.plain_chan_sizes = []
        for i, chan_code in enumerate(code):
            m = self.RE_plain_channel.match(chan_code)
            if m is None:
                break
            self.plain_chan_sizes.append(int(m.group(1)))
        # Keep the bit sizes in little-endian order
        self.plain_chan_sizes.reverse()

        self.code = code[i:]


class Gfx10FormatMapping(object):
    def __init__(self, vk_formats, gfx10_formats):
        self.vk_formats = vk_formats
        self.gfx10_formats = gfx10_formats

        self.plain_gfx10_formats = dict(
            (tuple(['_'.join(fmt.code)] + fmt.plain_chan_sizes), fmt)
            for fmt in gfx10_formats if fmt.plain_chan_sizes
        )

    def map(self, fmt):
        if fmt.layout == PLAIN:
            chan_type = set([chan.type for chan in fmt.le_channels if chan.type != VOID])
            chan_norm = set([chan.norm for chan in fmt.le_channels if chan.type != VOID])
            chan_pure = set([chan.pure for chan in fmt.le_channels if chan.type != VOID])
            if len(chan_type) > 1 or len(chan_norm) > 1 or len(chan_pure) > 1:
                print(('Format {fmt.name} has inconsistent channel types: ' +
                        '{chan_type} {chan_norm} {chan_pure}')
                      .format(**locals()),
                      file=sys.stderr)
                return None

            chan_type = chan_type.pop()
            chan_norm = chan_norm.pop()
            chan_pure = chan_pure.pop()
            chan_sizes = [chan.size for chan in fmt.le_channels if chan.size != 0]

            extra_flags = []

            if fmt.colorspace == SRGB:
                assert chan_type == UNSIGNED and chan_norm
                num_format = 'SRGB'
            else:
                if chan_type == UNSIGNED:
                    if chan_pure:
                        num_format = 'UINT'
                    elif chan_sizes[0] == 32:
                        # Shader-based work-around for 32-bit non-pure-integer
                        num_format = 'UINT'
                        extra_flags.append('buffers_only')
                    elif chan_norm:
                        num_format = 'UNORM'
                    else:
                        num_format = 'USCALED'
                elif chan_type == SIGNED:
                    if chan_pure:
                        num_format = 'SINT'
                    elif chan_sizes[0] == 32:
                        # Shader-based work-around for 32-bit non-pure-integer
                        num_format = 'SINT'
                        extra_flags.append('buffers_only')
                    elif chan_norm:
                        num_format = 'SNORM'
                    else:
                        num_format = 'SSCALED'
                elif chan_type == FLOAT:
                    num_format = 'FLOAT'

                    if chan_sizes[0] == 64:
                        # Shader-based work-around for doubles
                        if len(chan_sizes) % 2 == 1:
                            # 1 or 3 loads for 1 or 3 double channels
                            chan_sizes = [32, 32]
                        else:
                            # 1 or 2 loads for 2 or 4 double channels
                            chan_sizes = [32, 32, 32, 32]
                        extra_flags.append('buffers_only')
                else:
                    # Shader-based work-around
                    assert chan_type == FIXED
                    assert chan_sizes[0] == 32
                    num_format = 'SINT'
                    extra_flags.append('buffers_only')

            # These are not supported as render targets, so we don't support
            # them as images either.
            if (len(chan_sizes) == 3 and chan_sizes[0] in (8, 16, 32) and
                chan_sizes[0] == chan_sizes[1]):
                extra_flags.append('buffers_only')
                if chan_sizes[0] in (8, 16):
                    # Shader-based work-around: one load per channel
                    chan_sizes = [chan_sizes[0]]

            # Don't expose SRGB buffer formats
            if 'buffers_only' in extra_flags and fmt.colorspace == SRGB:
                return None

            # Don't support 4_4 because it's not supported as render targets
            # and it's useless in other cases.
            if len(chan_sizes) == 2 and chan_sizes[0] == 4:
                return None

            key = tuple([num_format] + chan_sizes)
            if key not in self.plain_gfx10_formats:
                return None

            gfx10_fmt = self.plain_gfx10_formats[key]
            return Object(
                img_format=gfx10_fmt.img_format,
                flags=gfx10_fmt.flags + extra_flags,
            )

        return None


if __name__ == '__main__':
    vk_formats = parse(sys.argv[1])

    with open(sys.argv[2], 'r') as filp:
        db = RegisterDatabase.from_json(json.load(filp))

    gfx10_formats = [Gfx10Format(entry) for entry in db.enum('IMG_FORMAT').entries]

    mapping = Gfx10FormatMapping(vk_formats, gfx10_formats)

    formats = []
    for fmt in vk_formats:
        if fmt.name in HARDCODED:
            obj = HARDCODED[fmt.name]
        else:
            obj = mapping.map(fmt)

        if obj is not None:
            args = obj.img_format
            if 'buffers_only' in obj.flags:
                args += ', .buffers_only = 1'
        else:
            args = None
        formats.append((fmt.name, args))

    print(header_template.render(formats=formats))
