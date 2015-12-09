/* $XFree86$ */
/*
 * Copyright 2002 Red Hat Inc., Durham, North Carolina.
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

%{
#ifdef HAVE_DMX_CONFIG_H
#include <dmx-config.h>
#endif

#include "dmxparse.h"
#include "parser.h"
#include "os.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
static int getdimension(int token, const char *text, int leng);
static int getstring(int token, const char *text, int leng);
static int gettoken(int token, const char *text, int leng);
static int getcomment(int token, const char *text, int leng);
static int lineno = 1;
%}
%s OTHER
comment         #.*
word            ([[:alpha:]_/:\-\+\.\*][[:alnum:]_/:\-\+\.\*]+)
string          \"(([^\"\n])|\"\")*\"
badstring       \"(([^\"\n])|\"\")*
number          [[:digit:]x]+
dimension       [[:digit:]]+[[:blank:]]*x[[:blank:]]*[[:digit:]]+
offset          [+-][[:digit:]]+[[:blank:]]*[+-][[:blank:]]*[[:digit:]]+
origin          @[[:blank:]]*[[:digit:]]+[[:blank:]]*[[:blank:]]*x[[:digit:]]+
NL              \n
WS              [[:blank:]]+
%%              
virtual         return gettoken(T_VIRTUAL, yytext, yyleng);
display         return gettoken(T_DISPLAY, yytext, yyleng);
wall            return gettoken(T_WALL, yytext, yyleng);
option          return gettoken(T_OPTION, yytext, yyleng);
param           return gettoken(T_PARAM, yytext, yyleng);
{dimension}     return getdimension(T_DIMENSION, yytext, yyleng);
{offset}        return getdimension(T_OFFSET, yytext+1, yyleng-1);
{origin}        return getdimension(T_ORIGIN, yytext+1, yyleng-1);
{word}          return getstring(T_STRING, yytext, yyleng);
{string}        return getstring(T_STRING, yytext+1, yyleng-2);
{NL}            ++lineno;
{WS}            
\{              return gettoken(yytext[0], yytext, yyleng);
\}              return gettoken(yytext[0], yytext, yyleng);
\;              return gettoken(yytext[0], yytext, yyleng);
\/              return gettoken(yytext[0], yytext, yyleng);
^{comment}      return getcomment(T_LINE_COMMENT, yytext, yyleng);
{comment}       return getcomment(T_COMMENT, yytext, yyleng);
.               return getstring(T_STRING, yytext, yyleng);
<<EOF>>         return 0;
%%
int yywrap(void)
{
    (void) &yyunput;
    (void) &input;
    return 1;
}

_X_NORETURN void yyerror(const char *message)
{
    const char *pt, *end;
    struct _entry {
        const char *from;
        const char *to;
    } *entry, list[] = {
        { "T_VIRTUAL",      "\"virtual\"" },
        { "T_DISPLAY",      "\"display\"" },
        { "T_WALL",         "\"wall\"" },
        { "T_OPTION",       "\"option\"" },
        { "T_PARAM",        "\"param\"" },
        { "T_DIMENSION",    "dimension (e.g., 2x2 or 1024x768)" },
        { "T_OFFSET",       "display offset (e.g., +10-10)" },
        { "T_ORIGIN",       "tile origin (e.g., @1280x1024)" },
        { "T_STRING",       "string" },
        { "T_COMMENT",      "comment (e.g., #...)" },
        { "T_LINE_COMMENT", "comment (e.g., #...)" },
        { NULL, NULL }
    };
    
    fprintf(stderr, "parse error on line %d at token \"%*.*s\"\n",
            lineno, (int)yyleng, (int)yyleng, yytext);
    end = message + strlen(message);
    for (pt = message; *pt; pt++) {
        if (pt[0] == 'T' && pt[1] == '_') {
            const char *next = strchr(pt, ' ');
            if (!next || !*next) next = strchr(pt, '\0');
            if (!next) goto bail;
            --next;
            if (next-pt == 1 && next[1]
                && next[2] == '\'' && next[3] == '\'') {
                fprintf(stderr, "\"%c\"", next[1]);
                pt += 4;
                goto cnt;
            }
            for (entry = list; entry->from; ++entry) {
                if (!strncmp(entry->from, pt, strlen(entry->from))) {
                    fprintf(stderr, "%s", entry->to);
                    pt = next;
                    goto cnt;
                }
            }
        } else if (end-pt >= 5 && pt[0] == '\'' && pt[1] == '\'' && pt[3]
                   && pt[4] == '\'' && pt[5] == '\'') {
            fprintf(stderr, "\"%c\"", pt[3]);
            pt += 5;
        } else if (end-pt >= 3 && pt[0] == '\'' && pt[1] && pt[2] == '\'') {
            fprintf(stderr, "\"%c\"", pt[1]);
            pt += 3;
        }
      bail:
        putc(*pt, stderr);
      cnt:
        ;
    }
    fprintf(stderr, "\n");
    exit( 1 );
}

static int getdimension(int token, const char *text, int leng)
{
    char *endptr;
    char *tmp = dmxConfigAlloc(leng+1);
    int  x, y;

    strlcpy(tmp, text, leng+1);
    x = strtol(tmp, &endptr, 10);
    while (*endptr && !isdigit(*endptr)) ++endptr;
    y = strtol(endptr, NULL, 10);
    dmxConfigFree(tmp);
    yylval.pair = dmxConfigCreatePair(token, lineno, NULL, x, y, 1, 1);
    return token;
}

static int getstring(int token, const char *text, int leng)
{
    yylval.string = dmxConfigCreateString(token, lineno, NULL,
                                          dmxConfigCopyString(leng ? text : "",
                                                              leng));
    return token;
}

static int gettoken(int token, const char *text, int leng)
{
    yylval.token = dmxConfigCreateToken(token, lineno, NULL);
    return token;
}

static int getcomment(int token, const char *text, int leng)
{
    yylval.comment = dmxConfigCreateComment(token, lineno,
                                            dmxConfigCopyString(text + 1,
                                                                leng - 1));
    return token;
}
