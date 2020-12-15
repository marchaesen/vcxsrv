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
parser.add_argument('-C', '--src')
parser.add_argument('-H', '--hdr')
args = parser.parse_args()
sys.path.insert(0, args.import_path)


from u_trace import Header
from u_trace import Tracepoint
from u_trace import utrace_generate

#
# Tracepoint definitions:
#

Header('pipe/p_state.h')
Header('util/format/u_format.h')

Tracepoint('surface',
    args=[['const struct pipe_surface *', 'psurf']],
    tp_struct=[['uint16_t',     'width',      'psurf->width'],
               ['uint16_t',     'height',     'psurf->height'],
               ['uint8_t',      'nr_samples', 'psurf->nr_samples'],
               ['const char *', 'format',     'util_format_short_name(psurf->format)']],
    tp_print=['%ux%u@%u, fmt=%s',
        '__entry->width',
        '__entry->height',
        '__entry->nr_samples',
        '__entry->format'],
)

# Note: called internally from trace_framebuffer_state()
Tracepoint('framebuffer',
    args=[['const struct pipe_framebuffer_state *', 'pfb']],
    tp_struct=[['uint16_t',     'width',      'pfb->width'],
               ['uint16_t',     'height',     'pfb->height'],
               ['uint8_t',      'layers',     'pfb->layers'],
               ['uint8_t',      'samples',    'pfb->samples'],
               ['uint8_t',      'nr_cbufs',   'pfb->nr_cbufs']],
    tp_print=['%ux%ux%u@%u, nr_cbufs: %u',
        '__entry->width',
        '__entry->height',
        '__entry->layers',
        '__entry->samples',
        '__entry->nr_cbufs'],
)

Tracepoint('grid_info',
    args=[['const struct pipe_grid_info *', 'pgrid']],
    tp_struct=[['uint8_t',  'work_dim',  'pgrid->work_dim'],
               ['uint16_t', 'block_x',   'pgrid->block[0]'],
               ['uint16_t', 'block_y',   'pgrid->block[1]'],
               ['uint16_t', 'block_z',   'pgrid->block[2]'],
               ['uint16_t', 'grid_x',    'pgrid->grid[0]'],
               ['uint16_t', 'grid_y',    'pgrid->grid[1]'],
               ['uint16_t', 'grid_z',    'pgrid->grid[2]']],
    tp_print=['work_dim=%u, block=%ux%ux%u, grid=%ux%ux%u', '__entry->work_dim',
        '__entry->block_x', '__entry->block_y', '__entry->block_z',
        '__entry->grid_x', '__entry->grid_y', '__entry->grid_z'],
)

utrace_generate(cpath=args.src, hpath=args.hdr)
