# coding=utf-8
#
# Copyright © 2015 Intel Corporation
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

import fileinput, re, sys

# Each function typedef in the vulkan.h header is all on one line and matches
# this regepx. We hope that won't change.

p = re.compile('typedef ([^ ]*) *\((?:VKAPI_PTR)? *\*PFN_vk([^(]*)\)(.*);')

entrypoints = []

# We generate a static hash table for entry point lookup
# (vkGetProcAddress). We use a linear congruential generator for our hash
# function and a power-of-two size table. The prime numbers are determined
# experimentally.

none = 0xffff
hash_size = 256
u32_mask = 2**32 - 1
hash_mask = hash_size - 1

prime_factor = 5024183
prime_step = 19

def hash(name):
    h = 0;
    for c in name:
        h = (h * prime_factor + ord(c)) & u32_mask

    return h

def get_platform_guard_macro(name):
    if "Xlib" in name:
        return "VK_USE_PLATFORM_XLIB_KHR"
    elif "Xcb" in name:
        return "VK_USE_PLATFORM_XCB_KHR"
    elif "Wayland" in name:
        return "VK_USE_PLATFORM_WAYLAND_KHR"
    elif "Mir" in name:
        return "VK_USE_PLATFORM_MIR_KHR"
    elif "Android" in name:
        return "VK_USE_PLATFORM_ANDROID_KHR"
    elif "Win32" in name:
        return "VK_USE_PLATFORM_WIN32_KHR"
    else:
        return None

def print_guard_start(name):
    guard = get_platform_guard_macro(name)
    if guard is not None:
        print "#ifdef {0}".format(guard)

def print_guard_end(name):
    guard = get_platform_guard_macro(name)
    if guard is not None:
        print "#endif // {0}".format(guard)

opt_header = False
opt_code = False

if (sys.argv[1] == "header"):
    opt_header = True
    sys.argv.pop()
elif (sys.argv[1] == "code"):
    opt_code = True
    sys.argv.pop()

# Parse the entry points in the header

i = 0
for line in fileinput.input():
    m  = p.match(line)
    if (m):
        if m.group(2) == 'VoidFunction':
            continue
        fullname = "vk" + m.group(2)
        h = hash(fullname)
        entrypoints.append((m.group(1), m.group(2), m.group(3), i, h))
        i = i + 1

# For outputting entrypoints.h we generate a radv_EntryPoint() prototype
# per entry point.

if opt_header:
    print "/* This file generated from vk_gen.py, don't edit directly. */\n"

    print "struct radv_dispatch_table {"
    print "   union {"
    print "      void *entrypoints[%d];" % len(entrypoints)
    print "      struct {"

    for type, name, args, num, h in entrypoints:
        guard = get_platform_guard_macro(name)
        if guard is not None:
            print "#ifdef {0}".format(guard)
            print "         PFN_vk{0} {0};".format(name)
            print "#else"
            print "         void *{0};".format(name)
            print "#endif"
        else:
            print "         PFN_vk{0} {0};".format(name)
    print "      };\n"
    print "   };\n"
    print "};\n"

    print "void radv_set_dispatch_devinfo(const struct radv_device_info *info);\n"

    for type, name, args, num, h in entrypoints:
        print_guard_start(name)
        print "%s radv_%s%s;" % (type, name, args)
        print "%s vi_%s%s;" % (type, name, args)
        print "%s cik_%s%s;" % (type, name, args)
        print "%s si_%s%s;" % (type, name, args)
        print "%s radv_validate_%s%s;" % (type, name, args)
        print_guard_end(name)
    exit()



print """/*
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

/* DO NOT EDIT! This is a generated file. */

#include "radv_private.h"

struct radv_entrypoint {
   uint32_t name;
   uint32_t hash;
};

/* We use a big string constant to avoid lots of reloctions from the entry
 * point table to lots of little strings. The entries in the entry point table
 * store the index into this big string.
 */

static const char strings[] ="""

offsets = []
i = 0;
for type, name, args, num, h in entrypoints:
    print "   \"vk%s\\0\"" % name
    offsets.append(i)
    i += 2 + len(name) + 1
print "   ;"

# Now generate the table of all entry points and their validation functions

print "\nstatic const struct radv_entrypoint entrypoints[] = {"
for type, name, args, num, h in entrypoints:
    print "   { %5d, 0x%08x }," % (offsets[num], h)
print "};\n"

print """

/* Weak aliases for all potential implementations. These will resolve to
 * NULL if they're not defined, which lets the resolve_entrypoint() function
 * either pick the correct entry point.
 */
"""

for layer in [ "radv", "validate", "si", "cik", "vi" ]:
    for type, name, args, num, h in entrypoints:
        print_guard_start(name)
        print "%s %s_%s%s __attribute__ ((weak));" % (type, layer, name, args)
        print_guard_end(name)
    print "\nconst struct radv_dispatch_table %s_layer = {" % layer
    for type, name, args, num, h in entrypoints:
        print_guard_start(name)
        print "   .%s = %s_%s," % (name, layer, name)
        print_guard_end(name)
    print "};\n"

print """
#ifdef DEBUG
static bool enable_validate = true;
#else
static bool enable_validate = false;
#endif

/* We can't use symbols that need resolving (like, oh, getenv) in the resolve
 * function. This means that we have to determine whether or not to use the
 * validation layer sometime before that. The constructor function attribute asks
 * the dynamic linker to invoke determine_validate() at dlopen() time which
 * works.
 */
static void __attribute__ ((constructor))
determine_validate(void)
{
   const char *s = getenv("ANV_VALIDATE");

   if (s)
      enable_validate = atoi(s);
}

static const struct radv_device_info *dispatch_devinfo;

void
radv_set_dispatch_devinfo(const struct radv_device_info *devinfo)
{
   dispatch_devinfo = devinfo;
}

void * __attribute__ ((noinline))
radv_resolve_entrypoint(uint32_t index)
{
   if (enable_validate && validate_layer.entrypoints[index])
      return validate_layer.entrypoints[index];

   if (dispatch_devinfo == NULL) {
      return radv_layer.entrypoints[index];
   }

   switch (dispatch_devinfo->chip_class) {
   case VI:
      if (vi_layer.entrypoints[index])
         return vi_layer.entrypoints[index];
      /* fall through */
   case CIK:
      if (cik_layer.entrypoints[index])
         return cik_layer.entrypoints[index];
      /* fall through */
   case SI:
      if (si_layer.entrypoints[index])
         return si_layer.entrypoints[index];
      /* fall through */
   case 0:
      return radv_layer.entrypoints[index];
   default:
      unreachable("unsupported gen\\n");
   }
}
"""

# Now generate the hash table used for entry point look up.  This is a
# uint16_t table of entry point indices. We use 0xffff to indicate an entry
# in the hash table is empty.

map = [none for f in xrange(hash_size)]
collisions = [0 for f in xrange(10)]
for type, name, args, num, h in entrypoints:
    level = 0
    while map[h & hash_mask] != none:
        h = h + prime_step
        level = level + 1
    if level > 9:
        collisions[9] += 1
    else:
        collisions[level] += 1
    map[h & hash_mask] = num

print "/* Hash table stats:"
print " * size %d entries" % hash_size
print " * collisions  entries"
for i in xrange(10):
    if (i == 9):
        plus = "+"
    else:
        plus = " "

    print " *     %2d%s     %4d" % (i, plus, collisions[i])
print " */\n"

print "#define none 0x%04x\n" % none

print "static const uint16_t map[] = {"
for i in xrange(0, hash_size, 8):
    print "   ",
    for j in xrange(i, i + 8):
        if map[j] & 0xffff == 0xffff:
            print "  none,",
        else:
            print "0x%04x," % (map[j] & 0xffff),
    print

print "};"    

# Finally we generate the hash table lookup function.  The hash function and
# linear probing algorithm matches the hash table generated above.

print """
void *
radv_lookup_entrypoint(const char *name)
{
   static const uint32_t prime_factor = %d;
   static const uint32_t prime_step = %d;
   const struct radv_entrypoint *e;
   uint32_t hash, h, i;
   const char *p;

   hash = 0;
   for (p = name; *p; p++)
      hash = hash * prime_factor + *p;

   h = hash;
   do {
      i = map[h & %d];
      if (i == none)
         return NULL;
      e = &entrypoints[i];
      h += prime_step;
   } while (e->hash != hash);

   if (strcmp(name, strings + e->name) != 0)
      return NULL;

   return radv_resolve_entrypoint(i);
}
""" % (prime_factor, prime_step, hash_mask)
