/*
 * Copyright © 2021 Google, Inc.
 * SPDX-License-Identifier: MIT
 */

#ifndef FREEDRENO_DT_H_
#define FREEDRENO_DT_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * A helper for extracting information about the GPU from devicetree, and
 * mapping it's i/o space, etc.
 *
 * Note, not-reentrant (due to use of nftw(), etc).
 */

bool fd_dt_find_freqs(uint32_t *min_freq, uint32_t *max_freq);
void *fd_dt_find_io(void);

#ifdef __cplusplus
} /* end of extern "C" */
#endif

#endif /* FREEDRENO_DT_H_ */
