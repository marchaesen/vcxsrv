/*
 * Copyright © 2012 Rob Clark <robclark@freedesktop.org>
 * Copyright © 2019 Khaled Emara <ekhaled1836@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef FD3_RESOURCE_H_
#define FD3_RESOURCE_H_

#include "pipe/p_state.h"

#include "freedreno_resource.h"

uint32_t fd3_setup_slices(struct fd_resource *rsc);
unsigned fd3_tile_mode(const struct pipe_resource *tmpl);

#endif /* FD3_RESOURCE_H_ */
