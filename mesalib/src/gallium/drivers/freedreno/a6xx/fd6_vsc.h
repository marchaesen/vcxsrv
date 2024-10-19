/*
 * Copyright © 2020 Google, Inc.
 * SPDX-License-Identifier: MIT
 */

#ifndef FD6_VSC_H_
#define FD6_VSC_H_

EXTERNC
void fd6_vsc_update_sizes(struct fd_batch *batch,
                          const struct pipe_draw_info *info,
                          const struct pipe_draw_start_count_bias *draw);

#endif /* FD6_VSC_H_ */
