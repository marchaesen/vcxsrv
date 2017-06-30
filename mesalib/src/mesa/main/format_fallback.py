COPYRIGHT = """\
/*
 * Copyright 2017 Google
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
"""

# stdlib
import argparse
from sys import stdout
from mako.template import Template

# local
import format_parser

def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument("csv")
    p.add_argument("out")
    return p.parse_args()

def get_rgbx_to_rgba_map(formats):
    names = {fmt.name for fmt in formats}

    for fmt in formats:
        if not fmt.has_channel('r') or not fmt.has_channel('x'):
            continue

        # The condition above will still let MESA_FORMAT_R9G9B9E5_FLOAT
        # through.  We need to ensure it actually has an X in the name.
        if not 'X' in fmt.name:
            continue

        rgbx_name = fmt.name
        rgba_name = rgbx_name.replace("X", "A")
        if rgba_name not in names:
            continue;

        yield rgbx_name, rgba_name

TEMPLATE = Template(COPYRIGHT + """
#include "formats.h"

/**
 * If the format has an alpha channel, and there exists a non-alpha
 * variant of the format with an identical bit layout, then return
 * the non-alpha format. Otherwise return the original format.
 *
 * Examples:
 *    Fallback exists:
 *       MESA_FORMAT_R8G8B8X8_UNORM -> MESA_FORMAT_R8G8B8A8_UNORM
 *       MESA_FORMAT_RGBX_UNORM16 -> MESA_FORMAT_RGBA_UNORM16
 *
 *    No fallback:
 *       MESA_FORMAT_R8G8B8A8_UNORM -> MESA_FORMAT_R8G8B8A8_UNORM
 *       MESA_FORMAT_Z_FLOAT32 -> MESA_FORMAT_Z_FLOAT32
 */
mesa_format
_mesa_format_fallback_rgbx_to_rgba(mesa_format format)
{
   switch (format) {
%for rgbx, rgba in rgbx_to_rgba_map:
   case ${rgbx}:
      return ${rgba};
%endfor
   default:
      return format;
   }
}
""");

def main():
    pargs = parse_args()

    formats = list(format_parser.parse(pargs.csv))

    template_env = {
        'rgbx_to_rgba_map': list(get_rgbx_to_rgba_map(formats)),
    }

    with open(pargs.out, 'w') as f:
        f.write(TEMPLATE.render(**template_env))

if __name__ == "__main__":
    main()
