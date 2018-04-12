/*
 * Copyright 2015 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "ac_debug.h"

#ifdef HAVE_VALGRIND
#include <valgrind.h>
#include <memcheck.h>
#define VG(x) x
#else
#define VG(x)
#endif

#include <inttypes.h>

#include "sid.h"
#include "gfx9d.h"
#include "sid_tables.h"
#include "util/u_math.h"
#include "util/u_memory.h"
#include "util/u_string.h"

#include <assert.h>

/* Parsed IBs are difficult to read without colors. Use "less -R file" to
 * read them, or use "aha -b -f file" to convert them to html.
 */
#define COLOR_RESET	"\033[0m"
#define COLOR_RED	"\033[31m"
#define COLOR_GREEN	"\033[1;32m"
#define COLOR_YELLOW	"\033[1;33m"
#define COLOR_CYAN	"\033[1;36m"

#define INDENT_PKT 8

struct ac_ib_parser {
	FILE *f;
	uint32_t *ib;
	unsigned num_dw;
	const int *trace_ids;
	unsigned trace_id_count;
	enum chip_class chip_class;
	ac_debug_addr_callback addr_callback;
	void *addr_callback_data;

	unsigned cur_dw;
};

static void ac_do_parse_ib(FILE *f, struct ac_ib_parser *ib);

static void print_spaces(FILE *f, unsigned num)
{
	fprintf(f, "%*s", num, "");
}

static void print_value(FILE *file, uint32_t value, int bits)
{
	/* Guess if it's int or float */
	if (value <= (1 << 15)) {
		if (value <= 9)
			fprintf(file, "%u\n", value);
		else
			fprintf(file, "%u (0x%0*x)\n", value, bits / 4, value);
	} else {
		float f = uif(value);

		if (fabs(f) < 100000 && f*10 == floor(f*10))
			fprintf(file, "%.1ff (0x%0*x)\n", f, bits / 4, value);
		else
			/* Don't print more leading zeros than there are bits. */
			fprintf(file, "0x%0*x\n", bits / 4, value);
	}
}

static void print_named_value(FILE *file, const char *name, uint32_t value,
			      int bits)
{
	print_spaces(file, INDENT_PKT);
	fprintf(file, COLOR_YELLOW "%s" COLOR_RESET " <- ", name);
	print_value(file, value, bits);
}

static const struct si_reg *find_register(const struct si_reg *table,
					  unsigned table_size,
					  unsigned offset)
{
	for (unsigned i = 0; i < table_size; i++) {
		const struct si_reg *reg = &table[i];

		if (reg->offset == offset)
			return reg;
	}

	return NULL;
}

void ac_dump_reg(FILE *file, enum chip_class chip_class, unsigned offset,
		 uint32_t value, uint32_t field_mask)
{
	const struct si_reg *reg = NULL;

	if (chip_class >= GFX9)
		reg = find_register(gfx9d_reg_table, ARRAY_SIZE(gfx9d_reg_table), offset);
	if (!reg)
		reg = find_register(sid_reg_table, ARRAY_SIZE(sid_reg_table), offset);

	if (reg) {
		const char *reg_name = sid_strings + reg->name_offset;
		bool first_field = true;

		print_spaces(file, INDENT_PKT);
		fprintf(file, COLOR_YELLOW "%s" COLOR_RESET " <- ",
			reg_name);

		if (!reg->num_fields) {
			print_value(file, value, 32);
			return;
		}

		for (unsigned f = 0; f < reg->num_fields; f++) {
			const struct si_field *field = sid_fields_table + reg->fields_offset + f;
			const int *values_offsets = sid_strings_offsets + field->values_offset;
			uint32_t val = (value & field->mask) >>
				       (ffs(field->mask) - 1);

			if (!(field->mask & field_mask))
				continue;

			/* Indent the field. */
			if (!first_field)
				print_spaces(file,
					     INDENT_PKT + strlen(reg_name) + 4);

			/* Print the field. */
			fprintf(file, "%s = ", sid_strings + field->name_offset);

			if (val < field->num_values && values_offsets[val] >= 0)
				fprintf(file, "%s\n", sid_strings + values_offsets[val]);
			else
				print_value(file, val,
					    util_bitcount(field->mask));

			first_field = false;
		}
		return;
	}

	print_spaces(file, INDENT_PKT);
	fprintf(file, COLOR_YELLOW "0x%05x" COLOR_RESET " <- 0x%08x\n", offset, value);
}

static uint32_t ac_ib_get(struct ac_ib_parser *ib)
{
	uint32_t v = 0;

	if (ib->cur_dw < ib->num_dw) {
		v = ib->ib[ib->cur_dw];
#ifdef HAVE_VALGRIND
		/* Help figure out where garbage data is written to IBs.
		 *
		 * Arguably we should do this already when the IBs are written,
		 * see RADEON_VALGRIND. The problem is that client-requests to
		 * Valgrind have an overhead even when Valgrind isn't running,
		 * and radeon_emit is performance sensitive...
		 */
		if (VALGRIND_CHECK_VALUE_IS_DEFINED(v))
			fprintf(ib->f, COLOR_RED "Valgrind: The next DWORD is garbage"
				COLOR_RESET "\n");
#endif
		fprintf(ib->f, "\n\035#%08x ", v);
	} else {
		fprintf(ib->f, "\n\035#???????? ");
	}

	ib->cur_dw++;
	return v;
}

static void ac_parse_set_reg_packet(FILE *f, unsigned count, unsigned reg_offset,
				    struct ac_ib_parser *ib)
{
	unsigned reg_dw = ac_ib_get(ib);
	unsigned reg = ((reg_dw & 0xFFFF) << 2) + reg_offset;
	unsigned index = reg_dw >> 28;
	int i;

	if (index != 0) {
		print_spaces(f, INDENT_PKT);
		fprintf(f, "INDEX = %u\n", index);
	}

	for (i = 0; i < count; i++)
		ac_dump_reg(f, ib->chip_class, reg + i*4, ac_ib_get(ib), ~0);
}

static void ac_parse_packet3(FILE *f, uint32_t header, struct ac_ib_parser *ib,
                             int *current_trace_id)
{
	unsigned first_dw = ib->cur_dw;
	int count = PKT_COUNT_G(header);
	unsigned op = PKT3_IT_OPCODE_G(header);
	const char *predicate = PKT3_PREDICATE(header) ? "(predicate)" : "";
	int i;

	/* Print the name first. */
	for (i = 0; i < ARRAY_SIZE(packet3_table); i++)
		if (packet3_table[i].op == op)
			break;

	if (i < ARRAY_SIZE(packet3_table)) {
		const char *name = sid_strings + packet3_table[i].name_offset;

		if (op == PKT3_SET_CONTEXT_REG ||
		    op == PKT3_SET_CONFIG_REG ||
		    op == PKT3_SET_UCONFIG_REG ||
		    op == PKT3_SET_SH_REG)
			fprintf(f, COLOR_CYAN "%s%s" COLOR_CYAN ":\n",
				name, predicate);
		else
			fprintf(f, COLOR_GREEN "%s%s" COLOR_RESET ":\n",
				name, predicate);
	} else
		fprintf(f, COLOR_RED "PKT3_UNKNOWN 0x%x%s" COLOR_RESET ":\n",
			op, predicate);

	/* Print the contents. */
	switch (op) {
	case PKT3_SET_CONTEXT_REG:
		ac_parse_set_reg_packet(f, count, SI_CONTEXT_REG_OFFSET, ib);
		break;
	case PKT3_SET_CONFIG_REG:
		ac_parse_set_reg_packet(f, count, SI_CONFIG_REG_OFFSET, ib);
		break;
	case PKT3_SET_UCONFIG_REG:
		ac_parse_set_reg_packet(f, count, CIK_UCONFIG_REG_OFFSET, ib);
		break;
	case PKT3_SET_SH_REG:
		ac_parse_set_reg_packet(f, count, SI_SH_REG_OFFSET, ib);
		break;
	case PKT3_ACQUIRE_MEM:
		ac_dump_reg(f, ib->chip_class, R_0301F0_CP_COHER_CNTL, ac_ib_get(ib), ~0);
		ac_dump_reg(f, ib->chip_class, R_0301F4_CP_COHER_SIZE, ac_ib_get(ib), ~0);
		ac_dump_reg(f, ib->chip_class, R_030230_CP_COHER_SIZE_HI, ac_ib_get(ib), ~0);
		ac_dump_reg(f, ib->chip_class, R_0301F8_CP_COHER_BASE, ac_ib_get(ib), ~0);
		ac_dump_reg(f, ib->chip_class, R_0301E4_CP_COHER_BASE_HI, ac_ib_get(ib), ~0);
		print_named_value(f, "POLL_INTERVAL", ac_ib_get(ib), 16);
		break;
	case PKT3_SURFACE_SYNC:
		if (ib->chip_class >= CIK) {
			ac_dump_reg(f, ib->chip_class, R_0301F0_CP_COHER_CNTL, ac_ib_get(ib), ~0);
			ac_dump_reg(f, ib->chip_class, R_0301F4_CP_COHER_SIZE, ac_ib_get(ib), ~0);
			ac_dump_reg(f, ib->chip_class, R_0301F8_CP_COHER_BASE, ac_ib_get(ib), ~0);
		} else {
			ac_dump_reg(f, ib->chip_class, R_0085F0_CP_COHER_CNTL, ac_ib_get(ib), ~0);
			ac_dump_reg(f, ib->chip_class, R_0085F4_CP_COHER_SIZE, ac_ib_get(ib), ~0);
			ac_dump_reg(f, ib->chip_class, R_0085F8_CP_COHER_BASE, ac_ib_get(ib), ~0);
		}
		print_named_value(f, "POLL_INTERVAL", ac_ib_get(ib), 16);
		break;
	case PKT3_EVENT_WRITE: {
		uint32_t event_dw = ac_ib_get(ib);
		ac_dump_reg(f, ib->chip_class, R_028A90_VGT_EVENT_INITIATOR, event_dw,
			    S_028A90_EVENT_TYPE(~0));
		print_named_value(f, "EVENT_INDEX", (event_dw >> 8) & 0xf, 4);
		print_named_value(f, "INV_L2", (event_dw >> 20) & 0x1, 1);
		if (count > 0) {
			print_named_value(f, "ADDRESS_LO", ac_ib_get(ib), 32);
			print_named_value(f, "ADDRESS_HI", ac_ib_get(ib), 16);
		}
		break;
	}
	case PKT3_EVENT_WRITE_EOP: {
		uint32_t event_dw = ac_ib_get(ib);
		ac_dump_reg(f, ib->chip_class, R_028A90_VGT_EVENT_INITIATOR, event_dw,
			    S_028A90_EVENT_TYPE(~0));
		print_named_value(f, "EVENT_INDEX", (event_dw >> 8) & 0xf, 4);
		print_named_value(f, "TCL1_VOL_ACTION_ENA", (event_dw >> 12) & 0x1, 1);
		print_named_value(f, "TC_VOL_ACTION_ENA", (event_dw >> 13) & 0x1, 1);
		print_named_value(f, "TC_WB_ACTION_ENA", (event_dw >> 15) & 0x1, 1);
		print_named_value(f, "TCL1_ACTION_ENA", (event_dw >> 16) & 0x1, 1);
		print_named_value(f, "TC_ACTION_ENA", (event_dw >> 17) & 0x1, 1);
		print_named_value(f, "ADDRESS_LO", ac_ib_get(ib), 32);
		uint32_t addr_hi_dw = ac_ib_get(ib);
		print_named_value(f, "ADDRESS_HI", addr_hi_dw, 16);
		print_named_value(f, "DST_SEL", (addr_hi_dw >> 16) & 0x3, 2);
		print_named_value(f, "INT_SEL", (addr_hi_dw >> 24) & 0x7, 3);
		print_named_value(f, "DATA_SEL", addr_hi_dw >> 29, 3);
		print_named_value(f, "DATA_LO", ac_ib_get(ib), 32);
		print_named_value(f, "DATA_HI", ac_ib_get(ib), 32);
		break;
	}
	case PKT3_RELEASE_MEM: {
		uint32_t event_dw = ac_ib_get(ib);
		ac_dump_reg(f, ib->chip_class, R_028A90_VGT_EVENT_INITIATOR, event_dw,
			    S_028A90_EVENT_TYPE(~0));
		print_named_value(f, "EVENT_INDEX", (event_dw >> 8) & 0xf, 4);
		print_named_value(f, "TCL1_VOL_ACTION_ENA", (event_dw >> 12) & 0x1, 1);
		print_named_value(f, "TC_VOL_ACTION_ENA", (event_dw >> 13) & 0x1, 1);
		print_named_value(f, "TC_WB_ACTION_ENA", (event_dw >> 15) & 0x1, 1);
		print_named_value(f, "TCL1_ACTION_ENA", (event_dw >> 16) & 0x1, 1);
		print_named_value(f, "TC_ACTION_ENA", (event_dw >> 17) & 0x1, 1);
		print_named_value(f, "TC_NC_ACTION_ENA", (event_dw >> 19) & 0x1, 1);
		print_named_value(f, "TC_WC_ACTION_ENA", (event_dw >> 20) & 0x1, 1);
		print_named_value(f, "TC_MD_ACTION_ENA", (event_dw >> 21) & 0x1, 1);
		uint32_t sel_dw = ac_ib_get(ib);
		print_named_value(f, "DST_SEL", (sel_dw >> 16) & 0x3, 2);
		print_named_value(f, "INT_SEL", (sel_dw >> 24) & 0x7, 3);
		print_named_value(f, "DATA_SEL", sel_dw >> 29, 3);
		print_named_value(f, "ADDRESS_LO", ac_ib_get(ib), 32);
		print_named_value(f, "ADDRESS_HI", ac_ib_get(ib), 32);
		print_named_value(f, "DATA_LO", ac_ib_get(ib), 32);
		print_named_value(f, "DATA_HI", ac_ib_get(ib), 32);
		print_named_value(f, "CTXID", ac_ib_get(ib), 32);
		break;
	}
	case PKT3_WAIT_REG_MEM:
		print_named_value(f, "OP", ac_ib_get(ib), 32);
		print_named_value(f, "ADDRESS_LO", ac_ib_get(ib), 32);
		print_named_value(f, "ADDRESS_HI", ac_ib_get(ib), 32);
		print_named_value(f, "REF", ac_ib_get(ib), 32);
		print_named_value(f, "MASK", ac_ib_get(ib), 32);
		print_named_value(f, "POLL_INTERVAL", ac_ib_get(ib), 16);
		break;
	case PKT3_DRAW_INDEX_AUTO:
		ac_dump_reg(f, ib->chip_class, R_030930_VGT_NUM_INDICES, ac_ib_get(ib), ~0);
		ac_dump_reg(f, ib->chip_class, R_0287F0_VGT_DRAW_INITIATOR, ac_ib_get(ib), ~0);
		break;
	case PKT3_DRAW_INDEX_2:
		ac_dump_reg(f, ib->chip_class, R_028A78_VGT_DMA_MAX_SIZE, ac_ib_get(ib), ~0);
		ac_dump_reg(f, ib->chip_class, R_0287E8_VGT_DMA_BASE, ac_ib_get(ib), ~0);
		ac_dump_reg(f, ib->chip_class, R_0287E4_VGT_DMA_BASE_HI, ac_ib_get(ib), ~0);
		ac_dump_reg(f, ib->chip_class, R_030930_VGT_NUM_INDICES, ac_ib_get(ib), ~0);
		ac_dump_reg(f, ib->chip_class, R_0287F0_VGT_DRAW_INITIATOR, ac_ib_get(ib), ~0);
		break;
	case PKT3_INDEX_TYPE:
		ac_dump_reg(f, ib->chip_class, R_028A7C_VGT_DMA_INDEX_TYPE, ac_ib_get(ib), ~0);
		break;
	case PKT3_NUM_INSTANCES:
		ac_dump_reg(f, ib->chip_class, R_030934_VGT_NUM_INSTANCES, ac_ib_get(ib), ~0);
		break;
	case PKT3_WRITE_DATA:
		ac_dump_reg(f, ib->chip_class, R_370_CONTROL, ac_ib_get(ib), ~0);
		ac_dump_reg(f, ib->chip_class, R_371_DST_ADDR_LO, ac_ib_get(ib), ~0);
		ac_dump_reg(f, ib->chip_class, R_372_DST_ADDR_HI, ac_ib_get(ib), ~0);
		/* The payload is written automatically */
		break;
	case PKT3_CP_DMA:
		ac_dump_reg(f, ib->chip_class, R_410_CP_DMA_WORD0, ac_ib_get(ib), ~0);
		ac_dump_reg(f, ib->chip_class, R_411_CP_DMA_WORD1, ac_ib_get(ib), ~0);
		ac_dump_reg(f, ib->chip_class, R_412_CP_DMA_WORD2, ac_ib_get(ib), ~0);
		ac_dump_reg(f, ib->chip_class, R_413_CP_DMA_WORD3, ac_ib_get(ib), ~0);
		ac_dump_reg(f, ib->chip_class, R_414_COMMAND, ac_ib_get(ib), ~0);
		break;
	case PKT3_DMA_DATA:
		ac_dump_reg(f, ib->chip_class, R_500_DMA_DATA_WORD0, ac_ib_get(ib), ~0);
		ac_dump_reg(f, ib->chip_class, R_501_SRC_ADDR_LO, ac_ib_get(ib), ~0);
		ac_dump_reg(f, ib->chip_class, R_502_SRC_ADDR_HI, ac_ib_get(ib), ~0);
		ac_dump_reg(f, ib->chip_class, R_503_DST_ADDR_LO, ac_ib_get(ib), ~0);
		ac_dump_reg(f, ib->chip_class, R_504_DST_ADDR_HI, ac_ib_get(ib), ~0);
		ac_dump_reg(f, ib->chip_class, R_414_COMMAND, ac_ib_get(ib), ~0);
		break;
	case PKT3_INDIRECT_BUFFER_SI:
	case PKT3_INDIRECT_BUFFER_CONST:
	case PKT3_INDIRECT_BUFFER_CIK: {
		uint32_t base_lo_dw = ac_ib_get(ib);
		ac_dump_reg(f, ib->chip_class, R_3F0_IB_BASE_LO, base_lo_dw, ~0);
		uint32_t base_hi_dw = ac_ib_get(ib);
		ac_dump_reg(f, ib->chip_class, R_3F1_IB_BASE_HI, base_hi_dw, ~0);
		uint32_t control_dw = ac_ib_get(ib);
		ac_dump_reg(f, ib->chip_class, R_3F2_CONTROL, control_dw, ~0);

		if (!ib->addr_callback)
			break;

		uint64_t addr = ((uint64_t)base_hi_dw << 32) | base_lo_dw;
		void *data = ib->addr_callback(ib->addr_callback_data, addr);
		if (!data)
			break;

		if (G_3F2_CHAIN(control_dw)) {
			ib->ib = data;
			ib->num_dw = G_3F2_IB_SIZE(control_dw);
			ib->cur_dw = 0;
			return;
		}

		struct ac_ib_parser ib_recurse;
		memcpy(&ib_recurse, ib, sizeof(ib_recurse));
		ib_recurse.ib = data;
		ib_recurse.num_dw = G_3F2_IB_SIZE(control_dw);
		ib_recurse.cur_dw = 0;
		if(ib_recurse.trace_id_count) {
			if (*current_trace_id == *ib->trace_ids) {
				++ib_recurse.trace_ids;
				--ib_recurse.trace_id_count;
			} else {
				ib_recurse.trace_id_count = 0;
			}
		}

		fprintf(f, "\n\035>------------------ nested begin ------------------\n");
		ac_do_parse_ib(f, &ib_recurse);
		fprintf(f, "\n\035<------------------- nested end -------------------\n");
		break;
	}
	case PKT3_CLEAR_STATE:
	case PKT3_INCREMENT_DE_COUNTER:
	case PKT3_PFP_SYNC_ME:
		break;
	case PKT3_NOP:
		if (header == 0xffff1000) {
			count = -1; /* One dword NOP. */
		} else if (count == 0 && ib->cur_dw < ib->num_dw &&
			   AC_IS_TRACE_POINT(ib->ib[ib->cur_dw])) {
			unsigned packet_id = AC_GET_TRACE_POINT_ID(ib->ib[ib->cur_dw]);

			print_spaces(f, INDENT_PKT);
			fprintf(f, COLOR_RED "Trace point ID: %u\n", packet_id);

			if (!ib->trace_id_count)
				break; /* tracing was disabled */

			*current_trace_id = packet_id;

			print_spaces(f, INDENT_PKT);
			if (packet_id < *ib->trace_ids)
				fprintf(f, COLOR_RED
					"This trace point was reached by the CP."
					COLOR_RESET "\n");
			else if (packet_id == *ib->trace_ids)
				fprintf(f, COLOR_RED
					"!!!!! This is the last trace point that "
					"was reached by the CP !!!!!"
					COLOR_RESET "\n");
			else if (packet_id+1 == *ib->trace_ids)
				fprintf(f, COLOR_RED
					"!!!!! This is the first trace point that "
					"was NOT been reached by the CP !!!!!"
					COLOR_RESET "\n");
			else
				fprintf(f, COLOR_RED
					"!!!!! This trace point was NOT reached "
					"by the CP !!!!!"
					COLOR_RESET "\n");
			break;
		}
		break;
	}

	/* print additional dwords */
	while (ib->cur_dw <= first_dw + count)
		ac_ib_get(ib);

	if (ib->cur_dw > first_dw + count + 1)
		fprintf(f, COLOR_RED "\n!!!!! count in header too low !!!!!"
			COLOR_RESET "\n");
}

/**
 * Parse and print an IB into a file.
 */
static void ac_do_parse_ib(FILE *f, struct ac_ib_parser *ib)
{
	int current_trace_id = -1;

	while (ib->cur_dw < ib->num_dw) {
		uint32_t header = ac_ib_get(ib);
		unsigned type = PKT_TYPE_G(header);

		switch (type) {
		case 3:
			ac_parse_packet3(f, header, ib, &current_trace_id);
			break;
		case 2:
			/* type-2 nop */
			if (header == 0x80000000) {
				fprintf(f, COLOR_GREEN "NOP (type 2)" COLOR_RESET "\n");
				break;
			}
			/* fall through */
		default:
			fprintf(f, "Unknown packet type %i\n", type);
			break;
		}
	}
}

static void format_ib_output(FILE *f, char *out)
{
	unsigned depth = 0;

	for (;;) {
		char op = 0;

		if (out[0] == '\n' && out[1] == '\035')
			out++;
		if (out[0] == '\035') {
			op = out[1];
			out += 2;
		}

		if (op == '<')
			depth--;

		unsigned indent = 4 * depth;
		if (op != '#')
			indent += 9;

		if (indent)
			print_spaces(f, indent);

		char *end = util_strchrnul(out, '\n');
		fwrite(out, end - out, 1, f);
		fputc('\n', f); /* always end with a new line */
		if (!*end)
			break;

		out = end + 1;

		if (op == '>')
			depth++;
	}
}

/**
 * Parse and print an IB into a file.
 *
 * \param f            file
 * \param ib_ptr       IB
 * \param num_dw       size of the IB
 * \param chip_class   chip class
 * \param trace_ids	the last trace IDs that are known to have been reached
 *			and executed by the CP, typically read from a buffer
 * \param trace_id_count The number of entries in the trace_ids array.
 * \param addr_callback Get a mapped pointer of the IB at a given address. Can
 *                      be NULL.
 * \param addr_callback_data user data for addr_callback
 */
void ac_parse_ib_chunk(FILE *f, uint32_t *ib_ptr, int num_dw, const int *trace_ids,
		       unsigned trace_id_count, enum chip_class chip_class,
                       ac_debug_addr_callback addr_callback, void *addr_callback_data)
{
	struct ac_ib_parser ib = {};
	ib.ib = ib_ptr;
	ib.num_dw = num_dw;
	ib.trace_ids = trace_ids;
	ib.trace_id_count = trace_id_count;
	ib.chip_class = chip_class;
	ib.addr_callback = addr_callback;
	ib.addr_callback_data = addr_callback_data;

	char *out;
	size_t outsize;
	FILE *memf = open_memstream(&out, &outsize);
	ib.f = memf;
	ac_do_parse_ib(memf, &ib);
	fclose(memf);

	if (out) {
		format_ib_output(f, out);
		free(out);
	}

	if (ib.cur_dw > ib.num_dw) {
		printf("\nPacket ends after the end of IB.\n");
		exit(1);
	}
}

/**
 * Parse and print an IB into a file.
 *
 * \param f		file
 * \param ib		IB
 * \param num_dw	size of the IB
 * \param chip_class	chip class
 * \param trace_ids	the last trace IDs that are known to have been reached
 *			and executed by the CP, typically read from a buffer
 * \param trace_id_count The number of entries in the trace_ids array.
 * \param addr_callback Get a mapped pointer of the IB at a given address. Can
 *                      be NULL.
 * \param addr_callback_data user data for addr_callback
 */
void ac_parse_ib(FILE *f, uint32_t *ib, int num_dw, const int *trace_ids,
		 unsigned trace_id_count, const char *name,
		 enum chip_class chip_class, ac_debug_addr_callback addr_callback,
		 void *addr_callback_data)
{
	fprintf(f, "------------------ %s begin ------------------\n", name);

	ac_parse_ib_chunk(f, ib, num_dw, trace_ids, trace_id_count,
			  chip_class, addr_callback,  addr_callback_data);

	fprintf(f, "------------------- %s end -------------------\n\n", name);
}

/**
 * Parse dmesg and return TRUE if a VM fault has been detected.
 *
 * \param chip_class		chip class
 * \param old_dmesg_timestamp	previous dmesg timestamp parsed at init time
 * \param out_addr		detected VM fault addr
 */
bool ac_vm_fault_occured(enum chip_class chip_class,
			 uint64_t *old_dmesg_timestamp, uint64_t *out_addr)
{
	char line[2000];
	unsigned sec, usec;
	int progress = 0;
	uint64_t dmesg_timestamp = 0;
	bool fault = false;

	FILE *p = popen("dmesg", "r");
	if (!p)
		return false;

	while (fgets(line, sizeof(line), p)) {
		char *msg, len;

		if (!line[0] || line[0] == '\n')
			continue;

		/* Get the timestamp. */
		if (sscanf(line, "[%u.%u]", &sec, &usec) != 2) {
			static bool hit = false;
			if (!hit) {
				fprintf(stderr, "%s: failed to parse line '%s'\n",
					__func__, line);
				hit = true;
			}
			continue;
		}
		dmesg_timestamp = sec * 1000000ull + usec;

		/* If just updating the timestamp. */
		if (!out_addr)
			continue;

		/* Process messages only if the timestamp is newer. */
		if (dmesg_timestamp <= *old_dmesg_timestamp)
			continue;

		/* Only process the first VM fault. */
		if (fault)
			continue;

		/* Remove trailing \n */
		len = strlen(line);
		if (len && line[len-1] == '\n')
			line[len-1] = 0;

		/* Get the message part. */
		msg = strchr(line, ']');
		if (!msg)
			continue;
		msg++;

		const char *header_line, *addr_line_prefix, *addr_line_format;

		if (chip_class >= GFX9) {
			/* Match this:
			 * ..: [gfxhub] VMC page fault (src_id:0 ring:158 vm_id:2 pas_id:0)
			 * ..:   at page 0x0000000219f8f000 from 27
			 * ..: VM_L2_PROTECTION_FAULT_STATUS:0x0020113C
			 */
			header_line = "VMC page fault";
			addr_line_prefix = "   at page";
			addr_line_format = "%"PRIx64;
		} else {
			header_line = "GPU fault detected:";
			addr_line_prefix = "VM_CONTEXT1_PROTECTION_FAULT_ADDR";
			addr_line_format = "%"PRIX64;
		}

		switch (progress) {
		case 0:
			if (strstr(msg, header_line))
				progress = 1;
			break;
		case 1:
			msg = strstr(msg, addr_line_prefix);
			if (msg) {
				msg = strstr(msg, "0x");
				if (msg) {
					msg += 2;
					if (sscanf(msg, addr_line_format, out_addr) == 1)
						fault = true;
				}
			}
			progress = 0;
			break;
		default:
			progress = 0;
		}
	}
	pclose(p);

	if (dmesg_timestamp > *old_dmesg_timestamp)
		*old_dmesg_timestamp = dmesg_timestamp;

	return fault;
}

static int compare_wave(const void *p1, const void *p2)
{
	struct ac_wave_info *w1 = (struct ac_wave_info *)p1;
	struct ac_wave_info *w2 = (struct ac_wave_info *)p2;

	/* Sort waves according to PC and then SE, SH, CU, etc. */
	if (w1->pc < w2->pc)
		return -1;
	if (w1->pc > w2->pc)
		return 1;
	if (w1->se < w2->se)
		return -1;
	if (w1->se > w2->se)
		return 1;
	if (w1->sh < w2->sh)
		return -1;
	if (w1->sh > w2->sh)
		return 1;
	if (w1->cu < w2->cu)
		return -1;
	if (w1->cu > w2->cu)
		return 1;
	if (w1->simd < w2->simd)
		return -1;
	if (w1->simd > w2->simd)
		return 1;
	if (w1->wave < w2->wave)
		return -1;
	if (w1->wave > w2->wave)
		return 1;

	return 0;
}

/* Return wave information. "waves" should be a large enough array. */
unsigned ac_get_wave_info(struct ac_wave_info waves[AC_MAX_WAVES_PER_CHIP])
{
	char line[2000];
	unsigned num_waves = 0;

	FILE *p = popen("umr -O halt_waves -wa", "r");
	if (!p)
		return 0;

	if (!fgets(line, sizeof(line), p) ||
	    strncmp(line, "SE", 2) != 0) {
		pclose(p);
		return 0;
	}

	while (fgets(line, sizeof(line), p)) {
		struct ac_wave_info *w;
		uint32_t pc_hi, pc_lo, exec_hi, exec_lo;

		assert(num_waves < AC_MAX_WAVES_PER_CHIP);
		w = &waves[num_waves];

		if (sscanf(line, "%u %u %u %u %u %x %x %x %x %x %x %x",
			   &w->se, &w->sh, &w->cu, &w->simd, &w->wave,
			   &w->status, &pc_hi, &pc_lo, &w->inst_dw0,
			   &w->inst_dw1, &exec_hi, &exec_lo) == 12) {
			w->pc = ((uint64_t)pc_hi << 32) | pc_lo;
			w->exec = ((uint64_t)exec_hi << 32) | exec_lo;
			w->matched = false;
			num_waves++;
		}
	}

	qsort(waves, num_waves, sizeof(struct ac_wave_info), compare_wave);

	pclose(p);
	return num_waves;
}
