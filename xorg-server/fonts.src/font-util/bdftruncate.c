/*-
 * Copyright (c) 2006 Martin Husemann.
 * Copyright (c) 2007 Joerg Sonnenberger.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.
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
 * This program is derived (in a straight forward way) from
 * bdftruncate.pl -- Markus Kuhn <http://www.cl.cam.ac.uk/~mgk25/>
 *
 * This utility allows you to generate from an ISO10646-1 encoded
 * BDF font other ISO10646-1 BDF fonts in which all characters above
 * a threshold code value are stored unencoded.
 */

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if (defined(__GNUC__) && ((__GNUC__ * 100 + __GNUC_MINOR__) >= 205))	\
        || (defined(__SUNPRO_C) && (__SUNPRO_C >= 0x590))
# define ATTR_NORETURN __attribute((noreturn))
#else
# define ATTR_NORETURN
#endif /* GNUC  */

static int iswide(unsigned int);
static void usage(void) ATTR_NORETURN;
static int parse_threshold(const char *str, unsigned long *threshold);

static int
parse_threshold(const char *str, unsigned long *threshold)
{
	int base;
	char *end_ptr;

	if (!isdigit((unsigned char)*str))
		return 1;
	if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X'))
		base = 16;
	else
		base = 10;

	errno = 0;
	*threshold = strtoul(str, &end_ptr, base);
	if (errno != 0 || *threshold == 0)
		return 1;
	return 0;
}

static void
process_line(const char *line, int removewide, unsigned long threshold)
{
	if (strncmp(line, "ENCODING", 8) == 0) {
		unsigned long enc;
		const char *v;

		v = line + 9;

		while (*v && isspace((unsigned char)(*v)))
			++v;
		enc = strtoul(v, NULL, 10);
		/* XXX Check for line-ending? */
		if (enc >= threshold || (removewide && iswide(enc))) {
			printf("ENCODING -1\n");
		} else {
			fputs(line, stdout);
		}
		return;
	}
	if (strncmp(line, "STARTFONT", 9) == 0) {
		fputs(line, stdout);
		printf("COMMENT AUTOMATICALLY GENERATED FILE. DO NOT EDIT!\n"
		    "COMMENT In this version of the font file, "
		    "all characters >= U+%04lx are\n"
		    "COMMENT not encoded to keep XFontStruct small.\n",
		    threshold);
		return;
	}
	if (strncmp(line, "COMMENT", 7) == 0) {
		const char *v = line + 8;

		while (*v && isspace((unsigned char)(*v)))
			v++;
		if (strncmp(v, "$id: ", 5) == 0 ||
		    strncmp(v, "$Id: ", 5) == 0) {
		    	const char *id = strchr(v+1, '$');
		    	if (id) {
		    		printf("COMMENT Derived from %.*s",
				     (int)(id - v - 4), v + 5);
				return;
		    	}
		}
	}
	fputs(line, stdout);
}

int
main(int argc, char **argv)
{
	int removewide;
	unsigned long threshold;
	int opt_minus_w = 0;
	int opt_plus_w = 0;
	char *line, *input_ptr;
	size_t line_len, rest_len;

	--argc;
	++argv;
	if (argc == 0)
		usage();

	if (strcmp(*argv, "-w") == 0 || strcmp(*argv, "+w") == 0) {
		if (**argv == '-')
			opt_minus_w = 1;
		else
			opt_plus_w = 1;
		--argc;
		++argv;
	}

	if (argc != 1 || (opt_plus_w && opt_minus_w))
		usage();
	if (parse_threshold(*argv, &threshold)) {
		fprintf(stderr, "Illegal threshold %s\n", *argv);
		usage();
	}

	if (opt_minus_w)
		removewide = 1;
	else if (opt_plus_w)
		removewide = 0;
	else
		removewide = (threshold <= 0x3200);

	line_len = 1024;
	if ((line = malloc(line_len)) == NULL) {
		fprintf(stderr, "malloc failed");
		exit(EXIT_FAILURE);
	}

	for (;;) {
		if (fgets(line, line_len, stdin) == NULL)
		     break;
		while (strlen(line) == line_len - 1 && !feof(stdin)) {
			if (line_len > SSIZE_MAX) {
				fprintf(stderr, "input line too large");
				exit(EXIT_FAILURE);
			}
			line = realloc(line, line_len * 2);
			if (line == NULL) {
				fprintf(stderr, "realloc failed");
				exit(EXIT_FAILURE);
			}
			input_ptr = line + line_len - 1;
			rest_len = line_len + 1;
			line_len *= 2;
			if (fgets(input_ptr, rest_len, stdin) == NULL) {
				/* Should not happen, but handle as EOF */
				break;
			}
		}
		process_line(line, removewide, threshold);
	}

	return EXIT_SUCCESS;
}

/*
 * Subroutine to identify whether the ISO 10646/Unicode character code
 * ucs belongs into the East Asian Wide (W) or East Asian FullWidth
 * (F) category as defined in Unicode Technical Report #11.
 */
static int
iswide(unsigned int ucs)
{
    return (ucs >= 0x1100 &&
            (ucs <= 0x115f ||                   /* Hangul Jamo */
             (ucs >= 0x2e80 && ucs <= 0xa4cf &&
              (ucs & ~0x0011) != 0x300a && ucs != 0x303f) || /* CJK .. Yi */
             (ucs >= 0xac00 && ucs <= 0xd7a3) || /* Hangul Syllables */
             (ucs >= 0xf900 && ucs <= 0xfaff) || /* CJK Comp. Ideographs */
             (ucs >= 0xfe30 && ucs <= 0xfe6f) || /* CJK Comp. Forms */
             (ucs >= 0xff00 && ucs <= 0xff5f) || /* Fullwidth Forms */
             (ucs >= 0xffe0 && ucs <= 0xffe6) ||
             (ucs >= 0x20000 && ucs <= 0x2ffff)));
}

static void
usage(void)
{
	fprintf(stderr,
	    "Usage: bdftruncate [+w|-w] threshold <source.bdf >destination.bdf\n"
	    "\n"
	    "Example:\n"
	    "\n"
	    "  bdftruncate 0x3200 <6x13.bdf >6x13t.bdf\n"
	    "\n"
	    "will generate the file 6x13t.bdf in which all glyphs with codes\n"
	    ">= 0x3200 will only be stored unencoded (i.e., ENCODING -1).\n"
	    "Option -w removes East Asian Wide and East Asian FullWidth characters\n"
	    "(default if threshold <= 0x3200), and option +w keeps them.\n");
	exit(EXIT_FAILURE);
}
