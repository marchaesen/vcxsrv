/*
 * Copyright (C) 2019 Ryan Houdek <Sonicadvance1@gmail.com>
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

#include "main/mtypes.h"
#include "compiler/glsl/standalone.h"
#include "compiler/glsl/glsl_to_nir.h"
#include "compiler/nir_types.h"
#include "disassemble.h"
#include "util/u_dynarray.h"

static void
disassemble(const char *filename)
{
        FILE *fp = fopen(filename, "rb");
        assert(fp);

        fseek(fp, 0, SEEK_END);
        int filesize = ftell(fp);
        rewind(fp);

        unsigned char *code = malloc(filesize);
        int res = fread(code, 1, filesize, fp);
        if (res != filesize) {
                printf("Couldn't read full file\n");
        }
        fclose(fp);

        disassemble_bifrost(code, filesize, false);
        free(code);
}

int
main(int argc, char **argv)
{
        if (argc < 2) {
                printf("Pass a command\n");
                exit(1);
        }
        if (strcmp(argv[1], "disasm") == 0) {
                disassemble(argv[2]);
        }
        return 0;
}
