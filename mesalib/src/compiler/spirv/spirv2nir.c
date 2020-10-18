/*
 * Copyright © 2015 Intel Corporation
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
 *
 * Authors:
 *    Jason Ekstrand (jason@jlekstrand.net)
 *
 */

/*
 * A simple executable that opens a SPIR-V shader, converts it to NIR, and
 * dumps out the result.  This should be useful for testing the
 * spirv_to_nir code.
 */

#include "spirv/nir_spirv.h"

#include <sys/mman.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <getopt.h>

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

static void
print_usage(char *exec_name, FILE *f)
{
   fprintf(f,
"Usage: %s [options] file\n"
"Options:\n"
"  -h  --help              Print this help.\n"
"  -s, --stage <stage>     Specify the shader stage.  Valid stages are:\n"
"                          vertex, tess-ctrl, tess-eval, geometry, fragment,\n"
"                          compute, and kernel (OpenCL-style compute).\n"
"  -e, --entry <name>      Specify the entry-point name.\n"
   , exec_name);
}

int main(int argc, char **argv)
{
   gl_shader_stage shader_stage = MESA_SHADER_FRAGMENT;
   char *entry_point = "main";
   int ch;

   static struct option long_options[] =
     {
       {"help",         no_argument, 0, 'h'},
       {"stage",  required_argument, 0, 's'},
       {"entry",  required_argument, 0, 'e'},
       {0, 0, 0, 0}
     };

   while ((ch = getopt_long(argc, argv, "hs:e:", long_options, NULL)) != -1)
   {
      switch (ch)
      {
      case 'h':
         print_usage(argv[0], stdout);
         return 0;
      case 's':
         shader_stage = stage_to_enum(optarg);
         if (shader_stage == MESA_SHADER_NONE)
         {
            fprintf(stderr, "Unknown stage \"%s\"\n", optarg);
            print_usage(argv[0], stderr);
            return 1;
         }
         break;
      case 'e':
         entry_point = optarg;
         break;
      default:
         fprintf(stderr, "Unrecognized option \"%s\".\n", optarg);
         print_usage(argv[0], stderr);
         return 1;
      }
   }

   const char *filename = argv[optind];
   int fd = open(filename, O_RDONLY);
   if (fd < 0)
   {
      fprintf(stderr, "Failed to open %s\n", filename);
      return 1;
   }

   off_t len = lseek(fd, 0, SEEK_END);
   if (len % WORD_SIZE != 0)
   {
      fprintf(stderr, "File length isn't a multiple of the word size\n");
      fprintf(stderr, "Are you sure this is a valid SPIR-V shader?\n");
      close(fd);
      return 1;
   }

   size_t word_count = len / WORD_SIZE;

   const void *map = mmap(NULL, len, PROT_READ, MAP_PRIVATE, fd, 0);
   if (map == MAP_FAILED)
   {
      fprintf(stderr, "Failed to mmap the file: errno=%d, %s\n",
              errno, strerror(errno));
      close(fd);
      return 1;
   }

   glsl_type_singleton_init_or_ref();

   struct spirv_to_nir_options spirv_opts = {0};
   if (shader_stage == MESA_SHADER_KERNEL) {
      spirv_opts.environment = NIR_SPIRV_OPENCL;
      spirv_opts.caps.address = true;
      spirv_opts.caps.float64 = true;
      spirv_opts.caps.int8 = true;
      spirv_opts.caps.int16 = true;
      spirv_opts.caps.int64 = true;
      spirv_opts.caps.kernel = true;
   }

   nir_shader *nir = spirv_to_nir(map, word_count, NULL, 0,
                                  shader_stage, entry_point,
                                  &spirv_opts, NULL);

   if (nir)
      nir_print_shader(nir, stderr);
   else
      fprintf(stderr, "SPIRV to NIR compilation failed\n");

   glsl_type_singleton_decref();

   return 0;
}
