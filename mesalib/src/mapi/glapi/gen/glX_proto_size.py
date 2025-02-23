
# (C) Copyright IBM Corporation 2004, 2005
# All Rights Reserved.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# on the rights to use, copy, modify, merge, publish, distribute, sub
# license, and/or sell copies of the Software, and to permit persons to whom
# the Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.  IN NO EVENT SHALL
# IBM AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.
#
# Authors:
#    Ian Romanick <idr@us.ibm.com>

import argparse
import sys, string

import gl_XML, glX_XML
import license


class glx_enum_function(object):
    def __init__(self, func_name, enum_dict):
        self.name = func_name
        self.mode = 1
        self.sig = None

        # "enums" is a set of lists.  The element in the set is the
        # value of the enum.  The list is the list of names for that
        # value.  For example, [0x8126] = {"POINT_SIZE_MIN",
        # "POINT_SIZE_MIN_ARB", "POINT_SIZE_MIN_EXT",
        # "POINT_SIZE_MIN_SGIS"}.

        self.enums = {}

        # "count" is indexed by count values.  Each element of count
        # is a list of index to "enums" that have that number of
        # associated data elements.  For example, [4] = 
        # {GL_AMBIENT, GL_DIFFUSE, GL_SPECULAR, GL_EMISSION,
        # GL_AMBIENT_AND_DIFFUSE} (the enum names are used here,
        # but the actual hexadecimal values would be in the array).

        self.count = {}


        # Fill self.count and self.enums using the dictionary of enums
        # that was passed in.  The generic Get functions (e.g.,
        # GetBooleanv and friends) are handled specially here.  In
        # the data the generic Get functions are referred to as "Get".

        if func_name in ["GetIntegerv", "GetBooleanv", "GetFloatv", "GetDoublev"]:
            match_name = "Get"
        else:
            match_name = func_name

        mode_set = 0
        for enum_name in enum_dict:
            e = enum_dict[ enum_name ]

            if match_name in e.functions:
                [count, mode] = e.functions[ match_name ]

                if mode_set and mode != self.mode:
                    raise RuntimeError("Not all enums for %s have the same mode." % (func_name))

                self.mode = mode

                if e.value in self.enums:
                    if e.name not in self.enums[ e.value ]:
                        self.enums[ e.value ].append( e )
                else:
                    if count not in self.count:
                        self.count[ count ] = []

                    self.enums[ e.value ] = [ e ]
                    self.count[ count ].append( e.value )


        return


    def signature( self ):
        if self.sig == None:
            self.sig = ""
            for i in self.count:
                if i == None:
                    raise RuntimeError("i is None.  WTF?")

                self.count[i].sort()
                for e in self.count[i]:
                    self.sig += "%04x,%d," % (e, i)

        return self.sig


    def is_set( self ):
        return self.mode


    def PrintUsingTable(self):
        """Emit the body of the __gl*_size function using a pair
        of look-up tables and a mask.  The mask is calculated such
        that (e & mask) is unique for all the valid values of e for
        this function.  The result of (e & mask) is used as an index
        into the first look-up table.  If it matches e, then the
        same entry of the second table is returned.  Otherwise zero
        is returned.

        It seems like this should cause better code to be generated.
        However, on x86 at least, the resulting .o file is about 20%
        larger then the switch-statment version.  I am leaving this
        code in because the results may be different on other
        platforms (e.g., PowerPC or x86-64)."""

        return 0
        count = 0
        for a in self.enums:
            count += 1

        if -1 in self.count:
            return 0

        # Determine if there is some mask M, such that M = (2^N) - 1,
        # that will generate unique values for all of the enums.

        mask = 0
        for i in [1, 2, 3, 4, 5, 6, 7, 8]:
            mask = (1 << i) - 1

            fail = 0;
            for a in self.enums:
                for b in self.enums:
                    if a != b:
                        if (a & mask) == (b & mask):
                            fail = 1;

            if not fail:
                break;
            else:
                mask = 0

        if (mask != 0) and (mask < (2 * count)):
            masked_enums = {}
            masked_count = {}

            for i in range(0, mask + 1):
                masked_enums[i] = "0";
                masked_count[i] = 0;

            for c in self.count:
                for e in self.count[c]:
                    i = e & mask
                    enum_obj = self.enums[e][0]
                    masked_enums[i] = '0x%04x /* %s */' % (e, enum_obj.name )
                    masked_count[i] = c


            print('    static const GLushort a[%u] = {' % (mask + 1))
            for e in masked_enums:
                print('        %s, ' % (masked_enums[e]))
            print('    };')

            print('    static const GLubyte b[%u] = {' % (mask + 1))
            for c in masked_count:
                print('        %u, ' % (masked_count[c]))
            print('    };')

            print('    const unsigned idx = (e & 0x%02xU);' % (mask))
            print('')
            print('    return (e == a[idx]) ? (GLint) b[idx] : 0;')
            return 1;
        else:
            return 0;


    def PrintUsingSwitch(self, name):
        """Emit the body of the __gl*_size function using a 
        switch-statement."""

        print('    switch( e ) {')

        for c in sorted(self.count):
            for e in self.count[c]:
                first = 1

                # There may be multiple enums with the same
                # value.  This happens has extensions are
                # promoted from vendor-specific or EXT to
                # ARB and to the core.  Emit the first one as
                # a case label, and emit the others as
                # commented-out case labels.

                list = {}
                for enum_obj in self.enums[e]:
                    list[ enum_obj.priority() ] = enum_obj.name

                keys = sorted(list.keys())
                for k in keys:
                    j = list[k]
                    if first:
                        print('        case GL_%s:' % (j))
                        first = 0
                    else:
                        print('/*      case GL_%s:*/' % (j))

            if c == -1:
                print('            return __gl%s_variable_size( e );' % (name))
            else:
                print('            return %u;' % (c))

        print('        default: return 0;')
        print('    }')


    def Print(self, name):
        print('GLint')
        print('__gl%s_size( GLenum e )' % (name))
        print('{')

        if not self.PrintUsingTable():
            self.PrintUsingSwitch(name)

        print('}')
        print('')


class PrintGlxSizeStubs_common(gl_XML.gl_print_base):
    do_get = (1 << 0)
    do_set = (1 << 1)

    def __init__(self, which_functions):
        gl_XML.gl_print_base.__init__(self)

        self.name = "glX_proto_size.py (from Mesa)"
        self.license = license.bsd_license_template % ( "(C) Copyright IBM Corporation 2004", "IBM")

        self.emit_set = ((which_functions & PrintGlxSizeStubs_common.do_set) != 0)
        self.emit_get = ((which_functions & PrintGlxSizeStubs_common.do_get) != 0)
        return


class PrintGlxSizeStubs_c(PrintGlxSizeStubs_common):
    def printRealHeader(self):
        print('')
        print('#include <X11/Xfuncproto.h>')
        print('#include "util/glheader.h"')
        if self.emit_get:
            print('#include "indirect_size_get.h"')
            print('#include "glxserver.h"')
            print('#include "indirect_util.h"')

        print('#include "indirect_size.h"')

        print('')
        print('')
        print('#ifdef HAVE_FUNC_ATTRIBUTE_ALIAS')
        print('#  define ALIAS2(from,to) \\')
        print('    GLint __gl ## from ## _size( GLenum e ) \\')
        print('        __attribute__ ((alias( # to )));')
        print('#  define ALIAS(from,to) ALIAS2( from, __gl ## to ## _size )')
        print('#else')
        print('#  define ALIAS(from,to) \\')
        print('    GLint __gl ## from ## _size( GLenum e ) \\')
        print('    { return __gl ## to ## _size( e ); }')
        print('#endif')
        print('')
        print('')


    def printBody(self, api):
        enum_sigs = {}
        aliases = []

        for func in api.functionIterateGlx():
            ef = glx_enum_function( func.name, api.enums_by_name )
            if len(ef.enums) == 0:
                continue

            if (ef.is_set() and self.emit_set) or (not ef.is_set() and self.emit_get):
                sig = ef.signature()
                if sig in enum_sigs:
                    aliases.append( [func.name, enum_sigs[ sig ]] )
                else:
                    enum_sigs[ sig ] = func.name
                    ef.Print( func.name )


        for [alias_name, real_name] in aliases:
            print('ALIAS( %s, %s )' % (alias_name, real_name))



class PrintGlxSizeStubs_h(PrintGlxSizeStubs_common):
    def printRealHeader(self):
        print("""/**
 * \\file
 * Prototypes for functions used to determine the number of data elements in
 * various GLX protocol messages.
 *
 * \\author Ian Romanick <idr@us.ibm.com>
 */
""")
        print('#include <X11/Xfuncproto.h>')
        print('')


    def printBody(self, api):
        for func in api.functionIterateGlx():
            ef = glx_enum_function( func.name, api.enums_by_name )
            if len(ef.enums) == 0:
                continue

            if (ef.is_set() and self.emit_set) or (not ef.is_set() and self.emit_get):
                print('extern GLint __gl%s_size(GLenum);' % (func.name))


class PrintGlxReqSize_common(gl_XML.gl_print_base):
    """Common base class for PrintGlxSizeReq_h and PrintGlxSizeReq_h.

    The main purpose of this common base class is to provide the infrastructure
    for the derrived classes to iterate over the same set of functions.
    """

    def __init__(self):
        gl_XML.gl_print_base.__init__(self)

        self.name = "glX_proto_size.py (from Mesa)"
        self.license = license.bsd_license_template % ( "(C) Copyright IBM Corporation 2005", "IBM")


def _parser():
    """Parse arguments and return a namespace."""
    parser = argparse.ArgumentParser()
    parser.set_defaults(which_functions=(PrintGlxSizeStubs_common.do_get |
                                         PrintGlxSizeStubs_common.do_set))
    parser.add_argument('-f',
                        dest='filename',
                        default='gl_API.xml',
                        help='an XML file describing an OpenGL API.')
    parser.add_argument('-m',
                        dest='mode',
                        choices=['size_c', 'size_h'],
                        help='Which file to generate')
    getset = parser.add_mutually_exclusive_group()
    getset.add_argument('--only-get',
                        dest='which_functions',
                        action='store_const',
                        const=PrintGlxSizeStubs_common.do_get,
                        help='only emit "get-type" functions')
    getset.add_argument('--only-set',
                        dest='which_functions',
                        action='store_const',
                        const=PrintGlxSizeStubs_common.do_set,
                        help='only emit "set-type" functions')
    parser.add_argument('--header-tag',
                        dest='header_tag',
                        action='store',
                        default=None,
                        help='set header tag value')
    return parser.parse_args()


def main():
    """Main function."""
    args = _parser()

    if args.mode == "size_c":
        printer = PrintGlxSizeStubs_c(args.which_functions)
    elif args.mode == "size_h":
        printer = PrintGlxSizeStubs_h(args.which_functions)
        if args.header_tag is not None:
            printer.header_tag = args.header_tag

    api = gl_XML.parse_GL_API(args.filename, glX_XML.glx_item_factory())

    printer.Print(api)


if __name__ == '__main__':
    main()
