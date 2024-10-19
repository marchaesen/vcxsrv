/*
 * Copyright © 2010 Marcin Kościelnicki <koriakin@0x04.net>
 * All Rights Reserved.
 * SPDX-License-Identifier: MIT
 */

#ifndef RNNDEC_H
#define RNNDEC_H

#include "rnn.h"
#include "colors.h"

struct rnndecvariant {
	struct rnnenum *en;
	int variant;
};

struct rnndeccontext {
	struct rnndb *db;
	struct rnndecvariant **vars;
	int varsnum;
	int varsmax;
	const struct envy_colors *colors;
};

struct rnndecaddrinfo {
	struct rnntypeinfo *typeinfo;
	int width;
	char *name;
};

struct rnndeccontext *rnndec_newcontext(struct rnndb *db);
int rnndec_varadd(struct rnndeccontext *ctx, char *varset, const char *variant);
int rnndec_varmatch(struct rnndeccontext *ctx, struct rnnvarinfo *vi);
const char *rnndec_decode_enum(struct rnndeccontext *ctx, const char *enumname, uint64_t enumval);
char *rnndec_decodeval(struct rnndeccontext *ctx, struct rnntypeinfo *ti, uint64_t value);
int rnndec_checkaddr(struct rnndeccontext *ctx, struct rnndomain *domain, uint64_t addr, int write);
struct rnndecaddrinfo *rnndec_decodeaddr(struct rnndeccontext *ctx, struct rnndomain *domain, uint64_t addr, int write);
uint64_t rnndec_decodereg(struct rnndeccontext *ctx, struct rnndomain *domain, const char *name);

#endif
