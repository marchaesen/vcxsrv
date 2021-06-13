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
#include "panfrost-quirks.h"

#include "compiler/nir/nir.h"
#include "nir/tgsi_to_nir.h"
#include "util/u_dynarray.h"
#include "util/u_upload_mgr.h"

#include "tgsi/tgsi_dump.h"

static void
pan_upload_shader_descriptor(struct panfrost_context *ctx,
                        struct panfrost_shader_state *state)
{
        const struct panfrost_device *dev = pan_device(ctx->base.screen);
        struct mali_state_packed *out;

        u_upload_alloc(ctx->state_uploader, 0, MALI_RENDERER_STATE_LENGTH, MALI_RENDERER_STATE_LENGTH,
                        &state->upload.offset, &state->upload.rsrc, (void **) &out);

        pan_pack(out, RENDERER_STATE, cfg) {
                pan_shader_prepare_rsd(dev, &state->info,
                                       state->bo ? state->bo->ptr.gpu : 0,
                                       &cfg);
        }

        u_upload_unmap(ctx->state_uploader);
}

void
panfrost_shader_compile(struct panfrost_context *ctx,
                        enum pipe_shader_ir ir_type,
                        const void *ir,
                        gl_shader_stage stage,
                        struct panfrost_shader_state *state)
{
        struct panfrost_device *dev = pan_device(ctx->base.screen);

        nir_shader *s;

        if (ir_type == PIPE_SHADER_IR_NIR) {
                s = nir_shader_clone(NULL, ir);
        } else {
                assert (ir_type == PIPE_SHADER_IR_TGSI);
                s = tgsi_to_nir(ir, ctx->base.screen, false);
        }

        s->info.stage = stage;

        /* Call out to Midgard compiler given the above NIR */
        struct panfrost_compile_inputs inputs = {
                .gpu_id = dev->gpu_id,
                .shaderdb = !!(dev->debug & PAN_DBG_PRECOMPILE),
        };

        memcpy(inputs.rt_formats, state->rt_formats, sizeof(inputs.rt_formats));

        struct util_dynarray binary;

        util_dynarray_init(&binary, NULL);
        pan_shader_compile(dev, s, &inputs, &binary, &state->info);

        /* Prepare the compiled binary for upload */
        if (binary.size) {
                state->bo = panfrost_bo_create(dev, binary.size, PAN_BO_EXECUTE);
                memcpy(state->bo->ptr.cpu, binary.data, binary.size);
        }

        /* Midgard needs the first tag on the bottom nibble */

        if (stage != MESA_SHADER_FRAGMENT)
                pan_upload_shader_descriptor(ctx, state);

        util_dynarray_fini(&binary);

        /* In both clone and tgsi_to_nir paths, the shader is ralloc'd against
         * a NULL context */
        ralloc_free(s);
}
