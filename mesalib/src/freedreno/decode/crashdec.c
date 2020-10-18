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

/*
 * Decoder for devcoredump traces from drm/msm.  In case of a gpu crash/hang,
 * the coredump should be found in:
 *
 *    /sys/class/devcoredump/devcd<n>/data
 *
 * The crashdump will hang around for 5min, it can be cleared by writing to
 * the file, ie:
 *
 *    echo 1 > /sys/class/devcoredump/devcd<n>/data
 *
 * (the driver won't log any new crashdumps until the previous one is cleared
 * or times out after 5min)
 */


#include <assert.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "buffers.h"
#include "cffdec.h"
#include "disasm.h"
#include "pager.h"
#include "rnnutil.h"
#include "util.h"
#include "ir3/instr-a3xx.h"


static FILE *in;
static bool verbose;

static struct rnn *rnn_gmu;
static struct rnn *rnn_control;
static struct rnn *rnn_pipe;

static struct cffdec_options options = {
	.draw_filter = -1,
};

static inline bool is_a6xx(void) { return (600 <= options.gpu_id) && (options.gpu_id < 700); }
static inline bool is_a5xx(void) { return (500 <= options.gpu_id) && (options.gpu_id < 600); }
static inline bool is_64b(void)  { return options.gpu_id >= 500; }

/*
 * Helpers to read register values:
 */

/* read registers that are 64b on 64b GPUs (ie. a5xx+) */
static uint64_t
regval64(const char *name)
{
	unsigned reg = regbase(name);
	assert(reg);
	uint64_t val = reg_val(reg);
	if (is_64b())
		val |= ((uint64_t)reg_val(reg + 1)) << 32;
	return val;
}

static uint32_t
regval(const char *name)
{
	unsigned reg = regbase(name);
	assert(reg);
	return reg_val(reg);
}

/*
 * Line reading and string helpers:
 */

static char *
replacestr(char *line, const char *find, const char *replace)
{
	char *tail, *s;

	if (!(s = strstr(line, find)))
		return line;

	tail = s + strlen(find);

	char *newline;
	asprintf(&newline, "%.*s%s%s", (int)(s - line), line, replace, tail);
	free(line);

	return newline;
}

static char *lastline;
static char *pushedline;

static const char *
popline(void)
{
	char *r = pushedline;

	if (r) {
		pushedline = NULL;
		return r;
	}

	free(lastline);

	size_t n = 0;
	if (getline(&r, &n, in) < 0)
		exit(0);

	/* Handle section name typo's from earlier kernels: */
	r = replacestr(r, "CP_MEMPOOOL", "CP_MEMPOOL");
	r = replacestr(r, "CP_SEQ_STAT", "CP_SQE_STAT");

	lastline = r;
	return r;
}

static void
pushline(void)
{
	assert(!pushedline);
	pushedline = lastline;
}

static uint32_t *
popline_ascii85(uint32_t sizedwords)
{
	const char *line = popline();

	/* At this point we exepct the ascii85 data to be indented *some*
	 * amount, and to terminate at the end of the line.  So just eat
	 * up the leading whitespace.
	 */
	assert(*line == ' ');
	while (*line == ' ')
		line++;

	uint32_t *buf = calloc(1, 4 * sizedwords);
	int idx = 0;

	while (*line != '\n') {
		if (*line == 'z') {
			buf[idx++] = 0;
			line++;
			continue;
		}

		uint32_t accum = 0;
		for (int i = 0; (i < 5) && (*line != '\n'); i++) {
			accum *= 85;
			accum += *line - '!';
			line++;
		}

		buf[idx++] = accum;
	}

	return buf;
}

static bool
startswith(const char *line, const char *start)
{
	return strstr(line, start) == line;
}

static void
parseline(const char *line, const char *fmt, ...)
{
	int fmtlen = strlen(fmt);
	int n = 0;
	int l = 0;

	/* scan fmt string to extract expected # of conversions: */
	for (int i = 0; i < fmtlen; i++) {
		if (fmt[i] == '%') {
			if (i == (l - 1)) { /* prev char was %, ie. we have %% */
				n--;
				l = 0;
			} else {
				n++;
				l = i;
			}
		}
	}

	va_list ap;
	va_start(ap, fmt);
	if (vsscanf(line, fmt, ap) != n) {
		fprintf(stderr, "parse error scanning: '%s'\n", fmt);
		exit(1);
	}
	va_end(ap);
}

#define foreach_line_in_section(_line) \
	for (const char *_line = popline(); _line; _line = popline()) \
		/* check for start of next section */                     \
		if (_line[0] != ' ') {                                    \
			pushline();                                           \
			break;                                                \
		} else

/*
 * Decode ringbuffer section:
 */

static struct {
	uint64_t iova;
	uint32_t rptr;
	uint32_t wptr;
	uint32_t size;
	uint32_t *buf;
} ringbuffers[5];

static void
decode_ringbuffer(void)
{
	int id = 0;

	foreach_line_in_section (line) {
		if (startswith(line, "  - id:")) {
			parseline(line, "  - id: %d", &id);
			assert(id < ARRAY_SIZE(ringbuffers));
		} else if (startswith(line, "    iova:")) {
			parseline(line, "    iova: %"PRIx64, &ringbuffers[id].iova);
		} else if (startswith(line, "    rptr:")) {
			parseline(line, "    rptr: %d", &ringbuffers[id].rptr);
		} else if (startswith(line, "    wptr:")) {
			parseline(line, "    wptr: %d", &ringbuffers[id].wptr);
		} else if (startswith(line, "    size:")) {
			parseline(line, "    size: %d", &ringbuffers[id].size);
		} else if (startswith(line, "    data: !!ascii85 |")) {
			ringbuffers[id].buf = popline_ascii85(ringbuffers[id].size / 4);
			add_buffer(ringbuffers[id].iova, ringbuffers[id].size, ringbuffers[id].buf);
			continue;
		}

		printf("%s", line);
	}
}

static bool
valid_header(uint32_t pkt)
{
	if (options.gpu_id >= 500) {
		return pkt_is_type4(pkt) || pkt_is_type7(pkt);
	} else {
		/* TODO maybe we can check validish looking pkt3 opc or pkt0
		 * register offset.. the cmds sent by kernel are usually
		 * fairly limited (other than initialization) which confines
		 * the search space a bit..
		 */
		return true;
	}
}

static void
dump_cmdstream(void)
{
	uint64_t rb_base = regval64("CP_RB_BASE");

	printf("got rb_base=%"PRIx64"\n", rb_base);

	options.ibs[1].base = regval64("CP_IB1_BASE");
	options.ibs[1].rem  = regval("CP_IB1_REM_SIZE");
	options.ibs[2].base = regval64("CP_IB2_BASE");
	options.ibs[2].rem  = regval("CP_IB2_REM_SIZE");

	/* Adjust remaining size to account for cmdstream slurped into ROQ
	 * but not yet consumed by SQE
	 *
	 * TODO add support for earlier GPUs once we tease out the needed
	 * registers.. see crashit.c in msmtest for hints.
	 *
	 * TODO it would be nice to be able to extract out register bitfields
	 * by name rather than hard-coding this.
	 */
	if (is_a6xx()) {
		options.ibs[1].rem += regval("CP_CSQ_IB1_STAT") >> 16;
		options.ibs[2].rem += regval("CP_CSQ_IB2_STAT") >> 16;
	}

	printf("IB1: %"PRIx64", %u\n", options.ibs[1].base, options.ibs[1].rem);
	printf("IB2: %"PRIx64", %u\n", options.ibs[2].base, options.ibs[2].rem);

	/* now that we've got the regvals we want, reset register state
	 * so we aren't seeing values from decode_registers();
	 */
	reset_regs();

	for (int id = 0; id < ARRAY_SIZE(ringbuffers); id++) {
		if (ringbuffers[id].iova != rb_base)
			continue;
		if (!ringbuffers[id].size)
			continue;

		printf("found ring!\n");

		/* The kernel level ringbuffer (RB) wraps around, which
		 * cffdec doesn't really deal with.. so figure out how
		 * many dwords are unread
		 */
		unsigned ringszdw = ringbuffers[id].size >> 2;  /* in dwords */

/* helper macro to deal with modulo size math: */
#define mod_add(b, v)  ((ringszdw + (int)(b) + (int)(v)) % ringszdw)

		/* The rptr will (most likely) have moved past the IB to
		 * userspace cmdstream, so back up a bit, and then advance
		 * until we find a valid start of a packet.. this is going
		 * to be less reliable on a4xx and before (pkt0/pkt3),
		 * compared to pkt4/pkt7 with parity bits
		 */
		const int lookback = 12;
		unsigned rptr = mod_add(ringbuffers[id].rptr, -lookback);

		for (int idx = 0; idx < lookback; idx++) {
			if (valid_header(ringbuffers[id].buf[rptr]))
				break;
			rptr = mod_add(rptr, 1);
		}

		unsigned cmdszdw = mod_add(ringbuffers[id].wptr, -rptr);

		printf("got cmdszdw=%d\n", cmdszdw);
		uint32_t *buf = malloc(cmdszdw * 4);

		for (int idx = 0; idx < cmdszdw; idx++) {
			int p = mod_add(rptr, idx);
			buf[idx] = ringbuffers[id].buf[p];
		}

		dump_commands(buf, cmdszdw, 0);
		free(buf);
	}
}

/*
 * Decode 'bos' (buffers) section:
 */

static void
decode_bos(void)
{
	uint32_t size = 0;
	uint64_t iova = 0;

	foreach_line_in_section (line) {
		if (startswith(line, "  - iova:")) {
			parseline(line, "  - iova: %"PRIx64, &iova);
		} else if (startswith(line, "    size:")) {
			parseline(line, "    size: %u", &size);
		} else if (startswith(line, "    data: !!ascii85 |")) {
			uint32_t *buf = popline_ascii85(size / 4);

			if (verbose)
				dump_hex_ascii(buf, size, 1);

			add_buffer(iova, size, buf);

			continue;
		}

		printf("%s", line);
	}
}

/*
 * Decode registers section:
 */

static void
dump_register(struct rnn *rnn, uint32_t offset, uint32_t value)
{
	struct rnndecaddrinfo *info = rnn_reginfo(rnn, offset);
	if (info && info->typeinfo) {
		char *decoded = rnndec_decodeval(rnn->vc, info->typeinfo, value);
		printf("%s: %s\n", info->name, decoded);
	} else if (info) {
		printf("%s: %08x\n", info->name, value);
	} else {
		printf("<%04x>: %08x\n", offset, value);
	}
}

static void
decode_gmu_registers(void)
{
	foreach_line_in_section (line) {
		uint32_t offset, value;
		parseline(line, "  - { offset: %x, value: %x }", &offset, &value);

		printf("\t%08x\t", value);
		dump_register(rnn_gmu, offset/4, value);
	}
}

static void
decode_registers(void)
{
	foreach_line_in_section (line) {
		uint32_t offset, value;
		parseline(line, "  - { offset: %x, value: %x }", &offset, &value);

		reg_set(offset/4, value);
		printf("\t%08x", value);
		dump_register_val(offset/4, value, 0);
	}
}

/* similar to registers section, but for banked context regs: */
static void
decode_clusters(void)
{
	foreach_line_in_section (line) {
		if (startswith(line, "  - cluster-name:") ||
				startswith(line, "    - context:")) {
			printf("%s", line);
			continue;
		}

		uint32_t offset, value;
		parseline(line, "      - { offset: %x, value: %x }", &offset, &value);

		printf("\t%08x", value);
		dump_register_val(offset/4, value, 0);
	}
}

/*
 * Decode indexed-registers.. these aren't like normal registers, but a
 * sort of FIFO where successive reads pop out associated debug state.
 */

static void
dump_cp_sqe_stat(uint32_t *stat)
{
	printf("\t PC: %04x\n", stat[0]);
	stat++;

	if (is_a6xx() && valid_header(stat[0])) {
		if (pkt_is_type7(stat[0])) {
			unsigned opc = cp_type7_opcode(stat[0]);
			const char *name = pktname(opc);
			if (name)
				printf("\tPKT: %s\n", name);
		} else {
			/* Not sure if this case can happen: */
		}
	}

	for (int i = 0; i < 16; i++) {
		printf("\t$%02x: %08x\t\t$%02x: %08x\n",
				i + 1, stat[i], i + 16 + 1, stat[i + 16]);
	}
}

static void
dump_control_regs(uint32_t *regs)
{
	if (!rnn_control)
		return;

	/* Control regs 0x100-0x17f are a scratch space to be used by the
	 * firmware however it wants, unlike lower regs which involve some
	 * fixed-function units. Therefore only these registers get dumped
	 * directly.
	 */
	for (uint32_t i = 0; i < 0x80; i++) {
		printf("\t%08x\t", regs[i]);
		dump_register(rnn_control, i + 0x100, regs[i]);
	}
}

static void
dump_cp_ucode_dbg(uint32_t *dbg)
{
	/* Notes on the data:
	 * There seems to be a section every 4096 DWORD's. The sections aren't
	 * all the same size, so the rest of the 4096 DWORD's are filled with
	 * mirrors of the actual data.
	 */

	for (int section = 0; section < 6; section++, dbg += 0x1000) {
		switch (section) {
		case 0:
			/* Contains scattered data from a630_sqe.fw: */
			printf("\tSQE instruction cache:\n");
			dump_hex_ascii(dbg, 4 * 0x400, 1);
			break;
		case 1:
			printf("\tUnknown 1:\n");
			dump_hex_ascii(dbg, 4 * 0x80, 1);
			break;
		case 2:
			printf("\tUnknown 2:\n");
			dump_hex_ascii(dbg, 4 * 0x200, 1);
			break;
		case 3:
			printf("\tUnknown 3:\n");
			dump_hex_ascii(dbg, 4 * 0x80, 1);
			break;
		case 4:
			/* Don't bother printing this normally */
			if (verbose) {
				printf("\tSQE packet jumptable contents:\n");
				dump_hex_ascii(dbg, 4 * 0x80, 1);
			}
			break;
		case 5:
			printf("\tSQE scratch control regs:\n");
			dump_control_regs(dbg);
			break;
		}
	}
}

static void
dump_mem_pool_reg_write(unsigned reg, uint32_t data, unsigned context, bool pipe)
{
	if (pipe) {
		struct rnndecaddrinfo *info = rnn_reginfo(rnn_pipe, reg);
		printf("\t\twrite %s (%02x) pipe\n", info->name, reg);

		if (!strcmp(info->typeinfo->name, "void")) {
			/* registers that ignore their payload */
		} else {
			printf("\t\t\t");
			dump_register(rnn_pipe, reg, data);
		}
	} else {
		printf("\t\twrite %s (%05x) context %d\n", regname(reg, 1), reg, context);
		dump_register_val(reg, data, 2);
	}
}

static void
dump_mem_pool_chunk(const uint32_t *chunk)
{
	struct __attribute__((packed)) {
		bool reg0_enabled : 1;
		bool reg1_enabled : 1;
		uint32_t data0 : 32;
		uint32_t data1 : 32;
		uint32_t reg0 : 18;
		uint32_t reg1 : 18;
		bool reg0_pipe : 1;
		bool reg1_pipe : 1;
		uint32_t reg0_context : 1;
		uint32_t reg1_context : 1;
		uint32_t padding : 22;
	} fields;

	memcpy(&fields, chunk, 4 * sizeof(uint32_t));

	if (fields.reg0_enabled) {
		dump_mem_pool_reg_write(fields.reg0, fields.data0, fields.reg0_context, fields.reg0_pipe);
	}

	if (fields.reg1_enabled) {
		dump_mem_pool_reg_write(fields.reg1, fields.data1, fields.reg1_context, fields.reg1_pipe);
	}
}

static void
dump_cp_mem_pool(uint32_t *mempool)
{
	/* The mem pool is a shared pool of memory used for storing in-flight
	 * register writes. There are 6 different queues, one for each
	 * cluster. Writing to $data (or for some special registers, $addr)
	 * pushes data onto the appropriate queue, and each queue is pulled
	 * from by the appropriate cluster. The queues are thus written to
	 * in-order, but may be read out-of-order.
	 *
	 * The queues are conceptually divided into 128-bit "chunks", and the
	 * read and write pointers are in units of chunks.  These chunks are
	 * organized internally into 8-chunk "blocks", and memory is allocated
	 * dynamically in terms of blocks. Each queue is represented as a
	 * singly-linked list of blocks, as well as 3-bit start/end chunk
	 * pointers that point within the first/last block.  The next pointers
	 * are located in a separate array, rather than inline.
	 */

	/* TODO: The firmware CP_MEM_POOL save/restore routines do something
	 * like:
	 *
	 * cread $02, [ $00 + 0 ]
	 * and $02, $02, 0x118
	 * ...
	 * brne $02, 0, #label
	 * mov $03, 0x2000
	 * mov $03, 0x1000
	 * label:
	 * ...
	 *
	 * I think that control register 0 is the GPU version, and some
	 * versions have a smaller mem pool. It seems some models have a mem
	 * pool that's half the size, and a bunch of offsets are shifted
	 * accordingly. Unfortunately the kernel driver's dumping code doesn't
	 * seem to take this into account, even the downstream android driver,
	 * and we don't know which versions 0x8, 0x10, or 0x100 correspond
	 * to. Or maybe we can use CP_DBG_MEM_POOL_SIZE to figure this out?
	 */
	bool small_mem_pool = false;

	/* The array of next pointers for each block. */
	const uint32_t *next_pointers = small_mem_pool ? &mempool[0x800] : &mempool[0x1000];

	/* Maximum number of blocks in the pool, also the size of the pointers
	 * array.
	 */
	const int num_blocks = small_mem_pool ? 0x30 : 0x80;

	/* Number of queues */
	const unsigned num_queues = 6;

	/* Unfortunately the per-queue state is a little more complicated than
	 * a simple pair of begin/end pointers. Instead of a single beginning
	 * block, there are *two*, with the property that either the two are
	 * equal or the second is the "next" of the first. Similarly there are
	 * two end blocks. Thus the queue either looks like this:
	 *
	 * A -> B -> ... -> C -> D
	 *
	 * Or like this, or some combination:
	 *
	 * A/B -> ... -> C/D
	 *
	 * However, there's only one beginning/end chunk offset. Now the
	 * question is, which of A or B is the actual start? I.e. is the chunk
	 * offset an offset inside A or B? It depends. I'll show a typical read
	 * cycle, starting here (read pointer marked with a *) with a chunk
	 * offset of 0:
	 *
	 *	  A                    B
	 *  _ _ _ _ _ _ _ _      _ _ _ _ _ _ _ _      _ _ _ _ _ _ _ _
	 * |_|_|_|_|_|_|_|_| -> |*|_|_|_|_|_|_|_| -> |_|_|_|_|_|_|_|_|
	 *
	 * Once the pointer advances far enough, the hardware decides to free
	 * A, after which the read-side state looks like:
	 *
	 *	(free)                A/B
	 *  _ _ _ _ _ _ _ _      _ _ _ _ _ _ _ _      _ _ _ _ _ _ _ _
	 * |_|_|_|_|_|_|_|_|    |_|_|_|*|_|_|_|_| -> |_|_|_|_|_|_|_|_|
	 *
	 * Then after advancing the pointer a bit more, the hardware fetches
	 * the "next" pointer for A and stores it in B:
	 *
	 *	(free)                 A                     B
	 *  _ _ _ _ _ _ _ _      _ _ _ _ _ _ _ _      _ _ _ _ _ _ _ _
	 * |_|_|_|_|_|_|_|_|    |_|_|_|_|_|_|_|*| -> |_|_|_|_|_|_|_|_|
	 *
	 * Then the read pointer advances into B, at which point we've come
	 * back to the first state having advanced a whole block:
	 *
	 *	(free)                 A                     B
	 *  _ _ _ _ _ _ _ _      _ _ _ _ _ _ _ _      _ _ _ _ _ _ _ _
	 * |_|_|_|_|_|_|_|_|    |_|_|_|_|_|_|_|_| -> |*|_|_|_|_|_|_|_|
	 *
	 *
	 * There is a similar cycle for the write pointer. Now, the question
	 * is, how do we know which state we're in? We need to know this to
	 * know whether the pointer (*) is in A or B if they're different. It
	 * seems like there should be some bit somewhere describing this, but
	 * after lots of experimentation I've come up empty-handed. For now we
	 * assume that if the pointer is in the first half, then we're in
	 * either the first or second state and use B, and otherwise we're in
	 * the second or third state and use A. So far I haven't seen anything
	 * that violates this assumption.
	 */

	struct {
		uint32_t unk0;
		uint32_t padding0[7]; /* Mirrors of unk0 */

		struct {
			uint32_t chunk : 3;
			uint32_t first_block : 32 - 3;
		} writer[6];
		uint32_t padding1[2]; /* Mirrors of writer[4], writer[5] */

		uint32_t unk1;
		uint32_t padding2[7]; /* Mirrors of unk1 */

		uint32_t writer_second_block[6];
		uint32_t padding3[2];

		uint32_t unk2[6];
		uint32_t padding4[2];

		struct {
			uint32_t chunk : 3;
			uint32_t first_block : 32 - 3;
		} reader[6];
		uint32_t padding5[2]; /* Mirrors of reader[4], reader[5] */

		uint32_t unk3;
		uint32_t padding6[7]; /* Mirrors of unk3 */

		uint32_t reader_second_block[6];
		uint32_t padding7[2];

		uint32_t block_count[6];
		uint32_t padding[2];

		uint32_t unk4;
		uint32_t padding9[7]; /* Mirrors of unk4 */
	} data1;

	const uint32_t *data1_ptr = small_mem_pool ? &mempool[0xc00] : &mempool[0x1800];
	memcpy(&data1, data1_ptr, sizeof(data1));

	/* Based on the kernel, the first dword is the mem pool size (in
	 * blocks?) and mirrors CP_MEM_POOL_DBG_SIZE.
	 */
	const uint32_t *data2_ptr = small_mem_pool ? &mempool[0x1000] : &mempool[0x2000];
	const int data2_size = 0x60;

	/* This seems to be the size of each queue in chunks. */
	const uint32_t *queue_sizes = &data2_ptr[0x18];

	printf("\tdata2:\n");
	dump_hex_ascii(data2_ptr, 4 * data2_size, 1);

	/* These seem to be some kind of counter of allocated/deallocated blocks */
	if (verbose) {
		printf("\tunk0: %x\n", data1.unk0);
		printf("\tunk1: %x\n", data1.unk1);
		printf("\tunk3: %x\n", data1.unk3);
		printf("\tunk4: %x\n\n", data1.unk4);
	}

	for (int queue = 0; queue < num_queues; queue++) {
		const char *cluster_names[6] = {
			"FE", "SP_VS", "PC_VS", "GRAS", "SP_PS", "PS"
		};
		printf("\tCLUSTER_%s:\n\n", cluster_names[queue]);

		if (verbose) {
			printf("\t\twriter_first_block: 0x%x\n", data1.writer[queue].first_block);
			printf("\t\twriter_second_block: 0x%x\n", data1.writer_second_block[queue]);
			printf("\t\twriter_chunk: %d\n", data1.writer[queue].chunk);
			printf("\t\treader_first_block: 0x%x\n", data1.reader[queue].first_block);
			printf("\t\treader_second_block: 0x%x\n", data1.reader_second_block[queue]);
			printf("\t\treader_chunk: %d\n", data1.reader[queue].chunk);
			printf("\t\tblock_count: %d\n", data1.block_count[queue]);
			printf("\t\tunk2: 0x%x\n", data1.unk2[queue]);
			printf("\t\tqueue_size: %d\n\n", queue_sizes[queue]);
		}

		uint32_t cur_chunk = data1.reader[queue].chunk;
		uint32_t cur_block = cur_chunk > 3 ?
			data1.reader[queue].first_block :
			data1.reader_second_block[queue];
		uint32_t last_chunk = data1.writer[queue].chunk;
		uint32_t last_block = last_chunk > 3 ?
			data1.writer[queue].first_block :
			data1.writer_second_block[queue];

		if (verbose)
			printf("\tblock %x\n", cur_block);
		if (cur_block >= num_blocks) {
			fprintf(stderr, "block %x too large\n", cur_block);
			exit(1);
		}
		unsigned calculated_queue_size = 0;
		while (cur_block != last_block || cur_chunk != last_chunk) {
			calculated_queue_size++;
			uint32_t *chunk_ptr = &mempool[cur_block * 0x20 + cur_chunk * 4];

			dump_mem_pool_chunk(chunk_ptr);

			printf("\t%05x: %08x %08x %08x %08x\n",
			       4 * (cur_block * 0x20 + cur_chunk + 4),
			       chunk_ptr[0], chunk_ptr[1], chunk_ptr[2], chunk_ptr[3]);

			cur_chunk++;
			if (cur_chunk == 8) {
				cur_block = next_pointers[cur_block];
				if (verbose)
					printf("\tblock %x\n", cur_block);
				if (cur_block >= num_blocks) {
					fprintf(stderr, "block %x too large\n", cur_block);
					exit(1);
				}
				cur_chunk = 0;
			}
		}
		if (calculated_queue_size != queue_sizes[queue]) {
			printf("\t\tCALCULATED SIZE %d DOES NOT MATCH!\n", calculated_queue_size);
		}
		printf("\n");
	}
}

static void
decode_indexed_registers(void)
{
	char *name = NULL;
	uint32_t sizedwords = 0;

	foreach_line_in_section (line) {
		if (startswith(line, "  - regs-name:")) {
			free(name);
			parseline(line, "  - regs-name: %ms", &name);
		} else if (startswith(line, "    dwords:")) {
			parseline(line, "    dwords: %u", &sizedwords);
		} else if (startswith(line, "    data: !!ascii85 |")) {
			uint32_t *buf = popline_ascii85(sizedwords);

			/* some of the sections are pretty large, and are (at least
			 * so far) not useful, so skip them if not in verbose mode:
			 */
			bool dump = verbose ||
				!strcmp(name, "CP_SQE_STAT") ||
				!strcmp(name, "CP_DRAW_STATE") ||
				!strcmp(name, "CP_ROQ") ||
				0;

			if (!strcmp(name, "CP_SQE_STAT"))
				dump_cp_sqe_stat(buf);

			if (!strcmp(name, "CP_UCODE_DBG_DATA"))
				dump_cp_ucode_dbg(buf);

			if (!strcmp(name, "CP_MEMPOOL"))
				dump_cp_mem_pool(buf);

			if (dump)
				dump_hex_ascii(buf, 4 * sizedwords, 1);

			free(buf);

			continue;
		}

		printf("%s", line);
	}
}

/*
 * Decode shader-blocks:
 */

static void
decode_shader_blocks(void)
{
	char *type = NULL;
	uint32_t sizedwords = 0;

	foreach_line_in_section (line) {
		if (startswith(line, "  - type:")) {
			free(type);
			parseline(line, "  - type: %ms", &type);
		} else if (startswith(line, "      size:")) {
			parseline(line, "      size: %u", &sizedwords);
		} else if (startswith(line, "    data: !!ascii85 |")) {
			uint32_t *buf = popline_ascii85(sizedwords);

			/* some of the sections are pretty large, and are (at least
			 * so far) not useful, so skip them if not in verbose mode:
			 */
			bool dump = verbose ||
				!strcmp(type, "A6XX_SP_INST_DATA") ||
				!strcmp(type, "A6XX_HLSQ_INST_RAM") ||
				0;

			if (!strcmp(type, "A6XX_SP_INST_DATA") ||
					!strcmp(type, "A6XX_HLSQ_INST_RAM")) {
				/* TODO this section actually contains multiple shaders
				 * (or parts of shaders?), so perhaps we should search
				 * for ends of shaders and decode each?
				 */
				try_disasm_a3xx(buf, sizedwords, 1, stdout, options.gpu_id);
			}

			if (dump)
				dump_hex_ascii(buf, 4 * sizedwords, 1);

			free(buf);

			continue;
		}

		printf("%s", line);
	}

	free(type);
}

/*
 * Decode debugbus section:
 */

static void
decode_debugbus(void)
{
	char *block = NULL;
	uint32_t sizedwords = 0;

	foreach_line_in_section (line) {
		if (startswith(line, "  - debugbus-block:")) {
			free(block);
			parseline(line, "  - debugbus-block: %ms", &block);
		} else if (startswith(line, "    count:")) {
			parseline(line, "    count: %u", &sizedwords);
		} else if (startswith(line, "    data: !!ascii85 |")) {
			uint32_t *buf = popline_ascii85(sizedwords);

			/* some of the sections are pretty large, and are (at least
			 * so far) not useful, so skip them if not in verbose mode:
			 */
			bool dump = verbose ||
				0;

			if (dump)
				dump_hex_ascii(buf, 4 * sizedwords, 1);

			free(buf);

			continue;
		}

		printf("%s", line);
	}
}

/*
 * Main crashdump decode loop:
 */

static void
decode(void)
{
	const char *line;

	while ((line = popline())) {
		printf("%s", line);
		if (startswith(line, "revision:")) {
			parseline(line, "revision: %u", &options.gpu_id);
			printf("Got gpu_id=%u\n", options.gpu_id);

			cffdec_init(&options);

			if (is_a6xx()) {
				rnn_gmu = rnn_new(!options.color);
				rnn_load_file(rnn_gmu, "adreno/a6xx_gmu.xml", "A6XX");
				rnn_control = rnn_new(!options.color);
				rnn_load_file(rnn_control, "adreno/adreno_control_regs.xml", "A6XX_CONTROL_REG");
				rnn_pipe = rnn_new(!options.color);
				rnn_load_file(rnn_pipe, "adreno/adreno_pipe_regs.xml", "A6XX_PIPE_REG");
			} else if (is_a5xx()) {
				rnn_control = rnn_new(!options.color);
				rnn_load_file(rnn_control, "adreno/adreno_control_regs.xml", "A5XX_CONTROL_REG");
			} else {
				rnn_control = NULL;
			}
		} else if (startswith(line, "bos:")) {
			decode_bos();
		} else if (startswith(line, "ringbuffer:")) {
			decode_ringbuffer();
		} else if (startswith(line, "registers:")) {
			decode_registers();

			/* after we've recorded buffer contents, and CP register values,
			 * we can take a stab at decoding the cmdstream:
			 */
			dump_cmdstream();
		} else if (startswith(line, "registers-gmu:")) {
			decode_gmu_registers();
		} else if (startswith(line, "indexed-registers:")) {
			decode_indexed_registers();
		} else if (startswith(line, "shader-blocks:")) {
			decode_shader_blocks();
		} else if (startswith(line, "clusters:")) {
			decode_clusters();
		} else if (startswith(line, "debugbus:")) {
			decode_debugbus();
		}
	}
}

/*
 * Usage and argument parsing:
 */

static void
usage(void)
{
	fprintf(stderr, "Usage:\n\n"
			"\tcrashdec [-achmsv] [-f FILE]\n\n"
			"Options:\n"
			"\t-a, --allregs   - show all registers (including ones not written since\n"
			"\t                  previous draw) at each draw\n"
			"\t-c, --color     - use colors\n"
			"\t-f, --file=FILE - read input from specified file (rather than stdin)\n"
			"\t-h, --help      - this usage message\n"
			"\t-m, --markers   - try to decode CP_NOP string markers\n"
			"\t-s, --summary   - don't show individual register writes, but just show\n"
			"\t                  register values on draws\n"
			"\t-v, --verbose   - dump more verbose output, including contents of\n"
			"\t                  less interesting buffers\n"
			"\n"
		);
	exit(2);
}

static const struct option opts[] = {
	{ .name = "allregs", .has_arg = 0, NULL, 'a' },
	{ .name = "color",   .has_arg = 0, NULL, 'c' },
	{ .name = "file",    .has_arg = 1, NULL, 'f' },
	{ .name = "help",    .has_arg = 0, NULL, 'h' },
	{ .name = "markers", .has_arg = 0, NULL, 'm' },
	{ .name = "summary", .has_arg = 0, NULL, 's' },
	{ .name = "verbose", .has_arg = 0, NULL, 'v' },
	{}
};

static bool interactive;

static void
cleanup(void)
{
	fflush(stdout);

	if (interactive) {
		pager_close();
	}
}

int
main(int argc, char **argv)
{
	int c;

	interactive = isatty(STDOUT_FILENO);
	options.color = interactive;

	/* default to read from stdin: */
	in = stdin;

	while ((c = getopt_long(argc, argv, "acf:hmsv", opts, NULL)) != -1) {
		switch (c) {
		case 'a':
			options.allregs = true;
			break;
		case 'c':
			options.color = true;
			break;
		case 'f':
			in = fopen(optarg, "r");
			break;
		case 'm':
			options.decode_markers = true;
			break;
		case 's':
			options.summary = true;
			break;
		case 'v':
			verbose = true;
			break;
		case 'h':
		default:
			usage();
		}
	}

	disasm_a3xx_set_debug(PRINT_RAW);

	if (interactive) {
		pager_open();
	}

	atexit(cleanup);

	decode();
	cleanup();
}
