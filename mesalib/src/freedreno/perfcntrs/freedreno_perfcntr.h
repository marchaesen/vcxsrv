/*
 * Copyright © 2018 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef FREEDRENO_PERFCNTR_H_
#define FREEDRENO_PERFCNTR_H_

#include "util/macros.h"

#include "freedreno_dev_info.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Mapping very closely to the AMD_performance_monitor extension, adreno has
 * groups of performance counters where each group has N counters, which can
 * select from M different countables (things that can be counted), where
 * generally M > N.
 */

/* Describes a single counter: */
struct fd_perfcntr_counter {
   /* offset of the select register to choose what to count: */
   unsigned select_reg;
   /* offset of the lo/hi 32b to read current counter value: */
   unsigned counter_reg_lo;
   unsigned counter_reg_hi;
   /* Optional, most counters don't have enable/clear registers: */
   unsigned enable;
   unsigned clear;
};

enum fd_perfcntr_type {
   FD_PERFCNTR_TYPE_UINT64,
   FD_PERFCNTR_TYPE_UINT,
   FD_PERFCNTR_TYPE_FLOAT,
   FD_PERFCNTR_TYPE_PERCENTAGE,
   FD_PERFCNTR_TYPE_BYTES,
   FD_PERFCNTR_TYPE_MICROSECONDS,
   FD_PERFCNTR_TYPE_HZ,
   FD_PERFCNTR_TYPE_DBM,
   FD_PERFCNTR_TYPE_TEMPERATURE,
   FD_PERFCNTR_TYPE_VOLTS,
   FD_PERFCNTR_TYPE_AMPS,
   FD_PERFCNTR_TYPE_WATTS,
};

/* Whether an average value per frame or a cumulative value should be
 * displayed.
 */
enum fd_perfcntr_result_type {
   FD_PERFCNTR_RESULT_TYPE_AVERAGE,
   FD_PERFCNTR_RESULT_TYPE_CUMULATIVE,
};

/* Describes a single countable: */
struct fd_perfcntr_countable {
   const char *name;
   /* selector register enum value to select this countable: */
   unsigned selector;

   /* description of the countable: */
   enum fd_perfcntr_type query_type;
   enum fd_perfcntr_result_type result_type;
};

/* Describes an entire counter group: */
struct fd_perfcntr_group {
   const char *name;
   unsigned num_counters;
   const struct fd_perfcntr_counter *counters;
   unsigned num_countables;
   const struct fd_perfcntr_countable *countables;
};

const struct fd_perfcntr_group *fd_perfcntrs(const struct fd_dev_id *id, unsigned *count);

#define COUNTER_BASE(_sel, _lo, _hi) {                                         \
      .select_reg = _sel, .counter_reg_lo = _lo, .counter_reg_hi = _hi,        \
   }

#define COUNTER(_sel, _lo, _hi) COUNTER_BASE(REG(_sel), REG(_lo), REG(_hi))

#define COUNTER2(_sel, _lo, _hi, _en, _clr) {                                  \
      .select_reg = REG(_sel), .counter_reg_lo = REG(_lo),                     \
      .counter_reg_hi = REG(_hi), .enable = REG(_en), .clear = REG(_clr),      \
   }

#define COUNTABLE_BASE(_sel_name, _sel, _query_type, _result_type ) {          \
      .name = _sel_name, .selector = _sel,                                     \
      .query_type = FD_PERFCNTR_TYPE_##_query_type,                            \
      .result_type = FD_PERFCNTR_RESULT_TYPE_##_result_type,                   \
   }

#define COUNTABLE(_selector, _query_type, _result_type)                        \
   COUNTABLE_BASE(#_selector, _selector, _query_type, _result_type)

#define GROUP(_name, _counters, _countables) {                                 \
      .name = _name, .num_counters = ARRAY_SIZE(_counters),                    \
      .counters = _counters, .num_countables = ARRAY_SIZE(_countables),        \
      .countables = _countables,                                               \
   }

#ifdef __cplusplus
} /* end of extern "C" */
#endif

#endif /* FREEDRENO_PERFCNTR_H_ */
