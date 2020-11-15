/*
 * Copyright (C) 2019 Collabora, Ltd.
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

#include <string.h>
#include "pan_scoreboard.h"
#include "pan_device.h"
#include "panfrost-quirks.h"

/*
 * There are various types of Mali jobs:
 *
 *  - WRITE_VALUE: generic write primitive, used to zero tiler field
 *  - VERTEX: runs a vertex shader
 *  - TILER: runs tiling and sets up a fragment shader
 *  - FRAGMENT: runs fragment shaders and writes out
 *  - COMPUTE: runs a compute shader
 *  - FUSED: vertex+tiler fused together, implicit intradependency (Bifrost)
 *  - GEOMETRY: runs a geometry shader (unimplemented)
 *  - CACHE_FLUSH: unseen in the wild, theoretically cache flush
 *
 * In between a full batch and a single Mali job is the "job chain", a series
 * of Mali jobs together forming a linked list. Within the job chain, each Mali
 * job can set (up to) two dependencies on other earlier jobs in the chain.
 * This dependency graph forms a scoreboard. The general idea of a scoreboard
 * applies: when there is a data dependency of job B on job A, job B sets one
 * of its dependency indices to job A, ensuring that job B won't start until
 * job A finishes.
 *
 * More specifically, here are a set of rules:
 *
 * - A write value job must appear if and only if there is at least one tiler
 *   job, and tiler jobs must depend on it.
 *
 * - Vertex jobs and tiler jobs are independent.
 *
 * - A tiler job must have a dependency on its data source. If it's getting
 *   data from a vertex job, it depends on the vertex job. If it's getting data
 *   from software, this is null.
 *
 * - Tiler jobs must depend on the write value job (chained or otherwise).
 *
 * - Tiler jobs must be strictly ordered. So each tiler job must depend on the
 *   previous job in the chain.
 *
 * - Jobs linking via next_job has no bearing on order of execution, rather it
 *   just establishes the linked list of jobs, EXCEPT:
 *
 * - A job's dependencies must appear earlier in the linked list (job chain).
 *
 * Justification for each rule:
 *
 * - Write value jobs are used to write a zero into a magic tiling field, which
 *   enables tiling to work. If tiling occurs, they are needed; if it does not,
 *   we cannot emit them since then tiling partially occurs and it's bad.
 *
 * - The hardware has no notion of a "vertex/tiler job" (at least not our
 *   hardware -- other revs have fused jobs, but --- crap, this just got even
 *   more complicated). They are independent units that take in data, process
 *   it, and spit out data.
 *
 * - Any job must depend on its data source, in fact, or risk a
 *   read-before-write hazard. Tiler jobs get their data from vertex jobs, ergo
 *   tiler jobs depend on the corresponding vertex job (if it's there).
 *
 * - The tiler is not thread-safe; this dependency prevents race conditions
 *   between two different jobs trying to write to the tiler outputs at the
 *   same time.
 *
 * - Internally, jobs are scoreboarded; the next job fields just form a linked
 *   list to allow the jobs to be read in; the execution order is from
 *   resolving the dependency fields instead.
 *
 * - The hardware cannot set a dependency on a job it doesn't know about yet,
 *   and dependencies are processed in-order of the next job fields.
 *
 */

/* Generates, uploads, and queues a a new job. All fields are written in order
 * except for next_job accounting (TODO: Should we be clever and defer the
 * upload of the header here until next job to keep the access pattern totally
 * linear? Or is that just a micro op at this point?). Returns the generated
 * index for dep management.
 *
 * Inject is used to inject a job at the front, for wallpapering. If you are
 * not wallpapering and set this, dragons will eat you. */

unsigned
panfrost_add_job(
                struct pan_pool *pool,
                struct pan_scoreboard *scoreboard,
                enum mali_job_type type,
                bool barrier,
                unsigned local_dep,
                const struct panfrost_ptr *job,
                bool inject)
{
        bool is_bifrost = !!(pool->dev->quirks & IS_BIFROST);
        unsigned global_dep = 0;

        if (type == MALI_JOB_TYPE_TILER) {
                /* Tiler jobs must be chained, and on Midgard, the first tiler
                 * job must depend on the write value job, whose index we
                 * reserve now */

                if (is_bifrost && !scoreboard->write_value_index)
                        scoreboard->write_value_index = ++scoreboard->job_index;

                if (scoreboard->tiler_dep && !inject)
                        global_dep = scoreboard->tiler_dep;
                else if (is_bifrost)
                        global_dep = scoreboard->write_value_index;
        }

        /* Assign the index */
        unsigned index = ++scoreboard->job_index;

        pan_pack(job->cpu, JOB_HEADER, header) {
                header.type = type;
                header.barrier = barrier;
                header.index = index;
                header.dependency_1 = local_dep;
                header.dependency_2 = global_dep;

                if (inject)
                        header.next = scoreboard->first_job;
        }

        if (inject) {
                if (type == MALI_JOB_TYPE_TILER) {
                        if (scoreboard->first_tiler) {
                                /* Manual update of the dep2 field. This is bad,
                                 * don't copy this pattern.
                                 */
                                scoreboard->first_tiler->opaque[5] =
                                        scoreboard->first_tiler_dep1 | (index << 16);
                        }

                        scoreboard->first_tiler = (void *)job->cpu;
                        scoreboard->first_tiler_dep1 = local_dep;
                }
                scoreboard->first_job = job->gpu;
                return index;
        }

        /* Form a chain */
        if (type == MALI_JOB_TYPE_TILER) {
                if (!scoreboard->first_tiler) {
                        scoreboard->first_tiler = (void *)job->cpu;
                        scoreboard->first_tiler_dep1 = local_dep;
                }
                scoreboard->tiler_dep = index;
        }

        if (scoreboard->prev_job) {
                /* Manual update of the next pointer. This is bad, don't copy
                 * this pattern.
                 * TODO: Find a way to defer last job header emission until we
                 * have a new job to queue or the batch is ready for execution.
                 */
                scoreboard->prev_job->opaque[6] = job->gpu;
                scoreboard->prev_job->opaque[7] = job->gpu >> 32;
	} else {
                scoreboard->first_job = job->gpu;
        }

        scoreboard->prev_job = (struct mali_job_header_packed *)job->cpu;
        return index;
}

/* Generates a write value job, used to initialize the tiler structures. Note
 * this is called right before frame submission. */

void
panfrost_scoreboard_initialize_tiler(struct pan_pool *pool,
                struct pan_scoreboard *scoreboard,
                mali_ptr polygon_list)
{
        /* Check if we even need tiling */
        if (pool->dev->quirks & IS_BIFROST || !scoreboard->tiler_dep)
                return;

        /* Okay, we do. Let's generate it. We'll need the job's polygon list
         * regardless of size. */

        struct panfrost_ptr transfer =
                panfrost_pool_alloc_aligned(pool,
                                            MALI_WRITE_VALUE_JOB_LENGTH,
                                            64);

        pan_section_pack(transfer.cpu, WRITE_VALUE_JOB, HEADER, header) {
                header.type = MALI_JOB_TYPE_WRITE_VALUE;
                header.index = scoreboard->write_value_index;
                header.next = scoreboard->first_job;
        }

        pan_section_pack(transfer.cpu, WRITE_VALUE_JOB, PAYLOAD, payload) {
                payload.address = polygon_list;
                payload.type = MALI_WRITE_VALUE_TYPE_ZERO;
        }

        scoreboard->first_job = transfer.gpu;
}
