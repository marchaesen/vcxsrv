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

#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util/bitset.h"
#include "util/compiler.h"
#include "util/half_float.h"
#include "util/hash_table.h"
#include "util/ralloc.h"
#include "util/u_debug.h"
#include "util/u_math.h"

#include "decode.h"
#include "isa.h"

/**
 * The set of leaf node bitsets in the bitset hiearchy which defines all
 * the possible instructions.
 *
 * TODO maybe we want to pass this in as parameter so this same decoder
 * can work with multiple different instruction sets.
 */
extern const struct isa_bitset *__instruction[];

struct decode_state;

/**
 * Decode scope.  When parsing a field that is itself a bitset, we push a
 * new scope to the stack.  A nested bitset is allowed to resolve fields
 * from an enclosing scope (needed, for example, to decode src register
 * bitsets, where half/fullness is determined by fields outset if bitset
 * in the instruction containing the bitset.
 *
 * But the field being resolved could be a derived field, or different
 * depending on an override at a higher level of the stack, requiring
 * expression evaluation which could in turn reference variables which
 * triggers a recursive field lookup.  But those lookups should not start
 * from the top of the stack, but instead the current stack level.  This
 * prevents a field from accidentally resolving to different values
 * depending on the starting point of the lookup.  (Not only causing
 * confusion, but this is behavior we don't want to depend on if we
 * wanted to optimize things by caching field lookup results.)
 */
struct decode_scope {
	/**
	 * Enclosing scope
	 */
	struct decode_scope *parent;

	/**
	 * Current bitset value being decoded
	 */
	uint64_t val;

	/**
	 * Current bitset.
	 */
	const struct isa_bitset *bitset;

	/**
	 * Field name remapping.
	 */
	const struct isa_field_params *params;

	/**
	 * Pointer back to decode state, for convenience.
	 */
	struct decode_state *state;

	/**
	 * Cache expression evaluation results.  Expressions for overrides can
	 * be repeatedly evaluated for each field being resolved.  And each
	 * field reference to a derived field (potentially from another expr)
	 * would require re-evaluation.  But for a given scope, each evaluation
	 * of an expression gives the same result.  So we can cache to speed
	 * things up.
	 *
	 * TODO we could maybe be clever and assign a unique idx to each expr
	 * and use a direct lookup table?  Would be a bit more clever if it was
	 * smart enough to allow unrelated expressions that are never involved
	 * in a given scope to have overlapping cache lookup idx's.
	 */
	struct hash_table *cache;
};

/**
 * Current decode state
 */
struct decode_state {
	const struct isa_decode_options *options;
	FILE *out;

	/**
	 * Current instruction being decoded:
	 */
	unsigned n;

	/**
	 * Number of instructions being decoded
	 */
	unsigned num_instr;

	/**
	 * Bitset of instructions that are branch targets (if options->branch_labels
	 * is enabled)
	 */
	BITSET_WORD *branch_targets;

	/**
	 * We allow a limited amount of expression evaluation recursion, but
	 * not recursive evaluation of any given expression, to prevent infinite
	 * recursion.
	 */
	int expr_sp;
	isa_expr_t expr_stack[8];

	/**
	 * Current topmost/innermost level of scope used for decoding fields,
	 * including derived fields which may in turn rely on decoding other
	 * fields, potentially from a lower/out level in the stack.
	 */
	struct decode_scope *scope;

	/**
	 * A small fixed upper limit on # of decode errors to capture per-
	 * instruction seems reasonable.
	 */
	unsigned num_errors;
	char *errors[4];
};

static void display(struct decode_scope *scope);
static void decode_error(struct decode_state *state, const char *fmt, ...) _util_printf_format(2,3);

static void
decode_error(struct decode_state *state, const char *fmt, ...)
{
	if (!state->options->show_errors) {
		return;
	}

	if (state->num_errors == ARRAY_SIZE(state->errors)) {
		/* too many errors, bail */
		return;
	}

	va_list ap;
	va_start(ap, fmt);
	vasprintf(&state->errors[state->num_errors++], fmt, ap);
	va_end(ap);
}

static unsigned
flush_errors(struct decode_state *state)
{
	unsigned num_errors = state->num_errors;
	if (num_errors > 0)
		fprintf(state->out, "\t; ");
	for (unsigned i = 0; i < num_errors; i++) {
		fprintf(state->out, "%s%s", (i > 0) ? ", " : "", state->errors[i]);
		free(state->errors[i]);
	}
	state->num_errors = 0;
	return num_errors;
}


static bool
push_expr(struct decode_state *state, isa_expr_t expr)
{
	for (int i = state->expr_sp - 1; i > 0; i--) {
		if (state->expr_stack[i] == expr) {
			return false;
		}
	}
	state->expr_stack[state->expr_sp++] = expr;
	return true;
}

static void
pop_expr(struct decode_state *state)
{
	assert(state->expr_sp > 0);
	state->expr_sp--;
}

static struct decode_scope *
push_scope(struct decode_state *state, const struct isa_bitset *bitset, uint64_t val)
{
	struct decode_scope *scope = rzalloc_size(state, sizeof(*scope));

	scope->val = val;
	scope->bitset = bitset;
	scope->parent = state->scope;
	scope->state  = state;

	state->scope = scope;

	return scope;
}

static void
pop_scope(struct decode_scope *scope)
{
	assert(scope->state->scope == scope);  /* must be top of stack */

	scope->state->scope = scope->parent;
	ralloc_free(scope);
}

/**
 * Evaluate an expression, returning it's resulting value
 */
static uint64_t
evaluate_expr(struct decode_scope *scope, isa_expr_t expr)
{
	if (scope->cache) {
		struct hash_entry *entry = _mesa_hash_table_search(scope->cache, expr);
		if (entry) {
			return *(uint64_t *)entry->data;
		}
	} else {
		scope->cache = _mesa_pointer_hash_table_create(scope);
	}

	if (!push_expr(scope->state, expr))
		return 0;

	uint64_t ret = expr(scope);

	pop_expr(scope->state);

	uint64_t *retp = ralloc_size(scope->cache, sizeof(*retp));
	*retp = ret;
	_mesa_hash_table_insert(scope->cache, expr, retp);

	return ret;
}

/**
 * Find the bitset in NULL terminated bitset hiearchy root table which
 * matches against 'val'
 */
static const struct isa_bitset *
find_bitset(struct decode_state *state, const struct isa_bitset **bitsets,
		uint64_t val)
{
	const struct isa_bitset *match = NULL;
	for (int n = 0; bitsets[n]; n++) {
		if (state->options->gpu_id > bitsets[n]->gen.max)
			continue;
		if (state->options->gpu_id < bitsets[n]->gen.min)
			continue;

		uint64_t m = (val & bitsets[n]->mask) & ~bitsets[n]->dontcare;

		if (m != bitsets[n]->match) {
			continue;
		}

		/* We should only have exactly one match
		 *
		 * TODO more complete/formal way to validate that any given
		 * bit pattern will only have a single match?
		 */
		if (match) {
			decode_error(state, "bitset conflict: %s vs %s", match->name,
					bitsets[n]->name);
			return NULL;
		}

		match = bitsets[n];
	}

	if (match && (match->dontcare & val)) {
		decode_error(state, "dontcare bits in %s: %"PRIx64,
				match->name, (match->dontcare & val));
	}

	return match;
}

static const struct isa_field *
find_field(struct decode_scope *scope, const struct isa_bitset *bitset,
		const char *name)
{
	for (unsigned i = 0; i < bitset->num_cases; i++) {
		const struct isa_case *c = bitset->cases[i];

		if (c->expr) {
			struct decode_state *state = scope->state;

			/* When resolving a field for evaluating an expression,
			 * temporarily assume the expression evaluates to true.
			 * This allows <override/>'s to speculatively refer to
			 * fields defined within the override:
			 */
			isa_expr_t cur_expr = NULL;
			if (state->expr_sp > 0)
				cur_expr = state->expr_stack[state->expr_sp - 1];
			if ((cur_expr != c->expr) && !evaluate_expr(scope, c->expr))
				continue;
		}

		for (unsigned i = 0; i < c->num_fields; i++) {
			if (!strcmp(name, c->fields[i].name)) {
				return &c->fields[i];
			}
		}
	}

	if (bitset->parent) {
		const struct isa_field *f = find_field(scope, bitset->parent, name);
		if (f) {
			return f;
		}
	}

	return NULL;
}

static uint64_t
extract_field(struct decode_scope *scope, const struct isa_field *field)
{
	uint64_t val = scope->val;
	val = (val >> field->low) & ((1ul << (1 + field->high - field->low)) - 1);
	return val;
}

/**
 * Find the display template for a given bitset, recursively searching
 * parents in the bitset hierarchy.
 */
static const char *
find_display(struct decode_scope *scope, const struct isa_bitset *bitset)
{
	for (unsigned i = 0; i < bitset->num_cases; i++) {
		const struct isa_case *c = bitset->cases[i];
		if (c->expr && !evaluate_expr(scope, c->expr))
			continue;
		/* since this is the chosen case, it seems like a good place
		 * to check asserted bits:
		 */
		for (unsigned j = 0; j < c->num_fields; j++) {
			if (c->fields[j].type == TYPE_ASSERT) {
				const struct isa_field *f = &c->fields[j];
				uint64_t val = extract_field(scope, f);
				if (val != f->val) {
					decode_error(scope->state, "WARNING: unexpected "
							"bits[%u:%u] in %s: 0x%"PRIx64" vs 0x%"PRIx64,
							f->low, f->high, bitset->name,
							val, f->val);
				}
			}
		}
		if (!c->display)
			continue;
		return c->display;
	}

	/**
	 * If we didn't find something check up the bitset hierarchy.
	 */
	if (bitset->parent) {
		return find_display(scope, bitset->parent);
	}

	return NULL;
}

/**
 * Decode a field that is itself another bitset type
 */
static void
display_bitset_field(struct decode_scope *scope, const struct isa_field *field, uint64_t val)
{
	const struct isa_bitset *b = find_bitset(scope->state, field->bitsets, val);
	if (!b) {
		decode_error(scope->state, "no match: FIELD: '%s.%s': 0x%"PRIx64,
				scope->bitset->name, field->name, val);
		return;
	}

	struct decode_scope *nested_scope =
			push_scope(scope->state, b, val);
	nested_scope->params = field->params;
	display(nested_scope);
	pop_scope(nested_scope);
}

static void
display_enum_field(struct decode_scope *scope, const struct isa_field *field, uint64_t val)
{
	FILE *out = scope->state->out;

	const struct isa_enum *e = field->enums;
	for (unsigned i = 0; i < e->num_values; i++) {
		if (e->values[i].val == val) {
			fprintf(out, "%s", e->values[i].display);
			return;
		}
	}

	fprintf(out, "%u", (unsigned)val);
}

static const struct isa_field *
resolve_field(struct decode_scope *scope, const char *field_name, uint64_t *valp)
{
	if (!scope) {
		/* We've reached the bottom of the stack! */
		return NULL;
	}

	const struct isa_field *field =
			find_field(scope, scope->bitset, field_name);

	if (!field && scope->params) {
		for (unsigned i = 0; i < scope->params->num_params; i++) {
			if (!strcmp(field_name, scope->params->params[i].as)) {
				const char *param_name = scope->params->params[i].name;
				return resolve_field(scope->parent, param_name, valp);
			}
		}
	}

	if (!field) {
		return NULL;
	}

	/* extract out raw field value: */
	if (field->expr) {
		*valp = evaluate_expr(scope, field->expr);
	} else {
		*valp = extract_field(scope, field);
	}

	return field;
}

/* This is also used from generated expr functions */
uint64_t
isa_decode_field(struct decode_scope *scope, const char *field_name)
{
	uint64_t val;
	const struct isa_field *field = resolve_field(scope, field_name, &val);
	if (!field) {
		decode_error(scope->state, "no field '%s'", field_name);
		return 0;
	}

	return val;
}

static void
display_field(struct decode_scope *scope, const char *field_name)
{
	const struct isa_decode_options *options = scope->state->options;

	/* Special case 'NAME' maps to instruction/bitset name: */
	if (!strcmp("NAME", field_name)) {
		if (options->field_cb) {
			options->field_cb(options->cbdata, field_name, &(struct isa_decode_value){
				.str = scope->bitset->name,
			});
		}

		fprintf(scope->state->out, "%s", scope->bitset->name);

		return;
	}

	uint64_t val;
	const struct isa_field *field = resolve_field(scope, field_name, &val);
	if (!field) {
		decode_error(scope->state, "no field '%s'", field_name);
		return;
	}

	if (options->field_cb) {
		options->field_cb(options->cbdata, field_name, &(struct isa_decode_value){
			.num = val,
		});
	}

	unsigned width = 1 + field->high - field->low;
	FILE *out = scope->state->out;

	switch (field->type) {
	/* Basic types: */
	case TYPE_BRANCH:
		if (scope->state->options->branch_labels) {
			int offset = util_sign_extend(val, width) + scope->state->n;
			if (offset < scope->state->num_instr) {
				fprintf(out, "l%d", offset);
				BITSET_SET(scope->state->branch_targets, offset);
				break;
			}
		}
		FALLTHROUGH;
	case TYPE_INT:
		fprintf(out, "%"PRId64, util_sign_extend(val, width));
		break;
	case TYPE_UINT:
		fprintf(out, "%"PRIu64, val);
		break;
	case TYPE_HEX:
		// TODO format # of digits based on field width?
		fprintf(out, "%"PRIx64, val);
		break;
	case TYPE_OFFSET:
		if (val != 0) {
			fprintf(out, "%+"PRId64, util_sign_extend(val, width));
		}
		break;
	case TYPE_UOFFSET:
		if (val != 0) {
			fprintf(out, "+%"PRIu64, val);
		}
		break;
	case TYPE_FLOAT:
		if (width == 16) {
			fprintf(out, "%f", _mesa_half_to_float(val));
		} else {
			assert(width == 32);
			fprintf(out, "%f", uif(val));
		}
		break;
	case TYPE_BOOL:
		if (field->display) {
			if (val) {
				fprintf(out, "%s", field->display);
			}
		} else {
			fprintf(out, "%u", (unsigned)val);
		}
		break;
	case TYPE_ENUM:
		display_enum_field(scope, field, val);
		break;

	case TYPE_ASSERT:
		/* assert fields are not for display */
		assert(0);
		break;

	/* For fields that are decoded with another bitset hierarchy: */
	case TYPE_BITSET:
		display_bitset_field(scope, field, val);
		break;
	default:
		decode_error(scope->state, "Bad field type: %d (%s)",
				field->type, field->name);
	}
}

static void
display(struct decode_scope *scope)
{
	const struct isa_bitset *bitset = scope->bitset;
	const char *display = find_display(scope, bitset);

	if (!display) {
		decode_error(scope->state, "%s: no display template", bitset->name);
		return;
	}

	const char *p = display;

	while (*p != '\0') {
		if (*p == '{') {
			const char *e = ++p;
			while (*e != '}') {
				e++;
			}

			char *field_name = strndup(p, e-p);
			display_field(scope, field_name);
			free(field_name);

			p = e;
		} else {
			fputc(*p, scope->state->out);
		}
		p++;
	}
}

static void
decode(struct decode_state *state, void *bin, int sz)
{
	uint64_t *instrs = bin;
	unsigned errors = 0;   /* number of consecutive unmatched instructions */

	for (state->n = 0; state->n < state->num_instr; state->n++) {
		uint64_t instr = instrs[state->n];

		if (state->options->max_errors && (errors > state->options->max_errors)) {
			break;
		}

		if (state->options->branch_labels &&
				BITSET_TEST(state->branch_targets, state->n)) {
			if (state->options->instr_cb) {
				state->options->instr_cb(state->options->cbdata,
						state->n, instr);
			}
			fprintf(state->out, "l%d:\n", state->n);
		}

		if (state->options->instr_cb) {
			state->options->instr_cb(state->options->cbdata, state->n, instr);
		}

		const struct isa_bitset *b = find_bitset(state, __instruction, instr);
		if (!b) {
			fprintf(state->out, "no match: %016"PRIx64"\n", instr);
			errors++;
			continue;
		}

		struct decode_scope *scope = push_scope(state, b, instr);

		display(scope);
		if (flush_errors(state)) {
			errors++;
		} else {
			errors = 0;
		}
		fprintf(state->out, "\n");

		pop_scope(scope);

		if (state->options->stop) {
			break;
		}
	}
}

void
isa_decode(void *bin, int sz, FILE *out, const struct isa_decode_options *options)
{
	const struct isa_decode_options default_options = {
		.branch_labels = options ? options->branch_labels : false
	};
	struct decode_state *state;

	if (!options)
		options = &default_options;

	util_cpu_detect();  /* needed for _mesa_half_to_float() */

	state = rzalloc_size(NULL, sizeof(*state));
	state->options = options;
	state->num_instr = sz / 8;

	if (state->options->branch_labels) {
		state->branch_targets = rzalloc_size(state,
				sizeof(BITSET_WORD) * BITSET_WORDS(state->num_instr));

		/* Do a pre-pass to find all the branch targets: */
		state->out = fopen("/dev/null", "w");
		state->options = &default_options;   /* skip hooks for prepass */
		decode(state, bin, sz);
		fclose(state->out);
		if (options) {
			state->options = options;
		}
	}

	state->out = out;

	decode(state, bin, sz);

	ralloc_free(state);
}
