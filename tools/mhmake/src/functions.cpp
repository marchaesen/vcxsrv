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
#include "mhmakefileparser.h"
#include "rule.h"

static const string s_QuoteString("\"");

funcdef mhmakefileparser::m_FunctionsDef[]= {
  {"filter",     &mhmakefileparser::f_filter}
 ,{"call",       &mhmakefileparser::f_call}
 ,{"subst",      &mhmakefileparser::f_subst}
 ,{"patsubst",   &mhmakefileparser::f_patsubst}
 ,{"concat",     &mhmakefileparser::f_concat}
 ,{"if",         &mhmakefileparser::f_if}
 ,{"findstring", &mhmakefileparser::f_findstring}
 ,{"firstword",  &mhmakefileparser::f_firstword}
 ,{"wildcard",   &mhmakefileparser::f_wildcard}
 ,{"basename",   &mhmakefileparser::f_basename}
 ,{"notdir",     &mhmakefileparser::f_notdir}
 ,{"dir",        &mhmakefileparser::f_dir}
 ,{"shell",      &mhmakefileparser::f_shell}
 ,{"relpath",    &mhmakefileparser::f_relpath}
 ,{"realpath",   &mhmakefileparser::f_realpath}
 ,{"abspath",    &mhmakefileparser::f_abspath}
 ,{"toupper",    &mhmakefileparser::f_toupper}
 ,{"tolower",    &mhmakefileparser::f_tolower}
 ,{"exist",      &mhmakefileparser::f_exist}
 ,{"filesindirs",&mhmakefileparser::f_filesindirs}
 ,{"fullname"   ,&mhmakefileparser::f_fullname}
 ,{"addprefix"  ,&mhmakefileparser::f_addprefix}
 ,{"addsuffix"  ,&mhmakefileparser::f_addsuffix}
 ,{"filter-out" ,&mhmakefileparser::f_filterout}
 ,{"word"       ,&mhmakefileparser::f_word}
 ,{"lastword"   ,&mhmakefileparser::f_lastword}
 ,{"words"      ,&mhmakefileparser::f_words}
 ,{"strip"      ,&mhmakefileparser::f_strip}
 ,{"which"      ,&mhmakefileparser::f_which}
 ,{"foreach"    ,&mhmakefileparser::f_foreach}
 ,{"eval"       ,&mhmakefileparser::f_eval}
 ,{"sort"       ,&mhmakefileparser::f_sort}
 ,{"error"      ,&mhmakefileparser::f_error}
 ,{"info"       ,&mhmakefileparser::f_info}
 ,{"warning"    ,&mhmakefileparser::f_warning}
};

map<string,function_f> mhmakefileparser::m_Functions;

bool mhmakefileparser::m_FunctionsInitialised;

inline const char *NextCharExprItem(const char *pTmp,string &Output,char Char)
{
  const char *pStart=pTmp;
  while (*pTmp)
  {
    char CurChar=*pTmp;
    if (CurChar==Char)
      break;
    pTmp++;
    if (CurChar=='$' && *pTmp!='$')
      pTmp=SkipMakeExpr(pTmp);
  }

  const char *pStop=pTmp;
  if (*pTmp) pTmp++;

  while (pStart<pStop && (*pStart==' ' || *pStart == '\t')) pStart++;
  pStop--;
  while (pStart<=pStop && (*pStop==' ' || *pStop == '\t')) pStop--;
  pStop++;

  Output=string(pStart,pStop);
  return pTmp;
}

///////////////////////////////////////////////////////////////////////////////
// Loop over a list of filenames
static string IterList(const string &List,string (*iterFunc)(const string &FileName,void *pArg), void *pArg=NULL)
{
  const char *pTmp=List.c_str();
  string Ret=g_EmptyString;
  bool first=true;
  while (*pTmp)
  {
    if (!first)
    {
      Ret+=g_SpaceString;
    }
    else
    {
      first=false;
    }
    string Item;
    pTmp=NextItem(pTmp,Item);
    Item=iterFunc(Item,pArg);
    if (Item.empty())
      first=true;  // Do not add space the next iteration
    else
      Ret+=Item;
  }

  return Ret;
}

///////////////////////////////////////////////////////////////////////////////
void mhmakefileparser::InitFuncs(void)
{
  for (int i=0; i<sizeof(m_FunctionsDef)/sizeof(funcdef); i++)
    m_Functions[m_FunctionsDef[i].szFuncName]=m_FunctionsDef[i].pFunc;
}

///////////////////////////////////////////////////////////////////////////////
static string TrimString(const string &Input)
{
  unsigned Start=0;
  const char *pInput=Input.c_str();
  while (strchr(" \t",pInput[Start])) Start++;
  if (Start>=Input.size())
    return g_EmptyString;
  size_t Stop=Input.size()-1;
  while (strchr(" \t",pInput[Stop])) Stop--;
  return Input.substr(Start,Stop-Start+1);
}

///////////////////////////////////////////////////////////////////////////////

static string filter(const string &FileName, void *pvFilter)
{
  string *pFilter=(string*)pvFilter;
  if (PercentMatchList(UnquoteFileName(FileName),*pFilter))
    return FileName;
  else
    return g_EmptyString;
}
///////////////////////////////////////////////////////////////////////////////
string mhmakefileparser::f_filter(const string & ArgIn) const
{
  string Arg=ExpandExpression(ArgIn);
  size_t ipos=Arg.find(',');
  #ifdef _DEBUG
  if (ipos==string::npos) {
    throw string("filter func should have 2 arguments: ")+Arg;
  }
  #endif
  string Str=TrimString(Arg.substr(0,ipos));
  string List=Arg.substr(ipos+1);

  if (Str.empty())
    return Str;

  return IterList(List,filter,(void*)&Str);
}

///////////////////////////////////////////////////////////////////////////////
string mhmakefileparser::f_filterout(const string & ArgIn) const
{
  string Arg=ExpandExpression(ArgIn);
  size_t ipos=Arg.find(',');
  #ifdef _DEBUG
  if (ipos==string::npos) {
    throw string("filter func should have 2 arguments: ")+Arg;
  }
  #endif
  string Str=TrimString(Arg.substr(0,ipos));
  string List=Arg.substr(ipos+1);

  if (Str.empty())
    return Str;

  bool First=true;
  string Ret;
  char *pTok=strtok((char*)List.c_str()," \t");   // doing this is changing string, so this is very dangerous
  while (pTok)
  {
    string Item(pTok);
    if (!PercentMatchList(Item,Str))
    {
      if (First)
      {
        Ret=Item;
        First=false;
      }
      else
      {
        Ret+=g_SpaceString;
        Ret+=Item;
      }
    }
    pTok=strtok(NULL," \t");
  }
  return Ret;
}

class varstack_t : public vector< pair<bool,string> >
{
  mhmakefileparser *m_pMakefileParser;
public:
  varstack_t(mhmakefileparser *pMakefileParser)
    : m_pMakefileParser(pMakefileParser)
  {
  }
  void PushArg(int ArgNr, const string &Value)
  {
    char szVarName[10];
    ::sprintf(szVarName,"%d",ArgNr);
    string VarName(szVarName);
    string VarVal;
    if (m_pMakefileParser->GetVariable(VarName,VarVal))
      vector< pair<bool,string> >::push_back(pair<bool,string>(true, VarVal));
    else
      vector< pair<bool,string> >::push_back(pair<bool,string>(false,g_EmptyString));
    m_pMakefileParser->SetVariable(VarName,Value);
  }
  void RestoreArgs()
  {
    vector< pair<bool,string> >::iterator It=begin();
    int ArgNr=0;
    while (It!=end())
    {
      char szVarName[10];
      ::sprintf(szVarName,"%d",ArgNr);
      string VarName(szVarName);
      if (It->first)
        m_pMakefileParser->SetVariable(VarName, It->second);
      else
      {
        m_pMakefileParser->DeleteVariable(VarName);
      }
      It++;
      ArgNr++;
    }
  }
};

///////////////////////////////////////////////////////////////////////////////
string mhmakefileparser::f_call(const string & ArgIn) const
{
  string Arg=ExpandExpression(ArgIn);
  varstack_t VarStack((mhmakefileparser*)this);
  const char *pTmp=Arg.c_str();

  bool LastCharIsComma=Arg[Arg.length()-1]==',';

  string Func;
  pTmp=NextCharItem(pTmp,Func,',');
  map<string,string>::const_iterator pFunc=m_Variables.find(Func);
  #ifdef _DEBUG
  if (pFunc==m_Variables.end())
  {
    throw string("call to non existing function ")+Func;
  }
  #endif
  // the 0 argument is the function itself
  VarStack.PushArg(0,Func);

  Func=pFunc->second;
  int i=0;
  while (*pTmp || LastCharIsComma) {
    if (!*pTmp)
      LastCharIsComma=false; /* To stop the loop */
    string Repl;
    pTmp=NextCharItem(pTmp,Repl,',');
    i++;
    VarStack.PushArg(i,Repl);
  }

  string Ret=ExpandExpression(Func);

  VarStack.RestoreArgs();

  return Ret;
}

///////////////////////////////////////////////////////////////////////////////
string mhmakefileparser::f_subst(const string & ArgIn) const
{
  string Arg=ExpandExpression(ArgIn);
  const char *pTmp=Arg.c_str();

  string FromString;
  pTmp=NextCharItem(pTmp,FromString,',');
  #ifdef _DEBUG
  if (!*pTmp) {
    throw string("Wrong number of arguments in function subst");
  }
  #endif

  string ToString;
  pTmp=NextCharItem(pTmp,ToString,',');
  string Text;
  NextCharItem(pTmp,Text,',');

  if (FromString.empty())
    return Text;

  string Ret;
  size_t Pos=Text.find(FromString);
  size_t PrevPos=0;
  while (Pos!=string::npos)
  {
    Ret+=Text.substr(PrevPos,Pos-PrevPos);
    Ret+=ToString;
    PrevPos=Pos+FromString.length();
    Pos=Text.find(FromString,PrevPos);
  }
  Ret+=Text.substr(PrevPos);

  return Ret;
}

///////////////////////////////////////////////////////////////////////////////
string mhmakefileparser::f_patsubst(const string & ArgIn) const
{
  string Arg=ExpandExpression(ArgIn);
  const char *pTmp=Arg.c_str();

  string FromString;
  pTmp=NextCharItem(pTmp,FromString,',');
  #ifdef _DEBUG
  if (!*pTmp) {
    throw string("Wrong number of arguments in function subst");
  }
  #endif

  string ToString;
  pTmp=NextCharItem(pTmp,ToString,',');
  string Text;
  NextCharItem(pTmp,Text,',');

  if (FromString.empty())
    return Text;

  return Substitute(Text,FromString,ToString);
}

///////////////////////////////////////////////////////////////////////////////
string mhmakefileparser::f_concat(const string & ArgIn) const
{
  string Arg=ExpandExpression(ArgIn);
  const char *pTmp=Arg.c_str();

  string JoinString;
  pTmp=NextCharItem(pTmp,JoinString,',');

  string List;
  pTmp=NextCharItem(pTmp,List,',');

  if (JoinString.empty() && List.empty())
  {
    /* assume as $(concat ,,items) construct */
    JoinString=",";
    pTmp=NextCharItem(pTmp,List,',');
  }

  bool First=true;
  string Ret;
  char *pTok=strtok((char*)List.c_str()," \t");   // doing this is changing string, so this is very dangerous
  while (pTok)
  {
    if (First)
    {
      First=false;
    }
    else
    {
      Ret+=JoinString;
    }
    Ret+=pTok;
    pTok=strtok(NULL," \t");
  }
  return Ret;
}

///////////////////////////////////////////////////////////////////////////////
// Make sure to only expand the expression which the condition is true for
string mhmakefileparser::f_if(const string & Arg) const
{
  const char *pTmp=Arg.c_str();

  string Cond;
  pTmp=NextCharExprItem(pTmp,Cond,',');
  Cond=ExpandExpression(Cond);
  string Ret;
  if (Cond.empty())
  {
    pTmp=NextCharExprItem(pTmp,Ret,',');
  }
  NextCharExprItem(pTmp,Ret,',');
  return ExpandExpression(Ret);
}

///////////////////////////////////////////////////////////////////////////////
string mhmakefileparser::f_findstring(const string & ArgIn) const
{
  string Arg=ExpandExpression(ArgIn);
  const char *pTmp=Arg.c_str();
  string find;
  pTmp=NextCharItem(pTmp,find,',');
  string instr;
  NextCharItem(pTmp,instr,',');

  if (instr.find(find) != instr.npos)
    return find;
  else
    return g_EmptyString;
}

///////////////////////////////////////////////////////////////////////////////
string mhmakefileparser::f_firstword(const string & ArgIn) const
{
  string Arg=ExpandExpression(ArgIn);
  string FirstWord;
  NextItem(Arg.c_str(),FirstWord);
  return FirstWord;
}

///////////////////////////////////////////////////////////////////////////////
static string wildcard(const string &Arg, void *pvVar)
{
  mhmakefileparser *pParser=(mhmakefileparser*)pvVar;

  fileinfo *pFileSpec=GetFileInfo(Arg,pParser->GetMakeDir()); /* Use GetFileInfo to make the relative path absolute */
  fileinfo *pDir=pFileSpec->GetDir();
#ifdef WIN32
  struct _finddata_t FileInfo;
  intptr_t hFile=_findfirst(pFileSpec->GetFullFileName().c_str(),&FileInfo);
  if (hFile==-1)
    return g_EmptyString;

  string Ret=g_EmptyString;

  /* We have to verify with percentmatch since the find functions *.ext also matches the functions *.extbrol */
  string CheckSpec=pFileSpec->GetName();
  if (PercentMatchNoCase(FileInfo.name,CheckSpec,NULL,'*'))
  {
    Ret=GetFileInfo(FileInfo.name,pDir)->GetQuotedFullFileName();
  }
  while (-1!=_findnext(hFile,&FileInfo))
  {
    if (PercentMatchNoCase(FileInfo.name,CheckSpec,NULL,'*'))
    {
      Ret+=g_SpaceString;
      Ret+=GetFileInfo(FileInfo.name,pDir)->GetQuotedFullFileName();
    }
  }
  _findclose(hFile);
#else
  glob_t Res;
  if (glob (pFileSpec->GetFullFileName().c_str(), GLOB_ERR|GLOB_NOSORT|GLOB_MARK, NULL, &Res))
    return g_EmptyString;

  string Ret=g_EmptyString;
  string SepStr=g_EmptyString;
  string CheckSpec=pFileSpec->GetName();
  for (int i=0; i<Res.gl_pathc; i++)
  {
    if (PercentMatch(Res.gl_pathv[i],CheckSpec,NULL,'*'))
    {
      Ret+=SepStr;
      Ret+=GetFileInfo(Res.gl_pathv[i],pDir)->GetQuotedFullFileName();
      SepStr=g_SpaceString;
    }
  }

  globfree(&Res);
#endif
  return Ret;
}
///////////////////////////////////////////////////////////////////////////////
string mhmakefileparser::f_wildcard(const string & Arg) const
{
  return IterList(ExpandExpression(Arg), wildcard, (void*)this);
}

///////////////////////////////////////////////////////////////////////////////
string mhmakefileparser::f_exist(const string & Arg) const
{
  string File=TrimString(ExpandExpression(Arg));
  fileinfo *pFile=GetFileInfo(File,m_MakeDir);
  if (pFile->Exists())
  {
    return string("1");
  }
  else
    return g_EmptyString;
}

///////////////////////////////////////////////////////////////////////////////
string mhmakefileparser::f_filesindirs(const string & ArgIn) const
{
  string Arg=ExpandExpression(ArgIn);
  const char *pTmp=Arg.c_str();

  string strFiles;
  pTmp=NextCharItem(pTmp,strFiles,',');
  #ifdef _DEBUG
  if (!*pTmp) {
    throw string("Wrong number of arguments in function filesindirs");
  }
  #endif
  string strDirs;
  NextCharItem(pTmp,strDirs,',');

  vector<fileinfo*> Dirs;
  SplitToItems(strDirs,Dirs);

  pTmp=strFiles.c_str();
  string Ret=g_EmptyString;
  bool first=true;
  while (*pTmp)
  {
    string File;
    fileinfo *pFile;
    pTmp=NextItem(pTmp,File);

    vector<fileinfo*>::iterator It=Dirs.begin();
    vector<fileinfo*>::iterator ItEnd=Dirs.end();
    while (It!=ItEnd)
    {
      pFile=GetFileInfo(File,*It++);
      if (pFile->Exists())
      {
        break;
      }
      pFile=NULL;
    }
    if (!pFile)
      continue;

    if (!first)
    {
      Ret+=g_SpaceString;
    }
    else
    {
      first=false;
    }
    Ret+=pFile->GetQuotedFullFileName();
  }

  return Ret;
}

///////////////////////////////////////////////////////////////////////////////
string mhmakefileparser::f_fullname(const string & Arg) const
{
  string File=TrimString(ExpandExpression(Arg));
  fileinfo *pFile=GetFileInfo(File,m_MakeDir);
  return pFile->GetQuotedFullFileName();
}

///////////////////////////////////////////////////////////////////////////////
static string basename(const string &FileName,void*)
{
  string Ret=UnquoteFileName(FileName);
  Ret=Ret.substr(0,Ret.find_last_of('.'));
  return QuoteFileName(Ret);
}

///////////////////////////////////////////////////////////////////////////////
string mhmakefileparser::f_basename(const string & FileNames) const
{
  return IterList(ExpandExpression(FileNames),basename);
}

///////////////////////////////////////////////////////////////////////////////
static string notdir(const string &FileName,void*)
{
  string Ret=UnquoteFileName(FileName);
  size_t Pos=Ret.find_last_of(OSPATHSEP);
  if (Pos==string::npos)
  {
    return FileName;
  }
  else
  {
    Ret=Ret.substr(Pos+1);
    return QuoteFileName(Ret);
  }

}

///////////////////////////////////////////////////////////////////////////////
string mhmakefileparser::f_notdir(const string & FileNames) const
{
  return IterList(ExpandExpression(FileNames),notdir);
}

///////////////////////////////////////////////////////////////////////////////
static string addprefix(const string &FileName,void *pPrefix)
{
  return *(const string *)pPrefix+FileName;
}

///////////////////////////////////////////////////////////////////////////////
string mhmakefileparser::f_addprefix(const string & ArgIn) const
{
  string Arg=ExpandExpression(ArgIn);
  const char *pTmp=Arg.c_str();
  string PreFix;
  pTmp=NextCharItem(pTmp,PreFix,',');
  #ifdef _DEBUG
  if (g_PrintAdditionalInfo && PreFix.empty()) {
    cout << "Warning: empty prefix in expression: " << ArgIn << endl;
  }
  #endif
  string FileNames;
  pTmp=NextCharItem(pTmp,FileNames,',');
  return IterList(FileNames,addprefix,&PreFix);
}

///////////////////////////////////////////////////////////////////////////////
static string addsuffix(const string &FileName,void *pSuffix)
{
  return FileName+*(const string *)pSuffix;
}

///////////////////////////////////////////////////////////////////////////////
string mhmakefileparser::f_addsuffix(const string & ArgIn) const
{
  string Arg=ExpandExpression(ArgIn);
  const char *pTmp=Arg.c_str();
  string SufFix;
  pTmp=NextCharItem(pTmp,SufFix,',');
  #ifdef _DEBUG
  if (!*pTmp) {
    throw string("Wrong number of arguments in function addsuffix");
  }
  #endif
  string FileNames;
  pTmp=NextCharItem(pTmp,FileNames,',');
  return IterList(FileNames,addsuffix,&SufFix);
}

///////////////////////////////////////////////////////////////////////////////
// Returns the n-th word number
string mhmakefileparser::f_word(const string & ArgIn) const
{
  string Arg=ExpandExpression(ArgIn);
  const char *pTmp=Arg.c_str();

  string strNum;
  pTmp=NextCharItem(pTmp,strNum,',');

  int WordNbr=atoi(strNum.c_str());

  #ifdef _DEBUG
  if (!WordNbr)
  {
    if (!WordNbr) {
      throw string ("Expecting a number bigger then 0 for the word function");
    }
  }
  #endif

  int CurWord=0;
  while (*pTmp)
  {
    string Word;
    pTmp=NextItem(pTmp,Word);
    CurWord++;
    if (CurWord==WordNbr)
      return Word;
  }

  return g_EmptyString;
}

///////////////////////////////////////////////////////////////////////////////
// Returns the last word
string mhmakefileparser::f_lastword(const string & ArgIn) const
{
  string Arg=ExpandExpression(ArgIn);
  const char *pTmp=Arg.c_str();

  string Word;
  while (*pTmp)
  {
    pTmp=NextItem(pTmp,Word);
  }
  return Word;
}

///////////////////////////////////////////////////////////////////////////////
// Returns the number of words
string mhmakefileparser::f_words(const string & ArgIn) const
{
  string Arg=ExpandExpression(ArgIn);
  const char *pTmp=Arg.c_str();
  int NrWords=0;
  char szNumber[10];
  while (*pTmp)
  {
    string Word;
    pTmp=NextItem(pTmp,Word);
    NrWords++;
  }
  sprintf(szNumber,"%d",NrWords);

  return szNumber;
}

///////////////////////////////////////////////////////////////////////////////
// Search for a command in the enivornment path
string mhmakefileparser::f_which(const string & Arg) const
{
  return SearchCommand(ExpandExpression(Arg));
}

///////////////////////////////////////////////////////////////////////////////
// Removes leading and trailing space
string mhmakefileparser::f_strip(const string & ArgIn) const
{
  string Arg=ExpandExpression(ArgIn);
  string::const_iterator pFirst=Arg.begin();
  string::const_iterator pLast=Arg.end();
  while (strchr(" \t\r\n",*pFirst) && pFirst!=pLast) pFirst++;
  if (pFirst==pLast)
    return "";
  while (strchr(" \t\r\n",*(--pLast)));
  pLast++;
  return Arg.substr(pFirst-Arg.begin(),pLast-pFirst);
}

///////////////////////////////////////////////////////////////////////////////
static string dir(const string &FileName, void *)
{
  size_t Pos=FileName.find_last_of(OSPATHSEP);
  if (Pos==string::npos)
  {
    #ifdef _WIN32
    Pos=FileName.find_last_of('/');
    if (Pos==string::npos)
    #endif
    return g_EmptyString;
  }

  string Ret=g_EmptyString;
  Ret+=FileName.substr(0,Pos+1);
  if (FileName[0]=='"' && FileName.end()[-1]=='"')
    Ret+=s_QuoteString;
  return Ret;
}

///////////////////////////////////////////////////////////////////////////////
string mhmakefileparser::f_dir(const string & FileNames) const
{
  return IterList(ExpandExpression(FileNames),dir);
}

///////////////////////////////////////////////////////////////////////////////
string mhmakefileparser::f_shell(const string & CommandIn) const
{
  string Command=ExpandExpression(CommandIn);
  string Output;

#ifdef _DEBUG
  if (g_PrintAdditionalInfo)
    cout << "shell: executing: Command '"<<Command<<"'"<<endl;
#endif

  ((mhmakefileparser*)this)->ExecuteCommand(string("@")+Command,&Output);  // Make sure that the command is not echoed
#ifdef _DEBUG
  if (g_PrintAdditionalInfo)
    cout << "shell returned '"<<Output<<"'"<<endl;
#endif
  return Output;
}

///////////////////////////////////////////////////////////////////////////////
static string relpath(const string &FileName,void *pvDir)
{
  const fileinfo *pDir=*(const fileinfo **)pvDir;
  const fileinfo *pPath=GetFileInfo(FileName,pDir);
  const char *pCur=pDir->GetFullFileName().c_str();
  const char *pszPath=pPath->GetFullFileName().c_str();

  const char *pLast=pszPath;
  while (*pCur==*pszPath)
  {
    char Char=*pszPath;
    if (!Char)
    {
      return "."; // Means that FileName is the same as the current directory
    }
    if (Char==OSPATHSEP)
      pLast=pszPath+1;
    pCur++;
    pszPath++;
  }
  if (*pszPath==OSPATHSEP && !*pCur)
    pLast=pszPath+1;
  string retPath;
  if (*pCur==OSPATHSEP) {
    bool first=true;
    pCur++;
    retPath="..";
    while (*pCur)
    {
      if (*pCur==OSPATHSEP)
        retPath+=OSPATHSEPSTR "..";
      pCur++;
    }
    if (pszPath)
      retPath=retPath+OSPATHSEPSTR+pLast;
  }
  else
  {
    if (*pCur)
      retPath=".." OSPATHSEPSTR;
    while (*pCur)
    {
      if (*pCur==OSPATHSEP)
        retPath+=".." OSPATHSEPSTR;
      pCur++;
    }
    retPath+=pLast;
  }
  return QuoteFileName(retPath);
}

///////////////////////////////////////////////////////////////////////////////
// Make a path name relative to the current directory
string mhmakefileparser::f_relpath(const string & FileNames) const
{
  return IterList(ExpandExpression(FileNames),relpath,(void*)&m_MakeDir);
}

///////////////////////////////////////////////////////////////////////////////
static string realpath(const string &FileName,void *pvDir)
{
  const fileinfo *pDir=*(const fileinfo **)pvDir;
  const fileinfo *pPath=GetFileInfo(FileName,pDir);
  if (pPath->Exists())
    return pPath->GetQuotedFullFileName();
  else
    return "";
}

///////////////////////////////////////////////////////////////////////////////
// Make a path name absolute (currently it has the same functionality as abspath, normally it should resolved symlinks, but currently it doesn't)
string mhmakefileparser::f_realpath(const string & FileNames) const
{
  return IterList(ExpandExpression(FileNames),realpath,(void*)&m_MakeDir);
}

///////////////////////////////////////////////////////////////////////////////
// Make a path name absolute (without resolving simlinks)
string mhmakefileparser::f_abspath(const string & FileNames) const
{
  return IterList(ExpandExpression(FileNames),realpath,(void*)&m_MakeDir);
}

///////////////////////////////////////////////////////////////////////////////
static string makeupper(const string &FileName,void *)
{
  string Ret=FileName;
  string::const_iterator pSrc=FileName.begin();
  string::iterator pDest=Ret.begin();
  while (pSrc!=FileName.end())
  {
    *pDest++ = toupper(*pSrc++);
  }
  return Ret;
}

///////////////////////////////////////////////////////////////////////////////
string mhmakefileparser::f_toupper(const string & FileNames) const
{
  return IterList(ExpandExpression(FileNames),makeupper);
}

///////////////////////////////////////////////////////////////////////////////
static string makelower(const string &FileName, void *)
{
  string Ret=FileName;
  string::const_iterator pSrc=FileName.begin();
  string::iterator pDest=Ret.begin();
  while (pSrc!=FileName.end())
  {
    *pDest++ = tolower(*pSrc++);
  }
  return Ret;
}

///////////////////////////////////////////////////////////////////////////////
string mhmakefileparser::f_tolower(const string & FileNames) const
{
  return IterList(ExpandExpression(FileNames),makelower);
}

///////////////////////////////////////////////////////////////////////////////
struct expanedexpr_arg
{
  string            Var;
  mhmakefileparser *pParser;
  string            Expr;
};

static string expandexpr(const string &VarVal, void *pvVar)
{
  expanedexpr_arg *pArg=(expanedexpr_arg*)pvVar;
  pArg->pParser->SetVariable(pArg->Var,VarVal);
  return pArg->pParser->ExpandExpression(pArg->Expr);
}

///////////////////////////////////////////////////////////////////////////////
string mhmakefileparser::f_foreach(const string & Arg) const
{
  const char *pTmp=Arg.c_str();

  expanedexpr_arg Args;
  Args.pParser=(mhmakefileparser*)this;
  pTmp=NextCharExprItem(pTmp,Args.Var,',');
  Args.Var=ExpandExpression(Args.Var);

  if (Args.Var.empty())
    throw(string("Wrong syntax in foreach instruction: '")+Arg+"'. Variable may not be empty.");

  string Items;
  pTmp=NextCharExprItem(pTmp,Items,',');
  Items=ExpandExpression(Items);

  if (Items.empty())
    return g_EmptyString; /* No items specified, so nothing needs to be done */

  pTmp=NextCharExprItem(pTmp,Args.Expr,',');

  /* Save the variable to be able to restore it after the foreach expansion */
  string VarVal;
  map<string,string>::const_iterator pVal=m_Variables.find(Args.Var);
  if (pVal!=m_Variables.end())
    VarVal=pVal->second;

  string Ret=IterList(Items,expandexpr,(void*)&Args);

  /* Restore the variable to it's original value */
  if (pVal!=m_Variables.end())
    Args.pParser->SetVariable(Args.Var,VarVal);
  else
    Args.pParser->DeleteVariable(Args.Var);

  return Ret;
}

///////////////////////////////////////////////////////////////////////////////
string mhmakefileparser::f_eval(const string & Arg) const
{
  ((mhmakefileparser*)this)->ParseString(ExpandExpression(Arg)+"\n");
  return g_EmptyString;
}

///////////////////////////////////////////////////////////////////////////////
static string AddToList(const string &String, void *pvList)
{
  vector<string> *pList=(vector<string>*)pvList;
  pList->push_back(String);
  return g_EmptyString;
}

string mhmakefileparser::f_sort(const string & Arg) const
{
  string ExpandedArg=ExpandExpression(Arg);
  vector<string> List;
  IterList(ExpandedArg, AddToList, (void*)&List);
  sort(List.begin(),List.end());
  string Prev;
  vector<string>::iterator It=List.begin();
  string Ret;
  bool First=true;
  while (It!=List.end())
  {
    string This=*It++;
    if (This!=Prev)
    {
      Prev=This;
      if (!First)
        Ret+=" ";
      else
        First=false;
      Ret+=This;
    }
  }
  return Ret;
}

///////////////////////////////////////////////////////////////////////////////
string mhmakefileparser::f_error(const string & Arg) const
{
  throw(GetFileNameLineNo()+ExpandExpression(Arg));
}

///////////////////////////////////////////////////////////////////////////////
string mhmakefileparser::f_warning(const string & Arg) const
{
  cout << GetFileNameLineNo() << ExpandExpression(Arg) << endl;
  return g_EmptyString;
}

///////////////////////////////////////////////////////////////////////////////
string mhmakefileparser::f_info(const string & Arg) const
{
  cout << ExpandExpression(Arg) << endl;
  return g_EmptyString;
}

