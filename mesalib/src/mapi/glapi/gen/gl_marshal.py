
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

from __future__ import print_function

import contextlib
import getopt
import gl_XML
import license
import marshal_XML
import sys

header = """
#include "api_exec.h"
#include "glthread_marshal.h"
#include "bufferobj.h"
#include "dispatch.h"

#define COMPAT (ctx->API != API_OPENGL_CORE)

UNUSED static inline int safe_mul(int a, int b)
{
    if (a < 0 || b < 0) return -1;
    if (a == 0 || b == 0) return 0;
    if (a > INT_MAX / b) return -1;
    return a * b;
}
"""


file_index = 0
file_count = 1
current_indent = 0


def out(str):
    if str:
        print(' '*current_indent + str)
    else:
        print('')


@contextlib.contextmanager
def indent(delta = 3):
    global current_indent
    current_indent += delta
    yield
    current_indent -= delta


class PrintCode(gl_XML.gl_print_base):
    def __init__(self):
        super(PrintCode, self).__init__()

        self.name = 'gl_marshal.py'
        self.license = license.bsd_license_template % (
            'Copyright (C) 2012 Intel Corporation', 'INTEL CORPORATION')

    def printRealHeader(self):
        print(header)

    def printRealFooter(self):
        pass

    def print_sync_call(self, func, unmarshal = 0):
        call = 'CALL_{0}(ctx->CurrentServerDispatch, ({1}))'.format(
            func.name, func.get_called_parameter_string())
        if func.return_type == 'void' or unmarshal:
            out('{0};'.format(call))
            if func.marshal_call_after and not unmarshal:
                out(func.marshal_call_after);
        else:
            out('return {0};'.format(call))
            assert not func.marshal_call_after

    def print_sync_body(self, func):
        out('/* {0}: marshalled synchronously */'.format(func.name))
        out('{0} GLAPIENTRY'.format(func.return_type))
        out('_mesa_marshal_{0}({1})'.format(func.name, func.get_parameter_string()))
        out('{')
        with indent():
            out('GET_CURRENT_CONTEXT(ctx);')
            out('_mesa_glthread_finish_before(ctx, "{0}");'.format(func.name))
            self.print_sync_call(func)
        out('}')
        out('')
        out('')

    def print_async_dispatch(self, func):
        out('cmd = _mesa_glthread_allocate_command(ctx, '
            'DISPATCH_CMD_{0}, cmd_size);'.format(func.name))

        # We want glthread to ignore variable-sized parameters if the only thing
        # we want is to pass the pointer parameter as-is, e.g. when a PBO is bound.
        # Making it conditional on marshal_sync is kinda hacky, but it's the easiest
        # path towards handling PBOs in glthread, which use marshal_sync to check whether
        # a PBO is bound.
        if func.marshal_sync:
            fixed_params = func.fixed_params + func.variable_params
            variable_params = []
        else:
            fixed_params = func.fixed_params
            variable_params = func.variable_params

        for p in fixed_params:
            if p.count:
                out('memcpy(cmd->{0}, {0}, {1});'.format(
                        p.name, p.size_string()))
            else:
                out('cmd->{0} = {0};'.format(p.name))
        if variable_params:
            out('char *variable_data = (char *) (cmd + 1);')
            i = 1
            for p in variable_params:
                if p.img_null_flag:
                    out('cmd->{0}_null = !{0};'.format(p.name))
                    out('if (!cmd->{0}_null) {{'.format(p.name))
                    with indent():
                        out(('memcpy(variable_data, {0}, {0}_size);').format(p.name))
                        if i < len(variable_params):
                            out('variable_data += {0}_size;'.format(p.name))
                    out('}')
                else:
                    out(('memcpy(variable_data, {0}, {0}_size);').format(p.name))
                    if i < len(variable_params):
                        out('variable_data += {0}_size;'.format(p.name))
                i += 1

        if not fixed_params and not variable_params:
            out('(void) cmd;')

        if func.marshal_call_after:
            out(func.marshal_call_after);

        # Uncomment this if you want to call _mesa_glthread_finish for debugging
        #out('_mesa_glthread_finish(ctx);')

    def get_type_size(self, str):
        if str.find('*') != -1:
            return 8;

        mapping = {
            'GLboolean': 1,
            'GLbyte': 1,
            'GLubyte': 1,
            'GLshort': 2,
            'GLushort': 2,
            'GLhalfNV': 2,
            'GLenum': 4,
            'GLint': 4,
            'GLuint': 4,
            'GLbitfield': 4,
            'GLsizei': 4,
            'GLfloat': 4,
            'GLclampf': 4,
            'GLfixed': 4,
            'GLclampx': 4,
            'GLhandleARB': 4,
            'int': 4,
            'float': 4,
            'GLdouble': 8,
            'GLclampd': 8,
            'GLintptr': 8,
            'GLsizeiptr': 8,
            'GLint64': 8,
            'GLuint64': 8,
            'GLuint64EXT': 8,
            'GLsync': 8,
        }
        val = mapping.get(str, 9999)
        if val == 9999:
            print('Unhandled type in gl_marshal.py.get_type_size: ' + str, file=sys.stderr)
        return val

    def print_async_struct(self, func):
        if func.marshal_sync:
            fixed_params = func.fixed_params + func.variable_params
            variable_params = []
        else:
            fixed_params = func.fixed_params
            variable_params = func.variable_params

        out('struct marshal_cmd_{0}'.format(func.name))
        out('{')
        with indent():
            out('struct marshal_cmd_base cmd_base;')

            # Sort the parameters according to their size to pack the structure optimally
            for p in sorted(fixed_params, key=lambda p: self.get_type_size(p.type_string())):
                if p.count:
                    out('{0} {1}[{2}];'.format(
                            p.get_base_type_string(), p.name, p.count))
                else:
                    out('{0} {1};'.format(p.type_string(), p.name))

            for p in variable_params:
                if p.img_null_flag:
                    out('bool {0}_null; /* If set, no data follows '
                        'for "{0}" */'.format(p.name))

            for p in variable_params:
                if p.count_scale != 1:
                    out(('/* Next {0} bytes are '
                         '{1} {2}[{3}][{4}] */').format(
                            p.size_string(marshal = 1), p.get_base_type_string(),
                            p.name, p.counter, p.count_scale))
                else:
                    out(('/* Next {0} bytes are '
                         '{1} {2}[{3}] */').format(
                            p.size_string(marshal = 1), p.get_base_type_string(),
                            p.name, p.counter))
        out('};')

    def print_async_unmarshal(self, func):
        if func.marshal_sync:
            fixed_params = func.fixed_params + func.variable_params
            variable_params = []
        else:
            fixed_params = func.fixed_params
            variable_params = func.variable_params

        out('void')
        out(('_mesa_unmarshal_{0}(struct gl_context *ctx, '
             'const struct marshal_cmd_{0} *cmd)').format(func.name))
        out('{')
        with indent():
            for p in fixed_params:
                if p.count:
                    p_decl = '{0} * {1} = cmd->{1};'.format(
                            p.get_base_type_string(), p.name)
                else:
                    p_decl = '{0} {1} = cmd->{1};'.format(
                            p.type_string(), p.name)

                if not p_decl.startswith('const ') and p.count:
                    # Declare all local function variables as const, even if
                    # the original parameter is not const.
                    p_decl = 'const ' + p_decl

                out(p_decl)

            if variable_params:
                for p in variable_params:
                    out('{0} * {1};'.format(
                            p.get_base_type_string(), p.name))
                out('const char *variable_data = (const char *) (cmd + 1);')
                i = 1
                for p in variable_params:
                    out('{0} = ({1} *) variable_data;'.format(
                            p.name, p.get_base_type_string()))

                    if p.img_null_flag:
                        out('if (cmd->{0}_null)'.format(p.name))
                        with indent():
                            out('{0} = NULL;'.format(p.name))
                        if i < len(variable_params):
                            out('else')
                            with indent():
                                out('variable_data += {0};'.format(p.size_string(False, marshal = 1)))
                    elif i < len(variable_params):
                        out('variable_data += {0};'.format(p.size_string(False, marshal = 1)))
                    i += 1

            self.print_sync_call(func, unmarshal = 1)
        out('}')

    def validate_count_or_fallback(self, func):
        # Check that any counts for variable-length arguments might be < 0, in
        # which case the command alloc or the memcpy would blow up before we
        # get to the validation in Mesa core.
        list = []
        for p in func.parameters:
            if p.is_variable_length():
                list.append('{0}_size < 0'.format(p.name))
                list.append('({0}_size > 0 && !{0})'.format(p.name))

        if len(list) == 0:
            return

        list.append('(unsigned)cmd_size > MARSHAL_MAX_CMD_SIZE')

        out('if (unlikely({0})) {{'.format(' || '.join(list)))
        with indent():
            out('_mesa_glthread_finish_before(ctx, "{0}");'.format(func.name))
            self.print_sync_call(func)
            out('return;')
        out('}')

    def print_async_marshal(self, func):
        out('{0} GLAPIENTRY'.format(func.return_type))
        out('_mesa_marshal_{0}({1})'.format(
                func.name, func.get_parameter_string()))
        out('{')
        with indent():
            out('GET_CURRENT_CONTEXT(ctx);')
            if not func.marshal_sync:
                for p in func.variable_params:
                    out('int {0}_size = {1};'.format(p.name, p.size_string(marshal = 1)))

            struct = 'struct marshal_cmd_{0}'.format(func.name)
            size_terms = ['sizeof({0})'.format(struct)]
            if not func.marshal_sync:
                for p in func.variable_params:
                    if p.img_null_flag:
                        size_terms.append('({0} ? {0}_size : 0)'.format(p.name))
                    else:
                        size_terms.append('{0}_size'.format(p.name))
            out('int cmd_size = {0};'.format(' + '.join(size_terms)))
            out('{0} *cmd;'.format(struct))

            if func.marshal_sync:
                out('if ({0}) {{'.format(func.marshal_sync))
                with indent():
                    out('_mesa_glthread_finish_before(ctx, "{0}");'.format(func.name))
                    self.print_sync_call(func)
                    out('return;')
                out('}')
            else:
                self.validate_count_or_fallback(func)

            self.print_async_dispatch(func)
            if func.return_type == 'GLboolean':
                out('return GL_TRUE;') # for glUnmapBuffer
        out('}')

    def print_async_body(self, func):
        out('/* {0}: marshalled asynchronously */'.format(func.name))
        self.print_async_struct(func)
        self.print_async_unmarshal(func)
        self.print_async_marshal(func)
        out('')
        out('')

    def print_unmarshal_dispatch_cmd(self, api):
        out('const _mesa_unmarshal_func _mesa_unmarshal_dispatch[NUM_DISPATCH_CMD] = {')
        with indent():
            for func in api.functionIterateAll():
                flavor = func.marshal_flavor()
                if flavor in ('skip', 'sync'):
                    continue
                out('[DISPATCH_CMD_{0}] = (_mesa_unmarshal_func)_mesa_unmarshal_{0},'.format(func.name))
        out('};')
        out('')
        out('')

    def print_create_marshal_table(self, api):
        out('/* _mesa_create_marshal_table takes a long time to compile with -O2 */')
        out('#if defined(__GNUC__) && !defined(__clang__)')
        out('__attribute__((optimize("O1")))')
        out('#endif')
        out('struct _glapi_table *')
        out('_mesa_create_marshal_table(const struct gl_context *ctx)')
        out('{')
        with indent():
            out('struct _glapi_table *table;')
            out('')
            out('table = _mesa_alloc_dispatch_table();')
            out('if (table == NULL)')
            with indent():
                out('return NULL;')
            out('')
            for func in api.functionIterateAll():
                if func.marshal_flavor() == 'skip':
                    continue
                # Don't use the SET_* functions, because they increase compile time
                # by 20 seconds (on Ryzen 1700X).
                out('if (_gloffset_{0} >= 0)'.format(func.name))
                out('   ((_glapi_proc *)(table))[_gloffset_{0}] = (_glapi_proc)_mesa_marshal_{0};'
                    .format(func.name))
            out('')
            out('return table;')
        out('}')
        out('')
        out('')

    def printBody(self, api):
        # The first file only contains the dispatch tables
        if file_index == 0:
            self.print_unmarshal_dispatch_cmd(api)
            self.print_create_marshal_table(api)
            return

        # The remaining files contain the marshal and unmarshal functions
        func_per_file = (len(api.functionIterateAll()) // (file_count - 1)) + 1
        i = -1
        for func in api.functionIterateAll():
            i += 1
            if i // func_per_file != (file_index - 1):
                continue

            flavor = func.marshal_flavor()
            if flavor in ('skip', 'custom'):
                continue
            elif flavor == 'async':
                self.print_async_body(func)
            elif flavor == 'sync':
                self.print_sync_body(func)


def show_usage():
    print('Usage: %s [-f input_file_name]' % sys.argv[0])
    sys.exit(1)


if __name__ == '__main__':
    file_name = 'gl_API.xml'

    try:
        (args, trail) = getopt.getopt(sys.argv[1:], 'm:f:i:n:')
    except Exception:
        show_usage()

    for (arg,val) in args:
        if arg == '-f':
            file_name = val
        elif arg == '-i':
            file_index = int(val)
        elif arg == '-n':
            file_count = int(val)

    assert file_index < file_count
    printer = PrintCode()

    api = gl_XML.parse_GL_API(file_name, marshal_XML.marshal_item_factory())
    printer.Print(api)
