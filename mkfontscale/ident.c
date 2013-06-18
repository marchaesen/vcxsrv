/*
  Copyright (c) 2003 by Juliusz Chroboczek

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.
*/
/*
 * Copyright (c) 2008, Oracle and/or its affiliates. All rights reserved.
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

/* The function identifyBitmap returns -1 if filename is definitively not
   a font file, 1 if it is a single-face bitmap font with a XLFD name,
   and 0 if it should be processed normally.  identifyBitmap is
   much faster than parsing the whole font. */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include "zlib.h"
#include "ident.h"

#ifdef X_BZIP2_FONT_COMPRESSION
# include <bzlib.h>
#endif

#define PCF_VERSION (('p'<<24)|('c'<<16)|('f'<<8)|1)
#define PCF_PROPERTIES (1 << 0)

typedef struct _Prop {
    unsigned name;
    int isString;
    unsigned value;
} PropRec, *PropPtr;

#ifdef X_BZIP2_FONT_COMPRESSION
typedef struct {
    enum { gzFontFile, bz2FontFile } type;
    union {
	gzFile gz;
	BZFILE *bz2;
    } f;
    unsigned pos;
} fontFile;

static inline void *
fontFileOpen(fontFile *ff, const char *filename) {
    int n = strlen(filename);

    if (strcmp(filename + n - 4, ".bz2") == 0) {
	ff->type = bz2FontFile;
	ff->f.bz2 = BZ2_bzopen(filename, "rb");
	ff->pos = 0;
	return ff->f.bz2;
    } else {
	ff->type = gzFontFile;
	ff->f.gz = gzopen(filename, "rb");
	return ff->f.gz;
    }
}

static inline int
fontFileRead(fontFile *ff, void *buf, unsigned len)
{
    if (ff->type == gzFontFile) {
	return gzread(ff->f.gz, buf, len);
    } else {
	int r = BZ2_bzread(ff->f.bz2, buf, len);
	ff->pos += r;
	return r;
    }
}

static inline int
fontFileGetc(fontFile *ff)
{
    if (ff->type == gzFontFile) {
	return gzgetc(ff->f.gz);
    } else {
	char buf;
	if (BZ2_bzread(ff->f.bz2, &buf, 1) != 1) {
	    return -1;
	} else {
	    ff->pos += 1;
	    return (int) buf;
	}
    }
}

static int
fontFileSeek(fontFile *ff, z_off_t offset, int whence)
{
    if (ff->type == gzFontFile) {
	return gzseek(ff->f.gz, offset, whence);
    } else {
	/* bzlib has no easy equivalent so we have to fake it,
	 * fortunately, we only have to handle a couple of cases
	 */
	int n;
	char buf[BUFSIZ];

	switch (whence) {
	  case SEEK_SET:
	    n = offset - ff->pos;
	    break;
	  case SEEK_CUR:
	    n = offset;
	    break;
	  default:
	    return -1;
	}

	while (n > BUFSIZ) {
	    if (BZ2_bzread(ff->f.bz2, buf, BUFSIZ) != BUFSIZ)
		return -1;
	    n -= BUFSIZ;
	}
	if (BZ2_bzread(ff->f.bz2, buf, n) != n)
	    return -1;
	ff->pos = offset;
	return offset;
    }
}


static inline int
fontFileClose(fontFile *ff)
{
    if (ff->type == gzFontFile) {
	return gzclose(ff->f.gz);
    } else {
	BZ2_bzclose(ff->f.bz2);
	return 0;
    }
}

#else /* no bzip2, only gzip */
typedef gzFile fontFile;
# define fontFileOpen(ff, filename)	(*(ff) = gzopen(filename, "rb"))
# define fontFileRead(ff, buf, len)	gzread(*(ff), buf, len)
# define fontFileGetc(ff)		gzgetc(*(ff))
# define fontFileSeek(ff, off, whence)	gzseek(*(ff), off, whence)
# define fontFileClose(ff)		gzclose(*(ff))
#endif

static int pcfIdentify(fontFile *f, char **name);
static int bdfIdentify(fontFile *f, char **name);

static int
getLSB32(fontFile *f)
{
    int rc;
    unsigned char c[4];

    rc = fontFileRead(f, c, 4);
    if(rc != 4)
        return -1;
    return (c[0]) | (c[1] << 8) | (c[2] << 16) | (c[3] << 24);
}

static int
getInt8(fontFile *f, int format)
{
    unsigned char c;
    int rc;

    rc = fontFileRead(f, &c, 1);
    if(rc != 1)
        return -1;
    return c;
}

static int
getInt32(fontFile *f, int format)
{
    int rc;
    unsigned char c[4];

    rc = fontFileRead(f, c, 4);
    if(rc != 4)
        return -1;

    if(format & (1 << 2)) {
        return (c[0] << 24) | (c[1] << 16) | (c[2] << 8) | (c[3]);
    } else {
        return (c[0]) | (c[1] << 8) | (c[2] << 16) | (c[3] << 24);
    }
}

int
bitmapIdentify(const char *filename, char **name)
{
    fontFile ff;
    int magic;

    if (fontFileOpen(&ff, filename) == NULL)
	return -1;

    magic = getLSB32(&ff);
    if(magic == PCF_VERSION)
        return pcfIdentify(&ff, name);
    else if(magic == ('S' | ('T' << 8) | ('A' << 16) | ('R') << 24))
        return bdfIdentify(&ff, name);

    fontFileClose(&ff);
    return 0;
}

static int
pcfIdentify(fontFile *f, char **name)
{
    int prop_position;
    PropPtr props = NULL;
    int format, count, nprops, i, string_size, rc;
    char *strings = NULL, *s;

    count = getLSB32(f);
    if(count <= 0)
        goto fail;

    prop_position = -1;
    for(i = 0; i < count; i++) {
        int type, offset;
        type = getLSB32(f);
        (void) getLSB32(f);
        (void) getLSB32(f);
        offset = getLSB32(f);
        if(type == PCF_PROPERTIES) {
            prop_position = offset;
            break;
        }
    }
    if(prop_position < 0)
        goto fail;

    rc = fontFileSeek(f, prop_position, SEEK_SET);
    if(rc < 0)
        goto fail;

    format = getLSB32(f);
    if((format & 0xFFFFFF00) != 0)
        goto fail;
    nprops = getInt32(f, format);
    if(nprops <= 0 || nprops > 1000)
        goto fail;
    props = malloc(nprops * sizeof(PropRec));
    if(props == NULL)
        goto fail;

    for(i = 0; i < nprops; i++) {
        props[i].name = getInt32(f, format);
        props[i].isString = getInt8(f, format);
        props[i].value = getInt32(f, format);
    }
    if(nprops & 3) {
	rc = fontFileSeek(f, 4 - (nprops & 3), SEEK_CUR);
        if(rc < 0)
            goto fail;
    }

    string_size = getInt32(f, format);
    if(string_size < 0 || string_size > 100000)
        goto fail;
    strings = malloc(string_size);
    if(!strings)
        goto fail;

    rc = fontFileRead(f, strings, string_size);
    if(rc != string_size)
        goto fail;

    for(i = 0; i < nprops; i++) {
        if(!props[i].isString ||
           props[i].name >= string_size - 4 ||
           props[i].value >= string_size)
            continue;
        if(strcmp(strings + props[i].name, "FONT") == 0)
            break;
    }

    if(i >= nprops)
        goto fail;

    s = strdup(strings + props[i].value);
    if(s == NULL)
        goto fail;
    *name = s;
    free(strings);
    free(props);
    fontFileClose(f);
    return 1;

 fail:
    if(strings) free(strings);
    if(props) free(props);
    fontFileClose(f);
    return 0;
}

#define NKEY 20

static char*
getKeyword(fontFile *f, int *eol)
{
    static char keyword[NKEY + 1];
    int c, i;
    i = 0;
    while(i < NKEY) {
        c = fontFileGetc(f);
        if(c == ' ' || c == '\n') {
            if(i <= 0)
                return NULL;
            if(eol)
                *eol = (c == '\n');
            keyword[i] = '\0';
            return keyword;
        }
        if(c < 'A' || c > 'Z')
            return NULL;
        keyword[i++] = c;
    }
    return NULL;
}

static int
bdfskip(fontFile *f)
{
    int c;
    do {
        c = fontFileGetc(f);
    } while(c >= 0 && c != '\n');
    if(c < 0)
        return -1;
    return 1;
}

static char *
bdfend(fontFile *f)
{
    int c;
    char *buf = NULL;
    int bufsize = 0;
    int i = 0;

    do {
        c = fontFileGetc(f);
    } while (c == ' ');

    while(i < 1000) {
        if(c < 0 || (c == '\n' && i == 0)) {
            goto fail;
        }
        if(bufsize < i + 1) {
            char *newbuf;
            if(bufsize == 0) {
                bufsize = 20;
                newbuf = malloc(bufsize);
            } else {
                bufsize = 2 * bufsize;
                newbuf = realloc(buf, bufsize);
            }
            if(newbuf == NULL)
                goto fail;
            buf = newbuf;
        }
        if(c == '\n') {
            buf[i] = '\0';
            return buf;
        }
        buf[i++] = c;
        c = fontFileGetc(f);
    }

 fail:
    if(buf)
        free(buf);
    return NULL;
}

static int
bdfIdentify(fontFile *f, char **name)
{
    char *k;
    int rc;
    int eol;
    /* bitmapIdentify already read "STAR", so we need to check for
       "TFONT" */
    k = getKeyword(f, &eol);
    if(k == NULL || eol)
        goto fail;
    if(strcmp(k, "TFONT") != 0)
        goto fail;
    while(1) {
        if(!eol) {
            rc = bdfskip(f);
            if(rc < 0)
                goto fail;
        }
        k = getKeyword(f, &eol);
        if(k == NULL)
            goto fail;
        else if(strcmp(k, "FONT") == 0) {
            if(eol)
                goto fail;
            k = bdfend(f);
            if(k == NULL)
                goto fail;
            *name = k;
            fontFileClose(f);
            return 1;
        } else if(strcmp(k, "CHARS") == 0)
            goto fail;
    }
 fail:
    fontFileClose(f);
    return 0;
}
