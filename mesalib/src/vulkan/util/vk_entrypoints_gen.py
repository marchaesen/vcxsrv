# coding=utf-8
COPYRIGHT=u"""
/* Copyright © 2015-2021 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
"""

import argparse
import os

from mako.template import Template

# Mesa-local imports must be declared in meson variable
# '{file_without_suffix}_depend_files'.
from vk_dispatch_table_gen import get_entrypoints_from_xml

TEMPLATE_H = Template(COPYRIGHT + """\
/* This file generated from ${filename}, don't edit directly. */

#include "vk_dispatch_table.h"

#ifndef ${guard}
#define ${guard}

#ifdef __cplusplus
extern "C" {
#endif

% for p in instance_prefixes:
extern const struct vk_instance_entrypoint_table ${p}_instance_entrypoints;
% endfor

% for p in physical_device_prefixes:
extern const struct vk_physical_device_entrypoint_table ${p}_physical_device_entrypoints;
% endfor

% for p in device_prefixes:
extern const struct vk_device_entrypoint_table ${p}_device_entrypoints;
% endfor

% if gen_proto:
% for e in instance_entrypoints:
  % if e.guard is not None:
#ifdef ${e.guard}
  % endif
  % for p in physical_device_prefixes:
  VKAPI_ATTR ${e.return_type} VKAPI_CALL ${p}_${e.name}(${e.decl_params()});
  % endfor
  % if e.guard is not None:
#endif // ${e.guard}
  % endif
% endfor

% for e in physical_device_entrypoints:
  % if e.guard is not None:
#ifdef ${e.guard}
  % endif
  % for p in physical_device_prefixes:
  VKAPI_ATTR ${e.return_type} VKAPI_CALL ${p}_${e.name}(${e.decl_params()});
  % endfor
  % if e.guard is not None:
#endif // ${e.guard}
  % endif
% endfor

% for e in device_entrypoints:
  % if e.guard is not None:
#ifdef ${e.guard}
  % endif
  % for p in device_prefixes:
  VKAPI_ATTR ${e.return_type} VKAPI_CALL ${p}_${e.name}(${e.decl_params()});
  % endfor
  % if e.guard is not None:
#endif // ${e.guard}
  % endif
% endfor
% endif

#ifdef __cplusplus
}
#endif

#endif /* ${guard} */
""")

TEMPLATE_C = Template(COPYRIGHT + """
/* This file generated from ${filename}, don't edit directly. */

#include "${header}"

/* Weak aliases for all potential implementations. These will resolve to
 * NULL if they're not defined, which lets the resolve_entrypoint() function
 * either pick the correct entry point.
 *
 * MSVC uses different decorated names for 32-bit versus 64-bit. Declare
 * all argument sizes for 32-bit because computing the actual size would be
 * difficult.
 */

<%def name="entrypoint_table(type, entrypoints, prefixes)">
% if gen_weak:
  % for e in entrypoints:
    % if e.guard is not None:
#ifdef ${e.guard}
    % endif
    % for p in prefixes:
#ifdef _MSC_VER
#ifdef _M_IX86
      % for args_size in [4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 60, 104]:
    #pragma comment(linker, "/alternatename:_${p}_${e.name}@${args_size}=_vk_entrypoint_stub")
      % endfor
#else
    #pragma comment(linker, "/alternatename:${p}_${e.name}=vk_entrypoint_stub")
#endif
#else
    VKAPI_ATTR ${e.return_type} VKAPI_CALL ${p}_${e.name}(${e.decl_params()}) __attribute__ ((weak));
#endif
    % endfor
    % if e.guard is not None:
#endif // ${e.guard}
    % endif
  % endfor
% endif

% for p in prefixes:
const struct vk_${type}_entrypoint_table ${p}_${type}_entrypoints = {
  % for e in entrypoints:
    % if e.guard is not None:
#ifdef ${e.guard}
    % endif
    .${e.name} = ${p}_${e.name},
    % if e.guard is not None:
#elif defined(_MSC_VER)
    .${e.name} = (PFN_vkVoidFunction)vk_entrypoint_stub,
#endif // ${e.guard}
    % endif
  % endfor
};
% endfor
</%def>

${entrypoint_table('instance', instance_entrypoints, instance_prefixes)}
${entrypoint_table('physical_device', physical_device_entrypoints, physical_device_prefixes)}
${entrypoint_table('device', device_entrypoints, device_prefixes)}
""")

def get_entrypoints_defines(doc):
    """Maps entry points to extension defines."""
    entrypoints_to_defines = {}

    platform_define = {}
    for platform in doc.findall('./platforms/platform'):
        name = platform.attrib['name']
        define = platform.attrib['protect']
        platform_define[name] = define

    for extension in doc.findall('./extensions/extension[@platform]'):
        platform = extension.attrib['platform']
        define = platform_define[platform]

        for entrypoint in extension.findall('./require/command'):
            fullname = entrypoint.attrib['name']
            entrypoints_to_defines[fullname] = define

    return entrypoints_to_defines


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--out-c', required=True, help='Output C file.')
    parser.add_argument('--out-h', required=True, help='Output H file.')
    parser.add_argument('--xml',
                        help='Vulkan API XML file.',
                        required=True, action='append', dest='xml_files')
    parser.add_argument('--proto', help='Generate entrypoint prototypes',
                        action='store_true', dest='gen_proto')
    parser.add_argument('--weak', help='Generate weak entrypoint declarations',
                        action='store_true', dest='gen_weak')
    parser.add_argument('--prefix',
                        help='Prefix to use for all dispatch tables.',
                        action='append', default=[], dest='prefixes')
    parser.add_argument('--device-prefix',
                        help='Prefix to use for device dispatch tables.',
                        action='append', default=[], dest='device_prefixes')
    args = parser.parse_args()

    instance_prefixes = args.prefixes
    physical_device_prefixes = args.prefixes
    device_prefixes = args.prefixes + args.device_prefixes

    entrypoints = get_entrypoints_from_xml(args.xml_files)

    device_entrypoints = []
    physical_device_entrypoints = []
    instance_entrypoints = []
    for e in entrypoints:
        if e.is_device_entrypoint():
            device_entrypoints.append(e)
        elif e.is_physical_device_entrypoint():
            physical_device_entrypoints.append(e)
        else:
            instance_entrypoints.append(e)

    assert os.path.dirname(args.out_c) == os.path.dirname(args.out_h)

    environment = {
        'gen_proto': args.gen_proto,
        'gen_weak': args.gen_weak,
        'header': os.path.basename(args.out_h),
        'instance_entrypoints': instance_entrypoints,
        'instance_prefixes': instance_prefixes,
        'physical_device_entrypoints': physical_device_entrypoints,
        'physical_device_prefixes': physical_device_prefixes,
        'device_entrypoints': device_entrypoints,
        'device_prefixes': device_prefixes,
        'filename': os.path.basename(__file__),
    }

    # For outputting entrypoints.h we generate a anv_EntryPoint() prototype
    # per entry point.
    try:
        with open(args.out_h, 'w') as f:
            guard = os.path.basename(args.out_h).replace('.', '_').upper()
            f.write(TEMPLATE_H.render(guard=guard, **environment))
        with open(args.out_c, 'w') as f:
            f.write(TEMPLATE_C.render(**environment))

    except Exception:
        # In the event there's an error, this imports some helpers from mako
        # to print a useful stack trace and prints it, then exits with
        # status 1, if python is run with debug; otherwise it just raises
        # the exception
        if __debug__:
            import sys
            from mako import exceptions
            sys.stderr.write(exceptions.text_error_template().render() + '\n')
            sys.exit(1)
        raise

if __name__ == '__main__':
    main()
