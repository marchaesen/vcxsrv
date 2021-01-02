/*
 * Copyright (C) 2014 Rob Clark <robclark@freedesktop.org>
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef IR3_GALLIUM_H_
#define IR3_GALLIUM_H_

#include "pipe/p_state.h"
#include "pipe/p_screen.h"
#include "ir3/ir3_shader.h"

struct ir3_shader * ir3_shader_create(struct ir3_compiler *compiler,
		const struct pipe_shader_state *cso,
		struct pipe_debug_callback *debug,
		struct pipe_screen *screen);
struct ir3_shader *
ir3_shader_create_compute(struct ir3_compiler *compiler,
		const struct pipe_compute_state *cso,
		struct pipe_debug_callback *debug,
		struct pipe_screen *screen);
struct ir3_shader_variant * ir3_shader_variant(struct ir3_shader *shader,
		struct ir3_shader_key key, bool binning_pass,
		struct pipe_debug_callback *debug);

void * ir3_shader_state_create(struct pipe_context *pctx, const struct pipe_shader_state *cso);
void ir3_shader_state_delete(struct pipe_context *pctx, void *hwcso);

void ir3_prog_init(struct pipe_context *pctx);
void ir3_screen_init(struct pipe_screen *pscreen);

/**
 * A helper to determine if a fs input 'i' is point/sprite coord, given
 * the specified sprite_coord_enable mask
 */
static inline bool
ir3_point_sprite(const struct ir3_shader_variant *fs, int i,
		uint32_t sprite_coord_enable, bool *coord_mode)
{
	gl_varying_slot slot = fs->inputs[i].slot;
	switch (slot) {
	case VARYING_SLOT_PNTC:
		*coord_mode = true;
		return true;
	case VARYING_SLOT_TEX0 ... VARYING_SLOT_TEX7:
		return !!(sprite_coord_enable & BITFIELD_BIT(slot - VARYING_SLOT_TEX0));
	default:
		return false;
	}
}

#endif /* IR3_GALLIUM_H_ */
