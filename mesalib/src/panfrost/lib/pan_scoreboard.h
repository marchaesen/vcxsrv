/*
 * Copyright (C) 2019-2020 Collabora Ltd.
 * Copyright (C) 2019 Alyssa Rosenzweig
 * Copyright (C) 2014-2017 Broadcom
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

#ifndef __PAN_SCOREBOARD_H__
#define __PAN_SCOREBOARD_H__

#include "midgard_pack.h"
#include "pan_pool.h"

struct pan_scoreboard {
        /* The first job in the batch */
        mali_ptr first_job;

        /* The number of jobs in the primary batch, essentially */
        unsigned job_index;

        /* A CPU-side pointer to the previous job for next_job linking */
        struct mali_job_header_packed *prev_job;

        /* A CPU-side pointer to the first tiler job for dep updates when
         * injecting a reload tiler job.
         */
        struct mali_job_header_packed *first_tiler;
        uint32_t first_tiler_dep1;

        /* The dependency for tiler jobs (i.e. the index of the last emitted
         * tiler job, or zero if none have been emitted) */
        unsigned tiler_dep;

        /* The job index of the WRITE_VALUE job (before it has been created) */
        unsigned write_value_index;
};

unsigned
panfrost_add_job(
                struct pan_pool *pool,
                struct pan_scoreboard *scoreboard,
                enum mali_job_type type,
                bool barrier,
                unsigned local_dep,
                const struct panfrost_ptr *job,
                bool inject);

void panfrost_scoreboard_initialize_tiler(
                struct pan_pool *pool,
                struct pan_scoreboard *scoreboard,
                mali_ptr polygon_list);

#endif
