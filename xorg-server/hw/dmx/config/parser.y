/* $XFree86$ */
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

%{
#ifdef HAVE_DMX_CONFIG_H
#include <dmx-config.h>
#endif

#include "dmxparse.h"
#include <string.h>
#include <stdlib.h>
#define YYDEBUG 1
#define YYERROR_VERBOSE
#define YY_USE_PROTOS

DMXConfigEntryPtr dmxConfigEntry = NULL;
#define APPEND(type, h, t)                 \
{                                          \
    type pt;                               \
    for (pt = h; pt->next; pt = pt->next); \
    pt->next = t;                          \
}
%}

%union {
    DMXConfigTokenPtr      token;
    DMXConfigStringPtr     string;
    DMXConfigNumberPtr     number;
    DMXConfigPairPtr       pair;
    DMXConfigFullDimPtr    fdim;
    DMXConfigPartDimPtr    pdim;
    DMXConfigDisplayPtr    display;
    DMXConfigWallPtr       wall;
    DMXConfigOptionPtr     option;
    DMXConfigParamPtr      param;
    DMXConfigCommentPtr    comment;
    DMXConfigSubPtr        subentry;
    DMXConfigVirtualPtr    virtual;
    DMXConfigEntryPtr      entry;
}

				/* Terminals */
%token <token>   '{' '}' ';' '/' T_VIRTUAL T_DISPLAY T_WALL T_OPTION T_PARAM
%token <string>  T_STRING
%token <pair>    T_DIMENSION T_OFFSET T_ORIGIN
%token <comment> T_COMMENT T_LINE_COMMENT

                                /* Non-termials */
%type  <token>    Display Wall Terminal Open Close
%type  <string>   NameList Name
%type  <pair>     Dimension Offset Origin
%type  <pdim>     PartialDim
%type  <fdim>     FullDim
%type  <display>  DisplayEntry
%type  <option>   OptionEntry
%type  <param>    ParamEntry ParamList Param
%type  <subentry> SubList Sub
%type  <wall>     WallEntry
%type  <virtual>  Virtual
%type  <entry>    Program EntryList Entry

%%

Program : EntryList { dmxConfigEntry = $1; }
        ;

EntryList : Entry
          | EntryList Entry { APPEND(DMXConfigEntryPtr,$1,$2); $$ = $1; }
          ;

Entry : Virtual        { $$ = dmxConfigEntryVirtual($1); }
      | T_LINE_COMMENT { $$ = dmxConfigEntryComment($1); }
      ;

Virtual : T_VIRTUAL Open SubList Close
          { $$ = dmxConfigCreateVirtual($1, NULL, NULL, $2, $3, $4); }
        | T_VIRTUAL Dimension Open SubList Close
          { $$ = dmxConfigCreateVirtual($1, NULL, $2, $3, $4, $5); }
        | T_VIRTUAL Name Open SubList Close
          { $$ = dmxConfigCreateVirtual($1, $2, NULL, $3, $4, $5); }
        | T_VIRTUAL Name Dimension Open SubList Close
          { $$ = dmxConfigCreateVirtual($1, $2, $3, $4, $5, $6 ); }
        ;

SubList : Sub
        | SubList Sub { APPEND(DMXConfigSubPtr,$1,$2); $$ = $1; }
        ;

Sub : T_LINE_COMMENT { $$ = dmxConfigSubComment($1); }
    | DisplayEntry   { $$ = dmxConfigSubDisplay($1); }
    | WallEntry      { $$ = dmxConfigSubWall($1); }
    | OptionEntry    { $$ = dmxConfigSubOption($1); }
    | ParamEntry     { $$ = dmxConfigSubParam($1); }
    ;

OptionEntry : T_OPTION NameList Terminal
              { $$ = dmxConfigCreateOption($1, $2, $3); }
            ;

ParamEntry : T_PARAM NameList Terminal
             { $$ = dmxConfigCreateParam($1, NULL, $2, NULL, $3); }
           | T_PARAM Open ParamList Close
             { $$ = dmxConfigCreateParam($1, $2, NULL, $4, NULL);
               $$->next = $3;
             }
           ;

ParamList : Param
          | ParamList Param { APPEND(DMXConfigParamPtr,$1,$2); $$ = $1; }
          ;

Param : NameList Terminal
        { $$ = dmxConfigCreateParam(NULL, NULL, $1, NULL, $2); }
      ;

PartialDim : Dimension Offset
             { $$ = dmxConfigCreatePartDim($1, $2); }
           | Dimension
             { $$ = dmxConfigCreatePartDim($1, NULL); }
           | Offset
             { $$ = dmxConfigCreatePartDim(NULL, $1); }
           ;

FullDim : PartialDim '/' PartialDim
          { $$ = dmxConfigCreateFullDim($1, $3); }
        | '/' PartialDim
          { $$ = dmxConfigCreateFullDim(NULL, $2); }
        | PartialDim
          { $$ = dmxConfigCreateFullDim($1, NULL); }
        ;

DisplayEntry : Display Name FullDim Origin Terminal
               { $$ = dmxConfigCreateDisplay($1, $2, $3, $4, $5); }
             | Display FullDim Origin Terminal
               { $$ = dmxConfigCreateDisplay($1, NULL, $2, $3, $4); }
             | Display Name Origin Terminal
               { $$ = dmxConfigCreateDisplay($1, $2, NULL, $3, $4); }

             | Display Name FullDim Terminal
               { $$ = dmxConfigCreateDisplay($1, $2, $3, NULL, $4); }
             | Display FullDim Terminal
               { $$ = dmxConfigCreateDisplay($1, NULL, $2, NULL, $3); }
             | Display Name Terminal
               { $$ = dmxConfigCreateDisplay($1, $2, NULL, NULL, $3); }
             | Display Terminal
               { $$ = dmxConfigCreateDisplay($1, NULL, NULL, NULL, $2); }
             ;

WallEntry : Wall Dimension Dimension NameList Terminal
            { $$ = dmxConfigCreateWall($1, $2, $3, $4, $5); }
          | Wall Dimension NameList Terminal
            { $$ = dmxConfigCreateWall($1, $2, NULL, $3, $4); }
          | Wall NameList Terminal
            { $$ = dmxConfigCreateWall($1, NULL, NULL, $2, $3); }
          ;

Display : T_DISPLAY
        | T_DISPLAY T_COMMENT { $$ = $1; $$->comment = $2->comment; }
        ;

Name : T_STRING
     | T_STRING T_COMMENT { $$ = $1; $$->comment = $2->comment; }
     ;

Dimension : T_DIMENSION
          | T_DIMENSION T_COMMENT { $$ = $1; $$->comment = $2->comment; }
          ;

Offset : T_OFFSET
       | T_OFFSET T_COMMENT { $$ = $1; $$->comment = $2->comment; }
       ;

Origin : T_ORIGIN
       | T_ORIGIN T_COMMENT { $$ = $1; $$->comment = $2->comment; }
       ;

Terminal : ';'
         | ';' T_COMMENT { $$ = $1; $$->comment = $2->comment; }
         ;

Open : '{'
     | '{' T_COMMENT { $$ = $1; $$->comment = $2->comment; }
     ;

Close : '}'
      | '}' T_COMMENT { $$ = $1; $$->comment = $2->comment; }
      ;

Wall : T_WALL
     | T_WALL T_COMMENT { $$ = $1; $$->comment = $2->comment; }
     ;

NameList : Name
         | NameList Name { APPEND(DMXConfigStringPtr, $1, $2); $$ = $1; }
         ;
