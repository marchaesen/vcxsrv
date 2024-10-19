/*
 * Copyright © 2018 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef FD4_RESOURCE_H_
#define FD4_RESOURCE_H_

#include "freedreno_resource.h"

uint32_t fd4_setup_slices(struct fd_resource *rsc);
unsigned fd4_tile_mode(const struct pipe_resource *tmpl);

#endif /* FD4_RESOURCE_H_ */
