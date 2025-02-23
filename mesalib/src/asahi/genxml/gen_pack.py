#encoding=utf-8

# Copyright 2016 Intel Corporation
# Copyright 2016 Broadcom
# Copyright 2020 Collabora, Ltd.
# SPDX-License-Identifier: MIT

import xml.parsers.expat
import sys
import operator
import math
import platform
from functools import reduce

global_prefix = "agx"

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
        '*': '',
        '"': '',
        '+': '',
        '\'': '',
        '?': '',
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
    return f"{global_prefix}_{safe_name(name)}".lower()

MODIFIERS = ["shr", "minus", "align", "log2", "groups"]

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
            print(f"#error Field {self.name} has bool type but more than one bit of size");

        if "prefix" in attrs:
            self.prefix = safe_name(attrs["prefix"]).upper()
        else:
            self.prefix = None

        self.modifier = parse_modifier(attrs.get("modifier"))
        self.exact = attrs.get("exact")
        self.default = None

        if self.exact is not None:
            self.default = self.exact
        elif self.modifier is not None:
            # Set the default value to encode to zero
            mod = self.modifier
            if mod[0] == 'log2':
                self.default = 1
            elif mod[0] == 'minus':
                self.default = mod[1]
            elif mod[0] == 'groups':
                # The zero encoding means "all"
                self.default = (1 << int(attrs["size"])) * mod[1]
            elif mod[0] in ['shr', 'align']:
                # Zero encodes to zero
                pass
            else:
                assert(0)

        # Map enum values
        if self.type in self.parser.enums and self.default is not None:
            self.default = safe_name(f'{global_prefix}_{self.type}_{self.default}').upper()


    def emit_template_struct(self, dim):
        if self.type == 'address':
            type = 'uint64_t'
        elif self.type == 'bool':
            type = 'bool'
        elif self.type in ['float', 'half', 'lod']:
            type = 'float'
        elif self.type in ['uint', 'hex'] and self.end - self.start > 32:
            type = 'uint64_t'
        elif self.type == 'int':
            type = 'int32_t'
        elif self.type in ['uint', 'hex']:
            type = 'uint32_t'
        elif self.type in self.parser.structs:
            type = 'struct ' + self.parser.gen_prefix(safe_name(self.type.upper()))
        elif self.type in self.parser.enums:
            type = 'enum ' + enum_name(self.type)
        else:
            print(f"#error unhandled type: {self.type}")
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

            any_fields = False
            for field in self.fields:
                if not field.exact:
                    field.emit_template_struct(dim)
                    any_fields = True

            if not any_fields:
                print("   int dummy;")

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
            field_path = f'{path}{field.name}'
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
            field_path = f'{path}{field.name}'
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
                print(f"   assert((values->{field.name} & {mask}) == 0);")
            elif field.modifier[0] == "minus":
                print(f"   assert(values->{field.name} >= {field.modifier[1]});")
            elif field.modifier[0] == "log2":
                print(f"   assert(IS_POT_NONZERO(values->{field.name}));")

        for index in range(math.ceil(self.length / 4)):
            # Handle MBZ words
            if not index in words:
                print("   cl[%2d] = 0;" % index)
                continue

            word = words[index]

            word_start = index * 32

            v = None
            prefix = "   cl[%2d] =" % index

            lines = []

            for contributor in word.contributors:
                field = contributor.field
                name = field.name
                start = contributor.start
                end = contributor.end
                contrib_word_start = (start // 32) * 32
                start -= contrib_word_start
                end -= contrib_word_start

                value = f"values->{contributor.path}"
                if field.exact:
                    value = field.default

                # These types all use util_bitpack_uint
                pack_as_uint = field.type in ["uint", "hex", "address", "bool"]
                pack_as_uint |= field.type in self.parser.enums
                start_adjusted = start
                value_unshifted = None

                if field.modifier is not None:
                    if field.modifier[0] == "shr":
                        if pack_as_uint and start >= field.modifier[1]:
                            # For uint, we fast path.  If we do `(a >> 2) << 2`,
                            # clang will generate a mask in release builds, even
                            # though we know we're aligned. So don't generate
                            # that to avoid the masking.
                            start_adjusted = start - field.modifier[1]
                        else:
                            value = f"{value} >> {field.modifier[1]}"
                    elif field.modifier[0] == "minus":
                        value = f"{value} - {field.modifier[1]}"
                    elif field.modifier[0] == "align":
                        value = f"ALIGN_POT({value}, {field.modifier[1]})"
                    elif field.modifier[0] == "log2":
                        value = f"util_logbase2({value})"
                    elif field.modifier[0] == "groups":
                        value = "__gen_to_groups({}, {}, {})".format(value,
                                field.modifier[1], end - start + 1)

                if pack_as_uint:
                    bits = (end - start_adjusted + 1)
                    if bits < 64 and not field.exact:
                        # Add some nicer error checking
                        label = f"{self.label}::{name}"
                        bound = hex(1 << bits)
                        print(f"   agx_genxml_validate_bounds(\"{label}\", {value}, {bound}ull);")

                    s = f"util_bitpack_uint({value}, {start_adjusted}, {end})"
                elif field.type == "int":
                    s = "util_bitpack_sint(%s, %d, %d)" % \
                        (value, start, end)
                elif field.type == "float":
                    assert(start == 0 and end == 31)
                    s = f"util_bitpack_float({value})"
                elif field.type == "half":
                    assert(start == 0 and end == 15)
                    s = f"_mesa_float_to_half({value})"
                elif field.type == "lod":
                    assert(end - start + 1 == 10)
                    s = "__gen_pack_lod(%s, %d, %d)" % (value, start, end)
                else:
                    s = f"#error unhandled field {contributor.path}, type {field.type}"

                if not s == None:
                    shift = word_start - contrib_word_start
                    if shift:
                        s = "%s >> %d" % (s, shift)

                    if contributor == word.contributors[-1]:
                        lines.append(f"{prefix} {s};")
                    else:
                        lines.append(f"{prefix} {s} |")
                    prefix = "           "

            for ln in lines:
                print(ln)

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
        validation = []

        for index in range(self.length // 4):
            base = index * 32
            word = words.get(index, self.Word())
            masks = [self.mask_for_word(index, c.start, c.end) for c in word.contributors]
            mask = reduce(lambda x,y: x | y, masks, 0)

            ALL_ONES = 0xffffffff

            if mask != ALL_ONES:
                bad_mask = hex(mask ^ ALL_ONES)
                validation.append(f'agx_genxml_validate_mask(fp, \"{self.label}\", cl, {index}, {bad_mask})')

        fieldrefs = []
        self.collect_fields(self.fields, 0, '', fieldrefs)
        for fieldref in fieldrefs:
            field = fieldref.field
            convert = None

            args = []
            args.append('(CONST uint32_t *) cl')
            args.append(str(fieldref.start))
            args.append(str(fieldref.end))

            if field.type in set(["uint", "address", "hex"]) | self.parser.enums:
                convert = "__gen_unpack_uint"
            elif field.type == "int":
                convert = "__gen_unpack_sint"
            elif field.type == "bool":
                convert = "__gen_unpack_uint"
            elif field.type == "float":
                convert = "__gen_unpack_float"
            elif field.type == "half":
                convert = "__gen_unpack_half"
            elif field.type == "lod":
                convert = "__gen_unpack_lod"
            else:
                s = f"/* unhandled field {field.name}, type {field.type} */\n"

            suffix = ""
            prefix = ""
            if field.modifier:
                if field.modifier[0] == "minus":
                    suffix = f" + {field.modifier[1]}"
                elif field.modifier[0] == "shr":
                    suffix = f" << {field.modifier[1]}"
                if field.modifier[0] == "log2":
                    prefix = "1 << "
                elif field.modifier[0] == "groups":
                    prefix = "__gen_from_groups("
                    suffix = ", {}, {})".format(field.modifier[1],
                                                fieldref.end - fieldref.start + 1)

            if field.type in self.parser.enums and not field.exact:
                prefix = f"(enum {enum_name(field.type)}) {prefix}"

            decoded = f"{prefix}{convert}({', '.join(args)}){suffix}"

            if field.exact:
                name = self.label
                validation.append(f'agx_genxml_validate_exact(fp, \"{name}\", {decoded}, {field.default})')
            else:
                print(f'   values->{fieldref.path} = {decoded};')

            if field.modifier and field.modifier[0] == "align":
                assert(not field.exact)
                mask = hex(field.modifier[1] - 1)
                print(f'   assert(!(values->{fieldref.path} & {mask}));')

        if len(validation) > 1:
            print('   bool valid = true;')
            for v in validation:
                print(f'   valid &= {v};')
            print("   return valid;")
        elif len(validation) == 1:
            print(f"   return {validation[0]};")
        else:
            print("   return true;")

    def emit_print_function(self):
        for field in self.fields:
            convert = None
            name, val = field.human_name, f'values->{field.name}'

            if field.exact:
                continue

            if field.type in self.parser.structs:
                pack_name = self.parser.gen_prefix(safe_name(field.type)).upper()
                print(f'   fprintf(fp, "%*s{field.human_name}:\\n", indent, "");')
                print(f"   {pack_name}_print(fp, &values->{field.name}, indent + 2);")
            elif field.type == "address":
                # TODO resolve to name
                print(f'   fprintf(fp, "%*s{name}: 0x%" PRIx64 "\\n", indent, "", {val});')
            elif field.type in self.parser.enums:
                print(f'   if ({enum_name(field.type)}_as_str({val}))')
                print(f'     fprintf(fp, "%*s{name}: %s\\n", indent, "", {enum_name(field.type)}_as_str({val}));')
                print(f'   else')
                print(f'     fprintf(fp, "%*s{name}: unknown %X (XXX)\\n", indent, "", {val});')
            elif field.type == "int":
                print(f'   fprintf(fp, "%*s{name}: %d\\n", indent, "", {val});')
            elif field.type == "bool":
                print(f'   fprintf(fp, "%*s{name}: %s\\n", indent, "", {val} ? "true" : "false");')
            elif field.type in ["float", "lod", "half"]:
                print(f'   fprintf(fp, "%*s{name}: %f\\n", indent, "", {val});')
            elif field.type in ["uint", "hex"] and (field.end - field.start) >= 32:
                print(f'   fprintf(fp, "%*s{name}: 0x%" PRIx64 "\\n", indent, "", {val});')
            elif field.type == "hex":
                print(f'   fprintf(fp, "%*s{name}: 0x%" PRIx32 "\\n", indent, "", {val});')
            else:
                print(f'   fprintf(fp, "%*s{name}: %u\\n", indent, "", {val});')

class Value(object):
    def __init__(self, attrs):
        self.name = attrs["name"]
        self.value = int(attrs["value"], 0)

class Parser(object):
    def __init__(self):
        self.parser = xml.parsers.expat.ParserCreate()
        self.parser.StartElementHandler = self.start_element
        self.parser.EndElementHandler = self.end_element

        self.struct = None
        self.structs = {}
        # Set of enum names we've seen.
        self.enums = set()

    def gen_prefix(self, name):
        return f'{global_prefix.upper()}_{name}'

    def start_element(self, name, attrs):
        if name == "genxml":
            print(pack_header)
        elif name == "struct":
            name = attrs["name"]
            object_name = self.gen_prefix(safe_name(name.upper()))
            self.struct = object_name

            self.group = Group(self, None, 0, 1, name)
            if "size" in attrs:
                self.group.length = int(attrs["size"])
            self.group.align = int(attrs["align"]) if "align" in attrs else None
            self.structs[attrs["name"]] = self.group
        elif name == "field" and self.group is not None:
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

    def end_element(self, name):
        if name == "struct":
            if self.struct is not None:
                self.emit_struct()
                self.struct = None

            self.group = None
        elif name  == "field" and self.group is not None:
            self.group.fields[-1].values = self.values
        elif name  == "enum":
            self.emit_enum()
            self.enum = None

    def emit_header(self, name):
        default_fields = []
        for field in self.group.fields:
            if not type(field) is Field or field.exact:
                continue
            if field.default is not None:
                default_fields.append(f"   .{field.name} = {field.default}")
            elif field.type in self.structs:
                default_fields.append(f"   .{field.name} = {{ {self.gen_prefix(safe_name(field.type.upper()))}_header }}")

        if default_fields:
            print('#define %-40s\\' % (name + '_header'))
            print(",  \\\n".join(default_fields))
        else:
            print(f'#define {name}_header 0')
        print('')

    def emit_template_struct(self, name, group):
        print("struct %s {" % name)
        group.emit_template_struct("")
        print("};\n")

    def emit_pack_function(self, name, group):
        print("static inline void\n%s_pack(GLOBAL uint32_t * restrict cl,\n%sconst struct %s * restrict values)\n{" %
              (name, ' ' * (len(name) + 6), name))

        group.emit_pack_function()

        print("}\n")

        print(f"#define {name + '_LENGTH'} {self.group.length}")
        if self.group.align != None:
            print(f"#define {name + '_ALIGN'} {self.group.align}")

        # round up to handle 6 half-word USC structures
        words = (self.group.length + 4 - 1) // 4
        print(f'struct {name.lower()}_packed {{ uint32_t opaque[{words}];}};')

    def emit_unpack_function(self, name, group):
        print("static inline bool")
        print("%s_unpack(FILE *fp, CONST uint8_t * restrict cl,\n%sstruct %s * restrict values)\n{" %
              (name.upper(), ' ' * (len(name) + 8), name))

        group.emit_unpack_function()

        print("}\n")

    def emit_print_function(self, name, group):
        print("#ifndef __OPENCL_VERSION__")
        print("static inline void")
        print(f"{name.upper()}_print(FILE *fp, const struct {name} * values, unsigned indent)\n{{")

        group.emit_print_function()

        print("}")
        print("#endif\n")

    def emit_struct(self):
        name = self.struct

        self.emit_template_struct(self.struct, self.group)
        self.emit_header(name)
        self.emit_pack_function(self.struct, self.group)
        self.emit_unpack_function(self.struct, self.group)
        self.emit_print_function(self.struct, self.group)

    def enum_prefix(self, name):
        return 

    def emit_enum(self):
        e_name = enum_name(self.enum)
        prefix = e_name if self.enum != 'Format' else global_prefix
        print(f'enum {e_name} {{')

        for value in self.values:
            name = f'{prefix}_{value.name}'
            name = safe_name(name).upper()
            print(f'   {name} = {value.value},')
        print('};\n')

        print("#ifndef __OPENCL_VERSION__")
        print("static inline const char *")
        print(f"{e_name.lower()}_as_str(enum {e_name} imm)\n{{")
        print("    switch (imm) {")
        for value in self.values:
            name = f'{prefix}_{value.name}'
            name = safe_name(name).upper()
            print(f'    case {name}: return "{value.name}";')
        print('    default: return NULL;')
        print("    }")
        print("}")
        print("#endif\n")

    def parse(self, filename):
        file = open(filename, "rb")
        self.parser.ParseFile(file)
        file.close()

if len(sys.argv) < 3:
    print("Missing input files file specified")
    sys.exit(1)

input_file = sys.argv[1]
pack_header = open(sys.argv[2]).read()

p = Parser()
p.parse(input_file)
