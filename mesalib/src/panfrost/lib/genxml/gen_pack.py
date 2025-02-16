#encoding=utf-8

# Copyright (C) 2016 Intel Corporation
# Copyright (C) 2016 Broadcom
# Copyright (C) 2020 Collabora, Ltd.
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

import argparse
import xml.parsers.expat
import sys
import operator
from functools import reduce

global_prefix = "mali"

v6_format_printer = """
#define mali_pixel_format_print(fp, format) \\
    fprintf(fp, "%*sFormat (v6): %s%s%s %s%s%s%s\\n", indent, "", \\
        mali_format_as_str((enum mali_format)((format >> 12) & 0xFF)), \\
        (format & (1 << 20)) ? " sRGB" : "", \\
        (format & (1 << 21)) ? " big-endian" : "", \\
        mali_channel_as_str((enum mali_channel)((format >> 0) & 0x7)), \\
        mali_channel_as_str((enum mali_channel)((format >> 3) & 0x7)), \\
        mali_channel_as_str((enum mali_channel)((format >> 6) & 0x7)), \\
        mali_channel_as_str((enum mali_channel)((format >> 9) & 0x7)));
"""

v7_format_printer = """
#define mali_pixel_format_print(fp, format) \\
    fprintf(fp, "%*sFormat (v7): %s%s %s%s\\n", indent, "", \\
        mali_format_as_str((enum mali_format)((format >> 12) & 0xFF)), \\
        (format & (1 << 20)) ? " sRGB" : "", \\
        mali_rgb_component_order_as_str((enum mali_rgb_component_order)(format & ((1 << 12) - 1))), \\
        (format & (1 << 21)) ? " XXX BAD BIT" : "");
"""

def to_alphanum(name):
    substitutions = {
        ' ': '_',
        '/': '_',
        '[': '',
        ']': '',
        '(': '',
        ')': '',
        '-': '_',
        ':': '',
        '.': '',
        ',': '',
        '=': '',
        '>': '',
        '#': '',
        '&': '',
        '%': '',
        '*': '',
        '"': '',
        '+': '',
        '\'': '',
    }

    for i, j in substitutions.items():
        name = name.replace(i, j)

    return name

def safe_name(name):
    name = to_alphanum(name)
    if not name[0].isalpha():
        name = '_' + name

    return name

def prefixed_upper_name(prefix, name):
    if prefix:
        name = prefix + "_" + name
    return safe_name(name).upper()

def enum_name(name):
    return "{}_{}".format(global_prefix, safe_name(name)).lower()

MODIFIERS = ["shr", "minus", "align", "log2"]

def parse_modifier(modifier):
    if modifier is None:
        return None

    for mod in MODIFIERS:
        if modifier[0:len(mod)] == mod:
            if mod == "log2":
                assert(len(mod) == len(modifier))
                return [mod]

            if modifier[len(mod)] == '(' and modifier[-1] == ')':
                ret = [mod, int(modifier[(len(mod) + 1):-1])]
                if ret[0] == 'align':
                    align = ret[1]
                    # Make sure the alignment is a power of 2
                    assert(align > 0 and not(align & (align - 1)));

                return ret

    print("Invalid modifier")
    assert(False)

class Aggregate(object):
    def __init__(self, parser, name, attrs):
        self.parser = parser
        self.sections = []
        self.name = name
        self.explicit_size = int(attrs["size"]) if "size" in attrs else 0
        self.size = 0
        self.align = int(attrs["align"]) if "align" in attrs else None

    class Section:
        def __init__(self, name):
            self.name = name

    def get_size(self):
        if self.size > 0:
            return self.size

        size = 0
        for section in self.sections:
            size = max(size, section.offset + section.type.get_length())

        if self.explicit_size > 0:
            assert(self.explicit_size >= size)
            self.size = self.explicit_size
        else:
            self.size = size
        return self.size

    def add_section(self, type_name, attrs):
        assert("name" in attrs)
        section = self.Section(safe_name(attrs["name"]).lower())
        section.human_name = attrs["name"]
        section.offset = int(attrs["offset"])
        assert(section.offset % 4 == 0)
        section.type = self.parser.structs[attrs["type"]]
        section.type_name = type_name
        self.sections.append(section)

class Field(object):
    def __init__(self, parser, attrs):
        self.parser = parser
        if "name" in attrs:
            self.name = safe_name(attrs["name"]).lower()
            self.human_name = attrs["name"]

        if ":" in str(attrs["start"]):
            (word, bit) = attrs["start"].split(":")
            self.start = (int(word) * 32) + int(bit)
        else:
            self.start = int(attrs["start"])

        self.end = self.start + int(attrs["size"]) - 1
        self.type = attrs["type"]

        if self.type == 'bool' and self.start != self.end:
            print("#error Field {} has bool type but more than one bit of size".format(self.name));

        if "prefix" in attrs:
            self.prefix = safe_name(attrs["prefix"]).upper()
        else:
            self.prefix = None

        self.default = attrs.get("default")

        # Map enum values
        if self.type in self.parser.enums and self.default is not None:
            self.default = safe_name('{}_{}_{}'.format(global_prefix, self.type, self.default)).upper()

        self.modifier  = parse_modifier(attrs.get("modifier"))

    def emit_template_struct(self, dim):
        if self.type == 'address':
            type = 'uint64_t'
        elif self.type == 'bool':
            type = 'bool'
        elif self.type in ['float', 'ulod', 'slod']:
            type = 'float'
        elif self.type in ['uint', 'hex'] and self.end - self.start > 32:
            type = 'uint64_t'
        elif self.type == 'int':
            type = 'int32_t'
        elif self.type in ['uint', 'hex', 'uint/float', 'padded', 'Pixel Format', 'Component Swizzle']:
            type = 'uint32_t'
        elif self.type in self.parser.structs:
            type = 'struct ' + self.parser.gen_prefix(safe_name(self.type.upper()))
        elif self.type in self.parser.enums:
            type = 'enum ' + enum_name(self.type)
        else:
            print("#error unhandled type: %s" % self.type)
            type = "uint32_t"

        print("   %-36s %s%s;" % (type, self.name, dim))

        for value in self.values:
            name = prefixed_upper_name(self.prefix, value.name)
            print("#define %-40s %d" % (name, value.value))

    def overlaps(self, field):
        return self != field and max(self.start, field.start) <= min(self.end, field.end)

class Group(object):
    def __init__(self, parser, parent, start, count, label):
        self.parser = parser
        self.parent = parent
        self.start = start
        self.count = count
        self.label = label
        self.size = 0
        self.length = 0
        self.fields = []

    def get_length(self):
        # Determine number of bytes in this group.
        calculated = max(field.end // 8 for field in self.fields) + 1 if len(self.fields) > 0 else 0
        if self.length > 0:
            assert(self.length >= calculated)
        else:
            self.length = calculated
        return self.length


    def emit_template_struct(self, dim):
        if self.count == 0:
            print("   /* variable length fields follow */")
        else:
            if self.count > 1:
                dim = "%s[%d]" % (dim, self.count)

            if len(self.fields) == 0:
                print("   int dummy;")

            for field in self.fields:
                field.emit_template_struct(dim)

    class Word:
        def __init__(self):
            self.size = 32
            self.contributors = []

    class FieldRef:
        def __init__(self, field, path, start, end):
            self.field = field
            self.path = path
            self.start = start
            self.end = end

    def collect_fields(self, fields, offset, path, all_fields):
        for field in fields:
            field_path = '{}{}'.format(path, field.name)
            field_offset = offset + field.start

            if field.type in self.parser.structs:
                sub_struct = self.parser.structs[field.type]
                self.collect_fields(sub_struct.fields, field_offset, field_path + '.', all_fields)
                continue

            start = field_offset
            end = offset + field.end
            all_fields.append(self.FieldRef(field, field_path, start, end))

    def collect_words(self, fields, offset, path, words):
        for field in fields:
            field_path = '{}{}'.format(path, field.name)
            start = offset + field.start

            if field.type in self.parser.structs:
                sub_fields = self.parser.structs[field.type].fields
                self.collect_words(sub_fields, start, field_path + '.', words)
                continue

            end = offset + field.end
            contributor = self.FieldRef(field, field_path, start, end)
            first_word = contributor.start // 32
            last_word = contributor.end // 32
            for b in range(first_word, last_word + 1):
                if not b in words:
                    words[b] = self.Word()
                words[b].contributors.append(contributor)

    def emit_pack_function(self):
        self.get_length()

        words = {}
        self.collect_words(self.fields, 0, '', words)

        # Validate the modifier is lossless
        for field in self.fields:
            if field.modifier is None:
                continue

            if field.modifier[0] == "shr":
                shift = field.modifier[1]
                mask = hex((1 << shift) - 1)
                print("   assert(((__unpacked)->{} & {}) == 0); \\".format(field.name, mask))
            elif field.modifier[0] == "minus":
                print("   assert((__unpacked)->{} >= {}); \\".format(field.name, field.modifier[1]))
            elif field.modifier[0] == "log2":
                print("   assert(IS_POT_NONZERO((__unpacked)->{})); \\".format(field.name))

        for index in range(self.length // 4):
            # Handle MBZ words
            if not index in words:
                print("   __tmp_packed.opaque[%2d] = 0; \\" % index)
                continue

            word = words[index]

            word_start = index * 32

            v = None
            prefix = "   __tmp_packed.opaque[%2d] =" % index

            for contributor in word.contributors:
                field = contributor.field
                name = field.name
                start = contributor.start
                end = contributor.end
                contrib_word_start = (start // 32) * 32
                start -= contrib_word_start
                end -= contrib_word_start

                value = "(__unpacked)->{}".format(contributor.path)
                if field.modifier is not None:
                    if field.modifier[0] == "shr":
                        value = "{} >> {}".format(value, field.modifier[1])
                    elif field.modifier[0] == "minus":
                        value = "{} - {}".format(value, field.modifier[1])
                    elif field.modifier[0] == "align":
                        value = "ALIGN_POT({}, {})".format(value, field.modifier[1])
                    elif field.modifier[0] == "log2":
                        value = "util_logbase2({})".format(value)

                if field.type in ["uint", "hex", "uint/float", "address", "Pixel Format", "Component Swizzle"]:
                    s = "util_bitpack_uint(%s, %d, %d)" % \
                        (value, start, end)
                elif field.type == "padded":
                    s = "__gen_padded(%s, %d, %d)" % \
                        (value, start, end)
                elif field.type in self.parser.enums:
                    s = "util_bitpack_uint(%s, %d, %d)" % \
                        (value, start, end)
                elif field.type == "int":
                    s = "util_bitpack_sint(%s, %d, %d)" % \
                        (value, start, end)
                elif field.type == "bool":
                    s = "util_bitpack_uint(%s, %d, %d)" % \
                        (value, start, end)
                elif field.type == "float":
                    assert(start == 0 and end == 31)
                    s = "util_bitpack_float({})".format(value)
                elif field.type == "ulod":
                    s = "util_bitpack_ufixed_clamp({}, {}, {}, 8)".format(value,
                                                                          start,
                                                                          end)
                elif field.type == "slod":
                    s = "util_bitpack_sfixed_clamp({}, {}, {}, 8)".format(value,
                                                                          start,
                                                                          end)
                else:
                    s = "#error unhandled field {}, type {}".format(contributor.path, field.type)

                if not s == None:
                    shift = word_start - contrib_word_start
                    if shift:
                        s = "%s >> %d" % (s, shift)

                    if contributor == word.contributors[-1]:
                        print("%s %s; \\" % (prefix, s))
                    else:
                        print("%s %s | \\" % (prefix, s))
                    prefix = "           "

            continue

    # Given a field (start, end) contained in word `index`, generate the 32-bit
    # mask of present bits relative to the word
    def mask_for_word(self, index, start, end):
        field_word_start = index * 32
        start -= field_word_start
        end -= field_word_start
        # Cap multiword at one word
        start = max(start, 0)
        end = min(end, 32 - 1)
        count = (end - start + 1)
        return (((1 << count) - 1) << start)

    def emit_unpack_function(self):
        # First, verify there is no garbage in unused bits
        words = {}
        self.collect_words(self.fields, 0, '', words)

        for index in range(self.length // 4):
            base = index * 32
            word = words.get(index, self.Word())
            masks = [self.mask_for_word(index, c.start, c.end) for c in word.contributors]
            mask = reduce(lambda x,y: x | y, masks, 0)

            ALL_ONES = 0xffffffff

            if mask != ALL_ONES:
                TMPL = '   if (__tmp_packed.opaque[{}] & {}) fprintf(stderr, "XXX: Invalid field of {} unpacked at word {}\\n"); \\'
                print(TMPL.format(index, hex(mask ^ ALL_ONES), self.label, index))

        fieldrefs = []
        self.collect_fields(self.fields, 0, '', fieldrefs)
        for fieldref in fieldrefs:
            field = fieldref.field
            convert = None

            args = []
            args.append('(__unpacked)->{}'.format(fieldref.path))
            args.append('&__tmp_packed.opaque[0]')
            args.append(str(fieldref.start))
            args.append(str(fieldref.end))

            if field.type in set(["uint", "hex", "uint/float", "address", "Pixel Format", "Component Swizzle"]):
                convert = "__gen_unpack_uint"
            elif field.type in self.parser.enums:
                convert = "__gen_unpack_uint"
            elif field.type == "int":
                convert = "__gen_unpack_sint"
            elif field.type == "padded":
                convert = "__gen_unpack_padded"
            elif field.type == "bool":
                convert = "__gen_unpack_uint"
            elif field.type == "float":
                convert = "__gen_unpack_float"
            elif field.type == "ulod":
                convert = "__gen_unpack_ulod"
            elif field.type == "slod":
                convert = "__gen_unpack_slod"
            else:
                s = "/* unhandled field %s, type %s */\n" % (field.name, field.type)

            suffix = ""
            prefix = ""
            if field.modifier:
                if field.modifier[0] == "minus":
                    suffix = " + {}".format(field.modifier[1])
                elif field.modifier[0] == "shr":
                    suffix = " << {}".format(field.modifier[1])
                if field.modifier[0] == "log2":
                    prefix = "1U << "

            print('   {}({}); \\'.format(convert, ', '.join(args)))

            if len(prefix) != 0 or len(suffix) != 0:
                print('   (__unpacked)->{} = {}(__unpacked)->{}{}; \\'.format(fieldref.path, prefix, fieldref.path, suffix))


            if field.modifier and field.modifier[0] == "align":
                mask = hex(field.modifier[1] - 1)
                print('   assert(!((__unpacked)->{} & {})); \\'.format(fieldref.path, mask))

    def emit_print_function(self):
        for field in self.fields:
            convert = None
            name, val = field.human_name, 'values->{}'.format(field.name)

            if field.type in self.parser.structs:
                pack_name = self.parser.gen_prefix(safe_name(field.type)).upper()
                print('   fprintf(fp, "%*s{}:\\n", indent, "");'.format(field.human_name))
                print("   {}_print(fp, &values->{}, indent + 2);".format(pack_name, field.name))
            elif field.type == "address":
                # TODO resolve to name
                print('   fprintf(fp, "%*s{}: 0x%" PRIx64 "\\n", indent, "", {});'.format(name, val))
            elif field.type in self.parser.enums:
                print('   fprintf(fp, "%*s{}: %s\\n", indent, "", {}_as_str({}));'.format(name, enum_name(field.type), val))
            elif field.type == "int":
                print('   fprintf(fp, "%*s{}: %d\\n", indent, "", {});'.format(name, val))
            elif field.type == "bool":
                print('   fprintf(fp, "%*s{}: %s\\n", indent, "", {} ? "true" : "false");'.format(name, val))
            elif field.type in ["float", "ulod", "slod"]:
                print('   fprintf(fp, "%*s{}: %f\\n", indent, "", {});'.format(name, val))
            elif field.type in ["uint", "hex"] and (field.end - field.start) >= 32:
                print('   fprintf(fp, "%*s{}: 0x%" PRIx64 "\\n", indent, "", {});'.format(name, val))
            elif field.type == "hex":
                print('   fprintf(fp, "%*s{}: 0x%x\\n", indent, "", {});'.format(name, val))
            elif field.type == "uint/float":
                print('   fprintf(fp, "%*s{}: 0x%X (%f)\\n", indent, "", {}, uif({}));'.format(name, val, val))
            elif field.type == "Pixel Format":
                print('   mali_pixel_format_print(fp, {});'.format(val))
            elif field.type == "Component Swizzle":
                print('   fprintf(fp, "%*s{}: %u (%s)\\n", indent, "", {}, mali_component_swizzle({}));'.format(name, val, val))
            else:
                print('   fprintf(fp, "%*s{}: %u\\n", indent, "", {});'.format(name, val))

class Value(object):
    def __init__(self, attrs):
        self.name = attrs["name"]
        self.value = int(attrs["value"], 0)

pack_header = """/* Autogenerated file, do not edit */
/*
 * Copyright 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PAN_PACK_H
#define PAN_PACK_H

#include "genxml/pan_pack_helpers.h"
"""

class Parser(object):
    def __init__(self):
        self.parser = xml.parsers.expat.ParserCreate()
        self.parser.StartElementHandler = self.start_element
        self.parser.EndElementHandler = self.end_element

        self.struct = None
        self.structs = {}
        # Set of enum names we've seen.
        self.enums = set()
        self.aggregate = None
        self.aggregates = {}

    def gen_prefix(self, name):
        return '{}_{}'.format(global_prefix.upper(), name)

    def start_element(self, name, attrs):
        if name == "panxml":
            print(pack_header)
            if "arch" in attrs:
                arch = int(attrs["arch"])
                if arch <= 6:
                    print(v6_format_printer)
                else:
                    print(v7_format_printer)
        elif name == "struct":
            name = attrs["name"]
            self.no_direct_packing = attrs.get("no-direct-packing", False)
            object_name = self.gen_prefix(safe_name(name.upper()))
            self.struct = object_name

            self.group = Group(self, None, 0, 1, name)
            if "size" in attrs:
                self.group.length = int(attrs["size"]) * 4
            self.group.align = int(attrs["align"]) if "align" in attrs else None
            self.structs[attrs["name"]] = self.group
        elif name == "field":
            self.group.fields.append(Field(self, attrs))
            self.values = []
        elif name == "enum":
            self.values = []
            self.enum = safe_name(attrs["name"])
            self.enums.add(attrs["name"])
            if "prefix" in attrs:
                self.prefix = attrs["prefix"]
            else:
                self.prefix= None
        elif name == "value":
            self.values.append(Value(attrs))
        elif name == "aggregate":
            aggregate_name = self.gen_prefix(safe_name(attrs["name"].upper()))
            self.aggregate = Aggregate(self, aggregate_name, attrs)
            self.aggregates[attrs['name']] = self.aggregate
        elif name == "section":
            type_name = self.gen_prefix(safe_name(attrs["type"].upper()))
            self.aggregate.add_section(type_name, attrs)

    def end_element(self, name):
        if name == "struct":
            self.emit_struct()
            self.struct = None
            self.group = None
        elif name  == "field":
            self.group.fields[-1].values = self.values
        elif name  == "enum":
            self.emit_enum()
            self.enum = None
        elif name == "aggregate":
            self.emit_aggregate()
            self.aggregate = None
        elif name == "panxml":
            # Include at the end so it can depend on us but not the converse
            print('#endif')

    def emit_header(self, name):
        default_fields = []
        for field in self.group.fields:
            if not type(field) is Field:
                continue
            if field.default is not None:
                default_fields.append("   .{} = {}".format(field.name, field.default))
            elif field.type in self.structs:
                default_fields.append("   .{} = {{ {}_header }}".format(field.name, self.gen_prefix(safe_name(field.type.upper()))))

        print('#define %-40s\\' % (name + '_header'))
        if default_fields:
            print(",  \\\n".join(default_fields))
        else:
            print('   0')
        print('')

    def emit_template_struct(self, name, group):
        print("struct %s {" % name)
        group.emit_template_struct("")
        print("};\n")

    def emit_aggregate(self):
        aggregate = self.aggregate
        print("struct %s_packed {" % aggregate.name.lower())
        print("   uint32_t opaque[{}];".format(aggregate.get_size() // 4))
        print("};\n")
        print('#define {}_PACKED_T struct {}_packed'.format(aggregate.name.upper(), aggregate.name.lower()))
        print('#define {}_LENGTH {}'.format(aggregate.name.upper(), aggregate.size))
        if aggregate.align != None:
            print('#define {}_ALIGN {}'.format(aggregate.name.upper(), aggregate.align))
        for section in aggregate.sections:
            print('#define {}_SECTION_{}_TYPE struct {}'.format(aggregate.name.upper(), section.name.upper(), section.type_name))
            print('#define {}_SECTION_{}_PACKED_TYPE {}_PACKED_T'.format(aggregate.name.upper(), section.name.upper(), section.type_name.upper()))
            print('#define {}_SECTION_{}_header {}_header'.format(aggregate.name.upper(), section.name.upper(), section.type_name))
            print('#define {}_SECTION_{}_pack {}_pack'.format(aggregate.name.upper(), section.name.upper(), section.type_name))
            print('#define {}_SECTION_{}_unpack {}_unpack'.format(aggregate.name.upper(), section.name.upper(), section.type_name))
            print('#define {}_SECTION_{}_print {}_print'.format(aggregate.name.upper(), section.name.upper(), section.type_name))
            print('#define {}_SECTION_{}_OFFSET {}'.format(aggregate.name.upper(), section.name.upper(), section.offset))
        print("")

    def emit_struct_detail(self, name, group):
        group.get_length()

        # Should be a whole number of words
        assert((self.group.length % 4) == 0)

        print('#define {} {}'.format (name + "_LENGTH", self.group.length))
        if self.group.align != None:
            print('#define {} {}'.format (name + "_ALIGN", self.group.align))
        print('struct {}_packed {{ uint32_t opaque[{}]; }};'.format(name.lower(), self.group.length // 4))
        print('#define {}_PACKED_T struct {}_packed'.format(name.upper(), name.lower()))

    def emit_pack_function(self, name, group):
        print("#define {}_pack(__packed, __unpacked) \\".format(name))
        print("do { \\")
        print("   {}_PACKED_T __tmp_packed; \\".format(name.upper()))
        group.emit_pack_function()
        print('   *(__packed) = __tmp_packed; \\')
        print("} while (0);\n")

    def emit_unpack_function(self, name, group):
        print("#define {}_unpack(__packed, __unpacked) \\".format(name))
        print("do { \\")
        print("   {}_PACKED_T __tmp_packed = *(__packed); \\".format(name))
        group.emit_unpack_function()
        print("} while (0);\n")

    def emit_print_function(self, name, group):
        print("#ifndef __OPENCL_VERSION__")
        print("static inline void")
        print("{}_print(FILE *fp, const struct {} * values, unsigned indent)\n{{".format(name.upper(), name))

        group.emit_print_function()

        print("}\n")
        print("#endif")

    def emit_struct(self):
        name = self.struct

        self.emit_template_struct(self.struct, self.group)
        self.emit_header(name)
        if self.no_direct_packing == False:
            self.emit_struct_detail(self.struct, self.group)
            self.emit_pack_function(self.struct, self.group)
            self.emit_unpack_function(self.struct, self.group)
        self.emit_print_function(self.struct, self.group)

    def enum_prefix(self, name):
        return 

    def emit_enum(self):
        e_name = enum_name(self.enum)
        prefix = e_name if self.enum != 'Format' else global_prefix
        print('enum {} {{'.format(e_name))

        for value in self.values:
            name = '{}_{}'.format(prefix, value.name)
            name = safe_name(name).upper()
            print('        % -36s = %6d,' % (name, value.value))
        print('};\n')

        print("#ifndef __OPENCL_VERSION__")
        print("static inline const char *")
        print("{}_as_str(enum {} imm)\n{{".format(e_name.lower(), e_name))
        print("    switch (imm) {")
        for value in self.values:
            name = '{}_{}'.format(prefix, value.name)
            name = safe_name(name).upper()
            print('    case {}: return "{}";'.format(name, value.name))
        print('    default: return "XXX: INVALID";')
        print("    }")
        print("}\n")
        print("#endif\n")

    def parse(self, filename):
        file = open(filename, "rb")
        self.parser.ParseFile(file)
        file.close()


parser = argparse.ArgumentParser()
parser.add_argument('input_file')
args = parser.parse_args()

p = Parser()
p.parse(args.input_file)
