/*
 * Copyright © 2020 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "common/freedreno_dev_info.h"

struct testcase {
   enum pipe_format format;

   int array_size; /* Size for array textures, or 0 otherwise. */
   bool is_3d;

   /* Partially filled layout of input parameters and expected results. */
   struct {
      uint32_t tile_mode : 2;
      bool tile_all : 1;
      bool ubwc : 1;
      uint32_t width0, height0, depth0;
      uint32_t nr_samples;
      struct {
         uint32_t offset;
         uint32_t pitch;
         uint32_t size0;
      } slices[FDL_MAX_MIP_LEVELS];
      struct {
         uint32_t offset;
         uint32_t pitch;
      } ubwc_slices[FDL_MAX_MIP_LEVELS];
   } layout;
};

bool fdl_test_layout(const struct testcase *testcase, const struct fd_dev_id *dev_id);
