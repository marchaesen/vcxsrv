# coding=utf-8
COPYRIGHT=u"""
/* Copyright © 2015-2021 Intel Corporation
 * Copyright © 2021 Collabora, Ltd.
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
import re
import xml.etree.ElementTree as et

from mako.template import Template

# Mesa-local imports must be declared in meson variable
# '{file_without_suffix}_depend_files'.
from vk_dispatch_table_gen import get_entrypoints_from_xml, EntrypointParam

MANUAL_COMMANDS = ['CmdPushDescriptorSetKHR',             # This script doesn't know how to copy arrays in structs in arrays
                   'CmdPushDescriptorSetWithTemplateKHR', # pData's size cannot be calculated from the xml
                   'CmdDrawMultiEXT',                     # The size of the elements is specified in a stride param
                   'CmdDrawMultiIndexedEXT',              # The size of the elements is specified in a stride param
                   'CmdBindDescriptorSets',               # The VkPipelineLayout object could be released before the command is executed
                   'CmdCopyImageToBuffer',                # There are wrappers that implement these in terms of the newer variants
                   'CmdCopyImage',
                   'CmdCopyBuffer',
                   'CmdCopyImage',
                   'CmdCopyBufferToImage',
                   'CmdCopyImageToBuffer',
                   'CmdBlitImage',
                   'CmdResolveImage',
                  ]

TEMPLATE_C = Template(COPYRIGHT + """
/* This file generated from ${filename}, don't edit directly. */

#define VK_PROTOTYPES
#include <vulkan/vulkan.h>

#include "lvp_private.h"
#include "pipe/p_context.h"
#include "vk_util.h"

% for c in commands:
% if c.name in manual_commands:
<% continue %>
% endif
% if c.guard is not None:
#ifdef ${c.guard}
% endif
VKAPI_ATTR ${c.return_type} VKAPI_CALL lvp_${c.name} (VkCommandBuffer commandBuffer
% for p in c.params[1:]:
, ${p.decl}
% endfor
)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);

   vk_enqueue_${to_underscore(c.name)}(&cmd_buffer->queue
% for p in c.params[1:]:
, ${p.name}
% endfor
   );

% if c.return_type == 'VkResult':
   return VK_SUCCESS;
% endif
}
% if c.guard is not None:
#endif // ${c.guard}
% endif
% endfor

""", output_encoding='utf-8')

def remove_prefix(text, prefix):
    if text.startswith(prefix):
        return text[len(prefix):]
    return text

def to_underscore(name):
    return remove_prefix(re.sub('([A-Z]+)', r'_\1', name).lower(), '_')

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--out-c', required=True, help='Output C file.')
    parser.add_argument('--xml',
                        help='Vulkan API XML file.',
                        required=True, action='append', dest='xml_files')
    parser.add_argument('--prefix',
                        help='Prefix to use for all dispatch tables.',
                        action='append', default=[], dest='prefixes')
    args = parser.parse_args()

    commands = []
    for e in get_entrypoints_from_xml(args.xml_files):
        if e.name.startswith('Cmd') and \
           not e.alias:
            commands.append(e)

    environment = {
        'commands': commands,
        'filename': os.path.basename(__file__),
        'to_underscore': to_underscore,
        'manual_commands': MANUAL_COMMANDS,
    }

    try:
        with open(args.out_c, 'wb') as f:
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
