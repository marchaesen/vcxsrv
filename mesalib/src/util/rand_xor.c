/*
 * Copyright 2017 Timothy Arceri
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
 *
 */

#if defined(__linux__)
#include <sys/file.h>
#include <unistd.h>
#include <fcntl.h>
#else
#include <time.h>
#endif

#include "rand_xor.h"

/* Super fast random number generator.
 *
 * This rand_xorshift128plus function by Sebastiano Vigna belongs
 * to the public domain.
 */
uint64_t
rand_xorshift128plus(uint64_t *seed)
{
   uint64_t *s = seed;

   uint64_t s1 = s[0];
   const uint64_t s0 = s[1];
   s[0] = s0;
   s1 ^= s1 << 23;
   s[1] = s1 ^ s0 ^ (s1 >> 18) ^ (s0 >> 5);

   return s[1] + s0;
}

void
s_rand_xorshift128plus(uint64_t *seed, bool randomised_seed)
{
   if (!randomised_seed)
      goto fixed_seed;

#if defined(__linux__)
   int fd = open("/dev/urandom", O_RDONLY);
   if (fd < 0)
      goto fixed_seed;

   size_t seed_size = sizeof(uint64_t) * 2;
   if (read(fd, seed, seed_size) != seed_size) {
      close(fd);
      goto fixed_seed;
   }

   close(fd);
   return;

#else
   seed[0] = 0x3bffb83978e24f88;
   seed[1] = time(NULL);

   return;
#endif

fixed_seed:

   /* Fallback to a fixed seed */
   seed[0] = 0x3bffb83978e24f88;
   seed[1] = 0x9238d5d56c71cd35;
}
