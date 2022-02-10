/*
 * Copyright (C) 2017-2019 Lyude Paul
 * Copyright (C) 2017-2019 Alyssa Rosenzweig
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
 */

#ifndef __PAN_DECODE_H__
#define __PAN_DECODE_H__

#include "genxml/gen_macros.h"
#include "util/rb_tree.h"

#include "wrap.h"

extern FILE *pandecode_dump_stream;

void pandecode_dump_file_open(void);

struct pandecode_mapped_memory {
        struct rb_node node;
        size_t length;
        void *addr;
        uint64_t gpu_va;
        bool ro;
        char name[32];
};

char *pointer_as_memory_reference(uint64_t ptr);

struct pandecode_mapped_memory *pandecode_find_mapped_gpu_mem_containing(uint64_t addr);

void pandecode_map_read_write(void);

void pandecode_dump_mappings(void);

static inline void *
__pandecode_fetch_gpu_mem(const struct pandecode_mapped_memory *mem,
                          uint64_t gpu_va, size_t size,
                          int line, const char *filename)
{
        if (!mem)
                mem = pandecode_find_mapped_gpu_mem_containing(gpu_va);

        if (!mem) {
                fprintf(stderr, "Access to unknown memory %" PRIx64 " in %s:%d\n",
                        gpu_va, filename, line);
                assert(0);
        }

        assert(mem);
        assert(size + (gpu_va - mem->gpu_va) <= mem->length);

        return mem->addr + gpu_va - mem->gpu_va;
}

#define pandecode_fetch_gpu_mem(mem, gpu_va, size) \
	__pandecode_fetch_gpu_mem(mem, gpu_va, size, __LINE__, __FILE__)

/* Returns a validated pointer to mapped GPU memory with the given pointer type,
 * size automatically determined from the pointer type
 */
#define PANDECODE_PTR(mem, gpu_va, type) \
	((type*)(__pandecode_fetch_gpu_mem(mem, gpu_va, sizeof(type), \
					 __LINE__, __FILE__)))

/* Usage: <variable type> PANDECODE_PTR_VAR(name, mem, gpu_va) */
#define PANDECODE_PTR_VAR(name, mem, gpu_va) \
	name = __pandecode_fetch_gpu_mem(mem, gpu_va, sizeof(*name), \
				       __LINE__, __FILE__)

#ifdef PAN_ARCH
void GENX(pandecode_jc)(mali_ptr jc_gpu_va, unsigned gpu_id);
void GENX(pandecode_abort_on_fault)(mali_ptr jc_gpu_va);
#endif

static inline void
pan_hexdump(FILE *fp, const uint8_t *hex, size_t cnt, bool with_strings)
{
        for (unsigned i = 0; i < cnt; ++i) {
                if ((i & 0xF) == 0)
                        fprintf(fp, "%06X  ", i);

                uint8_t v = hex[i];

                if (v == 0 && (i & 0xF) == 0) {
                        /* Check if we're starting an aligned run of zeroes */
                        unsigned zero_count = 0;

                        for (unsigned j = i; j < cnt; ++j) {
                                if (hex[j] == 0)
                                        zero_count++;
                                else
                                        break;
                        }

                        if (zero_count >= 32) {
                                fprintf(fp, "*\n");
                                i += (zero_count & ~0xF) - 1;
                                continue;
                        }
                }

                fprintf(fp, "%02X ", hex[i]);
                if ((i & 0xF) == 0xF && with_strings) {
                        fprintf(fp, " | ");
                        for (unsigned j = i & ~0xF; j <= i; ++j) {
                                uint8_t c = hex[j];
                                fputc((c < 32 || c > 128) ? '.' : c, fp);
                        }
                }

                if ((i & 0xF) == 0xF)
                        fprintf(fp, "\n");
        }

        fprintf(fp, "\n");
}

#endif /* __MMAP_TRACE_H__ */
