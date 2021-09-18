/*
 * Copyright (C) 2021 Collabora, Ltd.
 * Copyright (C) 2019 Ryan Houdek <Sonicadvance1@gmail.com>
 * Copyright (C) 2014 Rob Clark <robclark@freedesktop.org>
 * Copyright © 2015 Red Hat
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

#include <getopt.h>
#include <string.h>
#include "disassemble.h"
#include "compiler.h"

#include "main/mtypes.h"
#include "compiler/glsl/standalone.h"
#include "compiler/glsl/glsl_to_nir.h"
#include "compiler/glsl/gl_nir.h"
#include "compiler/nir_types.h"
#include "util/u_dynarray.h"
#include "bifrost_compile.h"

unsigned gpu_id = 0x7212;
int verbose = 0;

static gl_shader_stage
filename_to_stage(const char *stage)
{
        const char *ext = strrchr(stage, '.');

        if (ext == NULL) {
                fprintf(stderr, "No extension found in %s\n", stage);
                exit(1);
        }

        if (!strcmp(ext, ".cs") || !strcmp(ext, ".comp"))
                return MESA_SHADER_COMPUTE;
        else if (!strcmp(ext, ".vs") || !strcmp(ext, ".vert"))
                return MESA_SHADER_VERTEX;
        else if (!strcmp(ext, ".fs") || !strcmp(ext, ".frag"))
                return MESA_SHADER_FRAGMENT;
        else {
                fprintf(stderr, "Invalid extension %s\n", ext);
                exit(1);
        }

        unreachable("Should've returned or bailed");
}

static int
st_packed_uniforms_type_size(const struct glsl_type *type, bool bindless)
{
   return glsl_count_dword_slots(type, bindless);
}

static int
glsl_type_size(const struct glsl_type *type, bool bindless)
{
   return glsl_count_attribute_slots(type, false);
}

static void
insert_sorted(struct exec_list *var_list, nir_variable *new_var)
{
   nir_foreach_variable_in_list (var, var_list) {
      if (var->data.location > new_var->data.location) {
         exec_node_insert_node_before(&var->node, &new_var->node);
         return;
      }
   }
   exec_list_push_tail(var_list, &new_var->node);
}

static void
sort_varyings(nir_shader *nir, nir_variable_mode mode)
{
   struct exec_list new_list;
   exec_list_make_empty(&new_list);
   nir_foreach_variable_with_modes_safe (var, nir, mode) {
      exec_node_remove(&var->node);
      insert_sorted(&new_list, var);
   }
   exec_list_append(&nir->variables, &new_list);
}

static void
fixup_varying_slots(nir_shader *nir, nir_variable_mode mode)
{
   nir_foreach_variable_with_modes (var, nir, mode) {
      if (var->data.location >= VARYING_SLOT_VAR0) {
         var->data.location += 9;
      } else if ((var->data.location >= VARYING_SLOT_TEX0) &&
                 (var->data.location <= VARYING_SLOT_TEX7)) {
         var->data.location += VARYING_SLOT_VAR0 - VARYING_SLOT_TEX0;
      }
   }
}

static void
compile_shader(int stages, char **files)
{
        struct gl_shader_program *prog;
        nir_shader *nir[MESA_SHADER_COMPUTE + 1];
        unsigned shader_types[MESA_SHADER_COMPUTE + 1];

        if (stages > MESA_SHADER_COMPUTE) {
                fprintf(stderr, "Too many stages");
                exit(1);
        }

        for (unsigned i = 0; i < stages; ++i)
                shader_types[i] = filename_to_stage(files[i]);

        struct standalone_options options = {
                .glsl_version = 300, /* ES - needed for precision */
                .do_link = true,
                .lower_precision = true
        };

        static struct gl_context local_ctx;

        prog = standalone_compile_shader(&options, stages, files, &local_ctx);

        for (unsigned i = 0; i < stages; ++i) {
                gl_shader_stage stage = shader_types[i];
                prog->_LinkedShaders[stage]->Program->info.stage = stage;
        }

        struct util_dynarray binary;

        util_dynarray_init(&binary, NULL);

        for (unsigned i = 0; i < stages; ++i) {
                nir[i] = glsl_to_nir(&local_ctx, prog, shader_types[i], &bifrost_nir_options);

                if (shader_types[i] == MESA_SHADER_VERTEX) {
                        nir_assign_var_locations(nir[i], nir_var_shader_in, &nir[i]->num_inputs,
                                        glsl_type_size);
                        sort_varyings(nir[i], nir_var_shader_out);
                        nir_assign_var_locations(nir[i], nir_var_shader_out, &nir[i]->num_outputs,
                                        glsl_type_size);
                        fixup_varying_slots(nir[i], nir_var_shader_out);
                } else if (shader_types[i] == MESA_SHADER_FRAGMENT) {
                      sort_varyings(nir[i], nir_var_shader_in);
                      nir_assign_var_locations(nir[i], nir_var_shader_in, &nir[i]->num_inputs,
                                      glsl_type_size);
                      fixup_varying_slots(nir[i], nir_var_shader_in);
                      nir_assign_var_locations(nir[i], nir_var_shader_out, &nir[i]->num_outputs,
                                      glsl_type_size);
                }

                nir_assign_var_locations(nir[i], nir_var_uniform, &nir[i]->num_uniforms,
                                glsl_type_size);

                NIR_PASS_V(nir[i], nir_lower_global_vars_to_local);
                NIR_PASS_V(nir[i], nir_lower_io_to_temporaries, nir_shader_get_entrypoint(nir[i]), true, i == 0);
                NIR_PASS_V(nir[i], nir_opt_copy_prop_vars);
                NIR_PASS_V(nir[i], nir_opt_combine_stores, nir_var_all);

                NIR_PASS_V(nir[i], nir_lower_system_values);
                NIR_PASS_V(nir[i], gl_nir_lower_samplers, prog);
                NIR_PASS_V(nir[i], nir_split_var_copies);
                NIR_PASS_V(nir[i], nir_lower_var_copies);

                NIR_PASS_V(nir[i], nir_lower_io, nir_var_uniform,
                                st_packed_uniforms_type_size,
                                (nir_lower_io_options)0);
                NIR_PASS_V(nir[i], nir_lower_uniforms_to_ubo, true, false);

                /* before buffers and vars_to_ssa */
                NIR_PASS_V(nir[i], gl_nir_lower_images, true);

                NIR_PASS_V(nir[i], gl_nir_lower_buffers, prog);
                NIR_PASS_V(nir[i], nir_opt_constant_folding);

                struct panfrost_compile_inputs inputs = {
                        .gpu_id = gpu_id,
                };
                struct pan_shader_info info = { 0 };

                util_dynarray_clear(&binary);
                bifrost_compile_shader_nir(nir[i], &inputs, &binary, &info);

                char *fn = NULL;
                asprintf(&fn, "shader_%u.bin", i);
                assert(fn != NULL);
                FILE *fp = fopen(fn, "wb");
                fwrite(binary.data, 1, binary.size, fp);
                fclose(fp);
                free(fn);
        }

        util_dynarray_fini(&binary);
}

#define BI_FOURCC(ch0, ch1, ch2, ch3) ( \
  (uint32_t)(ch0)        | (uint32_t)(ch1) << 8 | \
  (uint32_t)(ch2) << 16  | (uint32_t)(ch3) << 24)

static void
disassemble(const char *filename)
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

int
main(int argc, char **argv)
{
        int c;

        if (argc < 2) {
                printf("Pass a command\n");
                exit(1);
        }

        static struct option longopts[] = {
                { "id", optional_argument, NULL, 'i' },
                { "gpu", optional_argument, NULL, 'g' },
                { "verbose", no_argument, &verbose, 'v' },
                { NULL, 0, NULL, 0 }
        };

        static struct {
                const char *name;
                unsigned major, minor;
        } gpus[] = {
                { "G71",   6, 0 },
                { "G72",   6, 2 },
                { "G51",   7, 0 },
                { "G76",   7, 1 },
                { "G52",   7, 2 },
                { "G31",   7, 3 },
                { "G77",   9, 0 },
                { "G57",   9, 1 },
                { "G78",   9, 2 },
                { "G57",   9, 3 },
                { "G68",   9, 4 },
                { "G78AE", 9, 5 },
        };

        while ((c = getopt_long(argc, argv, "v:", longopts, NULL)) != -1) {

                switch (c) {
                case 'i':
                        gpu_id = atoi(optarg);

                        if (!gpu_id) {
                                fprintf(stderr, "Expected GPU ID, got %s\n", optarg);
                                return 1;
                        }

                        break;
                case 'g':
                        gpu_id = 0;

                        /* Compatibility with the Arm compiler */
                        if (strncmp(optarg, "Mali-", 5) == 0) optarg += 5;

                        for (unsigned i = 0; i < ARRAY_SIZE(gpus); ++i) {
                                if (strcmp(gpus[i].name, optarg)) continue;

                                unsigned major = gpus[i].major;
                                unsigned minor = gpus[i].minor;

                                gpu_id = (major << 12) | (minor << 8);
                                break;
                        }

                        if (!gpu_id) {
                                fprintf(stderr, "Unknown GPU %s\n", optarg);
                                return 1;
                        }

                        break;
                default:
                        break;
                }
        }

        if (strcmp(argv[optind], "compile") == 0)
                compile_shader(argc - optind - 1, &argv[optind + 1]);
        else if (strcmp(argv[optind], "disasm") == 0)
                disassemble(argv[optind + 1]);
        else {
                fprintf(stderr, "Unknown command. Valid: compile/disasm\n");
                return 1;
        }

        return 0;
}
