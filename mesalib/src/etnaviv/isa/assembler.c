/*
 * Copyright © 2024 Igalia S.L.
 * SPDX-License-Identifier: MIT
 */

#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <unistd.h>

#include "asm.h"
#include "isa.h"

#include "util/u_dynarray.h"

#include <etnaviv/isa/etnaviv-isa.h>

struct encoded_instr {
   uint32_t word[4];
};

static void
pre_instr_cb(void *d, unsigned n, void *instr)
{
   uint32_t *dwords = (uint32_t *)instr;
   printf("%03d [%08x %08x %08x %08x] ", n, dwords[0], dwords[1], dwords[2], dwords[3]);
}

static void
store(const char *filename, void *data, unsigned size)
{
   int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
   if (fd == -1) {
      fprintf(stderr, "Error opening file (%s)", filename);
      return;
   }

   ssize_t bytes_written = write(fd, data, size);
   if (bytes_written == -1) {
      fprintf(stderr, "Error writing to file");
      close(fd);
      return;
   }

   close(fd);
}

static void
print_usage()
{
   printf("Usage: etnaviv-assembler -i FILE -o FILE -s\n");
}

int
main(int argc, char *argv[])
{
   bool show_disasm = false;
   bool dual_16_mode = false;
   const char *in = NULL;
   const char *out = NULL;

   int opt = 0;
   while ((opt = getopt(argc, argv, "i:o:sd")) != -1) {
      switch (opt) {
      case 'i':
         in = optarg;
         break;
      case 'o':
         out = optarg;
         break;
      case 's':
         show_disasm = true;
         break;
      case 'd':
         dual_16_mode = true;
         break;
      default:
         print_usage();
         exit(EXIT_FAILURE);
      }
   }

   if (!in || !out) {
      print_usage();

      return EXIT_FAILURE;
   }

   struct etna_asm_result *result = isa_parse_file(in, dual_16_mode);

   if (!result->success) {
      fprintf(stderr, "Failed to parse %s\n%s\n", in, result->error);
      isa_asm_result_destroy(result);

      return EXIT_FAILURE;
   }

   struct util_dynarray bin;
   util_dynarray_init(&bin, NULL);

   for (unsigned int i = 0; i < result->num_instr; i++) {
      struct encoded_instr encoded;

      isa_assemble_instruction(encoded.word, &result->instr[i]);
      util_dynarray_append(&bin, struct encoded_instr, encoded);
   }

   unsigned int num = util_dynarray_num_elements(&bin, struct encoded_instr);
   unsigned int size = num * sizeof(struct encoded_instr);
   void *data = util_dynarray_begin(&bin);

   store(out, data, size);

   if (show_disasm) {
      static struct isa_decode_options options = {
         .show_errors = true,
         .branch_labels = true,
         .pre_instr_cb = pre_instr_cb,
      };

      etnaviv_isa_disasm(data, size, stdout, &options);
   }

   util_dynarray_fini(&bin);

   return EXIT_SUCCESS;
}
