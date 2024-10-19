/* -*- mode: C; c-file-style: "k&r"; tab-width 4; indent-tabs-mode: t; -*- */

/*
 * Copyright © 2014 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef IO_H_
#define IO_H_

/* Simple API to abstract reading from file which might be compressed.
 * Maybe someday I'll add writing..
 */

struct io;

struct io *io_open(const char *filename);
struct io *io_openfd(int fd);
void io_close(struct io *io);
unsigned io_offset(struct io *io);
int io_readn(struct io *io, void *buf, int nbytes);

static inline int
check_extension(const char *path, const char *ext)
{
   return strcmp(path + strlen(path) - strlen(ext), ext) == 0;
}

#endif /* IO_H_ */
