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

#include "rule.h"
#include "util.h"
#include "mhmakeparser.hpp"

#ifdef WIN32

#include <WinIoCtl.h>

#define REPARSE_MOUNTPOINT_HEADER_SIZE   8

typedef struct {
  DWORD ReparseTag;
  DWORD ReparseDataLength;
  WORD Reserved;
  WORD ReparseTargetLength;
  WORD ReparseTargetMaximumLength;
  WORD Reserved1;
  WCHAR ReparseTarget[1];
} REPARSE_MOUNTPOINT_DATA_BUFFER, *PREPARSE_MOUNTPOINT_DATA_BUFFER;
#endif

static char s_UsageString[]=
"\
Usage: mhmake [-f <Makefile>] [-[c|C] <RunDir>] [<Var>=<Value>]\n\
              [-a] [-q] [-s] [-S] [-v] [-P <Nr Parallel Builds>] [targets]+\n"
#ifdef _DEBUG
"\
              [-p] [-n] [-e] [-l] [-w] [-d] [-CD] [-m] [-b]\n"
#endif
"\n\
  <Makefile>  : Makefile to load (if not specified 'makefile' is used\n\
  <RunDir>    : Make is setting the current directory to this directory at the\n\
                start.\n\
  <Var>       : Defines a variable\n\
  <Value>     : Value of the variable\n\
  -a          : Rebuild all targets\n\
  -s          : Rescan automatic dependencies\n\
  -S          : SKip checking if mhmake is started in a subdirectory of the\n\
                %MHMAKECONF dir \n\
  -v          : Print version information\n\
  -q          : Quiet. Disable all output \n\
  -P <Nr Parallel Builds> :\n\
                Number of parallel build commands executed at the \n\
                same time. Default is this the number of processor \n\
                cores. 1 disables parallel builds.\n"
#ifdef _DEBUG
"\n\
  The following options are additional options in mhmake_dbg which are not\n\
  available in mhmake. These are mainly options for debugging purposes.\n\
  -e          : Dump Vars and Rules on error\n\
  -w          : Print additional information\n\
  -p          : Prints the variables and the rules before building\n\
  -n          : Only prints the commands, but does not execute them\n\
  -l          : Print parser debug information\n\
  -d          : Print the dependency checking\n\
  -CD         : Do circular dependency checking (targets depending on itself)\n\
  -m          : Create md5 database in md5.database in start directory. \n\
  -b          : Print build tree. \n\
  -D          : Print all double defined rules (even if commands are the same) \n\
"
#else
"\
\n\
It is adviced during creation of makefiles to use mhmake_dbg. It has additional\n\
debugging options and does some extra error checking.\n\
For a description of the additional options: run mhmake_dbg -h\n\
"
#endif
;

#ifdef _DEBUG
bool g_PrintVarsAndRules=false;
bool g_DoNotExecute=false;
bool g_BuildMd5Db=false;
bool g_GenProjectTree=false;
bool g_DumpOnError=false;
bool g_PrintAdditionalInfo=false;
bool g_pPrintDependencyCheck=false;
bool g_CheckCircularDeps=false;
bool g_PrintLexYacc=false;
bool g_PrintMultipleDefinedRules=false;
#endif

bool g_Quiet=false;
bool g_RebuildAll=false;
bool g_ForceAutoDepRescan=false;
bool g_SkipMhMakeConfDirCheck=false;

const string g_EmptyString;
const string g_SpaceString(" ");

///////////////////////////////////////////////////////////////////////////////
void PrintVersionInfo(void)
{
  static const char VersionStr[]="\
mhmake : GNU compatible make tool with extensions\n\
version: " MHMAKEVER "\n\
Remarks and bug reports -> marha@sourceforge.net\n\
";
  cerr << VersionStr;
  exit(1);
}
///////////////////////////////////////////////////////////////////////////////
makecommand::makecommand()
{
  char ExeName[MAX_PATH];
#ifdef WIN32
  GetModuleFileName(NULL,ExeName,sizeof(ExeName));
  m_BuildCommand=ExeName;
  transform(m_BuildCommand.begin(),m_BuildCommand.end(),m_BuildCommand.begin(),(int(__CDECL *)(int))tolower);
#else
  int NrChars=readlink ("/proc/self/exe", ExeName, sizeof(ExeName));
  ExeName[NrChars]=0;
  m_BuildCommand=ExeName;
#endif
}

///////////////////////////////////////////////////////////////////////////////
string Substitute(const string &ToSubst,const string &iSrcStr,const string &iToStr)
{
  string Ret=g_EmptyString;
  matchres Res;
  string SrcStr=iSrcStr;
  string ToStr=iToStr;

  if (string::npos==SrcStr.find('%'))
  {
    string PerStr("%");
    SrcStr=PerStr+SrcStr;
    ToStr=PerStr+ToStr;
  }
  const char *pTmp=ToSubst.c_str();
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

    if (PercentMatch(Item,SrcStr,&Res))
    {
      Ret+=ReplaceWithStem(ToStr,Res.m_Stem);
    }
    else
    {
      Ret+=Item;
    }
  }

  return Ret;
}

///////////////////////////////////////////////////////////////////////////////
bool PercentMatch(const string &String,const string &Expr,matchres *pRes,const char Char)
{
  const char *pFirst=String.c_str();
  const char *pFirstExpr=Expr.c_str();
  while (*pFirstExpr && *pFirstExpr!=Char)
  {
    if (*pFirst!=*pFirstExpr)
      return false;
    pFirst++;
    pFirstExpr++;
  }

  if (!*pFirstExpr)
  {
    if (!*pFirst)
    {
      if (pRes)
      {
        pRes->m_First=String;
        pRes->m_Stem=pRes->m_Last=g_EmptyString;
      }
      return true;
    } else
      return false;
  }
  else if (!*pFirst)
    return false;

  const char *pEnd=pFirst+strlen(pFirst);

  const char *pLast=pEnd;
  const char *pLastExpr=pFirstExpr+strlen(pFirstExpr)-1;
  if (pLastExpr!=pFirstExpr)
  {
    pLast--;

    while (pLastExpr>pFirstExpr)
    {
      if (*pLastExpr!=*pLast)
        return false;
      pLastExpr--;
      pLast--;
    }
    pLast++;
  }
  string Stem=string(pFirst,pLast-pFirst);

  if (pRes)
  {
    pRes->m_First=string(String.c_str(),pFirst-String.c_str());

    pRes->m_Stem=Stem;

    pRes->m_Last=string(pLast,pEnd-pLast);
  }
  return true;
}
///////////////////////////////////////////////////////////////////////////////
bool PercentMatchNoCase(const string &String,const string &Expr,matchres *pRes,const char Char)
{
  const char *pFirst=String.c_str();
  const char *pFirstExpr=Expr.c_str();
  while (*pFirstExpr && *pFirstExpr!=Char)
  {
    if (tolower(*pFirst)!=tolower(*pFirstExpr))
      return false;
    pFirst++;
    pFirstExpr++;
  }

  if (!*pFirstExpr)
  {
    if (!*pFirst)
    {
      if (pRes)
      {
        pRes->m_First=String;
        pRes->m_Stem=pRes->m_Last=g_EmptyString;
      }
      return true;
    } else
      return false;
  }
  else if (!*pFirst)
    return false;

  const char *pEnd=pFirst+strlen(pFirst);

  const char *pLast=pEnd;
  const char *pLastExpr=pFirstExpr+strlen(pFirstExpr)-1;
  if (pLastExpr!=pFirstExpr)
  {
    pLast--;

    while (pLastExpr>pFirstExpr)
    {
      if (tolower(*pLastExpr)!=tolower(*pLast))
        return false;
      pLastExpr--;
      pLast--;
    }
    pLast++;
  }
  string Stem=string(pFirst,pLast-pFirst);

  if (pRes)
  {
    pRes->m_First=string(String.c_str(),pFirst-String.c_str());

    pRes->m_Stem=Stem;

    pRes->m_Last=string(pLast,pEnd-pLast);
  }
  return true;
}
///////////////////////////////////////////////////////////////////////////////
bool PercentMatchList(const string &String,const string &ExprList,matchres *pRes)
{
  const char *pTmp=ExprList.c_str();
  while (*pTmp)
  {
    string Expr;
    pTmp=NextItem(pTmp,Expr);
    if (PercentMatch(String,Expr,pRes))
      return true;
  }
  return false;
}

///////////////////////////////////////////////////////////////////////////////
string ReplaceWithStem(const string &String,const string &Stem)
{
  string Ret=String;
  size_t Pos=Ret.find('%');
  while (Pos!=string::npos)
  {
    Ret=Ret.substr(0,Pos)+Stem+Ret.substr(Pos+1);
    Pos=Ret.find('%');
  }
  return Ret;
}

///////////////////////////////////////////////////////////////////////////////
refptr<loadedmakefile> LOADEDMAKEFILES::find(const loadedmakefile &ToSearch)
{
  vector<refptr<loadedmakefile> >::const_iterator It=begin();
  while (It!=end())
  {
    if (*(*It)==ToSearch)
      return *It;
    It++;
  }
  return refptr<loadedmakefile>();
}

///////////////////////////////////////////////////////////////////////////////
bool loadedmakefile::loadedmakefile_statics::GetSvnRevision(void)
{
  if (getenv("nosvn"))
    return false;

  // Get the revision of the working copy
  // We do it with the svn info command (do it without path arguments, only current directory, to avoid problems with path names and junctions/links

  string Output;
  try
  {
    mhmakefileparser Dummy(m_MhMakeConf);
    string SvnCommand=Dummy.SearchCommand("svn",EXEEXT);
	if (SvnCommand.empty())
		return false;
    Dummy.OsExeCommand(SvnCommand,string(" info"),false,&Output);
  }
  catch (string Message)
  {
    #ifdef _DEBUG
    cerr << "Exception: " << Message << endl;
    #endif
  }

  char *pTok=strtok((char*)Output.c_str(),"\n");   // doing this is changing string, so this is very dangerous !!!
  while (pTok)
  {
    if (!strncmp(pTok,"URL: ",5))
    {
      m_GlobalCommandLineVars[WC_URL]=pTok+5+7;
    }
    else if (!strncmp(pTok,"Revision: ",10))
    {
      m_GlobalCommandLineVars[WC_REVISION]=pTok+10;
      return true;
    }
    pTok=strtok(NULL,"\n");
  }
  return false;
}

///////////////////////////////////////////////////////////////////////////////
bool loadedmakefile::loadedmakefile_statics::GetGitSvnRevision(void)
{
  // Get the revision of the working copy
  // We do it with the svn info command

  string Output;
  try
  {
    mhmakefileparser Dummy(m_MhMakeConf);
    string GitCommand=Dummy.SearchCommand("git",EXEEXT);
    Dummy.OsExeCommand(GitCommand,string(" svn info ."), false, &Output);
  }
  catch (string Message)
  {
    #ifdef _DEBUG
    cerr << "Exception: " << Message << endl;
    #endif
  }

  char *pTok=strtok((char*)Output.c_str(),"\n");   // doing this is changing string, so this is very dangerous !!!
  while (pTok)
  {
    if (!strncmp(pTok,"URL: ",5))
    {
      m_GlobalCommandLineVars[WC_URL]=pTok+5+7;
    }
    else if (!strncmp(pTok,"Revision: ",10))
    {
      m_GlobalCommandLineVars[WC_REVISION]=pTok+10;
      return true;
    }
    pTok=strtok(NULL,"\n");
  }
  return false;
}

///////////////////////////////////////////////////////////////////////////////
loadedmakefile::loadedmakefile_statics::loadedmakefile_statics()
{
  m_GlobalCommandLineVars[MAKE]=g_MakeCommand;
  const char *pEnv=getenv(MHMAKECONF);
  if (pEnv)
  {
    m_MhMakeConf=GetAbsFileInfo(pEnv);
    m_GlobalCommandLineVars[MHMAKECONF]=QuoteFileName(m_MhMakeConf->GetFullFileName());

    if (!GetSvnRevision())
      GetGitSvnRevision();
  }
}

///////////////////////////////////////////////////////////////////////////////
loadedmakefile::loadedmakefile(const fileinfo *pDir, vector<string> &Args,const string&Makefile)
{
  m_Makefile=NULL;
  m_CommandLineVars=sm_Statics.m_GlobalCommandLineVars;

  m_MakeDir=NULL;
  vector<string>::iterator ArgIt=Args.begin();
  while (ArgIt!=Args.end())
  {
    size_t EqPos=ArgIt->find('=');
    if (EqPos!=string::npos)
    {
      string Var=ArgIt->substr(0,EqPos);
      string Val=ArgIt->substr(EqPos+1);
      m_CommandLineVars[Var]=Val;
    }
    else if ((*ArgIt)[0]=='-')
    {
      switch ((*ArgIt)[1])
      {
        case 'f':
          if (ArgIt->size()>2)
          {
            if (!m_MakeDir)
            {
              m_Makefile=GetFileInfo(ArgIt->substr(2),pDir);
            }
            else
            {
              m_Makefile=GetFileInfo(ArgIt->substr(2),m_MakeDir);
            }
          }
          else
          {
            ArgIt++;
            if (!m_MakeDir)
            {
              m_Makefile=GetFileInfo(*ArgIt,pDir);
            }
            else
            {
              m_Makefile=GetFileInfo(*ArgIt,m_MakeDir);
            }
          }
          break;
        case 'C':
#ifdef _DEBUG
          if (ArgIt->size()>2 && (*ArgIt)[2]=='D')
          {
            g_CheckCircularDeps=true;
            break;
          }
#endif
          /* Fall through */
        case 'c':
          if (ArgIt->size()>2)
            m_MakeDir=GetFileInfo(ArgIt->substr(2),pDir);
          else
          {
            ArgIt++;
            m_MakeDir=GetFileInfo(*ArgIt,pDir);
          }
          break;
        case 'a':
          g_RebuildAll=true;
          break;
        case 'q':
          g_Quiet=true;
          break;
        case 's':
          g_ForceAutoDepRescan=true;
          break;
        case 'S':
          g_SkipMhMakeConfDirCheck=true;
          break;
        case 'v':
          PrintVersionInfo();
          break;
        case 'P':
          if (ArgIt->size()>2)
            mhmakefileparser::SetNrParallelBuilds(atoi(ArgIt->substr(2).c_str()));
          else
          {
            ArgIt++;
            mhmakefileparser::SetNrParallelBuilds(atoi((*ArgIt).c_str()));
          }
          break;
#ifdef _DEBUG
        case 'p':
          g_PrintVarsAndRules=true;
          break;
        case 'n':
          g_DoNotExecute=true;
          break;
        case 'w':
          g_PrintAdditionalInfo=true;
          break;
        case 'd':
          g_pPrintDependencyCheck=true;
          break;
        case 'D':
          g_PrintMultipleDefinedRules=true;
          break;
        case 'l':
          g_PrintLexYacc=true;
          break;
        case 'e':
          g_DumpOnError=true;
          break;
        case 'm':
          g_BuildMd5Db=true;
          break;
        case 'b':
          g_GenProjectTree=true;
          break;
#endif
        default:
          throw string("\nUnknown option: ")+*ArgIt+"\n\n"+ s_UsageString;
      }
    }
    else
    {
      m_CommandLineTargets.push_back(*ArgIt);
    }
    ArgIt++;
  }
  if (!m_Makefile)
  {
    if (!Makefile.empty())
    {
      if (!m_MakeDir)
        m_Makefile=GetFileInfo(Makefile,pDir);
      else
        m_Makefile=GetFileInfo(Makefile,m_MakeDir);
    }
  }
  if (!m_Makefile)
  {
    if (!m_MakeDir)
      m_Makefile=GetFileInfo(m_CommandLineTargets[0],pDir);
    else
      m_Makefile=GetFileInfo(m_CommandLineTargets[0],m_MakeDir);

    m_CommandLineTargets.erase(m_CommandLineTargets.begin());
  }
  if (!m_MakeDir)
  {
    if (Makefile==g_EmptyString)
      m_MakeDir=m_Makefile->GetDir(); /* This  is one from load_makefile, so we take the directory of the makefile itself. */
    else
      m_MakeDir=curdir::GetCurDir(); /* This means that this is the main makefile given on the command line, so we take the current directory */
  }

  if (!g_SkipMhMakeConfDirCheck)
  {
    if (loadedmakefile::sm_Statics.m_MhMakeConf)
    {
      const string &RootDir=loadedmakefile::sm_Statics.m_MhMakeConf->GetFullFileName();
      string MakeDir=m_MakeDir->GetFullFileName();
      if (RootDir.length()>MakeDir.length() || strncmp(RootDir.c_str(),MakeDir.c_str(),RootDir.length()))
      {
        cerr<<"mhmake needs to run in a directory that is a subdirectory of the directory specified with %MHMAKECONF : "<<RootDir<<", make dir : "<<m_MakeDir->GetFullFileName()<<"\nComparison is case sensitive!\n";
        exit(1);
      }
    }
  }
}

///////////////////////////////////////////////////////////////////////////////
void loadedmakefile::LoadMakefile()
{
  #ifdef _DEBUG
  if (g_PrintAdditionalInfo)
    cout << "Loading makefile "<<m_Makefile->GetQuotedFullFileName()<<endl;
  #endif

  m_pMakefileParser=refptr<mhmakefileparser>(new mhmakefileparser(m_CommandLineVars));

  // Add the MAKECMDGOALS environment variable
  string MakeCmdGoals;
  bool First=true;
  vector<string>::iterator TarIt=m_CommandLineTargets.begin();
  while (TarIt!=m_CommandLineTargets.end())
  {
    if (First)
      First=false;
    else
      MakeCmdGoals+=g_SpaceString;
    MakeCmdGoals+=*TarIt;
    TarIt++;
  }
  m_pMakefileParser->SetVariable("MAKECMDGOALS",MakeCmdGoals);

  string BaseAutoMak;

  // First parse the makefile.before makefile which is in the directory $(MHMAKECONF) environment variable
  fileinfo *pDepFile;
  if (sm_Statics.m_MhMakeConf)
  {
    BaseAutoMak=m_pMakefileParser->ExpandVar(BASEAUTOMAK);
    if (BaseAutoMak.empty())
    {
      BaseAutoMak="makefile";
      m_pMakefileParser->SetVariable(BASEAUTOMAK,BaseAutoMak);
    }
    const fileinfo *pBeforeMakefile=GetFileInfo(BaseAutoMak+".before",sm_Statics.m_MhMakeConf);

    int result=m_pMakefileParser->ParseFile(pBeforeMakefile,m_MakeDir);
    if (result)
    {
      throw string("Error parsing ")+pBeforeMakefile->GetQuotedFullFileName();
    }
    m_pMakefileParser->UpdateDate(pBeforeMakefile->GetDate());

    // Now parse the automaticly generated dependency file, which needs to be in the objdir directory
    string ObjDirName=m_pMakefileParser->ExpandExpression("$(OBJDIR)");
    if (!ObjDirName.size())
    {
      throw string("When making use of MHMAKECONF, you have to define OBJDIR in makefile.before");
    }
    pDepFile=GetFileInfo(ObjDirName+OSPATHSEPSTR "." + m_Makefile->GetName()+ ".dep",m_MakeDir);
    m_pMakefileParser->SetVariable(AUTODEPFILE,pDepFile->GetQuotedFullFileName());
  }
  else
  {
    pDepFile=GetFileInfo(string(".") + m_Makefile->GetName()+ ".dep",m_MakeDir);
    m_pMakefileParser->SetVariable(AUTODEPFILE,pDepFile->GetQuotedFullFileName());
  }

  if (pDepFile->Exists())
    m_pMakefileParser->LoadAutoDepsFile(pDepFile); /* Already load this autodep file before parsing of the makefile to avoid needless rebuilds. */

  //m_pMakefileParser->yydebug=1;
  int result=m_pMakefileParser->ParseFile(m_Makefile,m_MakeDir);
  if (result)
  {
    throw string("Error parsing ")+m_Makefile->GetQuotedFullFileName();
  }

  fileinfo *pMaybeNewDepFile=GetFileInfo(m_pMakefileParser->ExpandVar(AUTODEPFILE),m_MakeDir);
  #ifdef _DEBUG
  /* Check if the makefile has changed the AUTODEPFILE variable, if so generate a warning that a
   * rebuild could happen for the rules defined for making included makefiles */
  if (pMaybeNewDepFile!=pDepFile)
  {
    cout << "\n\nWARNING:\n  makefile '"<< m_Makefile->GetQuotedFullFileName() <<"' re-defines AUTODEPFILE\n  from '"<< pDepFile->GetQuotedFullFileName() <<"'\n  to '"<<
            m_pMakefileParser->ExpandVar(AUTODEPFILE) << "'\n  (may cause needless rebuilds when having rules for included makefiles!!!!!)\n\n\n";
  }

  if (g_PrintAdditionalInfo)
  {
    if (m_pMakefileParser->GetFirstTarget())
      cout<<"First target of "<<m_Makefile->GetQuotedFullFileName()<<" is "<<m_pMakefileParser->GetFirstTarget()->GetQuotedFullFileName()<<endl;
    else
      cout<<"No First target for "<<m_Makefile->GetQuotedFullFileName()<<endl;
  }
  #endif

  if (pMaybeNewDepFile!=pDepFile)
  {
    // this means the makefile has overruled the AUTODEPFILE, so parse it again
    pDepFile=pMaybeNewDepFile;
    if (pDepFile->Exists())
      m_pMakefileParser->LoadAutoDepsFile(pDepFile); /* If it already exists, load it. */
  }

  m_pMakefileParser->UpdateDate(m_Makefile->GetDate());

  if (sm_Statics.m_MhMakeConf)
  {
    fileinfo  *pAfterMakefile=GetFileInfo(BaseAutoMak+".after",sm_Statics.m_MhMakeConf);
    int result=m_pMakefileParser->ParseFile(pAfterMakefile);
    if (result) {
      throw string("Error parsing ")+pAfterMakefile->GetQuotedFullFileName();
    }
    m_pMakefileParser->UpdateDate(pAfterMakefile->GetDate());
  }
  bool ForceAutoDepRescan=g_ForceAutoDepRescan;
  if (pDepFile->Exists())
    m_pMakefileParser->LoadAutoDepsFile(pDepFile);
  else
    ForceAutoDepRescan=true;
  if (ForceAutoDepRescan)
    m_pMakefileParser->EnableAutoDepRescan();

  vector<string> &MakefilesToLoad=m_pMakefileParser->GetMakefilesToLoad();
  vector<string>::iterator It=MakefilesToLoad.begin();
  while (It!=MakefilesToLoad.end())
  {
    // First split the command into arguments
    const char *pTmp=It->c_str();
    vector<string> Args;
    while (*pTmp)
    {
      string Item;
      pTmp=NextItem(pTmp,Item);
      Args.push_back(Item);
    }

    refptr<loadedmakefile> pLoadedMakefile(new loadedmakefile(m_MakeDir,Args));
    refptr<loadedmakefile> Found=g_LoadedMakefiles.find(*pLoadedMakefile);
    if (Found)
    {
      #ifdef _DEBUG
      if (g_PrintAdditionalInfo)
        cout << "Makefile already loaded: "<<Found->m_Makefile->GetQuotedFullFileName()<<endl;
      #endif
    }
    else
    {
      g_LoadedMakefiles.push_back(pLoadedMakefile);

        /* If there is a rule to build the makefile, first check if it needs to be rebuild */
      m_pMakefileParser->BuildTarget(pLoadedMakefile->m_Makefile);
      pLoadedMakefile->LoadMakefile();
    }
    It++;
  }
}

/*****************************************************************************/
bool MakeDirs(fileinfo *pDir)
{
  fileinfo *pParentDir=pDir->GetDir();
  if (!pParentDir->GetDate().DoesExist())
  {  /* First make parent dirs */
    if (!MakeDirs(pParentDir))
      return false;
  }
  if (!pDir->GetDate().DoesExist())
  { /* Create directory */
    struct stat statbuf;
    if ( (-1==stat(pParentDir->GetFullFileName().c_str(),&statbuf)) ||
         (-1==mkdir(pDir->GetFullFileName().c_str(),statbuf.st_mode))
       )
    {
      cerr << "mkdir function failed for directory " << QuoteFileName(pDir->GetFullFileName()) << endl;
      return false;
    }
    pDir->InvalidateDate(); // Directory created successfully, so invalidate the date
  }
  return true;
}

#ifdef _DEBUG
///////////////////////////////////////////////////////////////////////////////
void DumpVarsAndRules()
{
  int i;
  LOADEDMAKEFILES::iterator LoadMakIt=g_LoadedMakefiles.begin();
  while (LoadMakIt!=g_LoadedMakefiles.end())
  {
    for (i=0; i<80; i++) cout << "_";
    cout << endl;
    cout << "Variables of makefile " << (*LoadMakIt)->m_Makefile->GetQuotedFullFileName() << endl;
    for (i=0; i<80; i++) cout << "_";
    cout << endl;
    (*LoadMakIt)->m_pMakefileParser->PrintVariables(true);
    cout << endl;
    LoadMakIt++;
  }
  for (i=0; i<80; i++) cout << "_";
  cout << endl;
  cout << "All Rules\n";
  for (i=0; i<80; i++) cout << "_";
  cout << endl;
  PrintFileInfos();
  for (i=0; i<80; i++) cout << "_";
  cout << endl;
  cout << "All Implicit Rules\n";
  for (i=0; i<80; i++) cout << "_";
  cout << endl;
  IMPLICITRULE::PrintImplicitRules();
}
#endif

