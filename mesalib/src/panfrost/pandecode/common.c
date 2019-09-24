/*
 * Copyright (C) 2019 Alyssa Rosenzweig
 * Copyright (C) 2017-2018 Lyude Paul
 * Copyright (C) 2019 Collabora, Ltd.
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

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "decode.h"
#include "util/macros.h"

/* Memory handling */

static struct pandecode_mapped_memory mmaps;

struct pandecode_mapped_memory *
pandecode_find_mapped_gpu_mem_containing(uint64_t addr)
{
        list_for_each_entry(struct pandecode_mapped_memory, pos, &mmaps.node, node) {
                if (addr >= pos->gpu_va && addr < pos->gpu_va + pos->length)
                        return pos;
        }

        return NULL;
}

static void
pandecode_add_name(struct pandecode_mapped_memory *mem, uint64_t gpu_va, const char *name)
{
        if (!name) {
                /* If we don't have a name, assign one */

                snprintf(mem->name, ARRAY_SIZE(mem->name) - 1,
                         "memory_%" PRIx64, gpu_va);
        } else {
                assert((strlen(name) + 1) < ARRAY_SIZE(mem->name));
                memcpy(mem->name, name, strlen(name) + 1);
        }
}

void
pandecode_inject_mmap(uint64_t gpu_va, void *cpu, unsigned sz, const char *name)
{
        /* First, search if we already mapped this and are just updating an address */

        list_for_each_entry(struct pandecode_mapped_memory, pos, &mmaps.node, node) {
                if (pos->gpu_va == gpu_va) {
                        /* TODO: Resizing weirdness. Only applies to tracing
                         * the legacy driver, not for native traces */

                        pos->length = sz;
                        pos->addr = cpu;
                        pandecode_add_name(pos, gpu_va, name);

                        return;
                }
        }

        /* Otherwise, add a fresh mapping */
        struct pandecode_mapped_memory *mapped_mem = NULL;

        mapped_mem = malloc(sizeof(*mapped_mem));
        list_inithead(&mapped_mem->node);

        mapped_mem->gpu_va = gpu_va;
        mapped_mem->length = sz;
        mapped_mem->addr = cpu;
        pandecode_add_name(mapped_mem, gpu_va, name);

        list_add(&mapped_mem->node, &mmaps.node);
}

char *
pointer_as_memory_reference(uint64_t ptr)
{
        struct pandecode_mapped_memory *mapped;
        char *out = malloc(128);

        /* Try to find the corresponding mapped zone */

        mapped = pandecode_find_mapped_gpu_mem_containing(ptr);

        if (mapped) {
                snprintf(out, 128, "%s + %d", mapped->name, (int) (ptr - mapped->gpu_va));
                return out;
        }

        /* Just use the raw address if other options are exhausted */

        snprintf(out, 128, "0x%" PRIx64, ptr);
        return out;

}

void
pandecode_initialize(void)
{
        list_inithead(&mmaps.node);

}
