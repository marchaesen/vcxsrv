/*
 * Copyright © 2012 Marcin Kościelnicki <koriakin@0x04.net>
 * All Rights Reserved.
 * SPDX-License-Identifier: MIT
 */

#ifndef COLORS_H
#define COLORS_H

struct envy_colors {
	const char *reset;
	const char *iname;	/* instruction name */
	const char *rname;	/* register or bitfield name */
	const char *mod;	/* instruction modifier */
	const char *sym;	/* auxiliary char like { , + */
	const char *reg;	/* ISA register */
	const char *regsp;	/* special ISA register */
	const char *num;	/* immediate number */
	const char *mem;	/* memory reference */
	const char *btarg;	/* branch target */
	const char *ctarg;	/* call target */
	const char *bctarg;	/* branch and call target */
	const char *eval;	/* enum value */
	const char *comm;	/* comment */
	const char *err;	/* error */
};

extern const struct envy_colors envy_null_colors;
extern const struct envy_colors envy_def_colors;

#endif
