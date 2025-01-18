/*
 * Copyright © 2012 Marcin Kościelnicki <koriakin@0x04.net>
 * All Rights Reserved.
 * SPDX-License-Identifier: MIT
 */

#include "colors.h"

const struct envy_colors envy_null_colors = {
	.reset	= "",
	.iname	= "",
	.rname	= "",
	.mod	= "",
	.sym	= "",
	.reg	= "",
	.regsp	= "",
	.num	= "",
	.mem	= "",
	.btarg	= "",
	.ctarg	= "",
	.bctarg	= "",
	.eval	= "",
	.comm	= "",
	.err	= "",
};

const struct envy_colors envy_def_colors = {
	.reset	= "\x1b[0m",
	.iname	= "\x1b[0;32m",
	.rname	= "\x1b[0;32m",
	.mod	= "\x1b[0;36m",
	.sym	= "\x1b[0;36m",
	.reg	= "\x1b[0;31m",
	.regsp	= "\x1b[0;35m",
	.num	= "\x1b[0;33m",
	.mem	= "\x1b[0;35m",
	.btarg	= "\x1b[0;35m",
	.ctarg	= "\x1b[0;1;37m",
	.bctarg	= "\x1b[0;1;35m",
	.eval	= "\x1b[0;35m",
	.comm	= "\x1b[0;34m",
	.err	= "\x1b[0;1;31m",
};
