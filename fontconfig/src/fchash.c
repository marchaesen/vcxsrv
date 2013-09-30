/*
 * fontconfig/src/fchash.c
 *
 * Copyright © 2003 Keith Packard
 * Copyright © 2013 Red Hat, Inc.
 * Red Hat Author(s): Akira TAGOH
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of the author(s) not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission.  The authors make no
 * representations about the suitability of this software for any purpose.  It
 * is provided "as is" without express or implied warranty.
 *
 * THE AUTHOR(S) DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */
#include "fcint.h"
#include <stdio.h>
#include <string.h>

#define ROTRN(w, v, n)	((((FcChar32)v) >> n) | (((FcChar32)v) << (w - n)))
#define ROTR32(v, n)	ROTRN(32, v, n)
#define SHR(v, n)	(v >> n)
#define Ch(x, y, z)	((x & y) ^ (~x & z))
#define Maj(x, y, z)	((x & y) ^ (x & z) ^ (y & z))
#define SS0(x)		(ROTR32(x, 2) ^ ROTR32(x, 13) ^ ROTR32(x, 22))
#define SS1(x)		(ROTR32(x, 6) ^ ROTR32(x, 11) ^ ROTR32(x, 25))
#define ss0(x)		(ROTR32(x, 7) ^ ROTR32(x, 18) ^ SHR(x, 3))
#define ss1(x)		(ROTR32(x, 17) ^ ROTR32(x, 19) ^ SHR(x, 10))


static FcChar32 *
FcHashInitSHA256Digest (void)
{
    int i;
    static const FcChar32 h[] = {
	0x6a09e667UL, 0xbb67ae85UL, 0x3c6ef372UL, 0xa54ff53aUL,
	0x510e527fUL, 0x9b05688cUL, 0x1f83d9abUL, 0x5be0cd19UL
    };
    FcChar32 *ret = malloc (sizeof (FcChar32) * 8);

    if (!ret)
	return NULL;

    for (i = 0; i < 8; i++)
	ret[i] = h[i];

    return ret;
}

static void
FcHashComputeSHA256Digest (FcChar32   *hash,
			   const char *block)
{
    static const FcChar32 k[] = {
	0x428a2f98UL, 0x71374491UL, 0xb5c0fbcfUL, 0xe9b5dba5UL,
	0x3956c25bUL, 0x59f111f1UL, 0x923f82a4UL, 0xab1c5ed5UL,
	0xd807aa98UL, 0x12835b01UL, 0x243185beUL, 0x550c7dc3UL,
	0x72be5d74UL, 0x80deb1feUL, 0x9bdc06a7UL, 0xc19bf174UL,
	0xe49b69c1UL, 0xefbe4786UL, 0x0fc19dc6UL, 0x240ca1ccUL,
	0x2de92c6fUL, 0x4a7484aaUL, 0x5cb0a9dcUL, 0x76f988daUL,
	0x983e5152UL, 0xa831c66dUL, 0xb00327c8UL, 0xbf597fc7UL,
	0xc6e00bf3UL, 0xd5a79147UL, 0x06ca6351UL, 0x14292967UL,
	0x27b70a85UL, 0x2e1b2138UL, 0x4d2c6dfcUL, 0x53380d13UL,
	0x650a7354UL, 0x766a0abbUL, 0x81c2c92eUL, 0x92722c85UL,
	0xa2bfe8a1UL, 0xa81a664bUL, 0xc24b8b70UL, 0xc76c51a3UL,
	0xd192e819UL, 0xd6990624UL, 0xf40e3585UL, 0x106aa070UL,
	0x19a4c116UL, 0x1e376c08UL, 0x2748774cUL, 0x34b0bcb5UL,
	0x391c0cb3UL, 0x4ed8aa4aUL, 0x5b9cca4fUL, 0x682e6ff3UL,
	0x748f82eeUL, 0x78a5636fUL, 0x84c87814UL, 0x8cc70208UL,
	0x90befffaUL, 0xa4506cebUL, 0xbef9a3f7UL, 0xc67178f2UL
    };
    FcChar32 w[64], i, j, t1, t2;
    FcChar32 a, b, c, d, e, f, g, h;

#define H(n)	(hash[n])

    a = H(0);
    b = H(1);
    c = H(2);
    d = H(3);
    e = H(4);
    f = H(5);
    g = H(6);
    h = H(7);

    for (i = 0; i < 16; i++)
    {
	j =  (block[(i * 4) + 0] & 0xff) << (8 * 3);
	j |= (block[(i * 4) + 1] & 0xff) << (8 * 2);
	j |= (block[(i * 4) + 2] & 0xff) << (8 * 1);
	j |= (block[(i * 4) + 3] & 0xff);
	w[i] = j;
    }
    for (i = 16; i < 64; i++)
	w[i] = ss1(w[i - 2]) + w[i - 7] + ss0(w[i - 15]) + w[i - 16];

    for (i = 0; i < 64; i++)
    {
	t1 = h + SS1(e) + Ch(e, f, g) + k[i] + w[i];
	t2 = SS0(a) + Maj(a, b, c);
	h = g;
	g = f;
	f = e;
	e = d + t1;
	d = c;
	c = b;
	b = a;
	a = t1 + t2;
    }

    H(0) += a;
    H(1) += b;
    H(2) += c;
    H(3) += d;
    H(4) += e;
    H(5) += f;
    H(6) += g;
    H(7) += h;

#undef H
}

static FcChar8 *
FcHashSHA256ToString (FcChar32 *hash)
{
    FcChar8 *ret = NULL;
    static const char hex[] = "0123456789abcdef";
    int i, j;

    if (hash)
    {
	ret = malloc (sizeof (FcChar8) * (8 * 8 + 7 + 1));
	if (!ret)
	    return NULL;
	memcpy (ret, "sha256:", 7);
#define H(n)	hash[n]
	for (i = 0; i < 8; i++)
	{
	    FcChar32 v = H(i);

	    for (j = 0; j < 8; j++)
		ret[7 + (i * 8) + j] = hex[(v >> (28 - j * 4)) & 0xf];
	}
	ret[7 + i * 8] = 0;
#undef H
	free (hash);
    }

    return ret;
}

FcChar8 *
FcHashGetSHA256Digest (const FcChar8 *input_strings,
		       size_t         len)
{
    size_t i, round_len = len / 64;
    char block[64];
    FcChar32 *ret = FcHashInitSHA256Digest ();

    if (!ret)
	return NULL;

    for (i = 0; i < round_len; i++)
    {
	FcHashComputeSHA256Digest (ret, (const char *)&input_strings[i * 64]);
    }
    /* padding */
    if ((len % 64) != 0)
	memcpy (block, &input_strings[len / 64], len % 64);
    memset (&block[len % 64], 0, 64 - (len % 64));
    block[len % 64] = 0x80;
    if ((64 - (len % 64)) < 9)
    {
	/* process a block once */
	FcHashComputeSHA256Digest (ret, block);
	memset (block, 0, 64);
    }
    /* set input size at the end */
    len *= 8;
    block[63 - 0] =  (uint64_t)len        & 0xff;
    block[63 - 1] = ((uint64_t)len >>  8) & 0xff;
    block[63 - 2] = ((uint64_t)len >> 16) & 0xff;
    block[63 - 3] = ((uint64_t)len >> 24) & 0xff;
    block[63 - 4] = ((uint64_t)len >> 32) & 0xff;
    block[63 - 5] = ((uint64_t)len >> 40) & 0xff;
    block[63 - 6] = ((uint64_t)len >> 48) & 0xff;
    block[63 - 7] = ((uint64_t)len >> 56) & 0xff;
    FcHashComputeSHA256Digest (ret, block);

    return FcHashSHA256ToString (ret);
}

FcChar8 *
FcHashGetSHA256DigestFromFile (const FcChar8 *filename)
{
    FILE *fp = fopen ((const char *)filename, "rb");
    char ibuf[64];
    FcChar32 *ret;
    size_t len;
    struct stat st;

    if (!fp)
	return NULL;

    if (FcStat (filename, &st))
	goto bail0;

    ret = FcHashInitSHA256Digest ();
    if (!ret)
	goto bail0;

    while (!feof (fp))
    {
	if ((len = fread (ibuf, sizeof (char), 64, fp)) < 64)
	{
	    uint64_t v;

	    /* add a padding */
	    memset (&ibuf[len], 0, 64 - len);
	    ibuf[len] = 0x80;
	    if ((64 - len) < 9)
	    {
		/* process a block once */
		FcHashComputeSHA256Digest (ret, ibuf);
		memset (ibuf, 0, 64);
	    }
	    /* set input size at the end */
	    v = (long)st.st_size * 8;
	    ibuf[63 - 0] =  v        & 0xff;
	    ibuf[63 - 1] = (v >>  8) & 0xff;
	    ibuf[63 - 2] = (v >> 16) & 0xff;
	    ibuf[63 - 3] = (v >> 24) & 0xff;
	    ibuf[63 - 4] = (v >> 32) & 0xff;
	    ibuf[63 - 5] = (v >> 40) & 0xff;
	    ibuf[63 - 6] = (v >> 48) & 0xff;
	    ibuf[63 - 7] = (v >> 56) & 0xff;
	    FcHashComputeSHA256Digest (ret, ibuf);
	    break;
	}
	else
	{
	    FcHashComputeSHA256Digest (ret, ibuf);
	}
    }
    fclose (fp);

    return FcHashSHA256ToString (ret);

bail0:
    fclose (fp);

    return NULL;
}

FcChar8 *
FcHashGetSHA256DigestFromMemory (const char *fontdata,
				 size_t      length)
{
    char ibuf[64];
    FcChar32 *ret;
    size_t i = 0;

    ret = FcHashInitSHA256Digest ();
    if (!ret)
	return NULL;

    while (i <= length)
    {
	if ((length - i) < 64)
	{
	    uint64_t v;
	    size_t n;

	    /* add a padding */
	    n = length - i;
	    if (n > 0)
		memcpy (ibuf, &fontdata[i], n);
	    memset (&ibuf[n], 0, 64 - n);
	    ibuf[n] = 0x80;
	    if ((64 - n) < 9)
	    {
		/* process a block once */
		FcHashComputeSHA256Digest (ret, ibuf);
		memset (ibuf, 0, 64);
	    }
	    /* set input size at the end */
	    v = length * 8;
	    ibuf[63 - 0] =  v        & 0xff;
	    ibuf[63 - 1] = (v >>  8) & 0xff;
	    ibuf[63 - 2] = (v >> 16) & 0xff;
	    ibuf[63 - 3] = (v >> 24) & 0xff;
	    ibuf[63 - 4] = (v >> 32) & 0xff;
	    ibuf[63 - 5] = (v >> 40) & 0xff;
	    ibuf[63 - 6] = (v >> 48) & 0xff;
	    ibuf[63 - 7] = (v >> 56) & 0xff;
	    FcHashComputeSHA256Digest (ret, ibuf);
	    break;
	}
	else
	{
	    FcHashComputeSHA256Digest (ret, &fontdata[i]);
	}
	i += 64;
    }

    return FcHashSHA256ToString (ret);
}
