#
# Copyright (C) 2020 Google, Inc.
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
#

from mako.template import Template
from collections import namedtuple
from enum import IntEnum
import os

TRACEPOINTS = {}

class Tracepoint(object):
    """Class that represents all the information about a tracepoint
    """
    def __init__(self, name, args=[],
                 tp_struct=None, tp_print=None, tp_perfetto=None,
                 end_of_pipe=False):
        """Parameters:

        - name: the tracepoint name, a tracepoint function with the given
          name (prefixed by 'trace_') will be generated with the specied
          args (following a u_trace ptr).  Calling this tracepoint will
          emit a trace, if tracing is enabled.
        - args: the tracepoint func args, an array of TracepointArg
        - tp_print: (optional) array of format string followed by expressions
        - tp_perfetto: (optional) driver provided callback which can generate
          perfetto events
        """
        assert isinstance(name, str)
        assert isinstance(args, list)
        assert name not in TRACEPOINTS

        self.name = name
        self.args = args
        if tp_struct is None:
           tp_struct = args
        self.tp_struct = tp_struct
        self.tp_print = tp_print
        self.tp_perfetto = tp_perfetto
        self.end_of_pipe = end_of_pipe

        TRACEPOINTS[name] = self

    def can_generate_print(self):
        return self.args is not None and len(self.args) > 0

class TracepointArgStruct():
    """Represents struct that is being passed as an argument
    """
    def __init__(self, type, var):
        """Parameters:

        - type: argument's C type.
        - var: name of the argument
        """
        assert isinstance(type, str)
        assert isinstance(var, str)

        self.type = type
        self.var = var

class TracepointArg(object):
    """Class that represents either an argument being passed or a field in a struct
    """
    def __init__(self, type, var, c_format, name=None, to_prim_type=None):
        """Parameters:

        - type: argument's C type.
        - var: either an argument name or a field in the struct
        - c_format: printf format to print the value.
        - name: (optional) name that will be used in intermidiate structs and will
          be displayed in output or perfetto, otherwise var will be used.
        - to_prim_type: (optional) C function to convert from arg's type to a type
          compatible with c_format.
        """
        assert isinstance(type, str)
        assert isinstance(var, str)
        assert isinstance(c_format, str)

        self.type = type
        self.var = var
        self.c_format = c_format
        if name is None:
           name = var
        self.name = name
        self.to_prim_type = to_prim_type


HEADERS = []

class HeaderScope(IntEnum):
   HEADER = (1 << 0)
   SOURCE = (1 << 1)

class Header(object):
    """Class that represents a header file dependency of generated tracepoints
    """
    def __init__(self, hdr, scope=HeaderScope.HEADER|HeaderScope.SOURCE):
        """Parameters:

        - hdr: the required header path
        """
        assert isinstance(hdr, str)
        self.hdr = hdr
        self.scope = scope

        HEADERS.append(self)


FORWARD_DECLS = []

class ForwardDecl(object):
   """Class that represents a forward declaration
   """
   def __init__(self, decl):
        assert isinstance(decl, str)
        self.decl = decl

        FORWARD_DECLS.append(self)


hdr_template = """\
/* Copyright (C) 2020 Google, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

<% guard_name = '_' + hdrname + '_H' %>
#ifndef ${guard_name}
#define ${guard_name}

% for header in HEADERS:
#include "${header.hdr}"
% endfor

#include "util/perf/u_trace.h"

#ifdef __cplusplus
extern "C" {
#endif

% for declaration in FORWARD_DECLS:
${declaration.decl};
% endfor

% for trace_name, trace in TRACEPOINTS.items():

/*
 * ${trace_name}
 */
struct trace_${trace_name} {
%    for arg in trace.tp_struct:
   ${arg.type} ${arg.name};
%    endfor
%    if len(trace.args) == 0:
#ifdef  __cplusplus
   /* avoid warnings about empty struct size mis-match in C vs C++..
    * the size mis-match is harmless because (a) nothing will deref
    * the empty struct, and (b) the code that cares about allocating
    * sizeof(struct trace_${trace_name}) (and wants this to be zero
    * if there is no payload) is C
    */
   uint8_t dummy;
#endif
%    endif
};
%    if trace.tp_perfetto is not None:
#ifdef HAVE_PERFETTO
void ${trace.tp_perfetto}(
   ${ctx_param},
   uint64_t ts_ns,
   const void *flush_data,
   const struct trace_${trace_name} *payload);
#endif
%    endif
void __trace_${trace_name}(
       struct u_trace *ut, void *cs
%    for arg in trace.args:
     , ${arg.type} ${arg.var}
%    endfor
);
static inline void trace_${trace_name}(
     struct u_trace *ut, void *cs
%    for arg in trace.args:
   , ${arg.type} ${arg.var}
%    endfor
) {
%    if trace.tp_perfetto is not None:
   if (!unlikely(ut->enabled || ut_trace_instrument || ut_perfetto_enabled))
%    else:
   if (!unlikely(ut->enabled || ut_trace_instrument))
%    endif
      return;
   __trace_${trace_name}(
        ut, cs
%    for arg in trace.args:
      , ${arg.var}
%    endfor
   );
}
% endfor

#ifdef __cplusplus
}
#endif

#endif /* ${guard_name} */
"""

src_template = """\
/* Copyright (C) 2020 Google, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

% for header in HEADERS:
#include "${header.hdr}"
% endfor

#include "${hdr}"

#define __NEEDS_TRACE_PRIV
#include "util/perf/u_trace_priv.h"

% for trace_name, trace in TRACEPOINTS.items():
/*
 * ${trace_name}
 */
 % if trace.can_generate_print():
static void __print_${trace_name}(FILE *out, const void *arg) {
   const struct trace_${trace_name} *__entry =
      (const struct trace_${trace_name} *)arg;
  % if trace.tp_print is not None:
   fprintf(out, "${trace.tp_print[0]}\\n"
   % for arg in trace.tp_print[1:]:
           , ${arg}
   % endfor
  % else:
   fprintf(out, ""
   % for arg in trace.tp_struct:
      "${arg.name}=${arg.c_format}, "
   % endfor
         "\\n"
   % for arg in trace.tp_struct:
    % if arg.to_prim_type:
   ,${arg.to_prim_type.format('__entry->' + arg.name)}
    % else:
   ,__entry->${arg.name}
    % endif
   % endfor
  % endif
   );
}
 % else:
#define __print_${trace_name} NULL
 % endif
static const struct u_tracepoint __tp_${trace_name} = {
    ALIGN_POT(sizeof(struct trace_${trace_name}), 8),   /* keep size 64b aligned */
    "${trace_name}",
    ${"true" if trace.end_of_pipe else "false"},
    __print_${trace_name},
 % if trace.tp_perfetto is not None:
#ifdef HAVE_PERFETTO
    (void (*)(void *pctx, uint64_t, const void *, const void *))${trace.tp_perfetto},
#endif
 % endif
};
void __trace_${trace_name}(
     struct u_trace *ut, void *cs
 % for arg in trace.args:
   , ${arg.type} ${arg.var}
 % endfor
) {
   struct trace_${trace_name} *__entry =
      (struct trace_${trace_name} *)u_trace_append(ut, cs, &__tp_${trace_name});
 % if len(trace.tp_struct) == 0:
   (void)__entry;
 % endif
 % for arg in trace.tp_struct:
   __entry->${arg.name} = ${arg.var};
 % endfor
}

% endfor
"""

def utrace_generate(cpath, hpath, ctx_param):
    if cpath is not None:
        hdr = os.path.basename(cpath).rsplit('.', 1)[0] + '.h'
        with open(cpath, 'w') as f:
            f.write(Template(src_template).render(
                hdr=hdr,
                ctx_param=ctx_param,
                HEADERS=[h for h in HEADERS if h.scope & HeaderScope.SOURCE],
                TRACEPOINTS=TRACEPOINTS))

    if hpath is not None:
        hdr = os.path.basename(hpath)
        with open(hpath, 'w') as f:
            f.write(Template(hdr_template).render(
                hdrname=hdr.rstrip('.h').upper(),
                ctx_param=ctx_param,
                HEADERS=[h for h in HEADERS if h.scope & HeaderScope.HEADER],
                FORWARD_DECLS=FORWARD_DECLS,
                TRACEPOINTS=TRACEPOINTS))


perfetto_utils_hdr_template = """\
/*
 * Copyright © 2021 Igalia S.L.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

<% guard_name = '_' + hdrname + '_H' %>
#ifndef ${guard_name}
#define ${guard_name}

#include <perfetto.h>

% for trace_name, trace in TRACEPOINTS.items():
static void UNUSED
trace_payload_as_extra_${trace_name}(perfetto::protos::pbzero::GpuRenderStageEvent *event,
                                     const struct trace_${trace_name} *payload)
{
 % if all([trace.tp_perfetto, trace.tp_struct]) and len(trace.tp_struct) > 0:
   char buf[128];

  % for arg in trace.tp_struct:
   {
      auto data = event->add_extra_data();
      data->set_name("${arg.name}");

   % if arg.to_prim_type:
      sprintf(buf, "${arg.c_format}", ${arg.to_prim_type.format('payload->' + arg.name)});
   % else:
      sprintf(buf, "${arg.c_format}", payload->${arg.name});
   % endif

      data->set_value(buf);
   }
  % endfor

 % endif
}
% endfor

#endif /* ${guard_name} */
"""

def utrace_generate_perfetto_utils(hpath):
    if hpath is not None:
        hdr = os.path.basename(hpath)
        with open(hpath, 'wb') as f:
            f.write(Template(perfetto_utils_hdr_template, output_encoding='utf-8').render(
                hdrname=hdr.rstrip('.h').upper(),
                TRACEPOINTS=TRACEPOINTS))
