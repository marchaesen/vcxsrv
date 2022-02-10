/*
 * Copyright © 2015 Intel Corporation
 * Copyright © Microsoft Corporation
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

/*
 * A simple executable that opens a SPIR-V shader, converts it to DXIL via
 * NIR, and dumps out the result.  This should be useful for testing the
 * nir_to_dxil code.  Based on spirv2nir.c.
 */

#include "nir_to_dxil.h"
#include "dxil_validation.h"
#include "spirv/nir_spirv.h"
#include "spirv_to_dxil.h"

#include "util/os_file.h"
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <string.h>

#define WORD_SIZE 4

static gl_shader_stage
stage_to_enum(char *stage)
{
   if (!strcmp(stage, "vertex"))
      return MESA_SHADER_VERTEX;
   else if (!strcmp(stage, "tess-ctrl"))
      return MESA_SHADER_TESS_CTRL;
   else if (!strcmp(stage, "tess-eval"))
      return MESA_SHADER_TESS_EVAL;
   else if (!strcmp(stage, "geometry"))
      return MESA_SHADER_GEOMETRY;
   else if (!strcmp(stage, "fragment"))
      return MESA_SHADER_FRAGMENT;
   else if (!strcmp(stage, "compute"))
      return MESA_SHADER_COMPUTE;
   else if (!strcmp(stage, "kernel"))
      return MESA_SHADER_KERNEL;
   else
      return MESA_SHADER_NONE;
}

int
main(int argc, char **argv)
{
   gl_shader_stage shader_stage = MESA_SHADER_FRAGMENT;
   char *entry_point = "main";
   char *output_file = "";
   int ch;
   bool validate = false;
   
   static struct option long_options[] = {
      {"stage", required_argument, 0, 's'},
      {"entry", required_argument, 0, 'e'},
      {"output", required_argument, 0, 'o'},
      {"validate", no_argument, 0, 'v'},
      {0, 0, 0, 0}};


   while ((ch = getopt_long(argc, argv, "s:e:o:v", long_options, NULL)) !=
          -1) {
      switch(ch)
      {
      case 's':
         shader_stage = stage_to_enum(optarg);
         if (shader_stage == MESA_SHADER_NONE) {
            fprintf(stderr, "Unknown stage %s\n", optarg);
            return 1;
         }
         break;
      case 'e':
         entry_point = optarg;
         break;
      case 'o':
         output_file = optarg;
         break;
      case 'v':
         validate = true;
         break;
      default:
         fprintf(stderr, "Unrecognized option.\n");
         return 1;
      }
   }

   if (optind != argc - 1) {
      if (optind < argc)
         fprintf(stderr, "Please specify only one input file.");
      else
         fprintf(stderr, "Please specify an input file.");
      return 1;
   }

   const char *filename = argv[optind];
   size_t file_size;
   char *file_contents = os_read_file(filename, &file_size);
   if (!file_contents) {
      fprintf(stderr, "Failed to open %s\n", filename);
      return 1;
   }

   if (file_size % WORD_SIZE != 0) {
      fprintf(stderr, "%s size == %zu is not a multiple of %d\n", filename,
              file_size, WORD_SIZE);
      return 1;
   }

   size_t word_count = file_size / WORD_SIZE;

   struct dxil_spirv_runtime_conf conf;
   memset(&conf, 0, sizeof(conf));
   conf.runtime_data_cbv.base_shader_register = 0;
   conf.runtime_data_cbv.register_space = 31;
   conf.zero_based_vertex_instance_id = true;

   struct dxil_spirv_debug_options dbg_opts = {
      .dump_nir = false,
   };

   struct dxil_spirv_object obj;
   memset(&obj, 0, sizeof(obj));
   if (spirv_to_dxil((uint32_t *)file_contents, word_count, NULL, 0,
                     (dxil_spirv_shader_stage)shader_stage, entry_point,
                     &dbg_opts, &conf, &obj)) {

      if (validate && !validate_dxil(&obj)) {
         fprintf(stderr, "Failed to validate DXIL\n");
         spirv_to_dxil_free(&obj);
         free(file_contents);
         return 1;
      }

      FILE *file = fopen(output_file, "wb");
      if (!file) {
         fprintf(stderr, "Failed to open %s, %s\n", output_file,
                 strerror(errno));
         spirv_to_dxil_free(&obj);
         free(file_contents);
         return 1;
      }

      fwrite(obj.binary.buffer, sizeof(char), obj.binary.size, file);
      fclose(file);
      spirv_to_dxil_free(&obj);
   } else {
      fprintf(stderr, "Compilation failed\n");
      return 1;
   }

   free(file_contents);

   return 0;
}
