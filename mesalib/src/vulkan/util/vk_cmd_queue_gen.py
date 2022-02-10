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
from collections import namedtuple
import xml.etree.ElementTree as et

from mako.template import Template

# Mesa-local imports must be declared in meson variable
# '{file_without_suffix}_depend_files'.
from vk_entrypoints import get_entrypoints_from_xml, EntrypointParam

MANUAL_COMMANDS = ['CmdPushDescriptorSetKHR',             # This script doesn't know how to copy arrays in structs in arrays
                   'CmdPushDescriptorSetWithTemplateKHR', # pData's size cannot be calculated from the xml
                   'CmdDrawMultiEXT',                     # The size of the elements is specified in a stride param
                   'CmdDrawMultiIndexedEXT',              # The size of the elements is specified in a stride param
                   'CmdBindDescriptorSets',               # The VkPipelineLayout object could be released before the command is executed
                   'CmdBeginRendering',               # The VkPipelineLayout object could be released before the command is executed
                   'CmdBeginRenderingKHR',               # The VkPipelineLayout object could be released before the command is executed
                  ]

TEMPLATE_H = Template(COPYRIGHT + """\
/* This file generated from ${filename}, don't edit directly. */

#pragma once

#include "util/list.h"

#define VK_PROTOTYPES
#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

struct vk_cmd_queue {
   VkAllocationCallbacks *alloc;
   struct list_head cmds;
};

enum vk_cmd_type {
% for c in commands:
% if c.guard is not None:
#ifdef ${c.guard}
% endif
   ${to_enum_name(c.name)},
% if c.guard is not None:
#endif // ${c.guard}
% endif
% endfor
};

extern const char *vk_cmd_queue_type_names[];

% for c in commands:
% if len(c.params) <= 1:             # Avoid "error C2016: C requires that a struct or union have at least one member"
<% continue %>
% endif
% if c.guard is not None:
#ifdef ${c.guard}
% endif
struct ${to_struct_name(c.name)} {
% for p in c.params[1:]:
   ${to_field_decl(p.decl)};
% endfor
};
% if c.guard is not None:
#endif // ${c.guard}
% endif
% endfor

struct vk_cmd_queue_entry {
   struct list_head cmd_link;
   enum vk_cmd_type type;
   union {
% for c in commands:
% if len(c.params) <= 1:
<% continue %>
% endif
% if c.guard is not None:
#ifdef ${c.guard}
% endif
      struct ${to_struct_name(c.name)} ${to_struct_field_name(c.name)};
% if c.guard is not None:
#endif // ${c.guard}
% endif
% endfor
   } u;
   void *driver_data;
};

% for c in commands:
% if c.name in manual_commands:
<% continue %>
% endif
% if c.guard is not None:
#ifdef ${c.guard}
% endif
  void vk_enqueue_${to_underscore(c.name)}(struct vk_cmd_queue *queue
% for p in c.params[1:]:
   , ${p.decl}
% endfor
  );
% if c.guard is not None:
#endif // ${c.guard}
% endif

% endfor

void vk_free_queue(struct vk_cmd_queue *queue);

#ifdef __cplusplus
}
#endif
""", output_encoding='utf-8')

TEMPLATE_C = Template(COPYRIGHT + """
/* This file generated from ${filename}, don't edit directly. */

#include "${header}"

#define VK_PROTOTYPES
#include <vulkan/vulkan.h>

#include "vk_alloc.h"

const char *vk_cmd_queue_type_names[] = {
% for c in commands:
% if c.guard is not None:
#ifdef ${c.guard}
% endif
   "${to_enum_name(c.name)}",
% if c.guard is not None:
#endif // ${c.guard}
% endif
% endfor
};

% for c in commands:
% if c.name in manual_commands:
<% continue %>
% endif
% if c.guard is not None:
#ifdef ${c.guard}
% endif
void vk_enqueue_${to_underscore(c.name)}(struct vk_cmd_queue *queue
% for p in c.params[1:]:
, ${p.decl}
% endfor
)
{
   struct vk_cmd_queue_entry *cmd = vk_zalloc(queue->alloc,
                                              sizeof(*cmd), 8,
                                              VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!cmd)
      return;

   cmd->type = ${to_enum_name(c.name)};
   list_addtail(&cmd->cmd_link, &queue->cmds);

% for p in c.params[1:]:
% if p.len:
   if (${p.name}) {
      ${get_array_copy(c, p)}
   }
% elif '[' in p.decl:
   memcpy(cmd->u.${to_struct_field_name(c.name)}.${to_field_name(p.name)}, ${p.name},
          sizeof(*${p.name}) * ${get_array_len(p)});
% elif p.type == "void":
   cmd->u.${to_struct_field_name(c.name)}.${to_field_name(p.name)} = (${remove_suffix(p.decl.replace("const", ""), p.name)}) ${p.name};
% elif '*' in p.decl:
   ${get_struct_copy("cmd->u.%s.%s" % (to_struct_field_name(c.name), to_field_name(p.name)), p.name, p.type, 'sizeof(%s)' % p.type, types)}
% else:
   cmd->u.${to_struct_field_name(c.name)}.${to_field_name(p.name)} = ${p.name};
% endif
% endfor
}
% if c.guard is not None:
#endif // ${c.guard}
% endif

% endfor

void
vk_free_queue(struct vk_cmd_queue *queue)
{
   struct vk_cmd_queue_entry *tmp, *cmd;
   LIST_FOR_EACH_ENTRY_SAFE(cmd, tmp, &queue->cmds, cmd_link) {
      switch(cmd->type) {
% for c in commands:
% if c.guard is not None:
#ifdef ${c.guard}
% endif
      case ${to_enum_name(c.name)}:
         vk_free(queue->alloc, cmd->driver_data);
% for p in c.params[1:]:
% if p.len:
         vk_free(queue->alloc, (${remove_suffix(p.decl.replace("const", ""), p.name)})cmd->u.${to_struct_field_name(c.name)}.${to_field_name(p.name)});
% elif '*' in p.decl:
         ${get_struct_free(c, p, types)}
% endif
% endfor
         break;
% if c.guard is not None:
#endif // ${c.guard}
% endif
% endfor
      }
      vk_free(queue->alloc, cmd);
   }
}

""", output_encoding='utf-8')

def remove_prefix(text, prefix):
    if text.startswith(prefix):
        return text[len(prefix):]
    return text

def remove_suffix(text, suffix):
    if text.endswith(suffix):
        return text[:-len(suffix)]
    return text

def to_underscore(name):
    return remove_prefix(re.sub('([A-Z]+)', r'_\1', name).lower(), '_')

def to_struct_field_name(name):
    return to_underscore(name).replace('cmd_', '')

def to_field_name(name):
    return remove_prefix(to_underscore(name).replace('cmd_', ''), 'p_')

def to_field_decl(decl):
    decl = decl.replace('const ', '')
    [decl, name] = decl.rsplit(' ', 1)
    return decl + ' ' + to_field_name(name)

def to_enum_name(name):
    return "VK_%s" % to_underscore(name).upper()

def to_struct_name(name):
    return "vk_%s" % to_underscore(name)

def get_array_len(param):
    return param.decl[param.decl.find("[") + 1:param.decl.find("]")]

def get_array_copy(command, param):
    field_name = "cmd->u.%s.%s" % (to_struct_field_name(command.name), to_field_name(param.name))
    if param.type == "void":
        field_size = "1"
    else:
        field_size = "sizeof(*%s)" % field_name
    allocation = "%s = vk_zalloc(queue->alloc, %s * %s, 8, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);" % (field_name, field_size, param.len)
    const_cast = remove_suffix(param.decl.replace("const", ""), param.name)
    copy = "memcpy((%s)%s, %s, %s * %s);" % (const_cast, field_name, param.name, field_size, param.len)
    return "%s\n   %s" % (allocation, copy)

def get_array_member_copy(struct, src_name, member):
    field_name = "%s->%s" % (struct, member.name)
    len_field_name = "%s->%s" % (struct, member.len)
    allocation = "%s = vk_zalloc(queue->alloc, sizeof(*%s) * %s, 8, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);" % (field_name, field_name, len_field_name)
    const_cast = remove_suffix(member.decl.replace("const", ""), member.name)
    copy = "memcpy((%s)%s, %s->%s, sizeof(*%s) * %s);" % (const_cast, field_name, src_name, member.name, field_name, len_field_name)
    return "%s\n   %s\n" % (allocation, copy)

def get_pnext_member_copy(struct, src_type, member, types, level):
    if not types[src_type].extended_by:
        return ""
    field_name = "%s->%s" % (struct, member.name)
    pnext_decl = "const VkBaseInStructure *pnext = %s;" % field_name
    case_stmts = ""
    for type in types[src_type].extended_by:
        case_stmts += """
      case %s:
         %s
         break;
      """ % (type.enum, get_struct_copy(field_name, "pnext", type.name, "sizeof(%s)" % type.name, types, level))
    return """
      %s
      if (pnext) {
         switch ((int32_t)pnext->sType) {
         %s
         }
      }
      """ % (pnext_decl, case_stmts)

def get_struct_copy(dst, src_name, src_type, size, types, level=0):
    global tmp_dst_idx
    global tmp_src_idx

    allocation = "%s = vk_zalloc(queue->alloc, %s, 8, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);" % (dst, size)
    copy = "memcpy((void*)%s, %s, %s);" % (dst, src_name, size)

    level += 1
    tmp_dst = "%s *tmp_dst%d = (void *) %s; (void) tmp_dst%d;" % (src_type, level, dst, level)
    tmp_src = "%s *tmp_src%d = (void *) %s; (void) tmp_src%d;" % (src_type, level, src_name, level)

    member_copies = ""
    if src_type in types:
        for member in types[src_type].members:
            if member.len and member.len != 'null-terminated':
                member_copies += get_array_member_copy("tmp_dst%d" % level, "tmp_src%d" % level, member)
            elif member.name == 'pNext':
                member_copies += get_pnext_member_copy("tmp_dst%d" % level, src_type, member, types, level)

    null_assignment = "%s = NULL;" % dst
    if_stmt = "if (%s) {" % src_name
    return "%s\n      %s\n      %s\n   %s\n   %s   \n   %s   } else {\n      %s\n   }" % (if_stmt, allocation, copy, tmp_dst, tmp_src, member_copies, null_assignment)

def get_struct_free(command, param, types):
    field_name = "cmd->u.%s.%s" % (to_struct_field_name(command.name), to_field_name(param.name))
    const_cast = remove_suffix(param.decl.replace("const", ""), param.name)
    struct_free = "vk_free(queue->alloc, (%s)%s);" % (const_cast, field_name)
    member_frees = ""
    if (param.type in types):
        for member in types[param.type].members:
            if member.len and member.len != 'null-terminated':
                member_name = "cmd->u.%s.%s->%s" % (to_struct_field_name(command.name), to_field_name(param.name), member.name)
                const_cast = remove_suffix(member.decl.replace("const", ""), member.name)
                member_frees += "vk_free(queue->alloc, (%s)%s);\n" % (const_cast, member_name)
    return "%s      %s\n" % (member_frees, struct_free)

EntrypointType = namedtuple('EntrypointType', 'name enum members extended_by')

def get_types(doc):
    """Extract the types from the registry."""
    types = {}

    for _type in doc.findall('./types/type'):
        if _type.attrib.get('category') != 'struct':
            continue
        members = []
        type_enum = None
        for p in _type.findall('./member'):
            member = EntrypointParam(type=p.find('./type').text,
                                     name=p.find('./name').text,
                                     decl=''.join(p.itertext()),
                                     len=p.attrib.get('len', None))
            members.append(member)

            if p.find('./name').text == 'sType':
                type_enum = p.attrib.get('values')
        types[_type.attrib['name']] = EntrypointType(name=_type.attrib['name'], enum=type_enum, members=members, extended_by=[])

    for _type in doc.findall('./types/type'):
        if _type.attrib.get('category') != 'struct':
            continue
        if _type.attrib.get('structextends') is None:
            continue
        for extended in _type.attrib.get('structextends').split(','):
            types[extended].extended_by.append(types[_type.attrib['name']])

    return types

def get_types_from_xml(xml_files):
    types = {}

    for filename in xml_files:
        doc = et.parse(filename)
        types.update(get_types(doc))

    return types

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--out-c', required=True, help='Output C file.')
    parser.add_argument('--out-h', required=True, help='Output H file.')
    parser.add_argument('--xml',
                        help='Vulkan API XML file.',
                        required=True, action='append', dest='xml_files')
    args = parser.parse_args()

    commands = []
    for e in get_entrypoints_from_xml(args.xml_files):
        if e.name.startswith('Cmd') and \
           not e.alias:
            commands.append(e)

    types = get_types_from_xml(args.xml_files)

    assert os.path.dirname(args.out_c) == os.path.dirname(args.out_h)

    environment = {
        'header': os.path.basename(args.out_h),
        'commands': commands,
        'filename': os.path.basename(__file__),
        'to_underscore': to_underscore,
        'get_array_len': get_array_len,
        'to_struct_field_name': to_struct_field_name,
        'to_field_name': to_field_name,
        'to_field_decl': to_field_decl,
        'to_enum_name': to_enum_name,
        'to_struct_name': to_struct_name,
        'get_array_copy': get_array_copy,
        'get_struct_copy': get_struct_copy,
        'get_struct_free': get_struct_free,
        'types': types,
        'manual_commands': MANUAL_COMMANDS,
        'remove_suffix': remove_suffix,
    }

    try:
        with open(args.out_h, 'wb') as f:
            guard = os.path.basename(args.out_h).replace('.', '_').upper()
            f.write(TEMPLATE_H.render(guard=guard, **environment))
        with open(args.out_c, 'wb') as f:
            f.write(TEMPLATE_C.render(**environment))
    except Exception:
        # In the event there's an error, this imports some helpers from mako
        # to print a useful stack trace and prints it, then exits with
        # status 1, if python is run with debug; otherwise it just raises
        # the exception
        import sys
        from mako import exceptions
        print(exceptions.text_error_template().render(), file=sys.stderr)
        sys.exit(1)

if __name__ == '__main__':
    main()
