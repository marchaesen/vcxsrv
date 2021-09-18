/*
 * Copyright © 2020 Valve Corporation
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
 *
 */

#include "freedreno_dev_info.h"
#include "util/macros.h"

/**
 * Table entry for a single GPU version
 */
struct fd_dev_rec {
   struct fd_dev_id id;
   const char *name;
   const struct fd_dev_info *info;
};

#include "freedreno_devices.h"

static bool
dev_id_compare(const struct fd_dev_id *a, const struct fd_dev_id *b)
{
   if (a->gpu_id && b->gpu_id) {
      return a->gpu_id == b->gpu_id;
   } else {
      assert(a->chip_id && b->chip_id);
      /* Match on either:
       * (a) exact match
       * (b) device table entry has 0xff wildcard patch_id and core/
       *     major/minor match
       */
      return (a->chip_id == b->chip_id) ||
             (((a->chip_id & 0xff) == 0xff) &&
              ((a->chip_id & UINT64_C(0xffffff00)) ==
               (b->chip_id & UINT64_C(0xffffff00))));
   }
}

const struct fd_dev_info *
fd_dev_info(const struct fd_dev_id *id)
{
   for (int i = 0; i < ARRAY_SIZE(fd_dev_recs); i++) {
      if (dev_id_compare(&fd_dev_recs[i].id, id)) {
         return fd_dev_recs[i].info;
      }
   }
   return NULL;
}

const char *
fd_dev_name(const struct fd_dev_id *id)
{
   for (int i = 0; i < ARRAY_SIZE(fd_dev_recs); i++) {
      if (dev_id_compare(&fd_dev_recs[i].id, id)) {
         return fd_dev_recs[i].name;
      }
   }
   return NULL;
}
