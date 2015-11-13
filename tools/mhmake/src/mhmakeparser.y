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

%require "3.0.2"
%defines
%define parser_class_name { mhmakeparser }
%define parser_base_class_name { mhmakeparserbase }
%define parser_class_constructor_init { : mhmakeparserbase(pMakefile,pLexer) }
%define parser_class_constructor_param { mhmakefileparser *pMakefile, mhmakeFlexLexer *pLexer }
%error-verbose

%code requires {
#include "mhmakefileparser.h"
}

%code provides {
const char Test[]="dit is een test";
}

%{
#include "mhmakefileparser.h"
#include "rule.h"
#include "util.h"
%}

%locations
%initial-action
{
  string listVar;
  m_pMakefile->GetVariable("MAKEFILE_LIST", listVar);
  if (listVar.length()!=0)
    listVar+=" ";
  m_pMakefile->SetVariable("MAKEFILE_LIST", listVar+m_ptheLexer->GetInputFilename());

  // Initialize the initial location.
  @$.initialize(&m_ptheLexer->GetInputFilename());
};

%token END      0 "end of file"
%token <theString> COMMAND
%token <theString> COMMA
%token <theString> STRING DOLLAREXPR PIPE EQUAL COLON DOUBLECOLON VARDEF VARVAL
%token IMEQUAL PEQUAL OPTEQUAL PHONY AUTODEPS EXPORT NEWLINE INCLUDEMAK SPACE VPATH ENVVARS_TOIGNORE

%type <theString> expression nonspaceexpression simpleexpression
%type <theString> maybeemptyexpression
%type <theString> expression_nocolorequal simpleexpression_nocolorequal nonspaceexpression_nocolorequal
%type <ival> rulecolon

%start file

/* -------------- rules section -------------- */
/* Sample parser. Does count Chars in a line, and lines in file */
%%
file : statements
       {
         if (m_pMakefile->m_pCurrentItems)
         {
           PRINTF(("Adding rule : %s\n",(*m_pMakefile->m_pCurrentItems)[0]->GetQuotedFullFileName().c_str()));
           m_pMakefile->AddRule();
         }
       }
;

statements :
             | statements statement
;

space : SPACE |
        space SPACE
;

statement: NEWLINE |
           SPACE |
           DOLLAREXPR NEWLINE {
             m_pMakefile->ExpandExpression($1);
           } |
           includemak |
           ruledef |
           phonyrule |
           autodepsrule |
           envvarstoignorerule |
           varassignment |
           imvarassignment |
           pvarassignment |
           optvarassignment |
           exportrule |
           vpathrule |
           COMMAND
           {
             if (m_pMakefile->m_pCurrentItems)
             {
               if (!m_pMakefile->m_pCurrentRule)
               {
                 m_pMakefile->m_pCurrentRule=refptr<rule>(new rule(m_pMakefile));
               }
               m_pMakefile->m_pCurrentRule->AddCommand($1);
               PRINTF(("Adding command : %s\n",$1.c_str()));
             }
             else
             {
               PRINTF(("Throwing away command : %s\n",$1.c_str()));
             }
           }
;

includemak:
         {
           if (m_pMakefile->m_pCurrentItems)
           {
             PRINTF(("Adding rule : %s\n",(*m_pMakefile->m_pCurrentItems)[0]->GetQuotedFullFileName().c_str()));
             m_pMakefile->AddRule();
           }
         } INCLUDEMAK
;

ruledef: expression_nocolorequal rulecolon maybeemptyexpression
         {
           if (m_pMakefile->m_pCurrentItems && m_pMakefile->m_pCurrentItems->size()>0)
           {
             PRINTF(("Adding rule : %s\n",(*m_pMakefile->m_pCurrentItems)[0]->GetQuotedFullFileName().c_str()));
             m_pMakefile->AddRule();
           }

           if (!m_pMakefile->ExpandExpression($1).size())
           {
             //throw string("Empty left hand side in rule: ") + $1 + " : " + $3;
             #ifdef _DEBUG
             cout << "Empty left hand side in rule: " << $1 << " : " << $3 << endl;
             #endif
             // We have to skip the commands that follow (rules with empty left hand size are thrown away)
             m_pMakefile->m_pCurrentItems=NULL;
             m_pMakefile->m_pCurrentDeps=NULL;
           }
           else
           {
             m_pMakefile->m_pCurrentItems=new fileinfoarray;
             m_pMakefile->m_pCurrentDeps=new fileinfoarray;

             m_pMakefile->SplitToItems(m_pMakefile->ExpandExpression($1), *m_pMakefile->m_pCurrentItems);
             m_pMakefile->SplitToItems(m_pMakefile->ExpandExpression($3), *m_pMakefile->m_pCurrentDeps);
             m_pMakefile->m_DoubleColonRule= ($2==1) ;
             PRINTF(("Defining rule %s : %s\n",$1.c_str(),$3.c_str()));
             PRINTF(("  Expanded to %s : %s\n",m_pMakefile->ExpandExpression($1).c_str(),m_pMakefile->ExpandExpression($3).c_str()));
           }
         }
;

rulecolon: COLON {$$=0;} |
           DOUBLECOLON {$$=1;}
;

phonyrule: PHONY COLON expression
           {
             vector<fileinfo*> Items;
             m_pMakefile->SplitToItems(m_pMakefile->ExpandExpression($3),Items);
             vector<fileinfo*>::iterator pIt=Items.begin();
             while (pIt!=Items.end())
             {
               (*pIt)->SetPhony();
               pIt++;
             }
             PRINTF(("Defining phony rule : %s\n",$3.c_str()));
             PRINTF(("  Expanded to : %s\n",m_pMakefile->ExpandExpression($3).c_str()));
           }
           NEWLINE
;

autodepsrule: AUTODEPS COLON expression
           {
             vector<fileinfo*> Items;
             m_pMakefile->SplitToItems(m_pMakefile->ExpandExpression($3),Items);
             vector<fileinfo*>::iterator pIt=Items.begin();
             while (pIt!=Items.end())
             {
               (*pIt)->SetAutoDepsScan(m_pMakefile);
               pIt++;
             }
             PRINTF(("Defining autodeps rule : %s\n",$3.c_str()));
             PRINTF(("  Expanded to : %s\n",m_pMakefile->ExpandExpression($3).c_str()));
           }
           NEWLINE
;

envvarstoignorerule: ENVVARS_TOIGNORE COLON expression
           {
             string VarsStr=m_pMakefile->ExpandExpression($3);
             PRINTF(("Defining envvarstoignore rule : %s\n",$3.c_str()));
             PRINTF(("  Expanded to : %s\n",m_pMakefile->ExpandExpression($3).c_str()));
             const char *pTmp=VarsStr.c_str();
             while (*pTmp)
             {
               string Var;
               pTmp=NextItem(pTmp,Var);
               if (!Var.empty())
               {  // Add it to the list of env vars to ignore
                 m_pMakefile->m_EnvVarsToIgnore.insert(Var);
               }
             }
           }
           NEWLINE
;


exportrule: EXPORT space exportstrings NEWLINE |
            EXPORT space STRING EQUAL maybeemptyexpression
            {
              string Val=m_pMakefile->ExpandExpression($5);
              m_pMakefile->SetVariable($3,Val);
              m_pMakefile->SetExport($3,Val);
              PRINTF(("Exporting %s : %s\n",$3.c_str(), Val.c_str()));
            }
;

exportstrings : exportstring |
                exportstring space exportstrings
;

exportstring : STRING
               {
                 m_pMakefile->SetExport($1,m_pMakefile->ExpandExpression(m_pMakefile->ExpandVar($1)));
                 PRINTF(("Exporting %s : %s\n",$1.c_str(),m_pMakefile->ExpandExpression(m_pMakefile->ExpandVar($1)).c_str()));
               }
;

vpathrule: VPATH space nonspaceexpression space expression NEWLINE
           {
             m_pMakefile->SetvPath(m_pMakefile->ExpandExpression($3),m_pMakefile->ExpandExpression($5));
             PRINTF(("Setting vpath %s to %s\n",$3.c_str(),m_pMakefile->ExpandExpression($5).c_str()));
           }
;

varassignment: VARDEF VARVAL
               {
                 m_pMakefile->m_Variables[m_pMakefile->f_strip($1)]=$2;
                 PRINTF(("Defining variable %s to %s\n",m_pMakefile->f_strip($1).c_str(), $2.c_str()));
               }
               | STRING EQUAL maybeemptyexpression
               {
                 m_pMakefile->m_Variables[$1]=$3;
                 PRINTF(("Setting variable %s to %s\n",$1.c_str(), $3.c_str()));
               }
;

imvarassignment: STRING IMEQUAL maybeemptyexpression
               {
                 m_pMakefile->m_Variables[$1]=m_pMakefile->ExpandExpression($3);
                 PRINTF(("Setting variable %s to %s\n",$1.c_str(), m_pMakefile->m_Variables[$1].c_str()));
               }
;

pvarassignment: STRING PEQUAL maybeemptyexpression
               {
                 m_pMakefile->m_Variables[$1]=m_pMakefile->ExpandVar($1)+g_SpaceString+$3;
                 PRINTF(("Adding to variable %s: %s\n",$1.c_str(), $3.c_str()));
               }
;

optvarassignment: STRING OPTEQUAL maybeemptyexpression
               {
                 if (!m_pMakefile->IsDefined($1))
                 {
                   m_pMakefile->m_Variables[$1]=$3;
                   PRINTF(("Setting variable %s to %s\n",$1.c_str(), $3.c_str()));
                 }
               }
;

maybeemptyexpression: NEWLINE {$$=g_EmptyString;} |
                      expression NEWLINE |
                      expression space NEWLINE
;

expression: nonspaceexpression |
            expression space nonspaceexpression {$$=$1+g_SpaceString+$3;}
;

expression_nocolorequal: nonspaceexpression_nocolorequal |
                         expression_nocolorequal space nonspaceexpression_nocolorequal {$$=$1+g_SpaceString+$3;}
;

nonspaceexpression: simpleexpression |
                    nonspaceexpression simpleexpression {$$=$1+$2;}
;

nonspaceexpression_nocolorequal: simpleexpression_nocolorequal |
                    nonspaceexpression_nocolorequal simpleexpression_nocolorequal {$$=$1+$2;}
;

simpleexpression: simpleexpression_nocolorequal |
                  PIPE |
                  EQUAL |
                  COLON |
                  DOUBLECOLON |
                  COMMA
;

simpleexpression_nocolorequal: STRING |
                               DOLLAREXPR
;

%%
/* -------------- body section -------------- */

void yy::mhmakeparser::error (const yy::mhmakeparser::location_type& l, const std::string& m)
{
  cerr << l << " -> "<<m<<endl;
}
