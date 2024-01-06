/*
 * Copyright (C) 1989-95 GROUPE BULL
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * GROUPE BULL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name of GROUPE BULL shall not be
 * used in advertising or otherwise to promote the sale, use or other dealings
 * in this Software without prior written authorization from GROUPE BULL.
 */

/*****************************************************************************\
* data.c:                                                                     *
*                                                                             *
*  XPM library                                                                *
*  IO utilities                                                               *
*                                                                             *
*  Developed by Arnaud Le Hors                                                *
\*****************************************************************************/

/* October 2004, source code review by Thomas Biege <thomas@suse.de> */

#ifndef CXPMPROG
#if 0
/* Official version number */
static char *RCS_Version = "$XpmVersion: 3.4k $";

/* Internal version number */
static char *RCS_Id = "Id: xpm.shar,v 3.71 1998/03/19 19:47:14 lehors Exp $";
#endif
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "XpmI.h"
#endif
#include <ctype.h>

#ifndef CXPMPROG
#define Getc(data, file) getc(file)
#define Ungetc(data, c, file) ungetc(c, file)
#endif

static int
ParseComment(xpmData *data)
{
    if (data->type == XPMBUFFER) {
	register char c;
	register unsigned int n = 0;
	unsigned int notend;
	char *s;
	const char *s2;

	s = data->Comment;
	*s = data->Bcmt[0];

	/* skip the string beginning comment */
	s2 = data->Bcmt;
	do {
	    c = *data->cptr++;
	    *++s = c;
	    n++;
	    s2++;
	} while (c == *s2 && *s2 != '\0' && c);

	if (*s2 != '\0') {
	    /* this wasn't the beginning of a comment */
	    data->cptr -= n;
	    return 0;
	}
	/* store comment */
	data->Comment[0] = *s;
	s = data->Comment;
	notend = 1;
	n = 0;
	while (notend) {
	    s2 = data->Ecmt;
	    while (*s != *s2 && c) {
		c = *data->cptr++;
		if (n == XPMMAXCMTLEN - 1)  { /* forget it */
		    s = data->Comment;
		    n = 0;
		}
		*++s = c;
		n++;
	    }
	    data->CommentLength = n;
	    do {
		c = *data->cptr++;
		if (n == XPMMAXCMTLEN - 1)  { /* forget it */
		    s = data->Comment;
		    n = 0;
		}
		*++s = c;
		n++;
		s2++;
	    } while (c == *s2 && *s2 != '\0' && c);
	    if (*s2 == '\0' || c == '\0') {
		/* this is the end of the comment */
		notend = 0;
		data->cptr--;
	    }
	}
	return 0;
    } else {
	FILE *file = data->stream.file;
	register int c;
	register unsigned int n = 0, a;
	unsigned int notend;
	char *s;
	const char *s2;

	s = data->Comment;
	*s = data->Bcmt[0];

	/* skip the string beginning comment */
	s2 = data->Bcmt;
	do {
	    c = Getc(data, file);
	    *++s = c;
	    n++;
	    s2++;
	} while (c == *s2 && *s2 != '\0' && c != EOF);

	if (*s2 != '\0') {
	    /* this wasn't the beginning of a comment */
	    /* put characters back in the order that we got them */
	    for (a = n; a > 0; a--, s--)
		Ungetc(data, *s, file);
	    return 0;
	}
	/* store comment */
	data->Comment[0] = *s;
	s = data->Comment;
	notend = 1;
	n = 0;
	while (notend) {
	    s2 = data->Ecmt;
	    while (*s != *s2 && c != EOF) {
		c = Getc(data, file);
		if (n == XPMMAXCMTLEN - 1)  { /* forget it */
		    s = data->Comment;
		    n = 0;
		}
		*++s = c;
		n++;
	    }
	    data->CommentLength = n;
	    do {
		c = Getc(data, file);
		if (n == XPMMAXCMTLEN - 1)  { /* forget it */
		    s = data->Comment;
		    n = 0;
		}
		*++s = c;
		n++;
		s2++;
	    } while (c == *s2 && *s2 != '\0' && c != EOF);
	    if (*s2 == '\0') {
		/* this is the end of the comment */
		notend = 0;
		Ungetc(data, *s, file);
	    }
	    else if (c == EOF) {
		/* hit end of file before the end of the comment */
		return XpmFileInvalid;
	    }
	}
	return 0;
    }
}

/*
 * skip to the end of the current string and the beginning of the next one
 */
int
xpmNextString(xpmData *data)
{
    if (!data->type)
	data->cptr = (data->stream.data)[++data->line];
    else if (data->type == XPMBUFFER) {
	register char c;

	/* get to the end of the current string */
	if (data->Eos) {
	    while ((c = *data->cptr++) && c != data->Eos && c != '\0');

	    if (c == '\0')
		return XpmFileInvalid;
	}

	/*
	 * then get to the beginning of the next string looking for possible
	 * comment
	 */
	if (data->Bos) {
	    while ((c = *data->cptr++) && c != data->Bos && c != '\0')
		if (data->Bcmt && c == data->Bcmt[0])
		    ParseComment(data);
	} else if (data->Bcmt) {	/* XPM2 natural */
	    while (((c = *data->cptr++) == data->Bcmt[0]) && c != '\0')
		ParseComment(data);
	    data->cptr--;
	}
    } else {
	register int c;
	FILE *file = data->stream.file;

	/* get to the end of the current string */
	if (data->Eos) {
	    while ((c = Getc(data, file)) != data->Eos && c != EOF);

	    if (c == EOF)
		return XpmFileInvalid;
	}

	/*
	 * then get to the beginning of the next string looking for possible
	 * comment
	 */
	if (data->Bos) {
	    while ((c = Getc(data, file)) != data->Bos && c != EOF)
		if (data->Bcmt && c == data->Bcmt[0])
		    ParseComment(data);

	} else if (data->Bcmt) {	/* XPM2 natural */
	    while ((c = Getc(data, file)) == data->Bcmt[0])
		ParseComment(data);
	    Ungetc(data, c, file);
	}
    }
    return XpmSuccess;
}


/*
 * skip whitespace and return the following word
 */
unsigned int
xpmNextWord(
    xpmData		*data,
    char		*buf,
    unsigned int	 buflen)
{
    register unsigned int n = 0;
    int c;

    if (!data->type || data->type == XPMBUFFER) {
	while ((c = *data->cptr) && isspace(c) && (c != data->Eos))
	    data->cptr++;
	do {
	    c = *data->cptr++;
	    *buf++ = c;
	    n++;
	} while (c && !isspace(c) && (c != data->Eos) && (n < buflen));
	n--;
	data->cptr--;
    } else {
	FILE *file = data->stream.file;

	while ((c = Getc(data, file)) != EOF && isspace(c) && c != data->Eos);
	while (!isspace(c) && c != data->Eos && c != EOF && n < buflen) {
	    *buf++ = c;
	    n++;
	    c = Getc(data, file);
	}
	Ungetc(data, c, file);
    }
    return (n); /* this returns bytes read + 1 */
}

/*
 * skip whitespace and compute the following unsigned int,
 * returns 1 if one is found and 0 if not
 */
int
xpmNextUI(
    xpmData		*data,
    unsigned int	*ui_return)
{
    char buf[BUFSIZ];
    int l;

    l = xpmNextWord(data, buf, BUFSIZ);
    return xpmatoui(buf, l, ui_return);
}

/*
 * return end of string - WARNING: malloc!
 */
int
xpmGetString(
    xpmData		 *data,
    char		**sptr,
    unsigned int	 *l)
{
    unsigned int i, n = 0;
    int c;
    char *p = NULL, *q, buf[BUFSIZ];

    if (!data->type || data->type == XPMBUFFER) {
	if (data->cptr) {
	    char *start = data->cptr;
	    while ((c = *data->cptr) && c != data->Eos)
		data->cptr++;
	    n = data->cptr - start + 1;
	    p = (char *) XpmMalloc(n);
	    if (!p)
		return (XpmNoMemory);
	    strncpy(p, start, n);
	    if (data->type)		/* XPMBUFFER */
		p[n - 1] = '\0';
	}
    } else {
	FILE *file = data->stream.file;

	if ((c = Getc(data, file)) == EOF)
	    return (XpmFileInvalid);

	i = 0;
	q = buf;
	p = (char *) XpmMalloc(1);
	while (c != data->Eos && c != EOF) {
	    if (i == BUFSIZ) {
		/* get to the end of the buffer */
		/* malloc needed memory */
		q = (char *) XpmRealloc(p, n + i);
		if (!q) {
		    XpmFree(p);
		    return (XpmNoMemory);
		}
		p = q;
		q += n;
		/* and copy what we already have */
		strncpy(q, buf, i);
		n += i;
		i = 0;
		q = buf;
	    }
	    *q++ = c;
	    i++;
	    c = Getc(data, file);
	}
	if (c == EOF) {
	    XpmFree(p);
	    return (XpmFileInvalid);
	}
	if (n + i != 0) {
	    /* malloc needed memory */
	    q = (char *) XpmRealloc(p, n + i + 1);
	    if (!q) {
		XpmFree(p);
		return (XpmNoMemory);
	    }
	    p = q;
	    q += n;
	    /* and copy the buffer */
	    strncpy(q, buf, i);
	    n += i;
	    p[n++] = '\0';
	} else {
	    *p = '\0';
	    n = 1;
	}
	Ungetc(data, c, file);
    }
    *sptr = p;
    *l = n;
    return (XpmSuccess);
}

/*
 * get the current comment line
 */
int
xpmGetCmt(
    xpmData	 *data,
    char	**cmt)
{
    if (!data->type)
	*cmt = NULL;
    else if (data->CommentLength != 0 && data->CommentLength < UINT_MAX - 1) {
	if( (*cmt = (char *) XpmMalloc(data->CommentLength + 1)) == NULL)
		return XpmNoMemory;
	strncpy(*cmt, data->Comment, data->CommentLength);
	(*cmt)[data->CommentLength] = '\0';
	data->CommentLength = 0;
    } else
	*cmt = NULL;
    return 0;
}

xpmDataType xpmDataTypes[] =
{
    {"", "!", "\n", '\0', '\n', "", "", "", ""},	/* Natural type */
    {"C", "/*", "*/", '"', '"', ",\n", "static char *", "[] = {\n", "};\n"},
    {"Lisp", ";", "\n", '"', '"', "\n", "(setq ", " '(\n", "))\n"},
    {NULL, NULL, NULL, 0, 0, NULL, NULL, NULL, NULL}
};

/*
 * parse xpm header
 */
int
xpmParseHeader(xpmData *data)
{
    char buf[BUFSIZ+1] = {0};
    int l, n = 0;

    if (data->type) {
	data->Bos = '\0';
	data->Eos = '\n';
	data->Bcmt = data->Ecmt = NULL;
	l = xpmNextWord(data, buf, BUFSIZ);
	if (l == 7 && !strncmp("#define", buf, 7)) {
	    /* this maybe an XPM 1 file */
	    char *ptr;

	    l = xpmNextWord(data, buf, BUFSIZ);
	    if (!l)
		return (XpmFileInvalid);
	    buf[l] = '\0';
	    ptr = strrchr(buf, '_');
	    if (!ptr || strncmp("_format", ptr, l - (ptr - buf)))
		return XpmFileInvalid;
	    /* this is definitely an XPM 1 file */
	    data->format = 1;
	    n = 1;			/* handle XPM1 as mainly XPM2 C */
	} else {

	    /*
	     * skip the first word, get the second one, and see if this is
	     * XPM 2 or 3
	     */
	    l = xpmNextWord(data, buf, BUFSIZ);
	    if ((l == 3 && !strncmp("XPM", buf, 3)) ||
		(l == 4 && !strncmp("XPM2", buf, 4))) {
		if (l == 3)
		    n = 1;		/* handle XPM as XPM2 C */
		else {
		    /* get the type key word */
		    l = xpmNextWord(data, buf, BUFSIZ);

		    /*
		     * get infos about this type
		     */
		    while (xpmDataTypes[n].type
			   && strncmp(xpmDataTypes[n].type, buf, l))
			n++;
		}
		data->format = 0;
	    } else
		/* nope this is not an XPM file */
		return XpmFileInvalid;
	}
	if (xpmDataTypes[n].type) {
	    if (n == 0) {		/* natural type */
		data->Bcmt = xpmDataTypes[n].Bcmt;
		data->Ecmt = xpmDataTypes[n].Ecmt;
		xpmNextString(data);	/* skip the end of the headerline */
		data->Bos = xpmDataTypes[n].Bos;
		data->Eos = xpmDataTypes[n].Eos;
	    } else {
		data->Bcmt = xpmDataTypes[n].Bcmt;
		data->Ecmt = xpmDataTypes[n].Ecmt;
		if (!data->format) {	/* XPM 2 or 3 */
		    data->Bos = xpmDataTypes[n].Bos;
		    data->Eos = '\0';
		    /* get to the beginning of the first string */
		    xpmNextString(data);
		    data->Eos = xpmDataTypes[n].Eos;
		} else			/* XPM 1 skip end of line */
		    xpmNextString(data);
	    }
	} else
	    /* we don't know about that type of XPM file... */
	    return XpmFileInvalid;
    }
    return XpmSuccess;
}
