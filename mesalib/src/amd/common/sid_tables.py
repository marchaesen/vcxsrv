#!/usr/bin/env python

CopyRight = '''
/*
 * Copyright 2015 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */
'''

import sys
import re


class StringTable:
    """
    A class for collecting multiple strings in a single larger string that is
    used by indexing (to avoid relocations in the resulting binary)
    """
    def __init__(self):
        self.table = []
        self.length = 0

    def add(self, string):
        # We might get lucky with string being a suffix of a previously added string
        for te in self.table:
            if te[0].endswith(string):
                idx = te[1] + len(te[0]) - len(string)
                te[2].add(idx)
                return idx

        idx = self.length
        self.table.append((string, idx, set((idx,))))
        self.length += len(string) + 1

        return idx

    def emit(self, filp, name, static=True):
        """
        Write
        [static] const char name[] = "...";
        to filp.
        """
        fragments = [
            '"%s\\0" /* %s */' % (
                te[0].encode('string_escape'),
                ', '.join(str(idx) for idx in te[2])
            )
            for te in self.table
        ]
        filp.write('%sconst char %s[] =\n%s;\n' % (
            'static ' if static else '',
            name,
            '\n'.join('\t' + fragment for fragment in fragments)
        ))

class IntTable:
    """
    A class for collecting multiple arrays of integers in a single big array
    that is used by indexing (to avoid relocations in the resulting binary)
    """
    def __init__(self, typename):
        self.typename = typename
        self.table = []
        self.idxs = set()

    def add(self, array):
        # We might get lucky and find the array somewhere in the existing data
        try:
            idx = 0
            while True:
                idx = self.table.index(array[0], idx, len(self.table) - len(array) + 1)

                for i in range(1, len(array)):
                    if array[i] != self.table[idx + i]:
                        break
                else:
                    self.idxs.add(idx)
                    return idx

                idx += 1
        except ValueError:
            pass

        idx = len(self.table)
        self.table += array
        self.idxs.add(idx)
        return idx

    def emit(self, filp, name, static=True):
        """
        Write
        [static] const typename name[] = { ... };
        to filp.
        """
        idxs = sorted(self.idxs) + [-1]

        fragments = [
            ('\t/* %s */ %s' % (
                idxs[i],
                ' '.join((str(elt) + ',') for elt in self.table[idxs[i]:idxs[i+1]])
            ))
            for i in range(len(idxs) - 1)
        ]

        filp.write('%sconst %s %s[] = {\n%s\n};\n' % (
            'static ' if static else '',
            self.typename, name,
            '\n'.join(fragments)
        ))

class Field:
    def __init__(self, reg, s_name):
        self.s_name = s_name
        self.name = strip_prefix(s_name)
        self.values = []
        self.varname_values = '%s__%s__values' % (reg.r_name.lower(), self.name.lower())

class Reg:
    def __init__(self, r_name):
        self.r_name = r_name
        self.name = strip_prefix(r_name)
        self.fields = []
        self.own_fields = True


def strip_prefix(s):
    '''Strip prefix in the form ._.*_, e.g. R_001234_'''
    return s[s[2:].find('_')+3:]


def parse(filename):
    stream = open(filename)
    regs = []
    packets = []

    for line in stream:
        if not line.startswith('#define '):
            continue

        line = line[8:].strip()

        if line.startswith('R_'):
            reg = Reg(line.split()[0])
            regs.append(reg)

        elif line.startswith('S_'):
            field = Field(reg, line[:line.find('(')])
            reg.fields.append(field)

        elif line.startswith('V_'):
            split = line.split()
            field.values.append((split[0], int(split[1], 0)))

        elif line.startswith('PKT3_') and line.find('0x') != -1 and line.find('(') == -1:
            packets.append(line.split()[0])

    # Copy fields to indexed registers which have their fields only defined
    # at register index 0.
    # For example, copy fields from CB_COLOR0_INFO to CB_COLORn_INFO, n > 0.
    match_number = re.compile('[0-9]+')
    reg_dict = dict()

    # Create a dict of registers with fields and '0' in their name
    for reg in regs:
        if len(reg.fields) and reg.name.find('0') != -1:
            reg_dict[reg.name] = reg

    # Assign fields
    for reg in regs:
        if not len(reg.fields):
            reg0 = reg_dict.get(match_number.sub('0', reg.name))
            if reg0 != None:
                reg.fields = reg0.fields
                reg.fields_owner = reg0
                reg.own_fields = False

    return (regs, packets)


def write_tables(tables):
    regs = tables[0]
    packets = tables[1]

    strings = StringTable()
    strings_offsets = IntTable("int")

    print '/* This file is autogenerated by sid_tables.py from sid.h. Do not edit directly. */'
    print
    print CopyRight.strip()
    print '''
#ifndef SID_TABLES_H
#define SID_TABLES_H

struct si_field {
        unsigned name_offset;
        unsigned mask;
        unsigned num_values;
        unsigned values_offset; /* offset into sid_strings_offsets */
};

struct si_reg {
        unsigned name_offset;
        unsigned offset;
        unsigned num_fields;
        unsigned fields_offset;
};

struct si_packet3 {
        unsigned name_offset;
        unsigned op;
};
'''

    print 'static const struct si_packet3 packet3_table[] = {'
    for pkt in packets:
        print '\t{%s, %s},' % (strings.add(pkt[5:]), pkt)
    print '};'
    print

    print 'static const struct si_field sid_fields_table[] = {'

    fields_idx = 0
    for reg in regs:
        if len(reg.fields) and reg.own_fields:
            print '\t/* %s */' % (fields_idx)

            reg.fields_idx = fields_idx

            for field in reg.fields:
                if len(field.values):
                    values_offsets = []
                    for value in field.values:
                        while value[1] >= len(values_offsets):
                            values_offsets.append(-1)
                        values_offsets[value[1]] = strings.add(strip_prefix(value[0]))
                    print '\t{%s, %s(~0u), %s, %s},' % (
                        strings.add(field.name), field.s_name,
                        len(values_offsets), strings_offsets.add(values_offsets))
                else:
                    print '\t{%s, %s(~0u)},' % (strings.add(field.name), field.s_name)
                fields_idx += 1

    print '};'
    print

    print 'static const struct si_reg sid_reg_table[] = {'
    for reg in regs:
        if len(reg.fields):
            print '\t{%s, %s, %s, %s},' % (strings.add(reg.name), reg.r_name,
                len(reg.fields), reg.fields_idx if reg.own_fields else reg.fields_owner.fields_idx)
        else:
            print '\t{%s, %s},' % (strings.add(reg.name), reg.r_name)
    print '};'
    print

    strings.emit(sys.stdout, "sid_strings")

    print

    strings_offsets.emit(sys.stdout, "sid_strings_offsets")

    print
    print '#endif'


def main():
    tables = []
    for arg in sys.argv[1:]:
        tables.extend(parse(arg))
    write_tables(tables)


if __name__ == '__main__':
    main()

# kate: space-indent on; indent-width 4; replace-tabs on;
