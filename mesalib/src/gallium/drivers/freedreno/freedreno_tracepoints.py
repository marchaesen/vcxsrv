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

import argparse
import sys

#
# TODO can we do this with less boilerplate?
#
parser = argparse.ArgumentParser()
parser.add_argument('-p', '--import-path', required=True)
parser.add_argument('-C', '--src', required=True)
parser.add_argument('-H', '--hdr', required=True)
args = parser.parse_args()
sys.path.insert(0, args.import_path)


from u_trace import Header
from u_trace import Tracepoint
from u_trace import TracepointArg
from u_trace import utrace_generate

#
# Tracepoint definitions:
#

Header('util/u_dump.h')
Header('freedreno_batch.h')

Tracepoint('start_state_restore')
Tracepoint('end_state_restore')

Tracepoint('flush_batch',
    args=[TracepointArg(type='struct fd_batch *', var='batch',       c_format='%x'),
          TracepointArg(type='uint16_t',          var='cleared',     c_format='%x'),
          TracepointArg(type='uint16_t',          var='gmem_reason', c_format='%x'),
          TracepointArg(type='uint16_t',          var='num_draws',   c_format='%u')],
    tp_print=['%p: cleared=%x, gmem_reason=%x, num_draws=%u', '__entry->batch',
        '__entry->cleared', '__entry->gmem_reason', '__entry->num_draws'],
)

Tracepoint('render_gmem',
    args=[TracepointArg(type='uint16_t', var='nbins_x', c_format='%u'),
          TracepointArg(type='uint16_t', var='nbins_y', c_format='%u'),
          TracepointArg(type='uint16_t', var='bin_w',   c_format='%u'),
          TracepointArg(type='uint16_t', var='bin_h',   c_format='%u')],
    tp_print=['%ux%u bins of %ux%u',
        '__entry->nbins_x', '__entry->nbins_y', '__entry->bin_w', '__entry->bin_h'],
)

Tracepoint('render_sysmem')

# Note that this doesn't include full information about all of the MRTs
# but seems to roughly match what I see with a blob trace
Tracepoint('start_render_pass',
    args=[TracepointArg(type='uint32_t',         var='submit_id',     c_format='%u'),
          TracepointArg(type='enum pipe_format', var='cbuf0_format',  c_format='%s', to_prim_type='util_format_description({})->short_name'),
          TracepointArg(type='enum pipe_format', var='zs_format',     c_format='%s', to_prim_type='util_format_description({})->short_name'),
          TracepointArg(type='uint16_t',         var='width',         c_format='%u'),
          TracepointArg(type='uint16_t',         var='height',        c_format='%u'),
          TracepointArg(type='uint8_t',          var='mrts',          c_format='%u'),
          TracepointArg(type='uint8_t',          var='samples',       c_format='%u'),
          TracepointArg(type='uint16_t',         var='nbins',         c_format='%u'),
          TracepointArg(type='uint16_t',         var='binw',          c_format='%u'),
          TracepointArg(type='uint16_t',         var='binh',          c_format='%u')],
    tp_perfetto='fd_start_render_pass'
)
Tracepoint('end_render_pass',
    tp_perfetto='fd_end_render_pass')

Tracepoint('start_binning_ib',
    tp_perfetto='fd_start_binning_ib')
Tracepoint('end_binning_ib',
    tp_perfetto='fd_end_binning_ib')

Tracepoint('start_vsc_overflow_test')
Tracepoint('end_vsc_overflow_test')

Tracepoint('start_prologue')
Tracepoint('end_prologue')

# For GMEM pass, where this could either be a clear or resolve
Tracepoint('start_clear_restore',
    args=[TracepointArg(type='uint16_t', var='fast_cleared', c_format='0x%x')],
    tp_print=['fast_cleared: 0x%x', '__entry->fast_cleared'],
    tp_perfetto='fd_start_clear_restore',
)
Tracepoint('end_clear_restore',
    tp_perfetto='fd_end_clear_restore')

Tracepoint('start_resolve',
    tp_perfetto='fd_start_resolve')
Tracepoint('end_resolve',
    tp_perfetto='fd_end_resolve')

Tracepoint('start_tile',
    args=[TracepointArg(type='uint16_t', var='bin_h', c_format='%u'),
          TracepointArg(type='uint16_t', var='yoff',  c_format='%u'),
          TracepointArg(type='uint16_t', var='bin_w', c_format='%u'),
          TracepointArg(type='uint16_t', var='xoff',  c_format='%u')],
    tp_print=['bin_h=%d, yoff=%d, bin_w=%d, xoff=%d',
        '__entry->bin_h', '__entry->yoff', '__entry->bin_w', '__entry->xoff'],
)

Tracepoint('start_draw_ib',
    tp_perfetto='fd_start_draw_ib')
Tracepoint('end_draw_ib',
    tp_perfetto='fd_end_draw_ib')

Tracepoint('start_blit',
    args=[TracepointArg(type='enum pipe_texture_target', var='src_target', c_format='%s', to_prim_type="util_str_tex_target({}, true)"),
          TracepointArg(type='enum pipe_texture_target', var='dst_target', c_format='%s', to_prim_type="util_str_tex_target({}, true)")],
    tp_print=['%s -> %s', 'util_str_tex_target(__entry->src_target, true)',
        'util_str_tex_target(__entry->dst_target, true)'],
    tp_perfetto='fd_start_blit',
)
Tracepoint('end_blit',
    tp_perfetto='fd_end_blit')

Tracepoint('start_compute',
    tp_perfetto='fd_start_compute')
Tracepoint('end_compute',
    tp_perfetto='fd_end_compute')

utrace_generate(cpath=args.src, hpath=args.hdr, ctx_param='struct pipe_context *pctx')
