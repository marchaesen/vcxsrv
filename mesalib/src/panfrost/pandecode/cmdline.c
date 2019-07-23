/*
 * Copyright (C) 2019 Alyssa Rosenzweig
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

/* Parsing */

static FILE *
pandecode_read_filename(const char *base, const char *name)
{
        char *fn = NULL;
        asprintf(&fn, "%s/%s", base, name);

        FILE *fp = fopen(fn, "rb");
        free(fn);

        return fp;
}

static void
pandecode_read_memory(const char *base, const char *name, mali_ptr gpu_va)
{
        FILE *fp = pandecode_read_filename(base, name);

        if (!fp) {
                fprintf(stderr, "Warning: missing %s\n", name);
                return;
        }

        fseek(fp, 0, SEEK_END);
        long sz = ftell(fp);
        fseek(fp, 0, SEEK_SET);

        char *buf = malloc(sz);
        assert(buf);
        fread(buf, 1, sz, fp);
        fclose(fp);

        pandecode_inject_mmap(gpu_va, buf, sz, name);
}

static void
pandecode_read_mmap(const char *base, const char *line)
{
        assert(strlen(line) < 500);

        mali_ptr addr;
        char name[512];

        sscanf(line, "MMAP %" PRIx64 " %s", &addr, name);
        pandecode_read_memory(base, name, addr);
}

static void
pandecode_read_job_submit(const char *base, const char *line)
{
        mali_ptr addr;
        unsigned core_req;
        unsigned is_bifrost;

        sscanf(line, "JS %" PRIx64 " %x %x", &addr, &core_req, &is_bifrost);
        pandecode_jc(addr, is_bifrost);
}



/* Reads the control file, processing as it goes. */

static void
pandecode_read_control(const char *base)
{
        FILE *fp = pandecode_read_filename(base, "control.log");

        if (!fp) {
                fprintf(stderr, "Invalid directory path\n");
                return;
        }

        char *line = NULL;
        size_t len = 0;

        while (getline(&line, &len, fp) != -1) {
                switch (line[0]) {
                case 'M':
                        pandecode_read_mmap(base, line);
                        break;

                case 'J':
                        pandecode_read_job_submit(base, line);
                        break;

                default:
                        assert(0);
                        break;
                }
        }
}

int
main(int argc, char **argv)
{
        if (argc < 2) {
                fprintf(stderr, "Usage: pandecode [directory]\n");
                exit(1);
        }

        pandecode_initialize();
        pandecode_read_control(argv[1]);
}
