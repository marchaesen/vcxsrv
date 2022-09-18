/*
 * © Copyright 2018 Alyssa Rosenzweig
 * Copyright (C) 2019-2020 Collabora, Ltd.
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
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pan_bo.h"
#include "pan_context.h"
#include "pan_shader.h"
#include "pan_util.h"

#include "compiler/nir/nir.h"
#include "util/u_dynarray.h"
#include "util/u_upload_mgr.h"

void
panfrost_shader_compile(struct pipe_screen *pscreen,
                        struct panfrost_pool *shader_pool,
                        struct panfrost_pool *desc_pool,
                        const nir_shader *ir,
                        struct util_debug_callback *dbg,
                        struct panfrost_shader_state *state,
                        unsigned req_local_mem)
{
        struct panfrost_screen *screen = pan_screen(pscreen);
        struct panfrost_device *dev = pan_device(pscreen);

        nir_shader *s = nir_shader_clone(NULL, ir);

        if (dev->arch >= 6 && s->xfb_info && !s->info.internal) {
                /* Create compute shader doing transform feedback */
                nir_shader *xfb = nir_shader_clone(NULL, s);
                xfb->info.name = ralloc_asprintf(xfb, "%s@xfb", xfb->info.name);
                xfb->info.internal = true;

                state->xfb = calloc(1, sizeof(struct panfrost_shader_state));
                panfrost_shader_compile(pscreen, shader_pool, desc_pool, xfb, dbg, state->xfb, 0);

                /* Main shader no longer uses XFB */
                s->info.has_transform_feedback_varyings = false;
        }

        /* Lower this early so the backends don't have to worry about it */
        if (s->info.stage == MESA_SHADER_FRAGMENT) {
                NIR_PASS_V(s, nir_lower_fragcolor, state->key.fs.nr_cbufs);

                if (state->key.fs.sprite_coord_enable) {
                        NIR_PASS_V(s, nir_lower_texcoord_replace,
                                   state->key.fs.sprite_coord_enable,
                                   true /* point coord is sysval */,
                                   false /* Y-invert */);
                }

                if (state->key.fs.clip_plane_enable) {
                        NIR_PASS_V(s, nir_lower_clip_fs,
                                   state->key.fs.clip_plane_enable,
                                   false);
                }
        }

        /* Call out to Midgard compiler given the above NIR */
        struct panfrost_compile_inputs inputs = {
                .debug = dbg,
                .gpu_id = dev->gpu_id,
                .fixed_sysval_ubo = -1,
                .fixed_varying_mask = state->key.fixed_varying_mask
        };

        /* No IDVS for internal XFB shaders */
        if (s->info.stage == MESA_SHADER_VERTEX && s->info.has_transform_feedback_varyings)
                inputs.no_idvs = true;

        memcpy(inputs.rt_formats, state->key.fs.rt_formats, sizeof(inputs.rt_formats));

        struct util_dynarray binary;

        util_dynarray_init(&binary, NULL);
        screen->vtbl.compile_shader(s, &inputs, &binary, &state->info);

        assert(req_local_mem >= state->info.wls_size);
        state->info.wls_size = req_local_mem;

        if (binary.size) {
                state->bin = panfrost_pool_take_ref(shader_pool,
                        pan_pool_upload_aligned(&shader_pool->base,
                                binary.data, binary.size, 128));
        }


        /* Don't upload RSD for fragment shaders since they need draw-time
         * merging for e.g. depth/stencil/alpha. RSDs are replaced by simpler
         * shader program descriptors on Valhall, which can be preuploaded even
         * for fragment shaders. */
        bool upload = !(s->info.stage == MESA_SHADER_FRAGMENT && dev->arch <= 7);
        screen->vtbl.prepare_shader(state, desc_pool, upload);

        panfrost_analyze_sysvals(state);

        util_dynarray_fini(&binary);

        /* In both clone and tgsi_to_nir paths, the shader is ralloc'd against
         * a NULL context */
        ralloc_free(s);
}
