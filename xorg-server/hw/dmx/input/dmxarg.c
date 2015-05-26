/*
 * Copyright 2002-2003 Red Hat Inc., Durham, North Carolina.
 *
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation on the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT.  IN NO EVENT SHALL RED HAT AND/OR THEIR SUPPLIERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * Authors:
 *   Rickard E. (Rik) Faith <faith@redhat.com>
 *
 */

/** \file
 * Generic comma-delimited argument processing. */

#ifdef HAVE_DMX_CONFIG_H
#include <dmx-config.h>
#endif

#define DMX_ARG_TEST 0

#include "dmx.h"
#include "dmxarg.h"
#include <stdio.h>
#include <string.h>

#if DMX_ARG_TEST
#include <stdlib.h>
#endif

/** Stores the parsed argument list. */
struct _dmxArg {
    int argc;          /**< Number of arguments in argv */
    int argm;          /**< Maximum number of arguments store-able in argv */
    const char **argv; /**< Arguments */
};

/** Create an (externally opaque) \a dmxArg object. */
dmxArg
dmxArgCreate(void)
{
    dmxArg a = malloc(sizeof(*a));

    a->argc = 0;
    a->argm = 2;
    a->argv = malloc(a->argm * sizeof(*a->argv));
    a->argv[0] = NULL;
    return a;
}

/** Free the specified \a dmxArg object. */
void
dmxArgFree(dmxArg a)
{
    int i;

    for (i = 0; i < a->argc; i++)
        free((char *) a->argv[i]);
    free(a->argv);
    free(a);
}

/** Add the \a string as the next argument in the \a dmxArg object. */
void
dmxArgAdd(dmxArg a, const char *string)
{
    if (a->argm <= a->argc + 2)
        a->argv = reallocarray(a->argv, (a->argm *= 2), sizeof(*a->argv));
    a->argv[a->argc++] = strdup(string);
    a->argv[a->argc] = NULL;
}

/** Return the argument number \a item in the \a dmxArg object.
 * Arguments are 0 based.  NULL will be returned for values less than 0
 * or equal to or greater than the number of arguments in the object. */
const char *
dmxArgV(dmxArg a, int item)
{
    if (item < 0 || item >= a->argc)
        return NULL;
    return a->argv[item];
}

/** Return the number of arguments in the \a dmxArg object. */
int
dmxArgC(dmxArg a)
{
    return a->argc;
}

/** Parse a string into arguments delimited by commas.  Return a new \a
 * dmxArg object containing the arguments. */
dmxArg
dmxArgParse(const char *string)
{
    char *tmp;
    char *start, *pt;
    dmxArg a = dmxArgCreate();
    int done;
    int len;

    if (!string)
        return a;

    len = strlen(string) + 2;
    tmp = malloc(len);
    strncpy(tmp, string, len);

    for (start = pt = tmp, done = 0; !done && *pt; start = ++pt) {
        for (; *pt && *pt != ','; pt++);
        if (!*pt)
            done = 1;
        *pt = '\0';
        dmxArgAdd(a, start);
    }
    if (!done)
        dmxArgAdd(a, "");       /* Final comma */

    free(tmp);
    return a;
}

#if DMX_ARG_TEST
static void
dmxArgPrint(dmxArg a)
{
    int i;

    printf("   argc = %d\n", dmxArgC(a));
    for (i = 0; i < dmxArgC(a); i++)
        printf("   argv[%d] = \"%s\"\n", i, dmxArgV(a, i));
}

static void
dmxArgTest(const char *string)
{
    dmxArg a;

    if (!string)
        printf("Testing NULL\n");
    else if (!strlen(string))
        printf("Testing (empty)\n");
    else
        printf("Testing \"%s\"\n", string);

    a = dmxArgParse(string);
    dmxArgPrint(a);
    dmxArgFree(a);
}

int
main(void)
{
    dmxArgTest(NULL);
    dmxArgTest("");
    dmxArgTest(",");

    dmxArgTest("a");
    dmxArgTest("a,");
    dmxArgTest(",a");

    dmxArgTest("a,b");
    dmxArgTest("a,b,");
    dmxArgTest("a,b,,");
    dmxArgTest("a,b,,c");

    return 0;
}
#endif
