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

#define WORD_SIZE 4

int main(int argc, char **argv)
{
   int fd = open(argv[1], O_RDONLY);
   if (fd < 0)
   {
      fprintf(stderr, "Failed to open %s\n", argv[1]);
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

   nir_function *func = spirv_to_nir(map, word_count, NULL, 0,
                                     MESA_SHADER_FRAGMENT, "main", NULL, NULL);
   nir_print_shader(func->shader, stderr);

   return 0;
}
