/*
 * Copyright 2023 Alyssa Rosenzweig
 * Copyright 2020 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "compiler/clc/clc.h"
#include "util/hash_table.h"
#include "util/set.h"
#include "util/u_dynarray.h"

#include <getopt.h>
#include <inttypes.h>
#include <stdio.h>

/* Shader functions */
#define SPIR_V_MAGIC_NUMBER 0x07230203

static void
msg_callback(void *priv, const char *msg)
{
   (void)priv;
   fprintf(stderr, "%s", msg);
}

static void
print_usage(char *exec_name, FILE *f)
{
   fprintf(
      f,
      "Usage: %s [options] -- [clang args]\n"
      "Options:\n"
      "  -h  --help              Print this help.\n"
      "  -o, --out <filename>    Specify the output filename.\n"
      "  -i, --in <filename>     Specify one input filename. Accepted multiple times.\n"
      "  -v, --verbose           Print more information during compilation.\n",
      exec_name);
}

static uint32_t
get_module_spirv_version(const uint32_t *spirv, size_t size)
{
   assert(size >= 8);
   assert(spirv[0] == SPIR_V_MAGIC_NUMBER);
   return spirv[1];
}

static void
set_module_spirv_version(uint32_t *spirv, size_t size, uint32_t version)
{
   assert(size >= 8);
   assert(spirv[0] == SPIR_V_MAGIC_NUMBER);
   spirv[1] = version;
}

int
main(int argc, char **argv)
{
   static struct option long_options[] = {
      {"help", no_argument, 0, 'h'},
      {"in", required_argument, 0, 'i'},
      {"out", required_argument, 0, 'o'},
      {"depfile", required_argument, 0, 'd'},
      {"verbose", no_argument, 0, 'v'},
      {0, 0, 0, 0},
   };

   char *outfile = NULL, *depfile = NULL;
   struct util_dynarray clang_args;
   struct util_dynarray input_files;
   struct util_dynarray spirv_objs;
   struct util_dynarray spirv_ptr_objs;

   void *mem_ctx = ralloc_context(NULL);

   util_dynarray_init(&clang_args, mem_ctx);
   util_dynarray_init(&input_files, mem_ctx);
   util_dynarray_init(&spirv_objs, mem_ctx);
   util_dynarray_init(&spirv_ptr_objs, mem_ctx);

   struct set *deps =
      _mesa_set_create(mem_ctx, _mesa_hash_string, _mesa_key_string_equal);

   int ch;
   while ((ch = getopt_long(argc, argv, "he:i:o:d:v", long_options, NULL)) !=
          -1) {
      switch (ch) {
      case 'h':
         print_usage(argv[0], stdout);
         return 0;
      case 'o':
         outfile = optarg;
         break;
      case 'd':
         depfile = optarg;
         break;
      case 'i':
         util_dynarray_append(&input_files, char *, optarg);
         break;
      default:
         fprintf(stderr, "Unrecognized option \"%s\".\n", optarg);
         print_usage(argv[0], stderr);
         return 1;
      }
   }

   for (int i = optind; i < argc; i++) {
      util_dynarray_append(&clang_args, char *, argv[i]);
   }

   if (util_dynarray_num_elements(&input_files, char *) == 0) {
      fprintf(stderr, "No input file(s).\n");
      print_usage(argv[0], stderr);
      return -1;
   }

   if (outfile == NULL) {
      fprintf(stderr, "No output specified.\n");
      print_usage(argv[0], stderr);
      return -1;
   }

   struct clc_logger logger = {
      .error = msg_callback,
      .warning = msg_callback,
   };

   util_dynarray_foreach(&input_files, char *, infile) {
      FILE *fp = fopen(*infile, "rb");
      if (!fp) {
         fprintf(stderr, "Failed to open %s\n", *infile);
         ralloc_free(mem_ctx);
         return 1;
      }

      fseek(fp, 0L, SEEK_END);
      size_t len = ftell(fp);
      rewind(fp);

      char *map = ralloc_array_size(mem_ctx, 1, len + 1);
      if (!map) {
         fprintf(stderr, "Failed to allocate");
         ralloc_free(mem_ctx);
         return 1;
      }

      fread(map, 1, len, fp);
      map[len] = 0;
      fclose(fp);

      struct clc_compile_args clc_args = {
         .source.name = *infile,
         .source.value = map,
         .args = util_dynarray_begin(&clang_args),
         .num_args = util_dynarray_num_elements(&clang_args, char *),
      };

      /* Enable all features, we don't know the target here and it is the
       * responsibility of the driver to only use features they will actually
       * support. Not our job to blow up here.
       */
      memset(&clc_args.features, true, sizeof(clc_args.features));

      struct clc_binary *spirv_out =
         util_dynarray_grow(&spirv_objs, struct clc_binary, 1);

      if (!clc_compile_c_to_spirv(&clc_args, &logger, spirv_out, deps)) {
         ralloc_free(mem_ctx);
         return 1;
      }
   }


   util_dynarray_foreach(&spirv_objs, struct clc_binary, p) {
      util_dynarray_append(&spirv_ptr_objs, struct clc_binary *, p);
   }

   /* The SPIRV-Tools linker started checking that all modules have the same
    * version. But SPIRV-LLVM-Translator picks the lower required version for
    * each module it compiles. So we have to iterate over all of them and set
    * the max found to make SPIRV-Tools link our modules.
    *
    * TODO: This is not the correct thing to do. We need SPIRV-LLVM-Translator
    *       to pick a given SPIRV version given to it and have all the modules
    *       at that version. We should remove this hack when this issue is
    *       fixed :
    *       https://github.com/KhronosGroup/SPIRV-LLVM-Translator/issues/1445
    */
   uint32_t max_spirv_version = 0;
   util_dynarray_foreach(&spirv_ptr_objs, struct clc_binary *, module) {
      max_spirv_version =
         MAX2(max_spirv_version,
              get_module_spirv_version((*module)->data, (*module)->size));
   }

   assert(max_spirv_version > 0);
   util_dynarray_foreach(&spirv_ptr_objs, struct clc_binary *, module) {
      set_module_spirv_version((*module)->data, (*module)->size,
                               max_spirv_version);
   }

   struct clc_linker_args link_args = {
      .in_objs = util_dynarray_begin(&spirv_ptr_objs),
      .num_in_objs =
         util_dynarray_num_elements(&spirv_ptr_objs, struct clc_binary *),
      .create_library = true,
   };
   struct clc_binary final_spirv;
   if (!clc_link_spirv(&link_args, &logger, &final_spirv)) {
      ralloc_free(mem_ctx);
      return 1;
   }

   FILE *fp = fopen(outfile, "w");
   fwrite(final_spirv.data, final_spirv.size, 1, fp);
   fclose(fp);

   if (depfile) {
      FILE *fp = fopen(depfile, "w");
      fprintf(fp, "%s:", outfile);
      set_foreach(deps, ent) {
         fprintf(fp, " %s", (const char *)ent->key);
      }
      fprintf(fp, "\n");
      fclose(fp);
   }

   util_dynarray_foreach(&spirv_objs, struct clc_binary, p) {
      clc_free_spirv(p);
   }

   clc_free_spirv(&final_spirv);
   ralloc_free(mem_ctx);

   return 0;
}
