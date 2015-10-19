/* Copyright © 2007 Carl Worth
 * Copyright © 2009 Jeremy Huddleston, Julien Cristau, and Matthieu Herrb
 * Copyright © 2009-2010 Mikhail Gusarov
 * Copyright © 2012 Yaakov Selkowitz and Keith Packard
 * Copyright © 2014 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "mesa-sha1.h"

#ifdef HAVE_SHA1

#if defined(HAVE_SHA1_IN_LIBMD)  /* Use libmd for SHA1 */ \
	|| defined(HAVE_SHA1_IN_LIBC)   /* Use libc for SHA1 */

#include <sha1.h>

struct mesa_sha1 *
_mesa_sha1_init(void)
{
   SHA1_CTX *ctx = malloc(sizeof(*ctx));

   if (!ctx)
      return NULL;

   SHA1Init(ctx);
   return (struct mesa_sha1 *) ctx;
}

int
_mesa_sha1_update(struct mesa_sha1 *ctx, const void *data, int size)
{
   SHA1_CTX *sha1_ctx = (SHA1_CTX *) ctx;

   SHA1Update(sha1_ctx, data, size);
   return 1;
}

int
_mesa_sha1_final(struct mesa_sha1 *ctx, unsigned char result[20])
{
   SHA1_CTX *sha1_ctx = (SHA1_CTX *) ctx;

   SHA1Final(result, sha1_ctx);
   free(sha1_ctx);
   return 1;
}

#elif defined(HAVE_SHA1_IN_COMMONCRYPTO)        /* Use CommonCrypto for SHA1 */

#include <CommonCrypto/CommonDigest.h>

struct mesa_sha1 *
_mesa_sha1_init(void)
{
   CC_SHA1_CTX *ctx = malloc(sizeof(*ctx));

   if (!ctx)
      return NULL;

   CC_SHA1_Init(ctx);
   return (struct mesa_sha1 *) ctx;
}

int
_mesa_sha1_update(struct mesa_sha1 *ctx, const void *data, int size)
{
   CC_SHA1_CTX *sha1_ctx = (CC_SHA1_CTX *) ctx;

   CC_SHA1_Update(sha1_ctx, data, size);
   return 1;
}

int
_mesa_sha1_final(struct mesa_sha1 *ctx, unsigned char result[20])
{
   CC_SHA1_CTX *sha1_ctx = (CC_SHA1_CTX *) ctx;

   CC_SHA1_Final(result, sha1_ctx);
   free(sha1_ctx);
   return 1;
}

#elif defined(HAVE_SHA1_IN_CRYPTOAPI)        /* Use CryptoAPI for SHA1 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>

static HCRYPTPROV hProv;

struct mesa_sha1 *
_mesa_sha1_init(void)
{
   HCRYPTHASH *ctx = malloc(sizeof(*ctx));

   if (!ctx)
      return NULL;

   CryptAcquireContext(&hProv, NULL, MS_DEF_PROV, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT);
   CryptCreateHash(hProv, CALG_SHA1, 0, 0, ctx);
   return (struct mesa_sha1 *) ctx;
}

int
_mesa_sha1_update(struct mesa_sha1 *ctx, const void *data, int size)
{
   HCRYPTHASH *hHash = (HCRYPTHASH *) ctx;

   CryptHashData(*hHash, data, size, 0);
   return 1;
}

int
_mesa_sha1_final(struct mesa_sha1 *ctx, unsigned char result[20])
{
   HCRYPTHASH *hHash = (HCRYPTHASH *) ctx;
   DWORD len = 20;

   CryptGetHashParam(*hHash, HP_HASHVAL, result, &len, 0);
   CryptDestroyHash(*hHash);
   CryptReleaseContext(hProv, 0);
   free(ctx);
   return 1;
}

#elif defined(HAVE_SHA1_IN_LIBNETTLE)   /* Use libnettle for SHA1 */

#include <nettle/sha.h>

struct mesa_sha1 *
_mesa_sha1_init(void)
{
   struct sha1_ctx *ctx = malloc(sizeof(*ctx));

   if (!ctx)
      return NULL;
   sha1_init(ctx);
   return (struct mesa_sha1 *) ctx;
}

int
_mesa_sha1_update(struct mesa_sha1 *ctx, const void *data, int size)
{
   sha1_update((struct sha1_ctx *) ctx, size, data);
   return 1;
}

int
_mesa_sha1_final(struct mesa_sha1 *ctx, unsigned char result[20])
{
   sha1_digest((struct sha1_ctx *) ctx, 20, result);
   free(ctx);
   return 1;
}

#elif defined(HAVE_SHA1_IN_LIBGCRYPT)   /* Use libgcrypt for SHA1 */

#include <gcrypt.h>

struct mesa_sha1 *
_mesa_sha1_init(void)
{
   static int init;
   gcry_md_hd_t h;
   gcry_error_t err;

   if (!init) {
      if (!gcry_check_version(NULL))
         return NULL;
      gcry_control(GCRYCTL_DISABLE_SECMEM, 0);
      gcry_control(GCRYCTL_INITIALIZATION_FINISHED, 0);
      init = 1;
   }

   err = gcry_md_open(&h, GCRY_MD_SHA1, 0);
   if (err)
      return NULL;
   return (struct mesa_sha1 *) h;
}

int
_mesa_sha1_update(struct mesa_sha1 *ctx, const void *data, int size)
{
   gcry_md_hd_t h = (gcry_md_hd_t) ctx;

   gcry_md_write(h, data, size);
   return 1;
}

int
_mesa_sha1_final(struct mesa_sha1 *ctx, unsigned char result[20])
{
   gcry_md_hd_t h = (gcry_md_hd_t) ctx;

   memcpy(result, gcry_md_read(h, GCRY_MD_SHA1), 20);
   gcry_md_close(h);
   return 1;
}

#elif defined(HAVE_SHA1_IN_LIBSHA1)     /* Use libsha1 */

#include <libsha1.h>

struct mesa_sha1 *
_mesa_sha1_init(void)
{
   sha1_ctx *ctx = malloc(sizeof(*ctx));

   if (!ctx)
      return NULL;
   sha1_begin(ctx);
   return (struct mesa_sha1 *) ctx;
}

int
_mesa_sha1_update(struct mesa_sha1 *ctx, const void *data, int size)
{
   sha1_hash(data, size, (sha1_ctx *) ctx);
   return 1;
}

int
_mesa_sha1_final(struct mesa_sha1 *ctx, unsigned char result[20])
{
   sha1_end(result, (sha1_ctx *) ctx);
   free(ctx);
   return 1;
}

#else                           /* Use OpenSSL's libcrypto */

#include <stddef.h>             /* buggy openssl/sha.h wants size_t */
#include <openssl/sha.h>

struct mesa_sha1 *
_mesa_sha1_init(void)
{
   int ret;
   SHA_CTX *ctx = malloc(sizeof(*ctx));

   if (!ctx)
      return NULL;
   ret = SHA1_Init(ctx);
   if (!ret) {
      free(ctx);
      return NULL;
   }
   return (struct mesa_sha1 *) ctx;
}

int
_mesa_sha1_update(struct mesa_sha1 *ctx, const void *data, int size)
{
   int ret;
   SHA_CTX *sha_ctx = (SHA_CTX *) ctx;

   ret = SHA1_Update(sha_ctx, data, size);
   if (!ret)
      free(sha_ctx);
   return ret;
}

int
_mesa_sha1_final(struct mesa_sha1 *ctx, unsigned char result[20])
{
   int ret;
   SHA_CTX *sha_ctx = (SHA_CTX *) ctx;

   ret = SHA1_Final(result, (SHA_CTX *) sha_ctx);
   free(sha_ctx);
   return ret;
}

#endif

void
_mesa_sha1_compute(const void *data, size_t size, unsigned char result[20])
{
   struct mesa_sha1 *ctx;

   ctx = _mesa_sha1_init();
   _mesa_sha1_update(ctx, data, size);
   _mesa_sha1_final(ctx, result);
}

char *
_mesa_sha1_format(char *buf, const unsigned char *sha1)
{
   static const char hex_digits[] = "0123456789abcdef";
   int i;

   for (i = 0; i < 40; i += 2) {
      buf[i] = hex_digits[sha1[i >> 1] >> 4];
      buf[i + 1] = hex_digits[sha1[i >> 1] & 0x0f];
   }
   buf[i] = '\0';

   return buf;
}

#endif
