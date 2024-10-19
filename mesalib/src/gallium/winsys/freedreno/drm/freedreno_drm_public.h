
#ifndef __FREEDRENO_DRM_PUBLIC_H__
#define __FREEDRENO_DRM_PUBLIC_H__

#include <stdbool.h>

struct pipe_screen;
struct renderonly;

struct pipe_screen *fd_drm_screen_create_renderonly(int drmFD,
                                                    struct renderonly *ro,
                                                    const struct pipe_screen_config *config);

struct virgl_renderer_capset_drm;

bool fd_drm_probe_nctx(int fd, const struct virgl_renderer_capset_drm *caps);

#endif
