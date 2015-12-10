/*-
 * Copyright (c) 2003 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Ben Collver <collver1@attbi.com>.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * This utility allows you to generate from an ISO10646-1 encoded
 * BDF font other BDF fonts in any possible encoding. This way, you can
 * derive from a single ISO10646-1 master font a whole set of 8-bit
 * fonts in all ISO 8859 and various other encodings. (Hopefully
 * a future XFree86 release will have a similar facility built into
 * the server, which can reencode ISO10646-1 on the fly, because
 * storing the same fonts in many different encodings is clearly
 * a waste of storage capacity).
*/

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#if !defined(NEED_BASENAME) && !defined(Lynx)
#include <libgen.h>
#endif
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* global variable for argv[0] */
static const char *my_name = NULL;

#ifdef NEED_BASENAME
static char *
basename(char *pathname)
{
	char	*ptr;

	ptr = strrchr(pathname, '/');
	return ((ptr == NULL) ? pathname : &ptr[1]);
}
#endif

/* "CLASS" "z" string and memory manipulation */

static void *
zmalloc(size_t size)
{
	void *r;
	r = malloc(size);
	if (r == NULL) {
		perror(my_name);
		exit(errno);
	}
	memset(r, 0, size);
	return r;
}

static void *
zrealloc(void *ptr, size_t size)
{
	void *temp;
	temp = realloc(ptr, size);
	if (temp == NULL) {
		perror(my_name);
		exit(errno);
	}
	return temp;
}

static char *
zstrdup(const char *str)
{
	char *retval;

	if (str == NULL) {
		fprintf(stderr, "%s: zstrdup(NULL)\n", my_name);
		exit(1);
	}
	retval = strdup(str);
	if (retval == NULL) {
		perror(my_name);
		exit(errno);
	}
	return retval;
}

static void 
zstrcpy(char **dest, const char *source)
{
	if (*dest != NULL)
		free(*dest);
	*dest = zstrdup(source);
}

static void 
zquotedcpy(char **dest, const char *source)
{
	const char *start, *end;

	if (*dest != NULL)
		free(*dest);
	*dest = NULL;
	start = source;
	if (*start == '"') {
		start = source+1;
		end = strrchr(start, '"');
		if (!end) return;
		*dest = zmalloc(end-start+1);
		strncpy(*dest, start, end-start);
		(*dest)[end-start] = '\0';
	} else {
		*dest = zstrdup(source);
	}
}

static void 
zstrcat(char **dest, const char *source)
{
	size_t dest_size = 1;
	size_t source_size;

	if (*dest != NULL)
		dest_size = strlen(*dest) + 1;
	source_size = strlen(source);
	*dest = zrealloc(*dest, dest_size + source_size);
	strcpy(*dest + dest_size - 1, source);
}

static void 
zstrtoupper(char *s)
{
	char *t;

	for (t = s; *t != '\000'; t++)
		*t = (char) toupper(*t);
}

#define zs_true(x)	(x != NULL && strcmp(x, "0") != 0)
#define zi_true(x)	(x == 1)

/* "CLASS" "dynamic array" */

typedef struct {
	char *name;
	int size;
	int count;
	void **values;
	void *nv;
} da_t;

static da_t *
da_new(const char *name)
{
	da_t *da;

	da = zmalloc(sizeof(da_t));
	da->size = 0;
	da->count = 0;
	da->values = NULL;
	da->nv = NULL;
	da->name = NULL;
	zstrcpy(&(da->name), name);
	return da;
}

static void *
da_fetch(da_t *da, int key)
{
	void *r = NULL;

	if (key >= 0 && key < da->size && da->values[key] != NULL)
		r = da->values[key];
	else
		if (key == -1 && da->nv != NULL)
			r = da->nv;

	return r;
}

static int 
da_fetch_int(da_t *da, int key)
{
	int *t;
	int r = -1;
	t = da_fetch(da, key);
	if (t != NULL)
		r = *t;
	return r;
}

#define da_fetch_str(a,k)	\
	(char *)da_fetch(a,k)

static void 
da_add(da_t *da, int key, void *value)
{
	int i = da->size;
	if (key >= 0) {
		if (key >= da->size) {
			da->size = key + 1;
			da->values = zrealloc(da->values,
				da->size * sizeof(void *));
			for (; i < da->size; i++)
				da->values[i] = NULL;
		}
		if (da->values[key] != NULL) {
			free(da->values[key]);
		} else {
			if (value == NULL) {
				if (da->count > 0)
					da->count--;
			} else {
				da->count++;
			}
		}
		da->values[key] = value;
	} else if (key == -1) {
		if (da->nv != NULL)
			free(da->nv);
		da->nv = value;
	}
}

static void 
da_add_str(da_t *da, int key, const char *value)
{
	da_add(da, key, value?zstrdup(value):NULL);
}

static void 
da_add_int(da_t *da, int key, int value)
{
	int *v;

	v = zmalloc(sizeof(int));
	*v = value;
	da_add(da, key, v);
}

#define da_count(da) (da->count)
#define da_size(da) (da->size)

static void 
da_clear(da_t *da)
{
	int i;

	for (i = da->size; i; i--)
		free(da->values[i]);
	if (da->values != NULL)
		free(da->values);
	da->size = 0;
	da->count = 0;
	da->values = NULL;
}

/* "CLASS" file input */

#define TYPICAL_LINE_SIZE (80)

/* read a line and strip trailing whitespace */
static int 
read_line(FILE *fp, char **buffer)
{
	int buffer_size = TYPICAL_LINE_SIZE;
	int eof = 0;
	int position = 0;
	int c;

	*buffer = zmalloc(TYPICAL_LINE_SIZE);
	(*buffer)[0] = '\0';

	if ((c = getc(fp)) == EOF)
		eof = 1;

	while (c != '\n' && !eof) {
		if (position + 1 >= buffer_size) {
			buffer_size = buffer_size * 2 + 1;
			*buffer = zrealloc(*buffer, buffer_size);
		}
		(*buffer)[position++] = (char) c;
		(*buffer)[position] = '\0';
		c = getc(fp);
		if (c == EOF)
			eof = 1;
	}

	if (eof) {
		free(*buffer);
		*buffer = NULL;
		return 0;
	}

	while (position > 1) {
		position--;
		if (!isspace((*buffer)[position]))
			break;
		(*buffer)[position] = '\0';
	}

	return 1;
}

/* BEGIN */

/*
DEC VT100 graphics characters in the range 1-31 (as expected by
some old xterm versions and a few other applications)
*/
#define decmap_size 31
static int decmap[decmap_size] = {
	0x25C6, /* BLACK DIAMOND */
	0x2592, /* MEDIUM SHADE */
	0x2409, /* SYMBOL FOR HORIZONTAL TABULATION */
	0x240C, /* SYMBOL FOR FORM FEED */
	0x240D, /* SYMBOL FOR CARRIAGE RETURN */
	0x240A, /* SYMBOL FOR LINE FEED */
	0x00B0, /* DEGREE SIGN */
	0x00B1, /* PLUS-MINUS SIGN */
	0x2424, /* SYMBOL FOR NEWLINE */
	0x240B, /* SYMBOL FOR VERTICAL TABULATION */
	0x2518, /* BOX DRAWINGS LIGHT UP AND LEFT */
	0x2510, /* BOX DRAWINGS LIGHT DOWN AND LEFT */
	0x250C, /* BOX DRAWINGS LIGHT DOWN AND RIGHT */
	0x2514, /* BOX DRAWINGS LIGHT UP AND RIGHT */
	0x253C, /* BOX DRAWINGS LIGHT VERTICAL AND HORIZONTAL */
	0x23BA, /* HORIZONTAL SCAN LINE-1 (Unicode 3.2 draft) */
	0x23BB, /* HORIZONTAL SCAN LINE-3 (Unicode 3.2 draft) */
	0x2500, /* BOX DRAWINGS LIGHT HORIZONTAL */
	0x23BC, /* HORIZONTAL SCAN LINE-7 (Unicode 3.2 draft) */
	0x23BD, /* HORIZONTAL SCAN LINE-9 (Unicode 3.2 draft) */
	0x251C, /* BOX DRAWINGS LIGHT VERTICAL AND RIGHT */
	0x2524, /* BOX DRAWINGS LIGHT VERTICAL AND LEFT */
	0x2534, /* BOX DRAWINGS LIGHT UP AND HORIZONTAL */
	0x252C, /* BOX DRAWINGS LIGHT DOWN AND HORIZONTAL */
	0x2502, /* BOX DRAWINGS LIGHT VERTICAL */
	0x2264, /* LESS-THAN OR EQUAL TO */
	0x2265, /* GREATER-THAN OR EQUAL TO */
	0x03C0, /* GREEK SMALL LETTER PI */
	0x2260, /* NOT EQUAL TO */
	0x00A3, /* POUND SIGN */
	0x00B7  /* MIDDLE DOT */
};

static int 
is_control(int ucs)
{
	return ((ucs >= 0x00 && ucs <= 0x1f) ||
		(ucs >= 0x7f && ucs <= 0x9f));
}

static int 
is_blockgraphics(int ucs)
{
	return ucs >= 0x2500 && ucs <= 0x25FF;
}

/* calculate the bounding box that covers both provided bounding boxes */
typedef struct {
	int cwidth;
	int cheight;
	int cxoff;
	int cyoff;
} bbx_t;

static bbx_t *
combine_bbx(int awidth, int aheight, int axoff, int ayoff,
	int cwidth, int cheight, int cxoff, int cyoff, bbx_t *r)
{
	r->cwidth = cwidth;
	r->cheight = cheight;
	r->cxoff = cxoff;
	r->cyoff = cyoff;

	if (axoff < r->cxoff) {
		r->cwidth += r->cxoff - axoff;
		r->cxoff = axoff;
	}
	if (ayoff < r->cyoff) {
		r->cheight += r->cyoff - ayoff;
		r->cyoff = ayoff;
	}
	if (awidth + axoff > r->cwidth + r->cxoff) {
		r->cwidth = awidth + axoff - r->cxoff;
	}
	if (aheight + ayoff > r->cheight + r->cyoff) {
		r->cheight = aheight + ayoff - r->cyoff;
	}

	return r;
}

static void 
usage(void) {
	printf("%s", "\n"
"Usage: ucs2any [+d|-d] <source-name> { <mapping-file> <registry-encoding> }\n"
"\n"
"where\n"
"\n"
"   +d                   put DEC VT100 graphics characters in the C0 range\n"
"                        (default for upright charcell fonts)\n"
"\n"
"   -d                   do not put DEC VT100 graphics characters in the\n"
"                        C0 range (default for all other font types)\n"
"\n"
"   <source-name>        is the name of an ISO10646-1 encoded BDF file\n"
"\n"
"   <mapping-file>       is the name of a character set table like those on\n"
"                        <ftp://ftp.unicode.org/Public/MAPPINGS/>\n"
"\n"
"   <registry-encoding>  are the CHARSET_REGISTRY and CHARSET_ENCODING\n"
"                        field values for the font name (XLFD) of the\n"
"                        target font, separated by a hyphen\n"
"\n"
"Example:\n"
"\n"
"   ucs2any 6x13.bdf 8859-1.TXT iso8859-1 8859-2.TXT iso8859-2\n"
"\n"
"will generate the files 6x13-iso8859-1.bdf and 6x13-iso8859-2.bdf\n"
"\n");
}

static int 
chars_compare(const void *aa, const void *bb)
{
	int a = *(const int *)aa;
	int b = *(const int *)bb;

	return a - b;
}

/*
 * Return != 0 if "string" starts with "pattern" followed by whitespace.
 * If it does, return a pointer to the first non space char.
 */
static const char *
startswith(const char *string, const char *pattern)
{
	size_t l = strlen(pattern);

	if (strlen(string) <= l) return NULL;
	if (strncmp(string, pattern, l) != 0) return NULL;
	string += l;
	if (!isspace(*string)) return NULL;
	while (isspace(*string))
		string++;
	return string;
}

int 
main(int argc, char *argv[])
{
	int ai = 1;
	int dec_chars = -1;
	char *fsource = NULL;
	FILE *fsource_fp;
	int properties;
	int default_char;
	char *l = NULL;
	char *t = NULL;
	const char *nextc = NULL;
	char *startfont = NULL;
	char *slant = NULL;
	char *spacing = NULL;
	char *sc = NULL;
	int code = -1;
	da_t *startchar;
	da_t *my_char;
	char *fmap = NULL;
	char *registry = NULL;
	char *encoding = NULL;
	char *fontname = NULL;
	FILE *fmap_fp;
	da_t *map;
	da_t *headers;
	int nextheader = -1;
	int default_char_index = -1;
	int startproperties_index = -1;
	int fontname_index = -1;
	int charset_registry_index = -1;
	int slant_index = -1;
	int spacing_index = -1;
	int charset_encoding_index = -1;
	int fontboundingbox_index = -1;
	int target;
	int ucs;
	int i;
	int j;
	int *chars = NULL;
	bbx_t bbx;
	char *fout = NULL;
	FILE *fout_fp;
	int k;
	char *registry_encoding = NULL;

	my_name = argv[0];
	bbx.cheight = bbx.cxoff = bbx.cyoff = -1;

	startchar = da_new("startchar");
	my_char = da_new("my_char");
	map = da_new("map");
	headers = da_new("headers");

	if (argc < 2) {
		usage();
		exit(0);
	}

	/* check options */
	if (strcmp(argv[ai], "+d") == 0) {
		ai++;
		dec_chars = 1;
	} else if (strcmp(argv[ai], "-d") == 0) {
		ai++;
		dec_chars = 0;
	}
	if (ai >= argc) {
		usage();
		exit(0);
	}

	/* open and read source file */
	fsource = argv[ai];
	fsource_fp = fopen(fsource, "r");
	if (fsource_fp == NULL) {
		fprintf(stderr, "%s: Can't read file '%s': %s!\n", my_name,
			fsource, strerror(errno));
		exit(1);
	}

	/* read header */
	properties = 0;
	default_char = 0;
	while (read_line(fsource_fp, &l)) {
		if (startswith(l, "CHARS"))
			break;
		if (startswith(l, "STARTFONT")) {
			zstrcpy(&startfont, l);
		} else if (startswith(l, "_XMBDFED_INFO") ||
			startswith(l, "XFREE86_GLYPH_RANGES"))
		{
			properties--;
		} else if ((nextc = startswith(l, "DEFAULT_CHAR")) != NULL)
		{
			default_char = atoi(nextc);
			default_char_index = ++nextheader;
			da_add_str(headers, default_char_index, NULL);
		} else {
			if ((nextc = startswith(l, "STARTPROPERTIES")) != NULL)
			{
				properties = atoi(nextc);
				startproperties_index = ++nextheader;
				da_add_str(headers, startproperties_index, NULL);
			} else if ((nextc = startswith(l, "FONT")) != NULL)
			{
				char * term;
				/* slightly simplistic check ... */
				zquotedcpy(&fontname, nextc);
				if ((term = strstr(fontname, "-ISO10646-1")) == NULL) {
					fprintf(stderr,
						"%s: FONT name in '%s' is '%s' and not '*-ISO10646-1'!\n",
						my_name, fsource, fontname);
					exit(1);
				}
				*term = '\0';
				fontname_index = ++nextheader;
				da_add_str(headers, fontname_index, NULL);
			} else if ((nextc = startswith(l, "CHARSET_REGISTRY")) != NULL)
			{
				if (strcmp(nextc, "\"ISO10646\"") != 0) {
					fprintf(stderr,
						"%s: CHARSET_REGISTRY in '%s' is '%s' and not 'ISO10646'!\n",
						my_name, fsource, nextc);
					exit(1);
				}
				charset_registry_index = ++nextheader;
				da_add_str(headers, charset_registry_index, NULL);
			} else if ((nextc = startswith(l, "CHARSET_ENCODING")) != NULL)
			{
				if (strcmp(nextc, "\"1\"") != 0) {
					fprintf(stderr,
						"%s: CHARSET_ENCODING in '%s' is '%s' and not '1'!\n",
						my_name, fsource, nextc);
					exit(1);
				}
				charset_encoding_index = ++nextheader;
				da_add_str(headers, charset_encoding_index, NULL);
			} else if (startswith(l, "FONTBOUNDINGBOX")) {
				fontboundingbox_index = ++nextheader;
				da_add_str(headers, fontboundingbox_index, NULL);
			} else if ((nextc = startswith(l, "SLANT")) != NULL)
			{
				zquotedcpy(&slant, nextc);
				slant_index = ++nextheader;
				da_add_str(headers, slant_index, NULL);
			} else if ((nextc = startswith(l, "SPACING")) != NULL)
			{
				zquotedcpy(&spacing, nextc);
				zstrtoupper(spacing);
				spacing_index = ++nextheader;
				da_add_str(headers, spacing_index, NULL);
			} else if ((nextc = startswith(l, "COMMENT")) != NULL) {
				if (strncmp(nextc, "$Id: ", 5)==0) {
					char *header = NULL;
					char *id = NULL, *end = NULL;
					id = zstrdup(nextc + 5);
					end = strrchr(id, '$');
					if (end) *end = '\0';
					zstrcpy(&header, "COMMENT Derived from ");
					zstrcat(&header, id);
					zstrcat(&header, "\n");
					free(id);
					da_add_str(headers, ++nextheader, header);
					free(header);
				} else {
					da_add_str(headers, ++nextheader, l);
				}
			} else {
				da_add_str(headers, ++nextheader, l);
			}
		}
		free(l);
	}

	if (startfont == NULL) {
		fprintf(stderr, "%s: No STARTFONT line found in '%s'!\n",
			my_name, fsource);
		exit(1);
	}

	/* read characters */
	while (read_line(fsource_fp, &l)) {
		if (startswith(l, "STARTCHAR")) {
			zstrcpy(&sc, l);
			zstrcat(&sc, "\n");
			code = -1;
		} else if ((nextc = startswith(l, "ENCODING")) != NULL) {
			code = atoi(nextc);
			da_add_str(startchar, code, sc);
			da_add_str(my_char, code, "");
		} else if (strcmp(l, "ENDFONT")==0) {
			code = -1;
			zstrcpy(&sc, "STARTCHAR ???\n");
		} else {
			zstrcpy(&t, da_fetch_str(my_char, code));
			zstrcat(&t, l);
			zstrcat(&t, "\n");
			da_add_str(my_char, code, t);
			if (strcmp(l, "ENDCHAR")==0) {
				code = -1;
				zstrcpy(&sc, "STARTCHAR ???\n");
			}
		}
		free(l);
	}

	fclose(fsource_fp);

	ai++;
	while (ai < argc) {
		zstrcpy(&fmap, argv[ai]);
		i = ai + 1;
		if (i < argc) {
			char *temp = NULL;
			char * hyphen = strchr(argv[i], '-');
			if (!hyphen || strchr(hyphen+1, '-') != NULL) {
				fprintf(stderr,
					"%s: Argument registry-encoding '%s' not in expected format!\n",
					my_name, i < argc ? fmap : "");
				exit(1);
			}
			temp = zstrdup(argv[i]);
			hyphen = strchr(temp, '-');
			if (hyphen) *hyphen = 0;
			zstrcpy(&registry, temp);
			zstrcpy(&encoding, hyphen+1);
			free(temp);
		} else {
			fprintf(stderr, "map file argument \"%s\" needs a "
			    "coresponding registry-encoding argument\n", fmap);
			exit(0);
		}

		ai++;
		ai++;

		/* open and read source file */
		fmap_fp = fopen(fmap, "r");
		if (fmap_fp == NULL) {
			fprintf(stderr,
				"%s: Can't read mapping file '%s': %s!\n",
				my_name, fmap, strerror(errno));
			exit(1);
		}

		da_clear(map);

		for (;read_line(fmap_fp, &l); free(l)) {
			char *p, *endp;

			for (p = l; isspace(p[0]); p++)
				;
			if (p[0] == '\0' || p[0] == '#')
				continue;
			if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
				target = (int) strtol(p+2, &endp, 16);
				if (*endp == '\0') goto bad;
				p = endp;
			} else
				goto bad;
			for (; isspace(p[0]); p++)
				;
			if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
				ucs = (int) strtol(p+2, &endp, 16);
				if (*endp == '\0') goto bad;
			} else
				goto bad;

			if (!is_control(ucs)) {
				if (zs_true(da_fetch_str(startchar, ucs)))
				{
					da_add_int(map, target, ucs);
				} else {
					if (!((is_blockgraphics(ucs) &&
						strcmp(slant, "R") != 0) ||
						(ucs >= 0x200e &&
						ucs <= 0x200f)))							{
						fprintf(stderr,
							"No glyph for character U+%04X (0x%02x) available.\n",
							ucs, target);
					}
				}
			}
			continue;
		bad:
			fprintf(stderr, "Unrecognized line in '%s':\n%s\n", fmap, l);
		}
		fclose(fmap_fp);

		/* add default character */
		if (!zi_true(da_fetch_int(map, 0))) {
			if (zs_true(da_fetch_str(startchar, default_char))) {
				da_add_int(map, 0, default_char);
				da_add_str(startchar, default_char,
					"STARTCHAR defaultchar\n");
			} else {
				fprintf(stderr, "%s",
					"No default character defined.\n");
			}
		}

		if (dec_chars == 1 ||
			(dec_chars == -1 && strcmp(slant, "R") == 0 &&
			strcmp(spacing, "C") == 0))
		{
			/* add DEC VT100 graphics characters in the range 1-31
			   (as expected by some old xterm versions) */
			for (i = 0; i < decmap_size; i++) {
				if (zs_true(da_fetch_str(startchar, decmap[i])))
				{
					da_add_int(map, i + 1, decmap[i]);
				}
			}
		}

		/* list of characters that will be written out */
		j = da_count(map);
		if (j < 0) {
			fprintf(stderr,
				"No characters found for %s-%s.\n",
				registry, encoding);
			continue;
		}
		if (chars != NULL)
			free(chars);
		chars = zmalloc(j * sizeof(int));
		memset(chars, 0, j * sizeof(int));
		for (k = 0, i = 0; k < da_count(map) && i < da_size(map); i++) {
			if (da_fetch(map, i) != NULL)
				chars[k++] = i;
		}
		qsort(chars, j, sizeof(int), chars_compare);

		/* find overall font bounding box */
		bbx.cwidth = -1;
		for (i = 0; i < j; i++) {
			ucs = da_fetch_int(map, chars[i]);
			zstrcpy(&t, da_fetch_str(my_char, ucs));
			if ((nextc = startswith(t, "BBX")) != NULL 
			    || (nextc = strstr(t, "\nBBX")) != NULL)
			{
				char *endp;
				int w, h, x, y;

				if (*nextc == '\n') {
					nextc += 4;
					while (isspace(*nextc))
						nextc++;
				}
				for (;isspace(*nextc);)
					nextc++;
				w = (int) strtol(nextc, &endp, 10);
				nextc = endp;
				if (*nextc == '\0') goto bbxbad;
				for (;isspace(*nextc);)
					nextc++;
				h = (int) strtol(nextc, &endp, 10);
				nextc = endp;
				if (*nextc == '\0') goto bbxbad;
				for (;isspace(*nextc);)
					nextc++;
				x = (int) strtol(nextc, &endp, 10);
				nextc = endp;
				if (*nextc == '\0') goto bbxbad;
				for (;isspace(*nextc);)
					nextc++;
				y = (int) strtol(nextc, &endp, 10);
				if (bbx.cwidth == -1) {
					bbx.cwidth = w;
					bbx.cheight = h;
					bbx.cxoff = x;
					bbx.cyoff = y;
				} else {
					combine_bbx(bbx.cwidth, bbx.cheight,
						bbx.cxoff, bbx.cyoff,
						w, h, x, y, &bbx);
				}
				continue;
			bbxbad:
				fprintf(stderr, "Unparsable BBX found for U+%04x!\n", ucs);
			} else {
				fprintf(stderr,
					"Warning: No BBX found for U+%04X!\n",
					ucs);
			}
		}

		if (!registry) registry = zstrdup("");
		if (!encoding) encoding = zstrdup("");

		/* generate output file name */
		zstrcpy(&registry_encoding, "-");
		zstrcat(&registry_encoding, registry);
		zstrcat(&registry_encoding, "-");
		zstrcat(&registry_encoding, encoding);

		{
			char * p = strstr(fsource, ".bdf");
			if (p) {
				zstrcpy(&fout, fsource);
				p = strstr(fout, ".bdf");
				*p = 0;
				zstrcat(&fout, registry_encoding);
				zstrcat(&fout, ".bdf");
			} else {
				zstrcpy(&fout, fsource);
				zstrcat(&fout, registry_encoding);
			}
		}

		/* remove path prefix */
		zstrcpy(&t, basename(fout));
		zstrcpy(&fout, t);

		/* write new BDF file */
		fprintf(stderr, "Writing %d characters into file '%s'.\n",
			j, fout);
		fout_fp = fopen(fout, "w");
		if (fout_fp == NULL) {
			fprintf(stderr, "%s: Can't write file '%s': %s!\n",
				my_name, fout, strerror(errno));
			exit(1);
		}

		fprintf(fout_fp, "%s\n", startfont);
		fprintf(fout_fp, "%s",
			"COMMENT AUTOMATICALLY GENERATED FILE. DO NOT EDIT!\n");
		fprintf(fout_fp,
			"COMMENT Generated with 'ucs2any %s %s %s-%s'\n",
			fsource, fmap, registry, encoding);
		fprintf(fout_fp, "%s",
			"COMMENT from an ISO10646-1 encoded source BDF font.\n");
		fprintf(fout_fp, "%s",
			"COMMENT ucs2any by Ben Collver <collver1@attbi.com>, 2003, based on\n");
		fprintf(fout_fp, "%s",
			"COMMENT ucs2any.pl by Markus Kuhn <http://www.cl.cam.ac.uk/~mgk25/>, 2000.\n");

		for (i = 0; i <= nextheader; i++) {
			if (i == default_char_index)
				fprintf(fout_fp, "DEFAULT_CHAR %d\n", default_char);
			else if (i == startproperties_index)
				fprintf(fout_fp, "STARTPROPERTIES %d\n", properties);
			else if (i == fontname_index) {
				fprintf(fout_fp, "FONT %s%s\n", fontname, registry_encoding);
			}
			else if (i == charset_registry_index)
				fprintf(fout_fp, "CHARSET_REGISTRY \"%s\"\n", registry);
			else if (i == slant_index)
				fprintf(fout_fp, "SLANT \"%s\"\n", slant);
			else if (i == charset_encoding_index)
				fprintf(fout_fp, "CHARSET_ENCODING \"%s\"\n", encoding);
			else if (i == fontboundingbox_index)
				fprintf(fout_fp, "FONTBOUNDINGBOX %d %d %d %d\n", bbx.cwidth, bbx.cheight, bbx.cxoff, bbx.cyoff);
			else if (i == spacing_index)
				fprintf(fout_fp, "SPACING \"%s\"\n", spacing);
			else
				fprintf(fout_fp, "%s\n", da_fetch_str(headers, i));
		}

		fprintf(fout_fp, "CHARS %d\n", j);

		/* Write characters */
		for (i = 0; i < j; i++) {
			ucs = da_fetch_int(map, chars[i]);
			fprintf(fout_fp, "%s", da_fetch_str(startchar,
				ucs));
			fprintf(fout_fp, "ENCODING %d\n", chars[i]);
			fprintf(fout_fp, "%s", da_fetch_str(my_char,
				ucs));
		}
		fprintf(fout_fp, "%s", "ENDFONT\n");
		fclose(fout_fp);
	}

	exit(0);
}
