/*
 * Copyright © 2012 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#include "util/u_inlines.h"
#include "util/u_screen.h"

#include "freedreno_drm_public.h"

#include "freedreno/freedreno_screen.h"

#include "virtio/virtio-gpu/drm_hw.h"

struct pipe_screen *
fd_drm_screen_create_renderonly(int fd, struct renderonly *ro,
		const struct pipe_screen_config *config)
{
	return u_pipe_screen_lookup_or_create(fd, config, ro, fd_screen_create);
}

/**
 * Check if the native-context type exposed by virtgpu is one we
 * support, and that we support the underlying device.
 */
bool
fd_drm_probe_nctx(int fd, const struct virgl_renderer_capset_drm *caps)
{
	if (caps->context_type != VIRTGPU_DRM_CONTEXT_MSM)
		return false;

	struct fd_dev_id dev_id = {
		.gpu_id = caps->u.msm.gpu_id,
		.chip_id = caps->u.msm.chip_id,
	};
	const struct fd_dev_info info = fd_dev_info(&dev_id);

	return info.chip != 0;
}
