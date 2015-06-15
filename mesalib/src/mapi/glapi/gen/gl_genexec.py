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

# This script generates the file api_exec.c, which contains
# _mesa_initialize_exec_table().  It is responsible for populating all
# entries in the "exec" dispatch table that aren't dynamic.

import argparse
import collections
import license
import gl_XML
import sys
import apiexec


exec_flavor_map = {
    'dynamic': None,
    'mesa': '_mesa_',
    'skip': None,
    }


header = """/**
 * \\file api_exec.c
 * Initialize dispatch table.
 */


#include "main/accum.h"
#include "main/api_loopback.h"
#include "main/api_exec.h"
#include "main/arbprogram.h"
#include "main/atifragshader.h"
#include "main/attrib.h"
#include "main/blend.h"
#include "main/blit.h"
#include "main/bufferobj.h"
#include "main/arrayobj.h"
#include "main/buffers.h"
#include "main/clear.h"
#include "main/clip.h"
#include "main/colortab.h"
#include "main/compute.h"
#include "main/condrender.h"
#include "main/context.h"
#include "main/convolve.h"
#include "main/copyimage.h"
#include "main/depth.h"
#include "main/dlist.h"
#include "main/drawpix.h"
#include "main/drawtex.h"
#include "main/rastpos.h"
#include "main/enable.h"
#include "main/errors.h"
#include "main/es1_conversion.h"
#include "main/eval.h"
#include "main/get.h"
#include "main/feedback.h"
#include "main/fog.h"
#include "main/fbobject.h"
#include "main/framebuffer.h"
#include "main/genmipmap.h"
#include "main/hint.h"
#include "main/histogram.h"
#include "main/imports.h"
#include "main/light.h"
#include "main/lines.h"
#include "main/matrix.h"
#include "main/multisample.h"
#include "main/objectlabel.h"
#include "main/performance_monitor.h"
#include "main/pipelineobj.h"
#include "main/pixel.h"
#include "main/pixelstore.h"
#include "main/points.h"
#include "main/polygon.h"
#include "main/program_resource.h"
#include "main/querymatrix.h"
#include "main/queryobj.h"
#include "main/readpix.h"
#include "main/samplerobj.h"
#include "main/scissor.h"
#include "main/stencil.h"
#include "main/texenv.h"
#include "main/texgetimage.h"
#include "main/teximage.h"
#include "main/texgen.h"
#include "main/texobj.h"
#include "main/texparam.h"
#include "main/texstate.h"
#include "main/texstorage.h"
#include "main/texturebarrier.h"
#include "main/textureview.h"
#include "main/transformfeedback.h"
#include "main/mtypes.h"
#include "main/varray.h"
#include "main/viewport.h"
#include "main/shaderapi.h"
#include "main/shaderimage.h"
#include "main/uniforms.h"
#include "main/syncobj.h"
#include "main/formatquery.h"
#include "main/dispatch.h"
#include "main/vdpau.h"
#include "vbo/vbo.h"


/**
 * Initialize a context's exec table with pointers to Mesa's supported
 * GL functions.
 *
 * This function depends on ctx->Version.
 *
 * \param ctx  GL context to which \c exec belongs.
 */
void
_mesa_initialize_exec_table(struct gl_context *ctx)
{
   struct _glapi_table *exec;

   exec = ctx->Exec;
   assert(exec != NULL);

   assert(ctx->Version > 0);

   vbo_initialize_exec_dispatch(ctx, exec);
"""


footer = """
}
"""


class PrintCode(gl_XML.gl_print_base):

    def __init__(self):
        gl_XML.gl_print_base.__init__(self)

        self.name = 'gl_genexec.py'
        self.license = license.bsd_license_template % (
            'Copyright (C) 2012 Intel Corporation',
            'Intel Corporation')

    def printRealHeader(self):
        print header

    def printRealFooter(self):
        print footer

    def printBody(self, api):
        # Collect SET_* calls by the condition under which they should
        # be called.
        settings_by_condition = collections.defaultdict(lambda: [])
        for f in api.functionIterateAll():
            if f.exec_flavor not in exec_flavor_map:
                raise Exception(
                    'Unrecognized exec flavor {0!r}'.format(f.exec_flavor))
            condition_parts = []
            if f.name in apiexec.functions:
                ex = apiexec.functions[f.name]
                unconditional_count = 0

                if ex.compatibility is not None:
                    condition_parts.append('ctx->API == API_OPENGL_COMPAT')
                    unconditional_count += 1

                if ex.core is not None:
                    condition_parts.append('ctx->API == API_OPENGL_CORE')
                    unconditional_count += 1

                if ex.es1 is not None:
                    condition_parts.append('ctx->API == API_OPENGLES')
                    unconditional_count += 1

                if ex.es2 is not None:
                    if ex.es2 > 20:
                        condition_parts.append('(ctx->API == API_OPENGLES2 && ctx->Version >= {0})'.format(ex.es2))
                    else:
                        condition_parts.append('ctx->API == API_OPENGLES2')
                        unconditional_count += 1

                # If the function is unconditionally available in all four
                # APIs, then it is always available.  Replace the complex
                # tautology condition with "true" and let GCC do the right
                # thing.
                if unconditional_count == 4:
                    condition_parts = ['true']
            else:
                if f.desktop:
                    if f.deprecated:
                        condition_parts.append('ctx->API == API_OPENGL_COMPAT')
                    else:
                        condition_parts.append('_mesa_is_desktop_gl(ctx)')
                if 'es1' in f.api_map:
                    condition_parts.append('ctx->API == API_OPENGLES')
                if 'es2' in f.api_map:
                    if f.api_map['es2'] > 2.0:
                        condition_parts.append('(ctx->API == API_OPENGLES2 && ctx->Version >= {0})'.format(int(f.api_map['es2'] * 10)))
                    else:
                        condition_parts.append('ctx->API == API_OPENGLES2')

            if not condition_parts:
                # This function does not exist in any API.
                continue
            condition = ' || '.join(condition_parts)
            prefix = exec_flavor_map[f.exec_flavor]
            if prefix is None:
                # This function is not implemented, or is dispatched
                # dynamically.
                continue
            settings_by_condition[condition].append(
                'SET_{0}(exec, {1}{0});'.format(f.name, prefix, f.name))
        # Print out an if statement for each unique condition, with
        # the SET_* calls nested inside it.
        for condition in sorted(settings_by_condition.keys()):
            print '   if ({0}) {{'.format(condition)
            for setting in sorted(settings_by_condition[condition]):
                print '      {0}'.format(setting)
            print '   }'


def _parser():
    """Parse arguments and return namespace."""
    parser = argparse.ArgumentParser()
    parser.add_argument('-f',
                        dest='filename',
                        default='gl_and_es_API.xml',
                        help='an xml file describing an API')
    return parser.parse_args()


def main():
    """Main function."""
    args = _parser()
    printer = PrintCode()
    api = gl_XML.parse_GL_API(args.filename)
    printer.Print(api)


if __name__ == '__main__':
    main()
