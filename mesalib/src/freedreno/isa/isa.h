/*
 * Copyright © 2020 Google, Inc.
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
 */

#ifndef _ISA_H_
#define _ISA_H_

#include <stdbool.h>
#include <stdint.h>

#include <stdio.h>

struct isa_decode_value {
	/** for {NAME} */
	const char *str;
	/** for all other fields */
	uint64_t num;
};

struct isa_decode_hook {
	const char *fieldname;
	void (*cb)(void *data, struct isa_decode_value *val);
};

struct isa_decode_options {
	uint32_t gpu_id;

	/** show errors detected in decoding, like unexpected dontcare bits */
	bool show_errors;

	/**
	 * If non-zero, maximum # of instructions that are unmatched before
	 * bailing, ie. to trigger stopping if we start trying to decode
	 * random garbage.
	 */
	unsigned max_errors;

	/** Generate branch target labels */
	bool branch_labels;

	/**
	 * Flag which can be set, for ex, but decode hook to trigger end of
	 * decoding
	 */
	bool stop;

	/**
	 * Data passed back to decode hooks
	 */
	void *cbdata;

	/**
	 * Callback for field decode
	 */
	void (*field_cb)(void *data, const char *field_name, struct isa_decode_value *val);

	/**
	 * Callback prior to instruction decode
	 */
	void (*instr_cb)(void *data, unsigned n, void *instr);
};

void isa_decode(void *bin, int sz, FILE *out, const struct isa_decode_options *options);


struct ir3_shader_variant;
void * isa_assemble(struct ir3_shader_variant *v);

#endif /* _ISA_H_ */
