
# (C) Copyright IBM Corporation 2005
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
import copy

import license
import gl_XML, glX_XML

def should_use_push(registers):
    for [reg, offset] in registers:
        if reg[1:4] == "xmm":
            return 0

    N = len(registers)
    return (N & 1) != 0


def local_size(registers):
    # The x86-64 ABI says "the value (%rsp - 8) is always a multiple of
    # 16 when control is transfered to the function entry point."  This
    # means that the local stack usage must be (16*N)+8 for some value
    # of N.  (16*N)+8 = (8*(2N))+8 = 8*(2N+1).  As long as N is odd, we
    # meet this requirement.

    N = (len(registers) | 1)
    return 8*N


def save_all_regs(registers):
    adjust_stack = 0
    if not should_use_push(registers):
        adjust_stack = local_size(registers)
        print('\tsubq\t$%u, %%rsp' % (adjust_stack))

    for [reg, stack_offset] in registers:
        save_reg( reg, stack_offset, adjust_stack )
    return


def restore_all_regs(registers):
    adjust_stack = 0
    if not should_use_push(registers):
        adjust_stack = local_size(registers)

    temp = copy.deepcopy(registers)
    while len(temp):
        [reg, stack_offset] = temp.pop()
        restore_reg(reg, stack_offset, adjust_stack)

    if adjust_stack:
        print('\taddq\t$%u, %%rsp' % (adjust_stack))
    return


def save_reg(reg, offset, use_move):
    if use_move:
        if offset == 0:
            print('\tmovq\t%s, (%%rsp)' % (reg))
        else:
            print('\tmovq\t%s, %u(%%rsp)' % (reg, offset))
    else:
        print('\tpushq\t%s' % (reg))

    return


def restore_reg(reg, offset, use_move):
    if use_move:
        if offset == 0:
            print('\tmovq\t(%%rsp), %s' % (reg))
        else:
            print('\tmovq\t%u(%%rsp), %s' % (offset, reg))
    else:
        print('\tpopq\t%s' % (reg))

    return


class PrintGenericStubs(gl_XML.gl_print_base):

    def __init__(self):
        gl_XML.gl_print_base.__init__(self)

        self.name = "gl_x86-64_asm.py (from Mesa)"
        self.license = license.bsd_license_template % ("(C) Copyright IBM Corporation 2005", "IBM")
        return


    def get_stack_size(self, f):
        size = 0
        for p in f.parameterIterator():
            size += p.get_stack_size()

        return size


    def printRealHeader(self):
        print("/* If we build with gcc's -fvisibility=hidden flag, we'll need to change")
        print(" * the symbol visibility mode to 'default'.")
        print(' */')
        print('')
        print('#include "x86/assyntax.h"')
        print('')
        print('#ifdef __GNUC__')
        print('#  pragma GCC visibility push(default)')
        print('#  define HIDDEN(x) .hidden x')
        print('#else')
        print('#  define HIDDEN(x)')
        print('#endif')
        print('')
        print('#  define GL_PREFIX(n) GLNAME(CONCAT(gl,n))')
        print('')
        print('\t.text')
        print('')
        print('_x86_64_get_dispatch:')
        print('\tmovq\t_mesa_glapi_tls_Dispatch@GOTTPOFF(%rip), %rax')
        print('\tmovq\t%fs:(%rax), %rax')
        print('\tret')
        print('\t.size\t_x86_64_get_dispatch, .-_x86_64_get_dispatch')
        print('')
        return


    def printRealFooter(self):
        print('')
        print('#if defined (__ELF__) && defined (__linux__)')
        print('	.section .note.GNU-stack,"",%progbits')
        print('#endif')
        return


    def printFunction(self, f):

        # The x86-64 ABI divides function parameters into a couple
        # classes.  For the OpenGL interface, the only ones that are
        # relevant are INTEGER and SSE.  Basically, the first 8
        # GLfloat or GLdouble parameters are placed in %xmm0 - %xmm7,
        # the first 6 non-GLfloat / non-GLdouble parameters are placed
        # in registers listed in int_parameters.
        #
        # If more parameters than that are required, they are passed
        # on the stack.  Therefore, we just have to make sure that
        # %esp hasn't changed when we jump to the actual function.
        # Since we're jumping to the function (and not calling it), we
        # have to make sure of that anyway!

        int_parameters = ["%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"]

        int_class = 0
        sse_class = 0
        stack_offset = 0
        registers = []
        for p in f.parameterIterator():
            type_name = p.get_base_type_string()

            if p.is_pointer() or (type_name != "GLfloat" and type_name != "GLdouble"):
                if int_class < 6:
                    registers.append( [int_parameters[int_class], stack_offset] )
                    int_class += 1
                    stack_offset += 8
            else:
                if sse_class < 8:
                    registers.append( ["%%xmm%u" % (sse_class), stack_offset] )
                    sse_class += 1
                    stack_offset += 8

        if ((int_class & 1) == 0) and (sse_class == 0):
            registers.append( ["%rbp", 0] )


        name = f.dispatch_name()

        print('\t.p2align\t4,,15')
        print('\t.globl\tGL_PREFIX(%s)' % (name))
        print('\t.type\tGL_PREFIX(%s), @function' % (name))
        if not f.is_static_entry_point(f.name):
            print('\tHIDDEN(GL_PREFIX(%s))' % (name))
        print('GL_PREFIX(%s):' % (name))
        print('\tcall\t_x86_64_get_dispatch@PLT')
        print('\tmovq\t%u(%%rax), %%r11' % (f.offset * 8))
        print('\tjmp\t*%r11')

        print('\t.size\tGL_PREFIX(%s), .-GL_PREFIX(%s)' % (name, name))
        print('')
        return


    def printBody(self, api):
        for f in api.functionIterateByOffset():
            self.printFunction(f)


        for f in api.functionIterateByOffset():
            dispatch = f.dispatch_name()
            for n in f.entry_points:
                if n != f.name:
                    if f.is_static_entry_point(n):
                        text = '\t.globl GL_PREFIX(%s) ; .set GL_PREFIX(%s), GL_PREFIX(%s)' % (n, n, dispatch)

                        if f.has_different_protocol(n):
                            print('#if GLAPI_EXPORT_PROTO_ENTRY_POINTS')
                            print(text)
                            print('#endif')
                        else:
                            print(text)

        return


def _parser():
    """Parse arguments and return a namespace."""
    parser = argparse.ArgumentParser()
    parser.add_argument('-f',
                        default='gl_API.xml',
                        dest='filename',
                        help='An XML file describing an API')
    return parser.parse_args()


def main():
    """Main file."""
    args = _parser()
    printer = PrintGenericStubs()
    api = gl_XML.parse_GL_API(args.filename, glX_XML.glx_item_factory())

    printer.Print(api)


if __name__ == '__main__':
    main()
