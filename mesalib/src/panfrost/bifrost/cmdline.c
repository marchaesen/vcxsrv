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

#include "disassemble.h"
#include "compiler.h"

#include "main/mtypes.h"
#include "compiler/glsl/standalone.h"
#include "compiler/glsl/glsl_to_nir.h"
#include "compiler/glsl/gl_nir.h"
#include "compiler/nir_types.h"
#include "util/u_dynarray.h"
#include "bifrost_compile.h"

static void
compile_shader(char **argv, bool vertex_only)
{
        struct gl_shader_program *prog;
        nir_shader *nir[2];
        unsigned shader_types[2] = {
                MESA_SHADER_VERTEX,
                MESA_SHADER_FRAGMENT,
        };

        struct standalone_options options = {
                .glsl_version = 300, /* ES - needed for precision */
                .do_link = true,
                .lower_precision = true
        };

        static struct gl_context local_ctx;

        prog = standalone_compile_shader(&options, 2, argv, &local_ctx);
        prog->_LinkedShaders[MESA_SHADER_FRAGMENT]->Program->info.stage = MESA_SHADER_FRAGMENT;

        struct util_dynarray binary;

        util_dynarray_init(&binary, NULL);

        for (unsigned i = 0; i < 2; ++i) {
                nir[i] = glsl_to_nir(&local_ctx, prog, shader_types[i], &bifrost_nir_options);
                NIR_PASS_V(nir[i], nir_lower_global_vars_to_local);
                NIR_PASS_V(nir[i], nir_lower_io_to_temporaries, nir_shader_get_entrypoint(nir[i]), true, i == 0);
                NIR_PASS_V(nir[i], nir_split_var_copies);
                NIR_PASS_V(nir[i], nir_lower_var_copies);

                /* before buffers and vars_to_ssa */
                NIR_PASS_V(nir[i], gl_nir_lower_images, true);

                NIR_PASS_V(nir[i], gl_nir_lower_buffers, prog);
                NIR_PASS_V(nir[i], nir_opt_constant_folding);

                struct panfrost_compile_inputs inputs = {
                        .gpu_id = 0x7212, /* Mali G52 */
                };
                struct pan_shader_info info;

                util_dynarray_clear(&binary);
                bifrost_compile_shader_nir(nir[i], &inputs, &binary, &info);

                if (vertex_only)
                        break;
        }

        util_dynarray_fini(&binary);
}

#define BI_FOURCC(ch0, ch1, ch2, ch3) ( \
  (uint32_t)(ch0)        | (uint32_t)(ch1) << 8 | \
  (uint32_t)(ch2) << 16  | (uint32_t)(ch3) << 24)

static void
disassemble(const char *filename, bool verbose)
{
        FILE *fp = fopen(filename, "rb");
        assert(fp);

        fseek(fp, 0, SEEK_END);
        unsigned filesize = ftell(fp);
        rewind(fp);

        uint32_t *code = malloc(filesize);
        unsigned res = fread(code, 1, filesize, fp);
        if (res != filesize) {
                printf("Couldn't read full file\n");
        }
        fclose(fp);

        if (filesize && code[0] == BI_FOURCC('M', 'B', 'S', '2')) {
                for (int i = 0; i < filesize / 4; ++i) {
                        if (code[i] != BI_FOURCC('O', 'B', 'J', 'C'))
                                continue;

                        unsigned size = code[i + 1];
                        unsigned offset = i + 2;

                        disassemble_bifrost(stdout, (uint8_t*)(code + offset), size, verbose);
                }
        } else {
                disassemble_bifrost(stdout, (uint8_t*)code, filesize, verbose);
        }

        free(code);
}

static int
bi_tests()
{
#ifndef NDEBUG
        bi_test_scheduler();
        bi_test_packing();
        bi_test_packing_formats();
        printf("Pass.\n");
        return 0;
#else
        printf("Tests omitted in release mode.");
        return 1;
#endif
}

int
main(int argc, char **argv)
{
        if (argc < 2) {
                printf("Pass a command\n");
                exit(1);
        }

        if (strcmp(argv[1], "compile") == 0)
                compile_shader(&argv[2], false);
        else if (strcmp(argv[1], "disasm") == 0)
                disassemble(argv[2], false);
        else if (strcmp(argv[1], "disasm-verbose") == 0)
                disassemble(argv[2], true);
        else if (strcmp(argv[1], "test") == 0)
                bi_tests();
        else
                unreachable("Unknown command. Valid: compile/disasm");

        return 0;
}
