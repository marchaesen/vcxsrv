/*
 * Copyright © 2020 Google, Inc.
 * SPDX-License-Identifier: MIT
 */

#ifndef __MAIN_H__
#define __MAIN_H__

#include <err.h>
#include <stdint.h>
#include <stdio.h>

#include "drm/freedreno_drmif.h"
#include "drm/freedreno_ringbuffer.h"

#include "adreno_common.xml.h"
#include "adreno_pm4.xml.h"

#define MAX_BUFS 4

struct kernel {
   /* filled in by backend when shader is assembled: */
   uint32_t local_size[3];
   uint32_t num_bufs;
   uint32_t buf_sizes[MAX_BUFS]; /* size in dwords */
   uint32_t buf_addr_regs[MAX_BUFS];
   uint32_t *buf_init_data[MAX_BUFS];

   /* filled in by frontend before launching grid: */
   struct fd_bo *bufs[MAX_BUFS];
};

struct perfcntr {
   const char *name;

   /* for backend to configure/read the counter, describes
    * the selected counter:
    */
   unsigned select_reg;
   unsigned counter_reg_lo;
   unsigned counter_reg_hi;
   /* and selected countable:
    */
   unsigned selector;
};

/* per-generation entry-points: */
struct backend {
   struct kernel *(*assemble)(struct backend *b, FILE *in);
   void (*disassemble)(struct kernel *kernel, FILE *out);
   void (*emit_grid)(struct kernel *kernel, uint32_t grid[3],
                     struct fd_submit *submit);

   /* performance-counter API: */
   void (*set_perfcntrs)(struct backend *b, const struct perfcntr *perfcntrs,
                         unsigned num_perfcntrs);
   void (*read_perfcntrs)(struct backend *b, uint64_t *results);
};

#define define_cast(_from, _to)                                                \
   static inline struct _to *to_##_to(struct _from *f)                         \
   {                                                                           \
      return (struct _to *)f;                                                  \
   }

struct backend *a4xx_init(struct fd_device *dev, const struct fd_dev_id *dev_id);
template<chip CHIP>
struct backend *a6xx_init(struct fd_device *dev, const struct fd_dev_id *dev_id);

/* for conditionally setting boolean flag(s): */
#define COND(bool, val) ((bool) ? (val) : 0)

#endif /* __MAIN_H__ */
