/*
 * Copyright © 2021 Raspberry Pi
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

#include "v3d_util.h"
#include "util/macros.h"

/* Choose a number of workgroups per supergroup that maximizes
 * lane occupancy. We can pack up to 16 workgroups into a supergroup.
 */
uint32_t
v3d_csd_choose_workgroups_per_supergroup(struct v3d_device_info *devinfo,
                                         bool has_subgroups,
                                         bool has_tsy_barrier,
                                         uint32_t threads,
                                         uint32_t num_wgs,
                                         uint32_t wg_size)
{
   /* FIXME: subgroups may restrict supergroup packing. For now, we disable it
    * completely if the shader uses subgroups.
    */
   if (has_subgroups)
           return 1;

   /* Compute maximum number of batches in a supergroup for this workgroup size.
    * Each batch is 16 elements, and we can have up to 16 work groups in a
    * supergroup:
    *
    * max_batches_per_sg = (wg_size * max_wgs_per_sg) / elements_per_batch
    * since max_wgs_per_sg = 16 and elements_per_batch = 16, we get:
    * max_batches_per_sg = wg_size
    */
   uint32_t max_batches_per_sg = wg_size;

   /* QPU threads will stall at TSY barriers until the entire supergroup
    * reaches the barrier. Limit the supergroup size to half the QPU threads
    * available, so we can have at least 2 supergroups executing in parallel
    * and we don't stall all our QPU threads when a supergroup hits a barrier.
    */
   if (has_tsy_barrier) {
      uint32_t max_qpu_threads = devinfo->qpu_count * threads;
      max_batches_per_sg = MIN2(max_batches_per_sg, max_qpu_threads / 2);
   }
   uint32_t max_wgs_per_sg = max_batches_per_sg * 16 / wg_size;

   uint32_t best_wgs_per_sg = 1;
   uint32_t best_unused_lanes = 16;
   for (uint32_t wgs_per_sg = 1; wgs_per_sg <= max_wgs_per_sg; wgs_per_sg++) {
      /* Don't try to pack more workgroups per supergroup than the total amount
       * of workgroups dispatched.
       */
      if (wgs_per_sg > num_wgs)
         return best_wgs_per_sg;

      /* Compute wasted lines for this configuration and keep track of the
       * config with less waste.
       */
      uint32_t unused_lanes = (16 - ((wgs_per_sg * wg_size) % 16)) & 0x0f;
      if (unused_lanes == 0)
         return wgs_per_sg;

      if (unused_lanes < best_unused_lanes) {
         best_wgs_per_sg = wgs_per_sg;
         best_unused_lanes = unused_lanes;
      }
   }

   return best_wgs_per_sg;
}
