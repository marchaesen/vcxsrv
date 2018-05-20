# coding=utf-8
#
# Copyright © 2015, 2017 Intel Corporation
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
#

import argparse
import functools
import math
import os
import xml.etree.cElementTree as et

from collections import OrderedDict, namedtuple
from mako.template import Template

from radv_extensions import *

# We generate a static hash table for entry point lookup
# (vkGetProcAddress). We use a linear congruential generator for our hash
# function and a power-of-two size table. The prime numbers are determined
# experimentally.

# We currently don't use layers in radv, but keeping the ability for anv
# anyways, so we can use it for device groups.
LAYERS = [
    'radv'
]

TEMPLATE_H = Template("""\
/* This file generated from ${filename}, don't edit directly. */

struct radv_dispatch_table {
   union {
      void *entrypoints[${len(entrypoints)}];
      struct {
      % for e in entrypoints:
        % if e.guard is not None:
#ifdef ${e.guard}
          PFN_${e.name} ${e.name};
#else
          void *${e.name};
# endif
        % else:
          PFN_${e.name} ${e.name};
        % endif
      % endfor
      };
   };
};

% for e in entrypoints:
  % if e.alias:
    <% continue %>
  % endif
  % if e.guard is not None:
#ifdef ${e.guard}
  % endif
  % for layer in LAYERS:
  ${e.return_type} ${e.prefixed_name(layer)}(${e.decl_params()});
  % endfor
  % if e.guard is not None:
#endif // ${e.guard}
  % endif
% endfor
""", output_encoding='utf-8')

TEMPLATE_C = Template(u"""\
/*
 * Copyright © 2015 Intel Corporation
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

/* This file generated from ${filename}, don't edit directly. */

#include "radv_private.h"

struct string_map_entry {
   uint32_t name;
   uint32_t hash;
   uint32_t num;
};

/* We use a big string constant to avoid lots of relocations from the entry
 * point table to lots of little strings. The entries in the entry point table
 * store the index into this big string.
 */

static const char strings[] =
% for s in strmap.sorted_strings:
    "${s.string}\\0"
% endfor
;

static const struct string_map_entry string_map_entries[] = {
% for s in strmap.sorted_strings:
    { ${s.offset}, ${'{:0=#8x}'.format(s.hash)}, ${s.num} }, /* ${s.string} */
% endfor
};

/* Hash table stats:
 * size ${len(strmap.sorted_strings)} entries
 * collisions entries:
% for i in xrange(10):
 *     ${i}${'+' if i == 9 else ' '}     ${strmap.collisions[i]}
% endfor
 */

#define none 0xffff
static const uint16_t string_map[${strmap.hash_size}] = {
% for e in strmap.mapping:
    ${ '{:0=#6x}'.format(e) if e >= 0 else 'none' },
% endfor
};

/* Weak aliases for all potential implementations. These will resolve to
 * NULL if they're not defined, which lets the resolve_entrypoint() function
 * either pick the correct entry point.
 */

% for layer in LAYERS:
  % for e in entrypoints:
    % if e.alias:
      <% continue %>
    % endif
    % if e.guard is not None:
#ifdef ${e.guard}
    % endif
    ${e.return_type} ${e.prefixed_name(layer)}(${e.decl_params()}) __attribute__ ((weak));
    % if e.guard is not None:
#endif // ${e.guard}
    % endif
  % endfor

  const struct radv_dispatch_table ${layer}_layer = {
  % for e in entrypoints:
    % if e.guard is not None:
#ifdef ${e.guard}
    % endif
    .${e.name} = ${e.prefixed_name(layer)},
    % if e.guard is not None:
#endif // ${e.guard}
    % endif
  % endfor
  };
% endfor

static void * __attribute__ ((noinline))
radv_resolve_entrypoint(uint32_t index)
{
   return radv_layer.entrypoints[index];
}

/** Return true if the core version or extension in which the given entrypoint
 * is defined is enabled.
 *
 * If instance is NULL, we only allow the 3 commands explicitly allowed by the vk
 * spec.
 *
 * If device is NULL, all device extensions are considered enabled.
 */
static bool
radv_entrypoint_is_enabled(int index, uint32_t core_version,
                          const struct radv_instance_extension_table *instance,
                          const struct radv_device_extension_table *device)
{
   switch (index) {
% for e in entrypoints:
   case ${e.num}:
   % if not e.device_command:
      if (device) return false;
   % endif
   % if e.name == 'vkCreateInstance' or e.name == 'vkEnumerateInstanceExtensionProperties' or e.name == 'vkEnumerateInstanceLayerProperties' or e.name == 'vkEnumerateInstanceVersion':
      return !device;
   % elif e.core_version:
      return instance && ${e.core_version.c_vk_version()} <= core_version;
   % elif e.extensions:
      % for ext in e.extensions:
         % if ext.type == 'instance':
      if (instance && instance->${ext.name[3:]}) return true;
         % else:
      if (instance && (!device || device->${ext.name[3:]})) return true;
         % endif
      %endfor
      return false;
   % else:
      return instance;
   % endif
% endfor
   default:
      return false;
   }
}

static int
radv_lookup_entrypoint(const char *name)
{
   static const uint32_t prime_factor = ${strmap.prime_factor};
   static const uint32_t prime_step = ${strmap.prime_step};
   const struct string_map_entry *e;
   uint32_t hash, h;
   uint16_t i;
   const char *p;

   hash = 0;
   for (p = name; *p; p++)
       hash = hash * prime_factor + *p;

   h = hash;
   while (1) {
       i = string_map[h & ${strmap.hash_mask}];
       if (i == none)
          return -1;
       e = &string_map_entries[i];
       if (e->hash == hash && strcmp(name, strings + e->name) == 0)
           return e->num;
       h += prime_step;
   }

   return -1;
}

void *
radv_lookup_entrypoint_unchecked(const char *name)
{
   int index = radv_lookup_entrypoint(name);
   if (index < 0)
      return NULL;
   return radv_resolve_entrypoint(index);
}

void *
radv_lookup_entrypoint_checked(const char *name,
                               uint32_t core_version,
                               const struct radv_instance_extension_table *instance,
                               const struct radv_device_extension_table *device)
{
   int index = radv_lookup_entrypoint(name);
   if (index < 0 || !radv_entrypoint_is_enabled(index, core_version, instance, device))
      return NULL;
   return radv_resolve_entrypoint(index);
}""", output_encoding='utf-8')

U32_MASK = 2**32 - 1

PRIME_FACTOR = 5024183
PRIME_STEP = 19

def round_to_pow2(x):
    return 2**int(math.ceil(math.log(x, 2)))

class StringIntMapEntry(object):
    def __init__(self, string, num):
        self.string = string
        self.num = num

        # Calculate the same hash value that we will calculate in C.
        h = 0
        for c in string:
            h = ((h * PRIME_FACTOR) + ord(c)) & U32_MASK
        self.hash = h

        self.offset = None

class StringIntMap(object):
    def __init__(self):
        self.baked = False
        self.strings = dict()

    def add_string(self, string, num):
        assert not self.baked
        assert string not in self.strings
        assert num >= 0 and num < 2**31
        self.strings[string] = StringIntMapEntry(string, num)

    def bake(self):
        self.sorted_strings = \
            sorted(self.strings.values(), key=lambda x: x.string)
        offset = 0
        for entry in self.sorted_strings:
            entry.offset = offset
            offset += len(entry.string) + 1

        # Save off some values that we'll need in C
        self.hash_size = round_to_pow2(len(self.strings) * 1.25)
        self.hash_mask = self.hash_size - 1
        self.prime_factor = PRIME_FACTOR
        self.prime_step = PRIME_STEP

        self.mapping = [-1] * self.hash_size
        self.collisions = [0] * 10
        for idx, s in enumerate(self.sorted_strings):
            level = 0
            h = s.hash
            while self.mapping[h & self.hash_mask] >= 0:
                h = h + PRIME_STEP
                level = level + 1
            self.collisions[min(level, 9)] += 1
            self.mapping[h & self.hash_mask] = idx

EntrypointParam = namedtuple('EntrypointParam', 'type name decl')

class EntrypointBase(object):
    def __init__(self, name):
        self.name = name
        self.alias = None
        self.guard = None
        self.enabled = False
        self.num = None
        # Extensions which require this entrypoint
        self.core_version = None
        self.extensions = []

class Entrypoint(EntrypointBase):
    def __init__(self, name, return_type, params, guard = None):
        super(Entrypoint, self).__init__(name)
        self.return_type = return_type
        self.params = params
        self.guard = guard
        self.device_command = len(params) > 0 and (params[0].type == 'VkDevice' or params[0].type == 'VkQueue' or params[0].type == 'VkCommandBuffer')

    def prefixed_name(self, prefix):
        assert self.name.startswith('vk')
        return prefix + '_' + self.name[2:]

    def decl_params(self):
        return ', '.join(p.decl for p in self.params)

    def call_params(self):
        return ', '.join(p.name for p in self.params)

class EntrypointAlias(EntrypointBase):
    def __init__(self, name, entrypoint):
        super(EntrypointAlias, self).__init__(name)
        self.alias = entrypoint
        self.device_command = entrypoint.device_command

    def prefixed_name(self, prefix):
        return self.alias.prefixed_name(prefix)

def get_entrypoints(doc, entrypoints_to_defines, start_index):
    """Extract the entry points from the registry."""
    entrypoints = OrderedDict()

    for command in doc.findall('./commands/command'):
       if 'alias' in command.attrib:
           alias = command.attrib['name']
           target = command.attrib['alias']
           entrypoints[alias] = EntrypointAlias(alias, entrypoints[target])
       else:
           name = command.find('./proto/name').text
           ret_type = command.find('./proto/type').text
           params = [EntrypointParam(
               type = p.find('./type').text,
               name = p.find('./name').text,
               decl = ''.join(p.itertext())
           ) for p in command.findall('./param')]
           guard = entrypoints_to_defines.get(name)
           # They really need to be unique
           assert name not in entrypoints
           entrypoints[name] = Entrypoint(name, ret_type, params, guard)

    for feature in doc.findall('./feature'):
        assert feature.attrib['api'] == 'vulkan'
        version = VkVersion(feature.attrib['number'])
        if version > MAX_API_VERSION:
            continue

        for command in feature.findall('./require/command'):
            e = entrypoints[command.attrib['name']]
            e.enabled = True
            assert e.core_version is None
            e.core_version = version

    supported_exts = dict((ext.name, ext) for ext in EXTENSIONS)
    for extension in doc.findall('.extensions/extension'):
        ext_name = extension.attrib['name']
        if ext_name not in supported_exts:
            continue

        if extension.attrib['supported'] != 'vulkan':
            continue

        ext = supported_exts[ext_name]
        ext.type = extension.attrib['type']

        for command in extension.findall('./require/command'):
            e = entrypoints[command.attrib['name']]
            e.enabled = True
            assert e.core_version is None
            e.extensions.append(ext)

    # if the base command is not supported by the driver yet, don't alias aliases
    for e in entrypoints.values():
        if e.alias and not e.alias.enabled:
            e_clone = copy.deepcopy(e.alias)
            e_clone.enabled = True
            e_clone.name = e.name
            entrypoints[e.name] = e_clone

    return [e for e in entrypoints.itervalues() if e.enabled]


def get_entrypoints_defines(doc):
    """Maps entry points to extension defines."""
    entrypoints_to_defines = {}

    for extension in doc.findall('./extensions/extension[@protect]'):
        define = extension.attrib['protect']

        for entrypoint in extension.findall('./require/command'):
            fullname = entrypoint.attrib['name']
            entrypoints_to_defines[fullname] = define

    for extension in doc.findall('./extensions/extension[@platform]'):
        platform = extension.attrib['platform']
        define = 'VK_USE_PLATFORM_' + platform.upper() + '_KHR'

        for entrypoint in extension.findall('./require/command'):
            fullname = entrypoint.attrib['name']
            entrypoints_to_defines[fullname] = define

    return entrypoints_to_defines


def gen_code(entrypoints):
    """Generate the C code."""
    strmap = StringIntMap()
    for e in entrypoints:
        strmap.add_string(e.name, e.num)
    strmap.bake()

    return TEMPLATE_C.render(entrypoints=entrypoints,
                             LAYERS=LAYERS,
                             strmap=strmap,
                             filename=os.path.basename(__file__))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--outdir', help='Where to write the files.',
                        required=True)
    parser.add_argument('--xml',
                        help='Vulkan API XML file.',
                        required=True,
                        action='append',
                        dest='xml_files')
    args = parser.parse_args()

    entrypoints = []

    for filename in args.xml_files:
        doc = et.parse(filename)
        entrypoints += get_entrypoints(doc, get_entrypoints_defines(doc),
                                       start_index=len(entrypoints))

    for num, e in enumerate(entrypoints):
        e.num = num

    # For outputting entrypoints.h we generate a radv_EntryPoint() prototype
    # per entry point.
    with open(os.path.join(args.outdir, 'radv_entrypoints.h'), 'wb') as f:
        f.write(TEMPLATE_H.render(entrypoints=entrypoints,
                                  LAYERS=LAYERS,
                                  filename=os.path.basename(__file__)))
    with open(os.path.join(args.outdir, 'radv_entrypoints.c'), 'wb') as f:
        f.write(gen_code(entrypoints))


if __name__ == '__main__':
    main()
