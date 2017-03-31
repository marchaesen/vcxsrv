#!/usr/bin/env python

# Copyright (C) 2012 Intel Corporation
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

import getopt
import gl_XML
import license
import marshal_XML
import sys


header = """
#ifndef MARSHAL_GENERATABLE_H
#define MARSHAL_GENERATABLE_H
"""

footer = """
#endif /* MARSHAL_GENERATABLE_H */
"""


class PrintCode(gl_XML.gl_print_base):
    def __init__(self):
        super(PrintCode, self).__init__()

        self.name = 'gl_marshal_h.py'
        self.license = license.bsd_license_template % (
            'Copyright (C) 2012 Intel Corporation', 'INTEL CORPORATION')

    def printRealHeader(self):
        print header

    def printRealFooter(self):
        print footer

    def printBody(self, api):
        print 'enum marshal_dispatch_cmd_id'
        print '{'
        for func in api.functionIterateAll():
            flavor = func.marshal_flavor()
            if flavor in ('skip', 'sync'):
                continue
            print '   DISPATCH_CMD_{0},'.format(func.name)
        print '};'


def show_usage():
    print 'Usage: %s [-f input_file_name]' % sys.argv[0]
    sys.exit(1)


if __name__ == '__main__':
    file_name = 'gl_API.xml'

    try:
        (args, trail) = getopt.getopt(sys.argv[1:], 'm:f:')
    except Exception,e:
        show_usage()

    for (arg,val) in args:
        if arg == '-f':
            file_name = val

    printer = PrintCode()

    api = gl_XML.parse_GL_API(file_name, marshal_XML.marshal_item_factory())
    printer.Print(api)
