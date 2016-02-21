/*  This file is part of mhmake.
 *
 *  Copyright (C) 2001-2010 marha@sourceforge.net
 *
 *  Mhmake is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  Mhmake is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Mhmake.  If not, see <http://www.gnu.org/licenses/>.
*/

/* $Rev$ */

/* -------------- declaration section -------------- */
%{

#include "fileinfo.h"
#include "rule.h"
#include "util.h"

static uint32 LoadMakMd5(fileinfo *pTarget)
{
  uint32 Md5_32=0;
  string FileName=pTarget->GetFullFileName();
  FileName+=".md5_32";
  FILE *pFile=fopen(FileName.c_str(),"rb");
  if (!pFile)
    return Md5_32;
  if (1!=fread(&Md5_32,sizeof(Md5_32),1,pFile))
  {
    fclose(pFile);
    return Md5_32;
  }
  pTarget->SetCommandsMd5_32(Md5_32);
  fclose(pFile);
  return Md5_32;
}

static void SaveMakMd5(fileinfo *pTarget)
{
  string FileName=pTarget->GetFullFileName();
  FileName+=".md5_32";
  FILE *pFile=fopen(FileName.c_str(),"wb");
  if (!pFile)
  {
    throw string("Error creating file ")+FileName;
  }
  pTarget->WriteMd5_32(pFile);
  fclose(pFile);
}

static void ReplaceCurlyBraces(string &String)
{
  int Pos=String.find_first_of('{',0);
  if (Pos!=(int)string::npos)
  {
    /* if not { found, } will not be there eather (or it is a valid syntax, so it may generate an error) */
    do
    {
      String.replace(Pos,1,1,'(');
      Pos=String.find_first_of('{',Pos);
    }
    while (Pos!=(int)string::npos);
    Pos=0;
    while ((Pos=String.find_first_of('}',Pos))!=(int)string::npos)
      String.replace(Pos,1,1,')');
  }
}

#include "mhmakeparser.hpp"

#define YY_DECL int mhmakeFlexLexer::yylex(YYSTYPE * yylval, yy::location* yylloc)

/* By default yylex returns int, we use token_type.
   Unfortunately yyterminate by default returns 0, which is
   not of token_type.  */
#define yyterminate() return yy::mhmakeparser::token::END

%}

%option prefix="mhmake"
%option never-interactive
%option 8bit
%option c++
%option full
%option noyywrap
%option warn

/* -------------- rules section -------------- */
%x INCLUDE IFDEF IF IFNDEF SKIPUNTILELSEORENDIF QUOTE MAKEEXPRES SINGLEQUOTE COMMANDPARSE
%x IFEQ IFNEQ LOAD_MAKEFILE
%x DEFINE

%{
# define YY_USER_ACTION  yylloc->columns (yyleng);

#define inclineno() yylloc->incline()
#define lineno()    yylloc->end.line
#define colno()     yylloc->end.column


%}
%%
%{
  yylloc->step ();
%}

 /*---------------------------------------------------------------------------*/
[ \t\r]*\n[ ][ \t]* {
  yy_set_bol(1); // Make sure the next rule also matches the ^
  inclineno();
  return yy::mhmakeparser::token::NEWLINE;
}

[ \t\r]*\n {
  PRINTF(("%s %d: NEWLINE:\n",m_InputFileName.c_str(),lineno()));
  inclineno();
  return yy::mhmakeparser::token::NEWLINE;
}

 /*---------------------------------------------------------------------------*/
^[s\-]?include {
  PRINTF(("%s %d: INCLUDE: ",m_InputFileName.c_str(),lineno()));
  BEGIN(INCLUDE);
  unsigned i=0;
  while (strchr(" \t",yytext[i])) i++;
  if (strchr("-s",yytext[i]))
    m_IgnoreIncludeError=true;
  else
    m_IgnoreIncludeError=false;
  return yy::mhmakeparser::token::INCLUDEMAK;  // Return a newline to be sure that the previous line is completely parse by yacc (in case it is a variable definition)
}

 /*****************************************************************************/
<INCLUDE>[ \t]*      /* eat the whitespace */
 /*---------------------------------------------------------------------------*/
<INCLUDE>[^\r\n]+ { /* got the include file name */
  mhmakefileparser *pParser=GetParser();

  /* replace the {} by () before expanding */
  string IncludeFileNames(yytext);
  ReplaceCurlyBraces(IncludeFileNames);
  IncludeFileNames=pParser->ExpandExpression(IncludeFileNames);
  PRINTF(("%s -> %s\n",yytext,IncludeFileNames.c_str()));

  const char *pTmp=IncludeFileNames.c_str();
  while (*pTmp)
  {
    string IncludeFileName;
    pTmp=NextItem(pTmp,IncludeFileName);
    if (!IncludeFileName.empty())
    {
      PRINTF(("%s -> %s\n",yytext,IncludeFileName.c_str()));
      fileinfo *pInclude=GetFileInfo(IncludeFileName,pParser->GetMakeDir());
      /* Already build the include file, in case we already have a rule for it. */
      if (pInclude->GetRule())
      {
        uint32 Md5_32=LoadMakMd5(pInclude);
        pParser->BuildTarget(pInclude);
        if (!pInclude->CompareMd5_32(Md5_32))
          SaveMakMd5(pInclude);
      }

      pParser->AddIncludedMakefile(pInclude);

      string strToInclude=pInclude->GetFullFileName();
      INSTACK *pStackElem=new INSTACK(YY_CURRENT_BUFFER, strToInclude, m_InputFileName, yylloc);
      if ( pStackElem->fail() )
      {
        delete pStackElem;
        if (!m_IgnoreIncludeError)
        {
          mystack::reverse_iterator StackIt=m_IncludeStack.rbegin();
          while (StackIt!=m_IncludeStack.rend())
          {
            cout<<" in "<<(*StackIt)->m_FileName<<" ("<<(*StackIt)->yylloc<<")";
            StackIt++;
          }
          cout<<endl;
          cout<<"Warning error opening file "<<strToInclude<<" in "<<m_InputFileName<<" ("<<lineno()<<")\n";
          pParser->IncludeAfterBuild(strToInclude);
        }
        else
         pInclude->SetPhony();  /* To be sure that no message is printed when mhmake is trying to build the file later */
      }
      else
      {
        m_IncludeStack.push(pStackElem);

        m_InputFileName=strToInclude;

        string listVar;
        GetParser()->GetVariable("MAKEFILE_LIST", listVar);
        if (listVar.length()!=0)
          listVar+=" ";
        GetParser()->SetVariable("MAKEFILE_LIST", listVar+strToInclude);

        yylloc->initialize(&m_InputFileName);

        yypush_buffer_state(yy_create_buffer( pStackElem->GetStream(), YY_BUF_SIZE ));
        yyrestart(pStackElem->GetStream());
      }

    }
  }

  BEGIN(INITIAL);
}

 /*---------------------------------------------------------------------------*/
load_makefile {
  PRINTF(("%s %d: LOAD_MAKEFILE:\n",m_InputFileName.c_str(),lineno()));
  BEGIN(LOAD_MAKEFILE);
  return yy::mhmakeparser::token::NEWLINE;  // Return a newline to be sure that the previous line is completely parse by yacc (in case it is a variable definition)
}

 /*****************************************************************************/
<LOAD_MAKEFILE>[^\r\n]+ {
  string ListOfMakefiles((const char*)yytext);
  ReplaceCurlyBraces(ListOfMakefiles);
  ListOfMakefiles=GetParser()->ExpandExpression(ListOfMakefiles);
  PRINTF(("%s %d: LOAD_MAKEFILE: '%s'\n",m_InputFileName.c_str(),lineno(),ListOfMakefiles.c_str()));

  const char *pTmp=ListOfMakefiles.c_str();
  while (*pTmp)
  {
    string Item;
    pTmp=NextCharItem(pTmp,Item,';');
    if (Item.empty())
    {
      throw m_InputFileName + "(" + stringify(lineno()) + "): Error in load_makefile statement";
    }
    GetParser()->AddMakefileToMakefilesToLoad(Item);
  }

}
 /*---------------------------------------------------------------------------*/
<LOAD_MAKEFILE>\r?\n {
  inclineno();
  BEGIN(INITIAL);
  return yy::mhmakeparser::token::NEWLINE;
}

 /*---------------------------------------------------------------------------*/
[ \t]+ {
  PRINTF(("%s %d: SPACE:\n",m_InputFileName.c_str(),lineno()));
  return yy::mhmakeparser::token::SPACE;
}

 /*---------------------------------------------------------------------------*/
[ \t]*override[ \t]* {
  // override is not supported by mhmake, it is the default behavior
  PRINTF(("%s %d: SPACE (ignoring override):\n",m_InputFileName.c_str(),lineno()));
  return yy::mhmakeparser::token::SPACE;
}

 /*---------------------------------------------------------------------------*/
[ \t]*\|[ \t]*\\[ \t\r]*\n[ \t]* {
  PRINTF(("%s %d: EQUAL: %s\n",m_InputFileName.c_str(),lineno(),yytext));
  inclineno();
  yylval->theString=" | ";
  return yy::mhmakeparser::token::PIPE;
}

[ \t]*\|[ \t]* {
  PRINTF(("%s %d: EQUAL: %s\n",m_InputFileName.c_str(),lineno(),yytext));
  yylval->theString=" | ";
  return yy::mhmakeparser::token::PIPE;
}

 /*---------------------------------------------------------------------------*/
[ \t]*=[ \t]*\\[ \t\r]*\n[ \t]* {
  PRINTF(("%s %d: EQUAL: %s\n",m_InputFileName.c_str(),lineno(),yytext));
  inclineno();
  yylval->theString=(const char *)yytext;
  return yy::mhmakeparser::token::EQUAL;
}

[ \t]*=[ \t]* {
  PRINTF(("%s %d: EQUAL: %s\n",m_InputFileName.c_str(),lineno(),yytext));
  yylval->theString=(const char *)yytext;
  return yy::mhmakeparser::token::EQUAL;
}

 /*---------------------------------------------------------------------------*/
[ \t]*:=[ \t]*\\[ \t\r]*\n[ \t]* {
  inclineno();
  PRINTF(("%s %d: IMEQUAL: %s\n",m_InputFileName.c_str(),lineno(),yytext));
  return yy::mhmakeparser::token::IMEQUAL;
}

[ \t]*:=[ \t]* {
  PRINTF(("%s %d: IMEQUAL: %s\n",m_InputFileName.c_str(),lineno(),yytext));
  return yy::mhmakeparser::token::IMEQUAL;
}

 /*---------------------------------------------------------------------------*/
[ \t]*\?=[ \t]*\\[ \t\r]*\n[ \t]* {
  inclineno();
  PRINTF(("%s %d: OPTEQUAL: %s\n",m_InputFileName.c_str(),lineno(),yytext));
  return yy::mhmakeparser::token::OPTEQUAL;
}

[ \t]*\?=[ \t]* {
  PRINTF(("%s %d: OPTEQUAL: %s\n",m_InputFileName.c_str(),lineno(),yytext));
  return yy::mhmakeparser::token::OPTEQUAL;
}

 /*---------------------------------------------------------------------------*/
[ \t]*\+=[ \t]*\\[ \t\r]*\n[ \t]* {
  PRINTF(("%s %d: PEQUAL: %s\n",m_InputFileName.c_str(),lineno(),yytext));
  inclineno();
  return yy::mhmakeparser::token::PEQUAL;
}

[ \t]*\+=[ \t]* {
  PRINTF(("%s %d: PEQUAL: %s\n",m_InputFileName.c_str(),lineno(),yytext));
  return yy::mhmakeparser::token::PEQUAL;
}

 /*---------------------------------------------------------------------------*/
\\; {
  PRINTF(("%s %d: -Escaped SEMICOLON (STRING): %s\n",m_InputFileName.c_str(),lineno(),yytext));
  yylval->theString=(const char *)yytext+1;
  return yy::mhmakeparser::token::STRING;
}
 /*---------------------------------------------------------------------------*/
[ \t]*;[ \t]*\\[ \t\r]*\n[ \t]* {
  PRINTF(("%s %d: -SEMICOLON (NEWLINE): %s\n",m_InputFileName.c_str(),lineno(),yytext));
  m_curtoken=g_EmptyString;
  inclineno();
  BEGIN(COMMANDPARSE);
  return yy::mhmakeparser::token::NEWLINE;
}

[ \t]*;[ \t]* {
  PRINTF(("%s %d: -SEMICOLON (NEWLINE): %s\n",m_InputFileName.c_str(),lineno(),yytext));
  m_curtoken=g_EmptyString;
  BEGIN(COMMANDPARSE);
  return yy::mhmakeparser::token::NEWLINE;
}

 /*---------------------------------------------------------------------------*/
[ \t]*::[ \t]*\\[ \t\r]*\n[ \t]* {
  PRINTF(("%s %d: DOUBLECOLON: %s\n",m_InputFileName.c_str(),lineno(),yytext));
  inclineno();
  yylval->theString=(const char *)yytext;
  return yy::mhmakeparser::token::DOUBLECOLON;
}

[ \t]*::[ \t]* {
  PRINTF(("%s %d: DOUBLECOLON: %s\n",m_InputFileName.c_str(),lineno(),yytext));
  yylval->theString=(const char *)yytext;
  return yy::mhmakeparser::token::DOUBLECOLON;
}

 /*---------------------------------------------------------------------------*/
[ \t]*:[ \t]*\\[ \t\r]*\n[ \t]* {
  PRINTF(("%s %d: COLON: %s\n",m_InputFileName.c_str(),lineno(),yytext));
  inclineno();
  yylval->theString=(const char *)yytext;
  return yy::mhmakeparser::token::COLON;
}

[ \t]*:[ \t]* {
  PRINTF(("%s %d: COLON: %s\n",m_InputFileName.c_str(),lineno(),yytext));
  yylval->theString=(const char *)yytext;
  return yy::mhmakeparser::token::COLON;
}

 /*---------------------------------------------------------------------------*/
, {
  PRINTF(("%s %d: COMMA: %s\n",m_InputFileName.c_str(),lineno(),yytext));
  yylval->theString=(const char *)yytext;
  return yy::mhmakeparser::token::COMMA;
}

 /*---------------------------------------------------------------------------*/
^endif {
  if (m_IndentStack.size())
  {
    m_IndentStack.pop();
    PRINTF(("%s %d: %s: depth %zd\n",m_InputFileName.c_str(),lineno(),yytext,m_IndentStack.size()));
  }
  else
  {
     throw string("Unexpected endif at line ") + stringify(lineno()) + " of " + m_InputFileName;
  }
}

 /*---------------------------------------------------------------------------*/
^ifdef[ \t]*\\[ \t\r]*\n[ \t]* {
  BEGIN(IFDEF);
  inclineno();
  return yy::mhmakeparser::token::NEWLINE;
}

^ifdef[ \t]+ {
  BEGIN(IFDEF);
  return yy::mhmakeparser::token::NEWLINE;
}

 /*---------------------------------------------------------------------------*/
^if[ \t]*\\[ \t\r]*\n[ \t]* {
  BEGIN(IF);
  inclineno();
  m_curtoken=g_EmptyString;
  return yy::mhmakeparser::token::NEWLINE;
}

^if[ \t]+ {
  BEGIN(IF);
  m_curtoken=g_EmptyString;
  return yy::mhmakeparser::token::NEWLINE;
}

 /*---------------------------------------------------------------------------*/
^ifndef[ \t]*\\[ \t\r]*\n[ \t]* {
  BEGIN(IFNDEF);
  inclineno();
  return yy::mhmakeparser::token::NEWLINE;
}

^ifndef[ \t]+ {
  BEGIN(IFNDEF);
  return yy::mhmakeparser::token::NEWLINE;
}

 /*---------------------------------------------------------------------------*/
^ifeq[ \t]*\\[ \t\r]*\n[ \t]* {
  BEGIN(IFEQ);
  m_curtoken=g_EmptyString;
  inclineno();
  return yy::mhmakeparser::token::NEWLINE;
}

^ifeq[ \t]+ {
  BEGIN(IFEQ);
  m_curtoken=g_EmptyString;
  return yy::mhmakeparser::token::NEWLINE;
}

 /*---------------------------------------------------------------------------*/
^ifneq[ \t]*\\[ \t\r]*\n[ \t]* {
  BEGIN(IFNEQ);
  m_curtoken=g_EmptyString;
  inclineno();
  return yy::mhmakeparser::token::NEWLINE;
}

^ifneq[ \t]+ {
  BEGIN(IFNEQ);
  m_curtoken=g_EmptyString;
  return yy::mhmakeparser::token::NEWLINE;
}

 /*---------------------------------------------------------------------------*/
^else[ \t]* {
  if (m_IndentStack.size() && (!m_IndentStack.top()))
  {
    PRINTF(("%s %d: skipping else: depth %zd\n",m_InputFileName.c_str(),lineno(),m_IndentStack.size()));
    m_IndentSkip=m_IndentStack.size();
    m_IndentStack.top()=1;
    BEGIN(SKIPUNTILELSEORENDIF);
  }
  else
  {
     throw string("Unexpected else at line ") + stringify(lineno()) + " of file " + m_InputFileName;
  }
}

 /*****************************************************************************/
<IFEQ>\n {
  yyless(0);
  m_IndentStack.push(0);
  if (GetParser()->IsEqual(m_curtoken))
  {
    PRINTF(("%s %d: Not Skipping ifeq %s: depth %zd\n",m_InputFileName.c_str(),lineno(),m_curtoken.c_str(),m_IndentStack.size()));
    BEGIN(INITIAL);
  }
  else
  {
    PRINTF(("%s %d: Skipping ifeq %s: depth %zd\n",m_InputFileName.c_str(),lineno(),m_curtoken.c_str(),m_IndentStack.size()));
    m_IndentSkip=m_IndentStack.size();
    BEGIN(SKIPUNTILELSEORENDIF);
  }
}

<IF,IFEQ,IFNEQ>[ \t]*\\[ \t\r]*\n[ \t]* { inclineno(); m_curtoken += g_SpaceString;}

<IF,IFEQ,IFNEQ>\r    /* skip */

<IF,IFEQ,IFNEQ>[^\\\r\n\$#]+ |
<IF,IFEQ,IFNEQ>[\\\$]         { m_curtoken += (const char *)yytext; }

 /*****************************************************************************/
<IFNEQ>\n {
  yyless(0);
  m_IndentStack.push(0);
  if (!GetParser()->IsEqual(m_curtoken))
  {
    PRINTF(("%s %d: Not Skipping ifneq %s: depth %zd\n",m_InputFileName.c_str(),lineno(),m_curtoken.c_str(),m_IndentStack.size()));
    BEGIN(INITIAL);
  }
  else
  {
    PRINTF(("%s %d: Skipping ifneq %s: depth %zd\n",m_InputFileName.c_str(),lineno(),m_curtoken.c_str(),m_IndentStack.size()));
    m_IndentSkip=m_IndentStack.size();
    BEGIN(SKIPUNTILELSEORENDIF);
  }
}

 /*****************************************************************************/
<IF>[ \t\r]*[a-zA-Z0-9_]+[ \t\r]*\n {
  yyless(yyleng-1);
  m_IndentStack.push(0);
#ifndef WIN32
  int lastidx=yyleng-1;
  if (yytext[lastidx]=='\r')
    yytext[lastidx]='\0';
#endif
  string Val=GetParser()->ExpandVar((const char *)yytext);
  if (Val.empty() || Val=="0")
  {
    PRINTF(("%s %d: Skipping if %s: depth %zd\n",m_InputFileName.c_str(),lineno(),yytext,m_IndentStack.size()));
    m_IndentSkip=m_IndentStack.size();
    BEGIN(SKIPUNTILELSEORENDIF);
  }
  else
  {
    PRINTF(("%s %d: Not Skipping if %s: depth %zd\n",m_InputFileName.c_str(),lineno(),yytext,m_IndentStack.size()));
    BEGIN(INITIAL);
  }
}

 /*---------------------------------------------------------------------------*/
<IF>\n {
  yyless(0);
  m_IndentStack.push(0);
  if (GetParser()->IsExprTrue(m_curtoken))
  {
    PRINTF(("%s %d: Not Skipping ifeq %s: depth %zd\n",m_InputFileName.c_str(),lineno(),m_curtoken.c_str(),m_IndentStack.size()));
    BEGIN(INITIAL);
  }
  else
  {
    PRINTF(("%s %d: Skipping ifeq %s: depth %zd\n",m_InputFileName.c_str(),lineno(),m_curtoken.c_str(),m_IndentStack.size()));
    m_IndentSkip=m_IndentStack.size();
    BEGIN(SKIPUNTILELSEORENDIF);
  }
}

 /*****************************************************************************/
<IFDEF,IFNDEF>[ \t\r]*  /* skip */

 /*---------------------------------------------------------------------------*/
<IFDEF>[a-zA-Z0-9_]+ {
  m_IndentStack.push(0);
  if (GetParser()->IsDefined((const char *)yytext))
  {
    PRINTF(("%s %d: Not Skipping ifdef %s: depth %zd\n",m_InputFileName.c_str(),lineno(),yytext,m_IndentStack.size()));
    BEGIN(INITIAL);
  }
  else
  {
    PRINTF(("%s %d: Skipping ifdef %s: depth %zd\n",m_InputFileName.c_str(),lineno(),yytext,m_IndentStack.size()));
    m_IndentSkip=m_IndentStack.size();
    BEGIN(SKIPUNTILELSEORENDIF);
  }
}

 /*****************************************************************************/
<IFNDEF>[a-zA-Z0-9_]+ {
  m_IndentStack.push(0);
  if (!GetParser()->IsDefined((const char *)yytext)) {
    PRINTF(("%s %d: Not Skipping ifndef %s: depth %zd\n",m_InputFileName.c_str(),lineno(),yytext,m_IndentStack.size()));
    BEGIN(INITIAL);
  }
  else
  {
    PRINTF(("%s %d: Skipping ifndef %s: depth %zd\n",m_InputFileName.c_str(),lineno(),yytext,m_IndentStack.size()));
    m_IndentSkip=m_IndentStack.size();
    BEGIN(SKIPUNTILELSEORENDIF);
  }
}

 /*****************************************************************************/
<SKIPUNTILELSEORENDIF>\n[ ]*endif {
  inclineno();
  if (!m_IndentStack.size())
  {
    throw string("Unexpected endif at line ") + stringify(lineno()) + " of " + m_InputFileName;
  }
  else
  {
    m_IndentStack.pop();
    PRINTF(("%s %d: endif: depth %zd\n",m_InputFileName.c_str(),lineno(),m_IndentStack.size()));
    if (m_IndentStack.size()==m_IndentSkip-1) BEGIN(INITIAL);
  }
}

 /*---------------------------------------------------------------------------*/
<SKIPUNTILELSEORENDIF>\n[ ]*else {
  inclineno();
  PRINTF(("%s %d: else: depth %zd\n",m_InputFileName.c_str(),lineno(),m_IndentStack.size()));
  if (m_IndentStack.top())
  {
    throw string("Unexpected else at line ") + stringify(lineno()) + " of file " + m_InputFileName;
  }
  m_IndentStack.top()=1;
  if (m_IndentStack.size()==m_IndentSkip)
  {
    BEGIN(INITIAL);
  }
}

 /*---------------------------------------------------------------------------*/
<SKIPUNTILELSEORENDIF>\n[ ]*if(def|ndef|eq|neq)? {
  inclineno();
  m_IndentStack.push(0);
  PRINTF(("%s %d: %s: depth %zd\n",m_InputFileName.c_str(),lineno(),yytext+1,m_IndentStack.size()));
}

 /*---------------------------------------------------------------------------*/
<SKIPUNTILELSEORENDIF>[a-zA-Z]+  /* skip */
 /*---------------------------------------------------------------------------*/
<SKIPUNTILELSEORENDIF>[^a-zA-Z\n]+ /* skip */
 /*---------------------------------------------------------------------------*/
<SKIPUNTILELSEORENDIF>\n[ ]*[a-zA-Z]+ {
  inclineno();
}
 /*---------------------------------------------------------------------------*/
<SKIPUNTILELSEORENDIF>\n {
  inclineno();
}

 /*---------------------------------------------------------------------------*/
<INITIAL,COMMANDPARSE,IF,IFEQ,IFNEQ>[ \t]*#[^\n]* {
  PRINTF(("%s %d: -COMMENT: %s\n",m_InputFileName.c_str(),lineno(),yytext));
}

 /*---------------------------------------------------------------------------*/
[ \t]*\\[ \t\r]*\n[ \t]* {
  PRINTF(("%s %d: SPACE:\n",m_InputFileName.c_str(),lineno()));
  inclineno();
  return yy::mhmakeparser::token::SPACE;
}

 /*---------------------------------------------------------------------------*/
\.PHONY {
  PRINTF(("%s %d: .PHONY: %s\n",m_InputFileName.c_str(),lineno(),yytext));
  return yy::mhmakeparser::token::PHONY;
}

 /*---------------------------------------------------------------------------*/
\.AUTODEPS {
  PRINTF(("%s %d: .AUTODEPS: %s\n",m_InputFileName.c_str(),lineno(),yytext));
  return yy::mhmakeparser::token::AUTODEPS;
}

 /*---------------------------------------------------------------------------*/
\.ENVVARS_TOIGNORE {
  PRINTF(("%s %d: .ENVVARS_TOIGNORE: %s\n",m_InputFileName.c_str(),lineno(),yytext));
  return yy::mhmakeparser::token::ENVVARS_TOIGNORE;
}

 /*---------------------------------------------------------------------------*/
export {
  PRINTF(("%s %d: export: %s\n",m_InputFileName.c_str(),lineno(),yytext));
  return yy::mhmakeparser::token::EXPORT;
}

 /*---------------------------------------------------------------------------*/
^vpath {
  PRINTF(("%s %d: vpath\n",m_InputFileName.c_str(),lineno()));
  return yy::mhmakeparser::token::VPATH;
}

 /*---------------------------------------------------------------------------*/
[a-zA-Z]:[a-zA-Z0-9\\\._\~\-%\@<&/]+\\[ \t\r]*\n {
  size_t EndIndex=yyleng;
  while (strchr(" \t\r\n\\",yytext[--EndIndex]));
  yyless(EndIndex+1);

  PRINTF(("%s %d: STRING: %s\n",m_InputFileName.c_str(),lineno(),yytext));
  yylval->theString=(const char *)yytext;
  return yy::mhmakeparser::token::STRING;
}

[a-zA-Z]:[a-zA-Z0-9\\\._\~\-%\@<&/]+ {
  PRINTF(("%s %d: STRING: %s\n",m_InputFileName.c_str(),lineno(),yytext));
  yylval->theString=(const char *)yytext;
  return yy::mhmakeparser::token::STRING;
}

 /*---------------------------------------------------------------------------*/
([a-zA-Z0-9\\\._\~\-\+%\@<&;/\*]|\\\ |\\#|\\\"|\|)+\\[ \t\r]*\n  {
  size_t EndIndex=yyleng;
  while (strchr(" \t\r\n\\",yytext[--EndIndex]));
  yyless(EndIndex+1);

  PRINTF(("%s %d: STRING: %s\n",m_InputFileName.c_str(),lineno(),yytext));
  yylval->theString=(const char *)yytext;
  return yy::mhmakeparser::token::STRING;
}

([a-zA-Z0-9\\\._\~\-\+%\@<&;/\*]|\\\ |\\#|\\\"|\|)+\+=  {
  PRINTF(("%s %d: STRING: %s\n",m_InputFileName.c_str(),lineno(),yytext));
  yyless(yyleng-2);
  yylval->theString=(const char *)yytext;
  return yy::mhmakeparser::token::STRING;
}

([a-zA-Z0-9\\\._\~\-\+%\@<&;/\*\[\]]|\\\ |\\#|\\\"|\|)+  {
  PRINTF(("%s %d: STRING: %s\n",m_InputFileName.c_str(),lineno(),yytext));
  yylval->theString=(const char *)yytext;
  return yy::mhmakeparser::token::STRING;
}

^define[ \t]+[a-zA-Z0-9_\.]+[ \t]*\n {
  const char *pVar=(const char *)yytext;
  while (strchr(" \t",*pVar)) pVar++;
  pVar+=6;
  yylval->theString=pVar;
  BEGIN(DEFINE);
  m_curtoken=g_EmptyString;
  PRINTF(("%s %d: VARDEF: %s\n",m_InputFileName.c_str(),lineno(),m_curtoken.c_str()));
  inclineno();
  return yy::mhmakeparser::token::VARDEF;
}

<DEFINE>[ \t]*\\[ \t\r]*\n[ \t]* {
  inclineno();
  m_curtoken+=g_SpaceString;
}

<DEFINE>\n {
  inclineno();
  m_curtoken+=(const char *)yytext;
}

<DEFINE>. {
  m_curtoken+=(const char *)yytext;
}

<DEFINE>[ \t]*\n[ \t]*endef {
  inclineno();
  yylval->theString=m_curtoken;
  PRINTF(("%s %d: VARVAL: %s\n",m_InputFileName.c_str(),lineno(),m_curtoken.c_str()));
  BEGIN(INITIAL);
  return yy::mhmakeparser::token::VARVAL;
}

 /*---------------------------------------------------------------------------*/
\"  {
  BEGIN(QUOTE);
  yymore();
}

 /*---------------------------------------------------------------------------*/
\' {
  BEGIN(SINGLEQUOTE);
  yymore();
}

 /*---------------------------------------------------------------------------*/
\$[\(\{]  {
  m_BraceIndent++;
  PRINTF(("%s %d: BEGIN MACRO $(: %d\n",m_InputFileName.c_str(),lineno(),m_BraceIndent));
  BEGIN(MAKEEXPRES);
  m_curtoken=(const char *)yytext;
}

 /*---------------------------------------------------------------------------*/
\$[<@/$] {
  PRINTF(("%s %d: DOLLAREXPR: %s\n",m_InputFileName.c_str(),lineno(),yytext));
  yylval->theString=(const char *)yytext;
  return yy::mhmakeparser::token::DOLLAREXPR;
}

 /*---------------------------------------------------------------------------*/
[ \t\r]*\n\t[ \t]* {
  /* token newline */
  PRINTF(("%s %d: NEWLINE\n",m_InputFileName.c_str(),lineno()));
  inclineno();
  m_curtoken=g_EmptyString;
  BEGIN(COMMANDPARSE);
  return yy::mhmakeparser::token::NEWLINE;
}

 /*---------------------------------------------------------------------------*/
[\(\)\{\}] {
  yylval->theString=(const char *)yytext;
  return yy::mhmakeparser::token::STRING;
}

 /*---------------------------------------------------------------------------*/
[^\n] {
  PRINTF(("%s %d: ANYCHAR: %d: %s\n",m_InputFileName.c_str(),lineno(),lineno(),yytext));
  throw string("Unexpected character '")+yytext+"' in makefile '" + m_InputFileName + "' at line "+stringify(lineno()) + ", column " + stringify(colno()-1);
}

 /*****************************************************************************/

<COMMANDPARSE>[ \t\r]*\n {
  PRINTF(("%s %d: COMMAND: %d: %s\n",m_InputFileName.c_str(),lineno(),lineno(),m_curtoken.c_str()));
  yylval->theString=m_curtoken;
  inclineno();
  BEGIN(INITIAL);
  return yy::mhmakeparser::token::COMMAND;
}

 /*---------------------------------------------------------------------------*/
<COMMANDPARSE>[ \t\r]*\n\t[ \t]* {
  PRINTF(("%s %d: COMMAND: %s\n",m_InputFileName.c_str(),lineno(),m_curtoken.c_str()));
  yylval->theString=m_curtoken;
  inclineno();
  m_curtoken=g_EmptyString;
  return yy::mhmakeparser::token::COMMAND;
}

 /*---------------------------------------------------------------------------*/
<COMMANDPARSE>[ \t]*\\[ \t\r]*\n[ \t]* {
  inclineno();
  m_curtoken+=g_SpaceString;
}

 /*---------------------------------------------------------------------------*/
<COMMANDPARSE>[ ]+ {
  m_curtoken+=g_SpaceString;
}

 /*---------------------------------------------------------------------------*/
<COMMANDPARSE>[^ \r\n#\\$]+ |
<COMMANDPARSE>[\\\$] {
  m_curtoken+=(const char *)yytext;
}

 /*---------------------------------------------------------------------------*/
<COMMANDPARSE>[ \t]*\\#[^\n]* {
  int nChars=(int)((strchr((const char *)yytext,'#')-(char*)yytext))+1;
  yyless(nChars);
  m_curtoken+=string(yytext,nChars-2);
  m_curtoken+='#';
}

 /*****************************************************************************/
<QUOTE>\" {
  PRINTF(("%s %d: QUOTEDSTRING: %s\n",m_InputFileName.c_str(),lineno(),yytext));
  BEGIN(INITIAL);
  yylval->theString=(const char *)yytext;
  return yy::mhmakeparser::token::STRING;
}

 /*---------------------------------------------------------------------------*/
<QUOTE>\r /* skip */

<QUOTE>[^\\\"\r\n$]+ |
<QUOTE>[\\\$]        |
<QUOTE>\\\"          |
<QUOTE>\\#  {
  yymore();
}

 /*****************************************************************************/
<SINGLEQUOTE>\' {
  PRINTF(("%s %d: QUOTEDSTRING: %s\n",m_InputFileName.c_str(),lineno(),yytext));
  BEGIN(INITIAL);
  yylval->theString=(const char *)yytext;
  return yy::mhmakeparser::token::STRING;
}

 /*---------------------------------------------------------------------------*/
<SINGLEQUOTE>\r /* skip */

<SINGLEQUOTE>[^\\\'\r\n$]+ |
<SINGLEQUOTE>[\\\$]        |
<SINGLEQUOTE>\\\'          |
<SINGLEQUOTE>\\# {
  yymore();
}

 /*****************************************************************************/
<MAKEEXPRES>[\)\}] {
  m_BraceIndent--;
  PRINTF(("%s %d: CLOSE BRACE MAKEEXPRES MACRO ): %d\n",m_InputFileName.c_str(),lineno(),m_BraceIndent));
  if (!m_BraceIndent)
  {
    BEGIN(INITIAL);
    m_curtoken+=(const char *)yytext;
    yylval->theString=m_curtoken;
    PRINTF(("%s %d: DOLLAREXPR: %s\n",m_InputFileName.c_str(),lineno(),m_curtoken.c_str()));
    return yy::mhmakeparser::token::DOLLAREXPR;
  }
  else
  {
    m_curtoken+=(const char *)yytext;
  }
}

 /*---------------------------------------------------------------------------*/
<MAKEEXPRES>\$[\(\{] {
  m_BraceIndent++;
  PRINTF(("%s %d: MACRO extra $(: %d\n",m_InputFileName.c_str(),lineno(),m_BraceIndent));
  m_curtoken+=(const char *)yytext;
}

 /*---------------------------------------------------------------------------*/
<MAKEEXPRES>[\(\{] {
  m_BraceIndent++;
  m_curtoken+=(const char *)yytext;
}

 /*---------------------------------------------------------------------------*/
<MAKEEXPRES>[^$\(\)\{\}\r\n\\]+ |
<MAKEEXPRES>[\(\$\\\{] {
  m_curtoken+=(const char *)yytext;
}
<MAKEEXPRES>[ \t\r]*\\[ \t\r]*\n[ \t\r]* {
  inclineno();
  m_curtoken+=g_SpaceString;
}

<MAKEEXPRES>\n {
  throw m_InputFileName + "(" + stringify(lineno()) + "): End of line inside macro is not allowed";
}

<SKIPUNTILELSEORENDIF><<EOF>> {
  throw string("Missing endif or else statement. #else or #endif used?");
}

<<EOF>> {
  if (m_BraceIndent)
  {
    throw string("Missing closing ) or } of macro usage in ") + m_InputFileName;
  }
  if (!m_IncludeStack.size())
  {
    if (m_IndentStack.size())
    {
      throw string("Missing endif or else statement in ") + m_InputFileName + ". #else or #endif used";
    }
    yyterminate();
  }
  else
  {
    INSTACK *pInStack=m_IncludeStack.top();
    yypop_buffer_state();
    m_InputFileName=pInStack->m_FileName;
    *yylloc=pInStack->yylloc;
    m_IncludeStack.pop();
  }
}
%%

