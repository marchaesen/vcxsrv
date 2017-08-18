
# (C) Copyright IBM Corporation 2004
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

import gl_XML
import license
import sys, getopt

class PrintFunctionTable(gl_XML.gl_print_base):
    def __init__(self):
        gl_XML.gl_print_base.__init__(self)

        self.header_tag = '__GLFUNCTIONS_H__'
        self.name = "gl_functions.py"
        self.license = license.bsd_license_template % ("(C) Copyright IBM Corporation 2005", "IBM")
        return


    def printRealHeader(self):
        print """
#include "glapitable.h"
#include "glapi.h"
#include "u_thread.h"
#include "dispatch.h"
"""
        return

    def printBody(self, api):

        functions = []
        abi_functions = []
        alias_functions = []
        count = 0
        for f in api.functionIterateByOffset():
            if not f.is_abi():
                functions.append( [f, count] )
                count += 1
            else:
                abi_functions.append( [f, -1] )

        for f, index in abi_functions + functions:
            arg_string=""
            nrParams=0
            comma=""

            for p in f.parameters:
              if not p.is_padding:
                nrParams+=1
                arg_string+=comma
                comma=", "
                arg_string+="a%d"%nrParams

            print '#define gl%s(%s) CALL_%s(GET_DISPATCH(), (%s))' % (f.name, arg_string, f.name, arg_string)

        return


def show_usage():
    print "Usage: %s [-f input_file_name]" % sys.argv[0]
    sys.exit(1)

if __name__ == '__main__':
    file_name = "gl_API.xml"

    try:
        (args, trail) = getopt.getopt(sys.argv[1:], "f:")
    except Exception,e:
        show_usage()

    for (arg,val) in args:
        if arg == "-f":
            file_name = val

    printer = PrintFunctionTable()

    api = gl_XML.parse_GL_API( file_name )

    printer.Print( api )
