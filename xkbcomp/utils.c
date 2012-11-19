
  /*\
   *
   *                          COPYRIGHT 1990
   *                    DIGITAL EQUIPMENT CORPORATION
   *                       MAYNARD, MASSACHUSETTS
   *                        ALL RIGHTS RESERVED.
   *
   * THE INFORMATION IN THIS SOFTWARE IS SUBJECT TO CHANGE WITHOUT NOTICE AND
   * SHOULD NOT BE CONSTRUED AS A COMMITMENT BY DIGITAL EQUIPMENT CORPORATION.
   * DIGITAL MAKES NO REPRESENTATIONS ABOUT THE SUITABILITY OF THIS SOFTWARE 
   * FOR ANY PURPOSE.  IT IS SUPPLIED "AS IS" WITHOUT EXPRESS OR IMPLIED 
   * WARRANTY.
   *
   * IF THE SOFTWARE IS MODIFIED IN A MANNER CREATING DERIVATIVE COPYRIGHT
   * RIGHTS, APPROPRIATE LEGENDS MAY BE PLACED ON THE DERIVATIVE WORK IN
   * ADDITION TO THAT SET FORTH ABOVE.
   *
   * Permission to use, copy, modify, and distribute this software and its
   * documentation for any purpose and without fee is hereby granted, provided
   * that the above copyright notice appear in all copies and that both that
   * copyright notice and this permission notice appear in supporting
   * documentation, and that the name of Digital Equipment Corporation not be
   * used in advertising or publicity pertaining to distribution of the 
   * software without specific, written prior permission.
   \*/

#include 	"utils.h"
#include	<ctype.h>
#include	<stdlib.h>
#include	<stdarg.h>

/***====================================================================***/

Opaque
uAlloc(unsigned size)
{
    return ((Opaque) malloc(size));
}

/***====================================================================***/

Opaque
uCalloc(unsigned n, unsigned size)
{
    return ((Opaque) calloc(n, size));
}

/***====================================================================***/

Opaque
uRealloc(Opaque old, unsigned newSize)
{
    if (old == NULL)
        return ((Opaque) malloc(newSize));
    else
        return ((Opaque) realloc((char *) old, newSize));
}

/***====================================================================***/

Opaque
uRecalloc(Opaque old, unsigned nOld, unsigned nNew, unsigned itemSize)
{
    char *rtrn;

    if (old == NULL)
        rtrn = (char *) calloc(nNew, itemSize);
    else
    {
        rtrn = (char *) realloc((char *) old, nNew * itemSize);
        if ((rtrn) && (nNew > nOld))
        {
            bzero(&rtrn[nOld * itemSize], (nNew - nOld) * itemSize);
        }
    }
    return (Opaque) rtrn;
}

/***====================================================================***/

void
uFree(Opaque ptr)
{
    if (ptr != (Opaque) NULL)
        free((char *) ptr);
    return;
}

/***====================================================================***/
/***                  FUNCTION ENTRY TRACKING                           ***/
/***====================================================================***/

static FILE *entryFile = NULL;
int uEntryLevel;

Boolean
uSetEntryFile(char *name)
{
    if ((entryFile != NULL) && (entryFile != stderr))
    {
        fprintf(entryFile, "switching to %s\n", name ? name : "stderr");
        fclose(entryFile);
    }
    if (name != NullString)
        entryFile = fopen(name, "w");
    else
        entryFile = stderr;
    if (entryFile == NULL)
    {
        entryFile = stderr;
        return (False);
    }
    return (True);
}

void
uEntry(int l, char *s, ...)
{
    int i;
    va_list args;

    for (i = 0; i < uEntryLevel; i++)
    {
        putc(' ', entryFile);
    }
    va_start(args, s);
    vfprintf(entryFile, s, args);
    va_end(args);
    uEntryLevel += l;
}

void
uExit(int l, char *rtVal)
{
    int i;

    uEntryLevel -= l;
    if (uEntryLevel < 0)
        uEntryLevel = 0;
    for (i = 0; i < uEntryLevel; i++)
    {
        putc(' ', entryFile);
    }
    fprintf(entryFile, "---> %p\n", rtVal);
    return;
}

/***====================================================================***/
/***			PRINT FUNCTIONS					***/
/***====================================================================***/

FILE *uDebugFile = NULL;
int uDebugIndentLevel = 0;
int uDebugIndentSize = 4;

Boolean
uSetDebugFile(char *name)
{
    if ((uDebugFile != NULL) && (uDebugFile != stderr))
    {
        fprintf(uDebugFile, "switching to %s\n", name ? name : "stderr");
        fclose(uDebugFile);
    }
    if (name != NullString)
        uDebugFile = fopen(name, "w");
    else
        uDebugFile = stderr;
    if (uDebugFile == NULL)
    {
        uDebugFile = stderr;
        return (False);
    }
    return (True);
}

void
uDebug(char *s, ...)
{
    int i;
    va_list args;

    for (i = (uDebugIndentLevel * uDebugIndentSize); i > 0; i--)
    {
        putc(' ', uDebugFile);
    }
    va_start(args, s);
    vfprintf(uDebugFile, s, args);
    va_end(args);
    fflush(uDebugFile);
}

void
uDebugNOI(char *s, ...)
{
    va_list args;

    va_start(args, s);
    vfprintf(uDebugFile, s, args);
    va_end(args);
    fflush(uDebugFile);
}

/***====================================================================***/

static FILE *errorFile = NULL;
static int outCount = 0;
static char *preMsg = NULL;
static char *postMsg = NULL;
static char *prefix = NULL;

Boolean
uSetErrorFile(char *name)
{
    if ((errorFile != NULL) && (errorFile != stderr))
    {
        fprintf(errorFile, "switching to %s\n", name ? name : "stderr");
        fclose(errorFile);
    }
    if (name != NullString)
        errorFile = fopen(name, "w");
    else
        errorFile = stderr;
    if (errorFile == NULL)
    {
        errorFile = stderr;
        return (False);
    }
    return (True);
}

void
uInformation(const char *s, ...)
{
    va_list args;

    va_start(args, s);
    vfprintf(errorFile, s, args);
    va_end(args);
    fflush(errorFile);
}

/***====================================================================***/

void
uAction(const char *s, ...)
{
    va_list args;

    if (prefix != NULL)
        fprintf(errorFile, "%s", prefix);
    fprintf(errorFile, "                  ");
    va_start(args, s);
    vfprintf(errorFile, s, args);
    va_end(args);
    fflush(errorFile);
}

/***====================================================================***/

void
uWarning(const char *s, ...)
{
    va_list args;

    if ((outCount == 0) && (preMsg != NULL))
        fprintf(errorFile, "%s\n", preMsg);
    if (prefix != NULL)
        fprintf(errorFile, "%s", prefix);
    fprintf(errorFile, "Warning:          ");
    va_start(args, s);
    vfprintf(errorFile, s, args);
    va_end(args);
    fflush(errorFile);
    outCount++;
}

/***====================================================================***/

void
uError(const char *s, ...)
{
    va_list args;

    if ((outCount == 0) && (preMsg != NULL))
        fprintf(errorFile, "%s\n", preMsg);
    if (prefix != NULL)
        fprintf(errorFile, "%s", prefix);
    fprintf(errorFile, "Error:            ");
    va_start(args, s);
    vfprintf(errorFile, s, args);
    va_end(args);
    fflush(errorFile);
    outCount++;
}

/***====================================================================***/

void
uFatalError(const char *s, ...)
{
    va_list args;

    if ((outCount == 0) && (preMsg != NULL))
        fprintf(errorFile, "%s\n", preMsg);
    if (prefix != NULL)
        fprintf(errorFile, "%s", prefix);
    fprintf(errorFile, "Fatal Error:      ");
    va_start(args, s);
    vfprintf(errorFile, s, args);
    va_end(args);
    fprintf(errorFile, "                  Exiting\n");
    fflush(errorFile);
    outCount++;
    exit(1);
    /* NOTREACHED */
}

/***====================================================================***/

void
uInternalError(const char *s, ...)
{
    va_list args;

    if ((outCount == 0) && (preMsg != NULL))
        fprintf(errorFile, "%s\n", preMsg);
    if (prefix != NULL)
        fprintf(errorFile, "%s", prefix);
    fprintf(errorFile, "Internal error:   ");
    va_start(args, s);
    vfprintf(errorFile, s, args);
    va_end(args);
    fflush(errorFile);
    outCount++;
}

void
uSetPreErrorMessage(char *msg)
{
    outCount = 0;
    preMsg = msg;
    return;
}

void
uSetPostErrorMessage(char *msg)
{
    postMsg = msg;
    return;
}

void
uSetErrorPrefix(char *pre)
{
    prefix = pre;
    return;
}

void
uFinishUp(void)
{
    if ((outCount > 0) && (postMsg != NULL))
        fprintf(errorFile, "%s\n", postMsg);
    return;
}

/***====================================================================***/

#ifndef HAVE_STRDUP
char *
uStringDup(const char *str)
{
    char *rtrn;

    if (str == NULL)
        return NULL;
    rtrn = (char *) uAlloc(strlen(str) + 1);
    strcpy(rtrn, str);
    return rtrn;
}
#endif

#ifndef HAVE_STRCASECMP
int
uStrCaseCmp(const char *str1, const char *str2)
{
    char buf1[512], buf2[512];
    char c, *s;
    register int n;

    for (n = 0, s = buf1; (c = *str1++); n++)
    {
        if (isupper(c))
            c = tolower(c);
        if (n > 510)
            break;
        *s++ = c;
    }
    *s = '\0';
    for (n = 0, s = buf2; (c = *str2++); n++)
    {
        if (isupper(c))
            c = tolower(c);
        if (n > 510)
            break;
        *s++ = c;
    }
    *s = '\0';
    return (strcmp(buf1, buf2));
}

int
uStrCasePrefix(const char *my_prefix, char *str)
{
    char c1;
    char c2;
    while (((c1 = *my_prefix) != '\0') && ((c2 = *str) != '\0'))
    {
        if (isupper(c1))
            c1 = tolower(c1);
        if (isupper(c2))
            c2 = tolower(c2);
        if (c1 != c2)
            return 0;
        my_prefix++;
        str++;
    }
    if (c1 != '\0')
        return 0;
    return 1;
}

#endif
