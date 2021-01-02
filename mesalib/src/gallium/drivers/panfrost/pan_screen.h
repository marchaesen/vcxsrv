/**************************************************************************
 *
 * Copyright 2018-2019 Alyssa Rosenzweig
 * Copyright 2018-2019 Collabora, Ltd.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#ifndef PAN_SCREEN_H
#define PAN_SCREEN_H

#include <xf86drm.h>
#include "pipe/p_screen.h"
#include "pipe/p_defines.h"
#include "renderonly/renderonly.h"
#include "util/u_dynarray.h"
#include "util/bitset.h"
#include "util/set.h"

#include "pan_device.h"
#include "pan_pool.h"

struct panfrost_batch;
struct panfrost_context;
struct panfrost_resource;

struct panfrost_screen {
        struct pipe_screen base;
        struct panfrost_device dev;
};

static inline struct panfrost_screen *
pan_screen(struct pipe_screen *p)
{
        return (struct panfrost_screen *)p;
}

static inline struct panfrost_device *
pan_device(struct pipe_screen *p)
{
        return &(pan_screen(p)->dev);
}

struct panfrost_fence *
panfrost_fence_create(struct panfrost_context *ctx);

#endif /* PAN_SCREEN_H */
