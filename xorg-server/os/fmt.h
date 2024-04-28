/* SPDX-License-Identifier: MIT OR X11
 *
 * Copyright © 2024 Enrico Weigelt, metux IT consult <info@metux.net>
 */
#ifndef _XSERVER_OS_FMT_H
#define _XSERVER_OS_FMT_H

#include <stdint.h>

void FormatInt64(int64_t num, char *string);
void FormatUInt64(uint64_t num, char *string);
void FormatUInt64Hex(uint64_t num, char *string);
void FormatDouble(double dbl, char *string);

#endif /* _XSERVER_OS_FMT_H */
