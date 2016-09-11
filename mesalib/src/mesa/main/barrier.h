/*
 * Copyright © 2011 Marek Olšák <maraeo@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/**
 * \file barrier.h
 * GL_NV_texture_barrier and friends.
 *
 * \author Marek Olšák <maraeo@gmail.com>
 */

#ifndef BARRIER_H
#define BARRIER_H

#include "glheader.h"

struct dd_function_table;

extern void
_mesa_init_barrier_functions(struct dd_function_table *driver);

extern void GLAPIENTRY
_mesa_TextureBarrierNV(void);

void GLAPIENTRY
_mesa_MemoryBarrier(GLbitfield barriers);

void GLAPIENTRY
_mesa_MemoryBarrierByRegion(GLbitfield barriers);

void GLAPIENTRY
_mesa_BlendBarrier(void);

#endif /* BARRIER_H */
