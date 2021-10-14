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
#include <sys/mman.h>

#include "decode.h"
#include "util/macros.h"
#include "util/u_debug.h"
#include "util/u_dynarray.h"
#include "util/hash_table.h"

FILE *pandecode_dump_stream;

/* Memory handling */

static struct hash_table_u64 *mmap_table;

static struct util_dynarray ro_mappings;

static struct pandecode_mapped_memory *
pandecode_find_mapped_gpu_mem_containing_rw(uint64_t addr)
{
        return _mesa_hash_table_u64_search(mmap_table, addr & ~(4096 - 1));
}

struct pandecode_mapped_memory *
pandecode_find_mapped_gpu_mem_containing(uint64_t addr)
{
        struct pandecode_mapped_memory *mem = pandecode_find_mapped_gpu_mem_containing_rw(addr);

        if (mem && mem->addr && !mem->ro) {
                mprotect(mem->addr, mem->length, PROT_READ);
                mem->ro = true;
                util_dynarray_append(&ro_mappings, struct pandecode_mapped_memory *, mem);
        }

        return mem;
}

void
pandecode_map_read_write(void)
{
        util_dynarray_foreach(&ro_mappings, struct pandecode_mapped_memory *, mem) {
                (*mem)->ro = false;
                mprotect((*mem)->addr, (*mem)->length, PROT_READ | PROT_WRITE);
        }
        util_dynarray_clear(&ro_mappings);
}

static void
pandecode_add_name(struct pandecode_mapped_memory *mem, uint64_t gpu_va, const char *name)
{
        if (!name) {
                /* If we don't have a name, assign one */

                snprintf(mem->name, sizeof(mem->name) - 1,
                         "memory_%" PRIx64, gpu_va);
        } else {
                assert((strlen(name) + 1) < sizeof(mem->name));
                memcpy(mem->name, name, strlen(name) + 1);
        }
}

void
pandecode_inject_mmap(uint64_t gpu_va, void *cpu, unsigned sz, const char *name)
{
        /* First, search if we already mapped this and are just updating an address */

        struct pandecode_mapped_memory *existing =
                pandecode_find_mapped_gpu_mem_containing_rw(gpu_va);

        if (existing && existing->gpu_va == gpu_va) {
                existing->length = sz;
                existing->addr = cpu;
                pandecode_add_name(existing, gpu_va, name);
                return;
        }

        /* Otherwise, add a fresh mapping */
        struct pandecode_mapped_memory *mapped_mem = NULL;

        mapped_mem = calloc(1, sizeof(*mapped_mem));
        mapped_mem->gpu_va = gpu_va;
        mapped_mem->length = sz;
        mapped_mem->addr = cpu;
        pandecode_add_name(mapped_mem, gpu_va, name);

        /* Add it to the table */
        assert((gpu_va & 4095) == 0);

        for (unsigned i = 0; i < sz; i += 4096)
                _mesa_hash_table_u64_insert(mmap_table, gpu_va + i, mapped_mem);
}

void
pandecode_inject_free(uint64_t gpu_va, unsigned sz)
{
        struct pandecode_mapped_memory *mem =
                pandecode_find_mapped_gpu_mem_containing_rw(gpu_va);

        if (!mem)
                return;

        assert(mem->gpu_va == gpu_va);
        assert(mem->length == sz);

        free(mem);

        for (unsigned i = 0; i < sz; i += 4096)
                _mesa_hash_table_u64_remove(mmap_table, gpu_va + i);
}

char *
pointer_as_memory_reference(uint64_t ptr)
{
        struct pandecode_mapped_memory *mapped;
        char *out = malloc(128);

        /* Try to find the corresponding mapped zone */

        mapped = pandecode_find_mapped_gpu_mem_containing_rw(ptr);

        if (mapped) {
                snprintf(out, 128, "%s + %d", mapped->name, (int) (ptr - mapped->gpu_va));
                return out;
        }

        /* Just use the raw address if other options are exhausted */

        snprintf(out, 128, "0x%" PRIx64, ptr);
        return out;

}

static int pandecode_dump_frame_count = 0;

static bool force_stderr = false;

void
pandecode_dump_file_open(void)
{
        if (pandecode_dump_stream)
                return;

        /* This does a getenv every frame, so it is possible to use
         * setenv to change the base at runtime.
         */
        const char *dump_file_base = debug_get_option("PANDECODE_DUMP_FILE", "pandecode.dump");
        if (force_stderr || !strcmp(dump_file_base, "stderr"))
                pandecode_dump_stream = stderr;
        else {
                char buffer[1024];
                snprintf(buffer, sizeof(buffer), "%s.%04d", dump_file_base, pandecode_dump_frame_count);
                printf("pandecode: dump command stream to file %s\n", buffer);
                pandecode_dump_stream = fopen(buffer, "w");
                if (!pandecode_dump_stream)
                        fprintf(stderr,
                                "pandecode: failed to open command stream log file %s\n",
                                buffer);
        }
}

static void
pandecode_dump_file_close(void)
{
        if (pandecode_dump_stream && pandecode_dump_stream != stderr) {
                if (fclose(pandecode_dump_stream))
                        perror("pandecode: dump file");

                pandecode_dump_stream = NULL;
        }
}

void
pandecode_initialize(bool to_stderr)
{
        force_stderr = to_stderr;
        mmap_table = _mesa_hash_table_u64_create(NULL);
        util_dynarray_init(&ro_mappings, NULL);
}

void
pandecode_next_frame(void)
{
        pandecode_dump_file_close();
        pandecode_dump_frame_count++;
}

void
pandecode_close(void)
{
        _mesa_hash_table_u64_destroy(mmap_table);
        util_dynarray_fini(&ro_mappings);
        pandecode_dump_file_close();
}

void pandecode_abort_on_fault_v4(mali_ptr jc_gpu_va);
void pandecode_abort_on_fault_v5(mali_ptr jc_gpu_va);
void pandecode_abort_on_fault_v6(mali_ptr jc_gpu_va);
void pandecode_abort_on_fault_v7(mali_ptr jc_gpu_va);

void
pandecode_abort_on_fault(mali_ptr jc_gpu_va, unsigned gpu_id)
{
        switch (pan_arch(gpu_id)) {
        case 4: pandecode_abort_on_fault_v4(jc_gpu_va); return;
        case 5: pandecode_abort_on_fault_v5(jc_gpu_va); return;
        case 6: pandecode_abort_on_fault_v6(jc_gpu_va); return;
        case 7: pandecode_abort_on_fault_v7(jc_gpu_va); return;
        default: unreachable("Unsupported architecture");
        }
}

void pandecode_jc_v4(mali_ptr jc_gpu_va, unsigned gpu_id);
void pandecode_jc_v5(mali_ptr jc_gpu_va, unsigned gpu_id);
void pandecode_jc_v6(mali_ptr jc_gpu_va, unsigned gpu_id);
void pandecode_jc_v7(mali_ptr jc_gpu_va, unsigned gpu_id);

void
pandecode_jc(mali_ptr jc_gpu_va, unsigned gpu_id)
{
        switch (pan_arch(gpu_id)) {
        case 4: pandecode_jc_v4(jc_gpu_va, gpu_id); return;
        case 5: pandecode_jc_v5(jc_gpu_va, gpu_id); return;
        case 6: pandecode_jc_v6(jc_gpu_va, gpu_id); return;
        case 7: pandecode_jc_v7(jc_gpu_va, gpu_id); return;
        default: unreachable("Unsupported architecture");
        }
}
