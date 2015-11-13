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

#include "stdafx.h"
#include "util.h"
#include "md5.h"

#include "mhmakefileparser.h"
#include "rule.h"
#include "flexlexer.h"
#include "mhmakeparser.hpp"

commandqueue             mhmakefileparser::sm_CommandQueue;
stack<yy::mhmakeparser*> mhmakefileparser::sm_ParserStack; // Keeps track of the currently active parser

string mhmakefileparser::GetFileNameLineNo(void) const
{
  if (!sm_ParserStack.empty())
  {
    yy::mhmakeparser* pParser=sm_ParserStack.top();

    if (this == pParser->getMakefile())
    {
      yy::position *pPos=pParser->GetCurPos();
      return pParser->GetInputFilename()+":"+stringify(pPos->line)+" ";
    }
    else
      return g_EmptyString; // The top of the parser stack is not valid anymore (probably in some exception handler)
  }
  else
    return g_EmptyString; // Currently not parsing, this is a run-time error after parsing
}

///////////////////////////////////////////////////////////////////////////////
int mhmakefileparser::ParseFile(const fileinfo *pFileInfo, const fileinfo *pMakeDir)
{
  if (pMakeDir)
  {
    m_MakeDir=pMakeDir;
    m_Variables[CURDIR]=m_MakeDir->GetQuotedFullFileName();
  }

  ifstream yyin(pFileInfo->GetFullFileName().c_str(),ios_base::in);
  if (yyin.fail())
  {
    cerr << "Error opening makefile: "<<pFileInfo->GetQuotedFullFileName()<<endl;
    return 1;
  }

  mhmakeFlexLexer theLexer(&yyin);

  theLexer.m_InputFileName=pFileInfo->GetFullFileName();
  theLexer.m_pMakefileParser=this;

  yy::mhmakeparser Parser(this,&theLexer);
  sm_ParserStack.push(&Parser);
  int Ret=Parser.parse();
  sm_ParserStack.pop();
  return Ret;
}

///////////////////////////////////////////////////////////////////////////////
int mhmakefileparser::ParseString(const string &StringIn)
{
  basic_istringstream<char> yyin(StringIn.c_str());

  mhmakeFlexLexer theLexer(&yyin);

  theLexer.m_InputFileName=string("eval(")+StringIn+")";
  theLexer.m_pMakefileParser=this;

  yy::mhmakeparser Parser(this,&theLexer);
  sm_ParserStack.push(&Parser);
  int Ret=Parser.parse();
  sm_ParserStack.pop();
  return Ret;
}

///////////////////////////////////////////////////////////////////////////////
bool mhmakefileparser::IsDefined(const string &Var) const
{
  bool Ret = m_Variables.find(Var)!=m_Variables.end();
  if (!Ret)
  {
    GetFromEnv(Var, &Ret);
  }
  return Ret;
}

///////////////////////////////////////////////////////////////////////////////
static inline size_t SkipUntilQuote(const string &Expr,size_t i,char Char)
{
  while (Expr[i++]!=Char) ;
  return i;
}

///////////////////////////////////////////////////////////////////////////////
// Splits expression on the Item, but the item may not occur within
// a macro or quoted string
static pair<string,string> SplitExpr(const string &Expr,char Item)
{
  size_t i=0;
  char Char=Expr[i++];
  while (Char!=Item)
  {
    if (Char=='"' || Char=='\'')
    {
      i=SkipUntilQuote(Expr,i,Char);
    }
    else if (Char=='$')
    {
      i=SkipMakeExpr(Expr,i);
    }
    Char=Expr[i++];
  }
  return pair<string,string>(Expr.substr(0,i-1),Expr.substr(i));
}

///////////////////////////////////////////////////////////////////////////////
bool mhmakefileparser::IsEqual(const string &EqualExpr) const
{
  string Expr=ExpandExpression(EqualExpr);
  const char *pStr=Expr.c_str();
  const char *pTmp=pStr;
  while (*pTmp && *pTmp!=',') pTmp++;
  ptrdiff_t Pos=pTmp-pStr;
  size_t Size=Expr.size();
  pTmp=pStr+Size-1;
  while (pTmp>pStr && strchr(" \t",*pTmp))
  {
    pTmp--;
  }
  if (2*Pos != pTmp-pStr)
  {
    return false;
  }
  pTmp=pStr;
  const char *pTmp2=pTmp+Pos+1;
  if (*pTmp=='(')
  {
    pTmp++;
    Pos--;
  }
  for (int i=0; i<Pos; i++)
  {
    if (pTmp[i]!=pTmp2[i])
    {
      return false;
    }
  }
  return true;
}

///////////////////////////////////////////////////////////////////////////////
inline string mhmakefileparser::ExpandExpression(const string &ExprIn) const
{
  bool Recurse=true;
  string Ret(ExprIn);
  while (Recurse)
  {
    Recurse=false;
    string Expr(Ret);
    Ret.clear();

    size_t i=0;
    size_t Length=Expr.size();
    string ToAdd;
    while (i<Length)
    {
      char Char=Expr[i++];
      if (Char=='$')
      {
        size_t inew=SkipMakeExpr(Expr,i);
        i++;
        if (inew>i)
        {
          ToAdd=ExpandMacro(Expr.substr(i,inew-i-1));
          i=inew;
        }
        else
        {
          // This is a single character expression
          ToAdd=ExpandMacro(string(1,Expr[i-1]));
        }
        if (ToAdd.find('$')!=string::npos && ToAdd.length()!=1)
        {
          Recurse=true;
        }
        Ret+=ToAdd;
      }
      else
      {
        Ret+=Char;
      }
    }
  }
  return Ret;
}

///////////////////////////////////////////////////////////////////////////////
string mhmakefileparser::ExpandMacro(const string &Expr) const
{
  const char *pTmp=Expr.c_str();
  /* First remove leading spaces */
  while (*pTmp==' ' || *pTmp=='\t') pTmp++;
  const char *pVar=pTmp;
  while (*pTmp && *pTmp!=' ' && *pTmp!='\t' && *pTmp!=':')
  {
    if (*pTmp=='$' && pTmp[1]) // We have a macro expansion inside a macro expansion, so recurse Do not consider isolated $ (This was a $$)
    {
      return ExpandMacro(ExpandExpression(Expr));
    }
    pTmp++;
  }
  const char *pVarEnd=pTmp;
  char Type=*pTmp++;
  while (*pTmp && (*pTmp==' ' || *pTmp=='\t')) pTmp++;
  if (Type&&*pTmp)
  { // We have a match for the regular expression ^([^ \\t:]+)([: \\t])[ \\t]*(.+)
    if (Type==':')
    {
      #ifdef WIN32
      bool IsFileName=false;
      if (pVarEnd-pVar == 1 && (*pVar=='<' || *pVar =='@'))
        IsFileName=true;
      #endif
      string ToSubst=ExpandExpression(ExpandVar(string(pVar,pVarEnd)));
      const char *pSrc=pTmp;
      const char *pStop=pSrc;
      while (*pStop!='=') pStop++;
      const char *pTo=pStop+1;
      string SrcStr(ExpandExpression(string(pSrc,pStop)));
      string ToStr(pTo);  // Do not expand yet to be able to use % inside a macro
      #ifdef WIN32
      if (IsFileName)
      {
        matchres Res;
        string FileName(UnquoteFileName(ToSubst));
        if (PercentMatch(FileName,UnquoteFileName(SrcStr),&Res))
        {
          FileName=ReplaceWithStem(UnquoteFileName(ToStr),Res.m_Stem);
        }
        return QuoteFileName(FileName);
      }
      #endif
      return Substitute(ToSubst,SrcStr,ToStr);
    }
    else if (Type==' ' || Type == '\t')
    {
      string Arg(pTmp);
      string Func(pVar,pVarEnd);
      function_f pFunc=m_Functions[Func];
      #ifdef _DEBUG
      if (pFunc)
      {
        return (this->*pFunc)(Arg);
      }
      else
      {
        throw GetFileNameLineNo() + "Unknown function specified in macro: "+Func;
      }
      #else
      return (this->*pFunc)(Arg);
      #endif
    }
    else
    {
    #ifdef _DEBUG
      throw string("Fatal error in ExpandMacro (bug in mhmake ? ? ?)");
    #else
      return g_EmptyString;
    #endif
    }
  }
  else
  {
    #ifdef _DEBUG
    if (Expr.find('$')!=string::npos && Expr.length()!=1)
      throw(string("Bug in mhmake: wasn't expecting a $ sign in: ")+Expr);
    #endif
    string Ret=ExpandVar(Expr);
    if (Ret.length()>1)
      return ExpandExpression(Ret);
    else
      return Ret;
  }
}

///////////////////////////////////////////////////////////////////////////////
string mhmakefileparser::ExpandVar(const string &Var) const
{
  map<string,string>::const_iterator pIt=m_Variables.find(Var);
  if (pIt==m_Variables.end())
  {
    if (Var.size()==1)
    {
      char Char=Var[0];
      if (m_RuleThatIsBuild)
      {
        switch (Char)
        {
          case '<': // return first prerequisit
#ifdef _DEBUG
            if (!m_RuleThatIsBuild->GetDeps().size())
            {
              return "<No Dependencies defined.>";
            }
#endif
            return m_RuleThatIsBuild->GetDeps()[0]->GetQuotedFullFileName();
          case '@': // return full target file name
            return m_RuleThatIsBuild->GetQuotedFullFileName();
          case '*': // return stem
            return m_RuleThatIsBuild->GetRule()->GetStem();
          case '^': // return all prerequisits
            return m_RuleThatIsBuild->GetPrerequisits();
          case '/':
            return OSPATHSEPSTR;
          case '$':
            return Var;
          case 'n':
            return "\n";
          default:
            break;
        }
      }
      else
      {
        switch (Char)
        {
          case '<': // return first prerequisit
          case '@': // return full target file name
          case '*': // return stem
          case '^': // return all prerequisits
          case '$': // $$ expansion
            return Var;  // To make comparing of rules more accurate
          case '/':
            return OSPATHSEPSTR;
          case 'n':
            return "\n";
          default:
            break;
        }
      }
    }
    string Env=GetFromEnv(Var);
    if (Env.empty())
    {
      #ifdef _DEBUG
      if (g_PrintAdditionalInfo)
      {
        cout<<"Warning: Variable "<<Var<<" not found\n";
      }
      #endif
      return g_EmptyString;
    }
    else
      return Env;
  }
  else
    return pIt->second;
}

///////////////////////////////////////////////////////////////////////////////
void mhmakefileparser::SplitToItems(const string &String,vector<fileinfo*> &Items) const
{
  const char *pTmp=String.c_str();
  while (*pTmp)
  {
    string Item;
    pTmp=NextItem(pTmp,Item);
    if (!Item.empty() && Item!="|")
    {
      Items.push_back(GetFileInfo(Item,m_MakeDir));
    }
  }
}

///////////////////////////////////////////////////////////////////////////////
void mhmakefileparser::ParseBuildedIncludeFiles()
{
  vector<string>::iterator It=m_ToBeIncludeAfterBuild.begin();
  while (It!=m_ToBeIncludeAfterBuild.end())
  {
    int result=ParseFile(GetFileInfo(*It,m_MakeDir));
    if (result)
    {
      throw string("Error parsing ")+*It;
    }
    It++;
  }
}

#ifdef _DEBUG
///////////////////////////////////////////////////////////////////////////////
void mhmakefileparser::PrintVariables(bool Expand) const
{
  map<string,string>::const_iterator It=m_Variables.begin();
  while (It!=m_Variables.end())
  {
    if (Expand)
    {
      try
      {
        cout<<It->first<<" : "<<ExpandExpression(It->second)<<endl;
      }
      catch (...)
      {
        cout<<endl;
      }
    }
    else
    {
      cout<<It->first<<" : "<<It->second<<endl;
    }
    It++;
  }
  if (m_pEnv)
  {
    cout << "Environment overruled:\n";
#ifdef WIN32
    char *pEnv=m_pEnv;
    while (*pEnv)
    {
      cout<<pEnv<<endl;
      pEnv+=strlen(pEnv)+1;
    }
#else
    char **pEnv=m_pEnv;
    while (*pEnv)
    {
      cout<<*pEnv<<endl;
      pEnv++;
    }
#endif
  }
}
#endif

//#define PAGETOSTRING(Nr)  #Nr
//#define PAGETONBR(Nr)  PAGETOSTRING(##Nr)
//#pragma message("ar=" PAGETONBR(NULL) ";")

///////////////////////////////////////////////////////////////////////////////
void mhmakefileparser::AddRule()
{
  fileinfoarray::iterator pIt=m_pCurrentItems->begin();
  while (pIt!=m_pCurrentItems->end())
  {
    if ((*pIt)->GetFullFileName().find('%')!=string::npos)
    {
      IMPLICITRULE::AddImplicitRule(*pIt,*m_pCurrentDeps,m_pCurrentRule);
    }
    else
    {
      // If we had a double colon we must make sure that the target is always build
      if (m_DoubleColonRule)
      {
        (*pIt)->SetNotExist();
      }
      (*pIt)->SetRuleIfNotExist(m_pCurrentRule);
      if (!m_pCurrentRule)
        (*pIt)->AddDeps(*m_pCurrentDeps);
      else
        (*pIt)->InsertDeps(*m_pCurrentDeps);
      if (!m_FirstTarget)
      { // Only check this if the rule is not an implicit rule
        m_FirstTarget=(*m_pCurrentItems)[0];
      }
    }
    pIt++;
  }
  m_pCurrentItems=NULL;
  m_pCurrentRule=NullRule;
}

///////////////////////////////////////////////////////////////////////////////
void mhmakefileparser::GetAutoDeps(const fileinfo *pFirstDep, deps_t &Autodeps)
{
  /* Here we have to scan only c/c++ headers so skip certain extensions */
  const char *pFullName=pFirstDep->GetFullFileName().c_str();
  const char *pExt=strrchr(pFullName,'.');
  bool bPython=false;
  if (pExt)
  {
    if (!_stricmp(pExt+1,"py"))
      bPython=true;
  }

  FILE *pIn=fopen(pFullName,"r");
  if (!pIn)
    return;

  char IncludeList[255];
  int PrevRet=0;
  int Ret=fgetc(pIn);
  while(Ret!=EOF)
  {
    char Type[2];
    bool bFound=false;
    if (bPython)
    {
      Type[0]='"';
      if (Ret=='i')
      {
        Ret=fscanf(pIn,"mport%*[ \t]%254[^\"\n]",IncludeList);
        if (Ret==1)
        {
          if (IncludeList[0]!='*')
            bFound=true;
        }
      }
    }
    else
    {
      if (PrevRet=='/')
      {
        if (Ret=='/')
        {
          /* This is a C++ command so read until the next line-feed */
          do
          {
            Ret=fgetc(pIn);
          } while (Ret!='\n' && Ret!=EOF);
        }
        else if (Ret=='*')
        {
          /* This is a standard C comment, so read until then end of the command */
          do
          {
            PrevRet=Ret;
            Ret=fgetc(pIn);
          } while ((PrevRet!='*' || Ret!='/') && Ret!=EOF);
        }
      }
      else if (Ret=='#')
      {
        Ret=fscanf(pIn,"%*[ \t]");
        Ret=fscanf(pIn,"include%*[ \t]%1[\"<]%254[^>\"]%*[\">]",(char*)&Type,IncludeList);
        if (Ret==2)
        {
          bFound=true;
        }
      }
      else if (Ret=='.')
      {
        Ret=fgetc(pIn);
        if (Ret=='i')
        {
          Ret=fgetc(pIn);
          if (Ret=='m')
            Ret=fscanf(pIn,"port%*[ \t]%1[\"<]%254[^>\"]%*[\">]",(char*)&Type,IncludeList);
          else if (Ret=='n')
            Ret=fscanf(pIn,"clude%*[ \t]%1[\"<]%254[^>\"]%*[\">]",(char*)&Type,IncludeList);
          if (Ret==2)
          {
            bFound=true;
          }
        }
      }
    }
    if (bFound)
    {
      const char *pTmp=IncludeList;
      while (*pTmp)
      {
        string IncludeFile;
        pTmp=NextItem(pTmp,IncludeFile," \t,");
        if (bPython)
          IncludeFile+=".py";

        if (SkipHeaderFile(IncludeFile))
          continue;
        #ifdef _DEBUG
        m_ImplicitSearch++; // This is to avoid warnings of targets that does not exist
        #endif
        if (Type[0]=='"')
        {
          fileinfo *pInclude=GetFileInfo(IncludeFile,pFirstDep->GetDir());
          /* Add the dependency when the file alrady exist or there is a rule available to be build */
          mh_time_t Date=BuildTarget(pInclude);
          if (Date.DoesExist())  // Try to build the target, and add it if it exists after building
          {
            deps_t::const_iterator pFind=Autodeps.find(pInclude);
            if (pFind==Autodeps.end())
            {
              Autodeps.insert(pInclude);
              GetAutoDepsIfNeeded(pInclude,pInclude);
            }
          }
        }
        const refptr<fileinfoarray> IncludeDirs=GetIncludeDirs();
        fileinfoarray::const_iterator It=IncludeDirs->begin();
        while (It<IncludeDirs->end())
        {
          fileinfo *pInclude=GetFileInfo(IncludeFile,*It);
          mh_time_t Date=BuildTarget(pInclude);
          if (Date.DoesExist()) // Try to build the target, and add it if it exists after building
          {
            deps_t::const_iterator pFind=Autodeps.find(pInclude);
            if (pFind==Autodeps.end())
            {
              Autodeps.insert(pInclude);
              GetAutoDepsIfNeeded(pInclude,pInclude);
            }
            break;
          }
          It++;
        }
        #ifdef _DEBUG
        m_ImplicitSearch--;
        #endif
      }
    }
    PrevRet=Ret;
    Ret=fgetc(pIn);
  }
  fclose(pIn);
}

///////////////////////////////////////////////////////////////////////////////

void mhmakefileparser::GetAutoDepsIfNeeded(fileinfo *pTarget, const fileinfo *pFirstDep)
{
  autodeps_entry_t &Autodeps=m_AutoDeps[pTarget];
  if (!Autodeps.first)
  {
    Autodeps.first=true;
    /* We are going to rescan, so throw away the old. */
    Autodeps.second.clear();
    GetAutoDeps(pFirstDep,Autodeps.second);
    // Now add these dependencies also to the rules
    deps_t::iterator It=Autodeps.second.begin();
    while (It!=Autodeps.second.end())
    {
      pTarget->AddDep(*It);
      It++;
    }
  }
}

///////////////////////////////////////////////////////////////////////////////

void mhmakefileparser::UpdateAutomaticDependencies(fileinfo *pTarget)
{
  if (pTarget->IsAutoDepExtention())
  {
    // we have to search for the include files in the first dependency of Target
    vector<fileinfo*> &Deps=pTarget->GetDeps();
    if (!Deps.size())
      return; // There is no first dep
    fileinfo *pFirstDep=Deps[0];
    GetAutoDepsIfNeeded(pTarget,pFirstDep);
    SetAutoDepsDirty();
  }
}

///////////////////////////////////////////////////////////////////////////////
void mhmakefileparser::UpdateNoRuleAutomaticDependencies(fileinfo *pTarget)
{
  // we have to search for the include files in the Target
  deps_t Autodeps;
  GetAutoDeps(pTarget,Autodeps);
  // Now add these dependencies also to the rules
  deps_t::iterator It=Autodeps.begin();
  while (It!=Autodeps.end())
  {
    pTarget->AddDep(*It);
    It++;
  }
}

///////////////////////////////////////////////////////////////////////////////
const refptr<fileinfoarray> mhmakefileparser::GetIncludeDirs() const
{
  string Includes=ExpandExpression("$(INCLUDES)");
  if (!m_pIncludeDirs || Includes != m_IncludeDirs)
  {
    ((mhmakefileparser*)this)->m_IncludeDirs=Includes;
    ((mhmakefileparser*)this)->m_pIncludeDirs=refptr<fileinfoarray>(new fileinfoarray);
    if (Includes.empty())  // If not defined try a default path
      Includes="include inc .." OSPATHSEPSTR "include .." OSPATHSEPSTR "inc";
    const char *pTmp=Includes.c_str();
    while (*pTmp)
    {
      string Item;
      pTmp=NextItem(pTmp,Item);
      fileinfo *pIncDir=GetFileInfo(Item,m_MakeDir);
      if (pIncDir->Exists() || pIncDir->GetRule())
        ((mhmakefileparser*)this)->m_pIncludeDirs->push_back(pIncDir);
    }
  }
  return m_pIncludeDirs;
}

static void ReadStr(FILE *pFile,char *Str)
{
  int i=0;
  while (1)
  {
    Str[i]=fgetc(pFile);
#ifdef _DEBUG
    if (Str[i]==EOF)
    {
      cout<<"Premature end of depency file.\n";
      Str[i]=0;
      return;
    }
#endif
    if (Str[i]=='\n')
      break;
    i++;
  }
  Str[i]='\0';
}

void mhmakefileparser::LoadAutoDepsFile(fileinfo *pDepFile)
{
  if (m_AutoDepFileLoaded && m_AutoDepFileLoaded==pDepFile)
    return;  /* This autodep file is already loaded. */

  m_AutoDepFileLoaded=pDepFile;

  FILE *pIn=fopen(pDepFile->GetFullFileName().c_str(),"rb");
  if (!pIn)
  {
    cerr << "Error opening autodep file "<<pDepFile->GetQuotedFullFileName()<<endl;
    return;
  }
  if (1!=fread(&m_EnvMd5_32,sizeof(m_EnvMd5_32),1,pIn))
  {
    cerr << "Wrong format of autodep file "<<pDepFile->GetQuotedFullFileName()<<endl;
    fclose(pIn);
    return;
  }
#ifdef _DEBUG
  if (g_PrintAdditionalInfo)
    cout << "Reading Env Md5 from "<<pDepFile->GetQuotedFullFileName()<<": "<<hex<<m_EnvMd5_32<<endl;
#endif
  char UsedEnvVars[1024];
  ReadStr(pIn,UsedEnvVars);
  SetVariable(USED_ENVVARS,UsedEnvVars);

  char FileName[MAX_PATH];
  ReadStr(pIn,FileName);
  while (FileName[0])
  {
    fileinfo *pTarget=GetFileInfo(FileName,m_MakeDir);
    autodeps_entry_t &Autodeps=m_AutoDeps[pTarget];
    ReadStr(pIn,FileName);
    while (FileName[0])
    {
      if (!g_ForceAutoDepRescan)  /* If we are forcing the autodepscan we do not have to load the dependencies. */
      {
        fileinfo *pDep=GetFileInfo(FileName,m_MakeDir);
        Autodeps.second.insert(pDep);
        pTarget->AddDep(pDep);
      }
      ReadStr(pIn,FileName);
    }
    ReadStr(pIn,FileName);
  }

  uint32 Md5_32;
  bool MakeNotDirty=true;
  while (fread(&Md5_32,sizeof(Md5_32),1,pIn))
  {
    ReadStr(pIn,FileName);

    fileinfo *pTarget=GetAbsFileInfo(FileName);

    if (!pTarget->CompareMd5_32(0) && !pTarget->CompareMd5_32(Md5_32))
    {
      MakeNotDirty=false; /* BuildTarget had set the dirty flag, but since the md5 did not change it was a false dirty. This is for BuildTargets called before this routine */
      #ifdef _DEBUG
      cout << "Warning: trying to set to different md5's for Target "<<pTarget->GetQuotedFullFileName()<<" Old: "<<hex<<pTarget->GetCommandsMd5_32()<<" New: "<<Md5_32<<endl;
      #endif
    }
    #ifdef _DEBUG
    if (g_PrintAdditionalInfo)
      cout << "Setting Md5 for Target "<<pTarget->GetQuotedFullFileName()<<" to "<<hex<<Md5_32<<endl;
    #endif
    pTarget->SetCommandsMd5_32(Md5_32);  // If it was already there, just update the md5 value

    AddTarget(pTarget);
  }
  if (MakeNotDirty)
    ClearAutoDepsDirty();

  fclose(pIn);
}

void mhmakefileparser::SaveAutoDepsFile()
{
  if (!IsAutoDepsDirty())
    return;

  if (g_Clean
#ifdef _DEBUG
      || g_DoNotExecute || g_GenProjectTree
#endif
     )
    return;   // do not save on clean or if no commands are executed

  string DepFile=ExpandVar(AUTODEPFILE);
  if (!DepFile.size())
  {
    return;
  }
  fileinfo *pDepFile=GetFileInfo(DepFile,m_MakeDir);

#ifdef _DEBUG
  if (g_PrintAdditionalInfo)
    cout<<"Saving automatic dependency file "<<DepFile<<endl;
#endif

  FILE *pOut=fopen(pDepFile->GetFullFileName().c_str(),"wb");
  if (!pOut)
  {
    /* Maybe it is because the directory does not exist, so try to create this first */
    if (!MakeDirs(pDepFile->GetDir()))
    {
      #ifdef _DEBUG
      if (!g_DoNotExecute)
      #endif
        cerr << "Error creating dir "<<pDepFile->GetDir()->GetFullFileName()<<endl;
      return;
    }
    pOut=fopen(pDepFile->GetFullFileName().c_str(),"wb");

    if (!pOut)
    {
      #ifdef _DEBUG
      if (!g_DoNotExecute)
      #endif
        cerr << "Error creating file "<<DepFile<<endl;
      return;
    }
  }
  // First update the USER_ENVVARS variable and then save it to the dep file together with the md5 string
  CreateUSED_ENVVARS();
  uint32 Md5_32=CreateEnvMd5_32();
  fwrite(&Md5_32,sizeof(Md5_32),1,pOut);
  fprintf(pOut,"%s\n",m_Variables[USED_ENVVARS].c_str());

  autodeps_t::const_iterator It=m_AutoDeps.begin();
  while (It!=m_AutoDeps.end())
  {
    if (!It->second.second.empty())
    {
      fprintf(pOut,"%s\n",It->first->GetFullFileName().c_str());
      deps_t::const_iterator DepIt=It->second.second.begin();
      while (DepIt!=It->second.second.end())
      {
        fprintf(pOut,"%s\n",(*DepIt)->GetFullFileName().c_str());
        DepIt++;
      }
      fprintf(pOut,"\n");
    }
    It++;
  }
  /* Now save the Md5 strings */
  fprintf(pOut,"\n");

  set<const fileinfo *>::iterator pIt=m_Targets.begin();
  while (pIt!=m_Targets.end())
  {
    if (!(*pIt)->CompareMd5_32(0))
    {
      (*pIt)->WriteMd5_32(pOut);
      string FileName=(*pIt)->GetFullFileName();
      fwrite(FileName.c_str(),FileName.size(),1,pOut);
      fputc('\n',pOut);
    }
    pIt++;
  }

  fclose(pOut);
}

///////////////////////////////////////////////////////////////////////////////
// Uses the SKIPHEADERS variable to check if we have to skip the header in
// the automatic dependency scanning

bool mhmakefileparser::SkipHeaderFile(const string &FileName)
{
  if (!m_SkipHeadersInitialized)
  {
    m_SkipHeadersInitialized=true;
    string HeadersToSkip=ExpandVar(SKIPHEADERS);
    const char *pTmp=HeadersToSkip.c_str();
    while (*pTmp)
    {
      string Item;
      pTmp=NextItem(pTmp,Item);
      if (Item.find('%')==string::npos)
      {
        m_SkipHeaders.insert(Item);
      }
      else
      {
        m_PercentHeaders.push_back(Item);
      }
    }
  }
  if (m_SkipHeaders.find(FileName)!=m_SkipHeaders.end())
    return true;

  vector<string>::const_iterator It=m_PercentHeaders.begin();
  vector<string>::const_iterator ItEnd=m_PercentHeaders.end();
  while (It!=ItEnd)
  {
    if (PercentMatch(FileName,*It++))
      return true;
  }

  return false;
}

///////////////////////////////////////////////////////////////////////////////
void mhmakefileparser::SetExport(const string &Var, const string &Val)
{
  m_Exports.insert(Var);
#ifdef WIN32
  if (!m_pEnv)
  {
    /* Environment not created yet, so create one */
    char *pEnv=GetEnvironmentStrings();
    char *pEnd=pEnv;
    while (*pEnd)
    {
      while (*pEnd++);
    }
    size_t Len=pEnd-pEnv+1;
    m_pEnv=(char*)malloc(Len);
    memcpy(m_pEnv,pEnv,Len);
    m_EnvLen=Len;
    FreeEnvironmentStrings(pEnv);
  }
  /* First check if the variable is in the environment, if so remove it first */
  char *pEnv=m_pEnv;
  while (*pEnv)
  {
    const char *pVar=Var.c_str();
    char *pStart=pEnv;
    while (*pEnv!='=' && tolower(*pEnv)==tolower(*pVar))
    {
      pEnv++; pVar++;
    }
    if (*pEnv=='=' && !*pVar)
    {
      /* Variable found, remove it */
      while (*pEnv++);
      m_EnvLen-=pEnv-pStart;
      while (*pEnv)
      {
        while (*pEnv)
        {
          *pStart=*pEnv++;
          pStart++;
        }
        *pStart=*pEnv++;
        pStart++;
      }
      *pStart=*pEnv;
      break;
    }
    while (*pEnv++);
  }
  size_t VarLen=Var.length();
  size_t ValLen=Val.length();
  size_t Extra=VarLen+ValLen+2;
  /* Add the variable at the end */
  m_pEnv=(char*)realloc(m_pEnv,m_EnvLen+Extra);
  pEnv=m_pEnv+m_EnvLen-1;
  memcpy(pEnv,Var.c_str(),VarLen);
  pEnv+=VarLen;
  *pEnv++='=';
  memcpy(pEnv,Val.c_str(),ValLen);
  pEnv+=ValLen;
  *pEnv++='\0';
  *pEnv++='\0';
  m_EnvLen+=Extra;
#else
  if (!m_pEnv)
  {
    /* Environment not created yet, so create one */
    char **pEnv=environ;
    char **pEnd=pEnv;
    int Len=1;
    while (*pEnd)
    {
      Len++;
      pEnd++;
    }
    m_EnvLen=Len;
    m_pEnv=(char**)malloc(Len*sizeof(pEnv));
    int i=0;
    while (*pEnv)
    {
      m_pEnv[i]=strdup(*pEnv);
      i++;
      pEnv++;
    }
    m_pEnv[i]=NULL;
  }
  /* First check if the variable is in the environment, if so replace it */
  char **pEnv=m_pEnv;
  while (*pEnv)
  {
    const char *pVar=Var.c_str();
    char *pStart=*pEnv;
    char *pTmp=pStart;
    while (*pTmp!='=' && *pTmp==*pVar)
    {
      pTmp++; pVar++;
    }
    if (*pTmp=='=' && !*pVar)
    {
      free(*pEnv);
      *pEnv=strdup((Var+"="+Val).c_str());
      break;
    }
    pEnv++;
  }
  if (!*pEnv)
  {
    // Add it at the end of the list
    m_pEnv=(char**)realloc(m_pEnv,(m_EnvLen+1)*sizeof(*pEnv));
    m_pEnv[m_EnvLen-1]=strdup((Var+"="+Val).c_str());
    m_pEnv[m_EnvLen++]=NULL;
  }

#endif
}

///////////////////////////////////////////////////////////////////////////////
void mhmakefileparser::SetvPath(const string &Pattern, const string &Path)
{
  // First create the array of directory from the Path parameter. For now all entries need to
  // be valid directories (extra checking). This could be improved by checking if there
  // is a rule for creating the directory entry, but like first said, not for now.
  refptr<fileinfoarray> pDirArray=new fileinfoarray;

  const char *pTmp=Path.c_str();
  while (*pTmp)
  {
    string Item;
    pTmp=NextItem(pTmp,Item," \t:;");
    if (!Item.empty())
    {
      fileinfo *pDir=GetFileInfo(Item,m_MakeDir);
      if (!pDir->IsDir())
      {
        #ifdef WIN32
        // On windows this could happen if a full pathname was specified, so we try together
        // with the next item to check if this is not a valid directories
        // but only if the the length of the item was 1 namely the driver letter
        if (Item.length()==1)
        {
          string Rest;
          pTmp=NextItem(pTmp,Rest," \t:;");
          pDir=GetFileInfo(Item+":"+Rest,m_MakeDir);
          #ifdef _DEBUG
          if (g_PrintAdditionalInfo && !pDir->IsDir())
            throw(pDir->GetFullFileName()+" is not a valid directory.");
          #endif
        }
        #ifdef _DEBUG
        else if (g_PrintAdditionalInfo)
          cout << pDir->GetFullFileName() << " is not a valid directory.\n";
        #endif
        #else
        #ifdef _DEBUG
        if (g_PrintAdditionalInfo)
          cout << pDir->GetFullFileName() << " is not a valid directory.\n";
        #endif
        #endif
      }
      pDirArray->push_back(pDir);
    }
  }
  m_vPath.push_back(pair<string, refptr<fileinfoarray> >(Pattern,pDirArray));
}

///////////////////////////////////////////////////////////////////////////////
//
// Search for the target using the vPath
fileinfo* mhmakefileparser::SearchvPath(const fileinfo* pTarget)
{
  string TargetName=pTarget->GetName();
  vector< pair< string, refptr<fileinfoarray> > >::iterator vPathIt=m_vPath.begin();
  while (vPathIt!=m_vPath.end())
  {
    matchres Res;

    if (PercentMatch(TargetName,vPathIt->first,&Res))
    {
      fileinfoarray::iterator pIt=vPathIt->second->begin();
      while (pIt!=vPathIt->second->end())
      {
        fileinfo* pNewTarget=GetFileInfo(TargetName,*pIt);
        mh_time_t TargetDate=StartBuildTarget(pNewTarget,false);
        if (!TargetDate.IsDateValid())
          TargetDate=WaitBuildTarget(pNewTarget);
        if (pNewTarget->GetDate().DoesExist())
          return pNewTarget;
        pIt++;
      }
    }
    vPathIt++;
  }
  return NULL;
}

///////////////////////////////////////////////////////////////////////////////
//
//Checks if the variables retreived from the environment or command-line have been
//changed. Do this at late as possible because they can also be changed in theLexer
//makefiles.
//
void mhmakefileparser::CheckEnv(void)
{
  if (CompareEnv())
  {
    #ifdef _DEBUG
    if (!g_GenProjectTree)
      cout << "Rebuilding everything of "<< m_MakeDir->GetQuotedFullFileName() <<" because environment and/or command-line variables have been changed.\n";
    #endif
    SetRebuildAll();
  }

}

///////////////////////////////////////////////////////////////////////////////
//
//Create a Md5 string from m_GlobalCommandLineVars and USED_ENVVARS
//


#define SkipVar(Var) (m_EnvVarsToIgnore.find(Var)!=m_EnvVarsToIgnore.end())

#define DBGOUT(stuff)

uint32 mhmakefileparser::CreateEnvMd5_32() const
{
  md5_context ctx;
  string Md5;
  string EnvVars=ExpandVar(USED_ENVVARS);
  const char *pTmp=EnvVars.c_str();

  // Now create the md5 string
  md5_starts( &ctx );

  DBGOUT(cout << "MD5 of " << m_MakeDir->GetQuotedFullFileName() << endl);

  while (*pTmp)
  {
    string Var;
    pTmp=NextItem(pTmp,Var,";");
    if (!SkipVar(Var))
    {
      string Val=ExpandVar(Var);
      transform(Val.begin(),Val.end(),Val.begin(),(int(__CDECL *)(int))toupper);
      DBGOUT(cout << GetMakeDir()->GetQuotedFullFileName() << " -> Setting GetFromEnv var " << Var << " to " << Val << endl);
      md5_update( &ctx, (uint8 *) Var.c_str(), (unsigned long)Var.size());
      md5_update( &ctx, (uint8 *) "=", 1);
      md5_update( &ctx, (uint8 *) Val.c_str(), (unsigned long)Val.size());
    }
  }

  return md5_finish32( &ctx);
}

///////////////////////////////////////////////////////////////////////////////
// Makes sure that the makefile exports are set in the environment

bool mhmakefileparser::CompareEnv() const
{
  uint32 Md5_32=CreateEnvMd5_32();
#ifdef _DEBUG
  if (!g_GenProjectTree && Md5_32!=m_EnvMd5_32)
    cout << "Environment has been changed: "<<hex<<m_EnvMd5_32<<" to "<<Md5_32<<endl;
#endif
  return Md5_32!=m_EnvMd5_32;
}

///////////////////////////////////////////////////////////////////////////////
// Makes sure that the makefile exports are set in the environment
mh_time_t mhmakefileparser::m_sBuildTime;

void mhmakefileparser::InitBuildTime()
{
#ifdef WIN32
  FILETIME ft;
  GetSystemTimeAsFileTime(&ft);
  m_sBuildTime=*(mh_basetime_t*)&ft;
#else
  m_sBuildTime=time(NULL);
#endif
}

///////////////////////////////////////////////////////////////////////////////
// Returns a variable from the environment or from the command line and adds it the m_UsedEnvVars
string mhmakefileparser::GetFromEnv(const string &Var, bool *pDefined) const
{
  /* First we look into the command line variables, before we are looking in the environment */
  map<string,string>::const_iterator pLineFind=m_CommandLineVars.find(Var);
  if (pLineFind!=m_CommandLineVars.end())
  {
    ((mhmakefileparser*)this)->m_UsedEnvVars.insert(Var);
    ((mhmakefileparser*)this)->m_Variables[Var]=pLineFind->second;
    if (pDefined) *pDefined=true;
    return pLineFind->second;
  }

  const char *pEnv=getenv(Var.c_str());
  if (!pEnv)
  {
    if (pDefined) *pDefined=false;
    return g_EmptyString;
  }
  ((mhmakefileparser*)this)->m_UsedEnvVars.insert(Var);

  ((mhmakefileparser*)this)->m_Variables[Var]=pEnv;
  if (pDefined) *pDefined=true;
  return pEnv;
}

///////////////////////////////////////////////////////////////////////////////
// Creates the variable USED_ENVVARS to be saved in the autodeps file
//

void mhmakefileparser::CreateUSED_ENVVARS()
{

  set<string> Variables;

  set<string>::const_iterator It=m_UsedEnvVars.begin();
  set<string>::const_iterator ItEnd=m_UsedEnvVars.end();

  while (It!=ItEnd)
  {
    string Var=*It;
    if (!SkipVar(Var))
    {
      transform(Var.begin(),Var.end(),Var.begin(),(int(__CDECL *)(int))toupper);
      Variables.insert(Var);
    }
    It++;
  }

  It=m_Exports.begin();
  ItEnd=m_Exports.end();

  while (It!=ItEnd)
  {
    string Var=*It;
    if (!SkipVar(Var))
    {
      transform(Var.begin(),Var.end(),Var.begin(),(int(__CDECL *)(int))toupper);
      Variables.insert(Var);
    }
    It++;
  }

  map<string,string>::const_iterator CLItEnd=loadedmakefile::sm_Statics.m_GlobalCommandLineVars.end();
  map<string,string>::const_iterator CLIt=loadedmakefile::sm_Statics.m_GlobalCommandLineVars.begin();
  while (CLIt!=CLItEnd)
  {
    string Var=CLIt->first;
    if (!SkipVar(Var))
    {
      transform(Var.begin(),Var.end(),Var.begin(),(int(__CDECL *)(int))toupper);
      Variables.insert(Var);
    }
    CLIt++;
  }

  It=Variables.begin();
  ItEnd=Variables.end();
  string Val;
  while (It!=ItEnd)
  {
    Val+=*It;
    Val+=";";
    It++;
  }

  m_Variables[USED_ENVVARS]=Val;
}

static string s_TrueString("1");
static string s_FalseString("0");

static string AndExpression(const string &First, const string &Second)
{
  if (First.empty() || First==s_FalseString)
  {
    return g_EmptyString;
  }
  else
  {
    return Second.empty() || Second==s_FalseString ? g_EmptyString : s_TrueString;
  }
}

static string OrExpression(const string &First, const string &Second)
{
  if (First.empty() || First==s_FalseString)
  {
    return Second.empty() || Second==s_FalseString ? g_EmptyString : s_TrueString;
  }
  else
  {
    return s_TrueString;
  }
}

string mhmakefileparser::ResolveExpression(const string &InExpr,string &Rest) const
{
  unsigned i=0;
  string Ret;
  string Expr=InExpr;

  Rest=g_EmptyString;

  while (i<Expr.length())
  {
    while (strchr(" \t\r",Expr[i])) i++;
    switch (Expr[i])
    {
    case '!':
      if (i==Expr.length()-1)
      {
        i++; // to break out of the loop
        Ret=s_TrueString; /* the input was ! which means true */
      }
      else if (Expr[i+1]!='=')
      {
        Ret=ResolveExpression(Expr.substr(i+1),Expr);
        Ret = Ret.empty() || Ret==s_FalseString ? s_TrueString : g_EmptyString;
        i=0;
      }
      else
      {
        Ret = Ret!=ResolveExpression(Expr.substr(i+2),Expr) ? s_TrueString : g_EmptyString;
        i=0;
      }
      break;
    case '&':
      #ifdef _DEBUG
      if (i==Expr.length()-1)
      {
        throw(string("Error in expression near ")+Expr[i]+": "+InExpr);
      } else
      #endif
      if (Expr[i+1]!='&')
      {
        Ret+=Expr[i++];
      }
      else
      {
        Ret=AndExpression(Ret,ResolveExpression(Expr.substr(i+2),Expr));
        i=0;
      }
      break;
    case '|':
      #ifdef _DEBUG
      if (i==Expr.length()-1)
      {
        throw(string("Error in expression near ")+Expr[i]+": "+InExpr);
      } else
      #endif
      if (Expr[i+1]!='|')
      {
        Ret+=Expr[i++];
      }
      else
      {
        Ret=OrExpression(Ret,ResolveExpression(Expr.substr(i+2),Expr));
        i=0;
      }
      break;
    case '(':
      if (Ret=="defined")
      {
        Ret=IsDefined(ResolveExpression(Expr.substr(i+1),Expr)) ? s_TrueString : g_EmptyString;
      }
      else
      {
        Ret+=ResolveExpression(Expr.substr(i+1),Expr);
      }
      i=0;
      break;
    case ')':
      Rest=Expr.substr(i+1);
      return Ret;
      break;
    case '"':
      {
        i++;
        while (i<Expr.length())
        {
          char Char=Expr[i++];
          if (Char=='"')
            break;
          Ret+=Char;
        }
      }
      break;
    case '=':
      if (i==Expr.length()-1)
      {
        i++; // To break out of the loop
      }
      else
      if (Expr[i+1]!='=')
      {
        Ret+=Expr[i++];
      }
      else
      {
        Ret = Ret==ResolveExpression(Expr.substr(i+2),Expr) ? s_TrueString : g_EmptyString;
        i=0;
      }
      break;
    default:
      Ret+=Expr[i++];
      break;
    }
  }
  return Ret;
}
///////////////////////////////////////////////////////////////////////////////
bool mhmakefileparser::IsExprTrue(const string &EqualExpr) const
{
  string Expr=ExpandExpression(EqualExpr);
  string Rest;
  Expr=ResolveExpression(Expr,Rest);
  if (Expr.empty() || Expr==s_FalseString)
    return false;
  else
    return true;
}

