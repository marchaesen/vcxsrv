/*
 * Copyright 2019 Intel Corporation
 * SPDX-License-Identifier: MIT
 *
 * File operations helpers
 */

#ifndef _OS_FILE_H_
#define _OS_FILE_H_

#ifdef  __cplusplus
extern "C" {
#endif

/*
 * Read a file.
 * Returns a char* that the caller must free(), or NULL and sets errno.
 */
char *
os_read_file(const char *filename);

#ifdef __cplusplus
}
#endif

#endif /* _OS_FILE_H_ */
