
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
#include "context.h"
#include "dispatch.h"
#include "glthread.h"
#include "marshal.h"
#include "marshal_generated.h"
"""


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
        print('static inline int safe_mul(int a, int b)')
        print('{')
        print('    if (a < 0 || b < 0) return -1;')
        print('    if (a == 0 || b == 0) return 0;')
        print('    if (a > INT_MAX / b) return -1;')
        print('    return a * b;')
        print('}')
        print()

    def printRealFooter(self):
        pass

    def print_sync_call(self, func):
        call = 'CALL_{0}(ctx->CurrentServerDispatch, ({1}))'.format(
            func.name, func.get_called_parameter_string())
        if func.return_type == 'void':
            out('{0};'.format(call))
        else:
            out('return {0};'.format(call))

    def print_sync_dispatch(self, func):
        out('debug_print_sync_fallback("{0}");'.format(func.name))
        self.print_sync_call(func)

    def print_sync_body(self, func):
        out('/* {0}: marshalled synchronously */'.format(func.name))
        out('static {0} GLAPIENTRY'.format(func.return_type))
        out('_mesa_marshal_{0}({1})'.format(func.name, func.get_parameter_string()))
        out('{')
        with indent():
            out('GET_CURRENT_CONTEXT(ctx);')
            out('_mesa_glthread_finish(ctx);')
            out('debug_print_sync("{0}");'.format(func.name))
            self.print_sync_call(func)
        out('}')
        out('')
        out('')

    def print_async_dispatch(self, func):
        out('cmd = _mesa_glthread_allocate_command(ctx, '
            'DISPATCH_CMD_{0}, cmd_size);'.format(func.name))
        for p in func.fixed_params:
            if p.count:
                out('memcpy(cmd->{0}, {0}, {1});'.format(
                        p.name, p.size_string()))
            else:
                out('cmd->{0} = {0};'.format(p.name))
        if func.variable_params:
            out('char *variable_data = (char *) (cmd + 1);')
            for p in func.variable_params:
                if p.img_null_flag:
                    out('cmd->{0}_null = !{0};'.format(p.name))
                    out('if (!cmd->{0}_null) {{'.format(p.name))
                    with indent():
                        out(('memcpy(variable_data, {0}, {1});').format(
                            p.name, p.size_string(False)))
                        out('variable_data += {0};'.format(
                            p.size_string(False)))
                    out('}')
                else:
                    out(('memcpy(variable_data, {0}, {1});').format(
                        p.name, p.size_string(False)))
                    out('variable_data += {0};'.format(
                        p.size_string(False)))

        if not func.fixed_params and not func.variable_params:
            out('(void) cmd;\n')
        out('_mesa_post_marshal_hook(ctx);')

    def print_async_struct(self, func):
        out('struct marshal_cmd_{0}'.format(func.name))
        out('{')
        with indent():
            out('struct marshal_cmd_base cmd_base;')
            for p in func.fixed_params:
                if p.count:
                    out('{0} {1}[{2}];'.format(
                            p.get_base_type_string(), p.name, p.count))
                else:
                    out('{0} {1};'.format(p.type_string(), p.name))

            for p in func.variable_params:
                if p.img_null_flag:
                    out('bool {0}_null; /* If set, no data follows '
                        'for "{0}" */'.format(p.name))

            for p in func.variable_params:
                if p.count_scale != 1:
                    out(('/* Next {0} bytes are '
                         '{1} {2}[{3}][{4}] */').format(
                            p.size_string(), p.get_base_type_string(),
                            p.name, p.counter, p.count_scale))
                else:
                    out(('/* Next {0} bytes are '
                         '{1} {2}[{3}] */').format(
                            p.size_string(), p.get_base_type_string(),
                            p.name, p.counter))
        out('};')

    def print_async_unmarshal(self, func):
        out('static inline void')
        out(('_mesa_unmarshal_{0}(struct gl_context *ctx, '
             'const struct marshal_cmd_{0} *cmd)').format(func.name))
        out('{')
        with indent():
            for p in func.fixed_params:
                if p.count:
                    p_decl = '{0} * {1} = cmd->{1};'.format(
                            p.get_base_type_string(), p.name)
                else:
                    p_decl = '{0} {1} = cmd->{1};'.format(
                            p.type_string(), p.name)

                if not p_decl.startswith('const '):
                    # Declare all local function variables as const, even if
                    # the original parameter is not const.
                    p_decl = 'const ' + p_decl

                out(p_decl)

            if func.variable_params:
                for p in func.variable_params:
                    out('const {0} * {1};'.format(
                            p.get_base_type_string(), p.name))
                out('const char *variable_data = (const char *) (cmd + 1);')
                for p in func.variable_params:
                    out('{0} = (const {1} *) variable_data;'.format(
                            p.name, p.get_base_type_string()))

                    if p.img_null_flag:
                        out('if (cmd->{0}_null)'.format(p.name))
                        with indent():
                            out('{0} = NULL;'.format(p.name))
                        out('else')
                        with indent():
                            out('variable_data += {0};'.format(p.size_string(False)))
                    else:
                        out('variable_data += {0};'.format(p.size_string(False)))

            self.print_sync_call(func)
        out('}')

    def validate_count_or_fallback(self, func):
        # Check that any counts for variable-length arguments might be < 0, in
        # which case the command alloc or the memcpy would blow up before we
        # get to the validation in Mesa core.
        for p in func.parameters:
            if p.is_variable_length():
                out('if (unlikely({0} < 0)) {{'.format(p.size_string()))
                with indent():
                    out('goto fallback_to_sync;')
                out('}')
                return True
        return False


    def print_async_marshal(self, func):
        need_fallback_sync = False
        out('static void GLAPIENTRY')
        out('_mesa_marshal_{0}({1})'.format(
                func.name, func.get_parameter_string()))
        out('{')
        with indent():
            out('GET_CURRENT_CONTEXT(ctx);')
            struct = 'struct marshal_cmd_{0}'.format(func.name)
            size_terms = ['sizeof({0})'.format(struct)]
            for p in func.variable_params:
                size = p.size_string()
                if p.img_null_flag:
                    size = '({0} ? {1} : 0)'.format(p.name, size)
                size_terms.append(size)
            out('size_t cmd_size = {0};'.format(' + '.join(size_terms)))
            out('{0} *cmd;'.format(struct))

            out('debug_print_marshal("{0}");'.format(func.name))

            need_fallback_sync = self.validate_count_or_fallback(func)

            if func.marshal_fail:
                out('if ({0}) {{'.format(func.marshal_fail))
                with indent():
                    out('_mesa_glthread_finish(ctx);')
                    out('_mesa_glthread_restore_dispatch(ctx, __func__);')
                    self.print_sync_dispatch(func)
                    out('return;')
                out('}')

            out('if (cmd_size <= MARSHAL_MAX_CMD_SIZE) {')
            with indent():
                self.print_async_dispatch(func)
                out('return;')
            out('}')

        out('')
        if need_fallback_sync:
            out('fallback_to_sync:')
        with indent():
            out('_mesa_glthread_finish(ctx);')
            self.print_sync_dispatch(func)

        out('}')

    def print_async_body(self, func):
        out('/* {0}: marshalled asynchronously */'.format(func.name))
        self.print_async_struct(func)
        self.print_async_unmarshal(func)
        self.print_async_marshal(func)
        out('')
        out('')

    def print_unmarshal_dispatch_cmd(self, api):
        out('size_t')
        out('_mesa_unmarshal_dispatch_cmd(struct gl_context *ctx, '
            'const void *cmd)')
        out('{')
        with indent():
            out('const struct marshal_cmd_base *cmd_base = cmd;')
            out('switch (cmd_base->cmd_id) {')
            for func in api.functionIterateAll():
                flavor = func.marshal_flavor()
                if flavor in ('skip', 'sync'):
                    continue
                out('case DISPATCH_CMD_{0}:'.format(func.name))
                with indent():
                    out('debug_print_unmarshal("{0}");'.format(func.name))
                    out(('_mesa_unmarshal_{0}(ctx, (const struct marshal_cmd_{0} *)'
                         ' cmd);').format(func.name))
                    out('break;')
            out('default:')
            with indent():
                out('assert(!"Unrecognized command ID");')
                out('break;')
            out('}')
            out('')
            out('return cmd_base->cmd_size;')
        out('}')
        out('')
        out('')

    def print_create_marshal_table(self, api):
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
                out('SET_{0}(table, _mesa_marshal_{0});'.format(func.name))
            out('')
            out('return table;')
        out('}')
        out('')
        out('')

    def printBody(self, api):
        async_funcs = []
        for func in api.functionIterateAll():
            flavor = func.marshal_flavor()
            if flavor in ('skip', 'custom'):
                continue
            elif flavor == 'async':
                self.print_async_body(func)
                async_funcs.append(func)
            elif flavor == 'sync':
                self.print_sync_body(func)
        self.print_unmarshal_dispatch_cmd(api)
        self.print_create_marshal_table(api)


def show_usage():
    print('Usage: %s [-f input_file_name]' % sys.argv[0])
    sys.exit(1)


if __name__ == '__main__':
    file_name = 'gl_API.xml'

    try:
        (args, trail) = getopt.getopt(sys.argv[1:], 'm:f:')
    except Exception:
        show_usage()

    for (arg,val) in args:
        if arg == '-f':
            file_name = val

    printer = PrintCode()

    api = gl_XML.parse_GL_API(file_name, marshal_XML.marshal_item_factory())
    printer.Print(api)
