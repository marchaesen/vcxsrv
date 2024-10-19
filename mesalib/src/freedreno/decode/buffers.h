/*
 * Copyright © 2012 Rob Clark <robdclark@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef __BUFFERS_H__
#define __BUFFERS_H__

#include <stdbool.h>
#include <stdint.h>

uint64_t gpuaddr(void *hostptr);
uint64_t gpubaseaddr(uint64_t gpuaddr);
void *hostptr(uint64_t gpuaddr);
unsigned hostlen(uint64_t gpuaddr);
bool has_dumped(uint64_t gpuaddr, unsigned enable_mask);

void reset_buffers(void);
void add_buffer(uint64_t gpuaddr, unsigned int len, void *hostptr);

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

#endif /* __BUFFERS_H__ */
