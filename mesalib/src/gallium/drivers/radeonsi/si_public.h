/*
 * Copyright 2010 Jerome Glisse <glisse@freedesktop.org>
 * Copyright 2018 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef SI_PUBLIC_H
#define SI_PUBLIC_H

struct pipe_screen;
struct pipe_screen_config;
struct virgl_renderer_capset_drm;

struct pipe_screen *radeonsi_screen_create(int fd, const struct pipe_screen_config *config);

bool si_virtgpu_probe_nctx(int fd, const struct virgl_renderer_capset_drm *caps);

#endif
