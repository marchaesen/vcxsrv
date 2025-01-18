/* -*- mode: C; c-file-style: "k&r"; tab-width 4; indent-tabs-mode: t; -*- */

/*
 * Copyright © 2014 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef RNNUTIL_H_
#define RNNUTIL_H_

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "rnn.h"
#include "rnndec.h"

struct rnn {
   struct rnndb *db;
   struct rnndeccontext *vc, *vc_nocolor;
   struct rnndomain *dom[2];
   const char *variant;
};

union rnndecval {
   uint64_t u;
   int64_t i;
};

void _rnn_init(struct rnn *rnn, int nocolor);
struct rnn *rnn_new(int nocolor);
void rnn_load_file(struct rnn *rnn, char *file, char *domain);
void rnn_load(struct rnn *rnn, const char *gpuname);
uint32_t rnn_regbase(struct rnn *rnn, const char *name);
const char *rnn_regname(struct rnn *rnn, uint32_t regbase, int color);
struct rnndecaddrinfo *rnn_reginfo(struct rnn *rnn, uint32_t regbase);
void rnn_reginfo_free(struct rnndecaddrinfo *info);
const char *rnn_enumname(struct rnn *rnn, const char *name, uint32_t val);

struct rnndelem *rnn_regelem(struct rnn *rnn, const char *name);
struct rnndelem *rnn_regoff(struct rnn *rnn, uint32_t offset);
enum rnnttype rnn_decodelem(struct rnn *rnn, struct rnntypeinfo *info,
                            uint64_t regval, union rnndecval *val);

#endif /* RNNUTIL_H_ */
