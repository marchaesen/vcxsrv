
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

import collections
import functools
import itertools
import os.path
import re
import sys


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
        idxs = sorted(self.idxs) + [len(self.table)]

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

    def format(self, string_table, idx_table):
        if len(self.values):
            values_offsets = []
            for value in self.values:
                while value[1] >= len(values_offsets):
                    values_offsets.append(-1)
                values_offsets[value[1]] = string_table.add(strip_prefix(value[0]))
            return '{%s, %s(~0u), %s, %s}' % (
                string_table.add(self.name), self.s_name,
                len(values_offsets), idx_table.add(values_offsets))
        else:
            return '{%s, %s(~0u)}' % (string_table.add(self.name), self.s_name)

    def __eq__(self, other):
        return (self.s_name == other.s_name and
                self.name == other.name and
                len(self.values) == len(other.values) and
                all(a[0] == b[0] and a[1] == b[1] for a, b, in zip(self.values, other.values)))

    def __ne__(self, other):
        return not (self == other)


class FieldTable:
    """
    A class for collecting multiple arrays of register fields in a single big
    array that is used by indexing (to avoid relocations in the resulting binary)
    """
    def __init__(self):
        self.table = []
        self.idxs = set()
        self.name_to_idx = collections.defaultdict(lambda: [])

    def add(self, array):
        """
        Add an array of Field objects, and return the index of where to find
        the array in the table.
        """
        # Check if we can find the array in the table already
        for base_idx in self.name_to_idx.get(array[0].name, []):
            if base_idx + len(array) > len(self.table):
                continue

            for i, a in enumerate(array):
                b = self.table[base_idx + i]
                if a != b:
                    break
            else:
                return base_idx

        base_idx = len(self.table)
        self.idxs.add(base_idx)

        for field in array:
            self.name_to_idx[field.name].append(len(self.table))
            self.table.append(field)

        return base_idx

    def emit(self, filp, string_table, idx_table):
        """
        Write
        static const struct si_field sid_fields_table[] = { ... };
        to filp.
        """
        idxs = sorted(self.idxs) + [len(self.table)]

        filp.write('static const struct si_field sid_fields_table[] = {\n')

        for start, end in zip(idxs, idxs[1:]):
            filp.write('\t/* %s */\n' % (start))
            for field in self.table[start:end]:
                filp.write('\t%s,\n' % (field.format(string_table, idx_table)))

        filp.write('};\n')


class Reg:
    def __init__(self, r_name):
        self.r_name = r_name
        self.name = strip_prefix(r_name)
        self.fields = []

    def __eq__(self, other):
        if not isinstance(other, Reg):
            return False
        return (self.r_name == other.r_name and
                self.name == other.name and
                len(self.fields) == len(other.fields) and
                all(a == b for a, b in zip(self.fields, other.fields)))

    def __ne__(self, other):
        return not (self == other)


def strip_prefix(s):
    '''Strip prefix in the form ._.*_, e.g. R_001234_'''
    return s[s[2:].find('_')+3:]


class Asic:
    """
    Store the registers of one ASIC class / group of classes.
    """
    def __init__(self, name):
        self.name = name
        self.registers = []

    def parse(self, filp, packets, older_asics):
        """
        Parse registers from the given header file. Packets are separately
        stored in the packets array.
        """
        for line in filp:
            if not line.startswith('#define '):
                continue

            line = line[8:].strip()

            if line.startswith('R_'):
                name = line.split()[0]

                for it in self.registers:
                    if it.r_name == name:
                        sys.exit('Duplicate register define: %s' % (name))
                else:
                    reg = Reg(name)
                    self.registers.append(reg)

            elif line.startswith('S_'):
                name = line[:line.find('(')]

                for it in reg.fields:
                    if it.s_name == name:
                        sys.exit('Duplicate field define: %s' % (name))
                else:
                    field = Field(reg, name)
                    reg.fields.append(field)

            elif line.startswith('V_'):
                split = line.split()
                name = split[0]
                value = int(split[1], 0)

                for (n,v) in field.values:
                    if n == name:
                        sys.exit('Duplicate value define: name = ' + name)

                field.values.append((name, value))

            elif line.startswith('PKT3_') and line.find('0x') != -1 and line.find('(') == -1:
                packets.append(line.split()[0])

        # Copy values for corresponding fields from older ASICs if they were
        # not redefined
        for reg in self.registers:
            old_reg = False
            for field in reg.fields:
                if len(field.values) > 0:
                    continue
                if old_reg is False:
                    for old_reg in itertools.chain(
                            *(asic.registers for asic in reversed(older_asics))):
                        if old_reg.name == reg.name:
                            break
                    else:
                        old_reg = None
                if old_reg is not None:
                    for old_field in old_reg.fields:
                        if old_field.name == field.name:
                            field.values = old_field.values
                            break

        # Copy fields to indexed registers which have their fields only defined
        # at register index 0.
        # For example, copy fields from CB_COLOR0_INFO to CB_COLORn_INFO, n > 0.
        match_number = re.compile('[0-9]+')
        reg_dict = dict()

        # Create a dict of registers with fields and '0' in their name
        for reg in self.registers:
            if len(reg.fields) and reg.name.find('0') != -1:
                reg_dict[reg.name] = reg

        # Assign fields
        for reg in self.registers:
            if not len(reg.fields):
                reg0 = reg_dict.get(match_number.sub('0', reg.name))
                if reg0 != None:
                    reg.fields = reg0.fields


def write_tables(asics, packets):
    strings = StringTable()
    strings_offsets = IntTable("int")
    fields = FieldTable()

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

    regs = {}
    for asic in asics:
        print 'static const struct si_reg %s_reg_table[] = {' % (asic.name)
        for reg in asic.registers:
            # Only output a register that was changed or added relative to
            # the previous generation
            previous = regs.get(reg.r_name, None)
            if previous == reg:
                continue

            if len(reg.fields):
                print '\t{%s, %s, %s, %s},' % (strings.add(reg.name), reg.r_name,
                    len(reg.fields), fields.add(reg.fields))
            else:
                print '\t{%s, %s},' % (strings.add(reg.name), reg.r_name)

            regs[reg.r_name] = reg
        print '};'
        print

    fields.emit(sys.stdout, strings, strings_offsets)

    print

    strings.emit(sys.stdout, "sid_strings")

    print

    strings_offsets.emit(sys.stdout, "sid_strings_offsets")

    print
    print '#endif'


def main():
    asics = []
    packets = []
    for arg in sys.argv[1:]:
        basename = os.path.basename(arg)
        m = re.match(r'(.*)\.h', basename)
        asic = Asic(m.group(1))
        with open(arg) as filp:
            asic.parse(filp, packets, asics)
        asics.append(asic)
    write_tables(asics, packets)


if __name__ == '__main__':
    main()

# kate: space-indent on; indent-width 4; replace-tabs on;
