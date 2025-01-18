/*
 * Copyright © 2011 Marcin Kościelnicki <koriakin@0x04.net>
 * All Rights Reserved.
 * SPDX-License-Identifier: MIT
 */

#include "util.h"
#include <string.h>

FILE *find_in_path(const char *name, const char *path, char **pfullname) {
	if (!path)
		return 0;
	while (path) {
		const char *npath = strchr(path, ':');
		size_t plen;
		if (npath) {
			plen = npath - path;
			npath++;
		} else {
			plen = strlen(path);
		}
		if (plen) {
			/* also look for .gz compressed xml: */
			const char *exts[] = { "", ".gz" };
			for (int i = 0; i < ARRAY_SIZE(exts); i++) {
				char *fullname;

				int ret = asprintf(&fullname, "%.*s/%s%s", (int)plen, path, name, exts[i]);
				if (ret < 0)
					return NULL;

				FILE *file = fopen(fullname, "r");
				if (file) {
					if (pfullname)
						*pfullname = fullname;
					else
						free(fullname);
					return file;
				}
				free(fullname);
			}
		}
		path = npath;
	}
	return 0;
}
