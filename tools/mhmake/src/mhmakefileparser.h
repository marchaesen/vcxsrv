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

#ifndef __MHMAKEFILE_PARSER__
#define __MHMAKEFILE_PARSER__

#include "fileinfo.h"
#include "util.h"
#include "commandqueue.h"

class rule;

#include "flexlexer.h"

class mhmakefileparser;
typedef string (mhmakefileparser::*function_f)(const string &) const;

struct funcdef
{
  const char *szFuncName;
  function_f   pFunc;
};

class fileinfoarray : public refbase,public vector<fileinfo*>
{
};

typedef set<fileinfo*> deps_t;
typedef pair< bool, deps_t > autodeps_entry_t;
typedef map<fileinfo*, autodeps_entry_t > autodeps_t;

namespace yy
{
  class location;
  class mhmakeparser;
}

class mhmakefileparser : public refbase
{
  friend class yy::mhmakeparser;

private:
  static commandqueue             sm_CommandQueue;
  static stack<yy::mhmakeparser*> sm_ParserStack; // Keeps track of the currently active parser
private:
  fileinfo             *m_RuleThatIsBuild;
  vector<string>        m_ToBeIncludeAfterBuild;
  vector<string>        m_MakefilesToLoad;
  fileinfo*             m_AutoDepFileLoaded;
  mh_time_t             m_Date;
  uint32                m_EnvMd5_32;   /* Cached Md5_32 value of the userd environment variables */
#ifdef _DEBUG
  int                   m_ImplicitSearch;
#endif
  map<string,string>    m_CommandCache;

protected:
  map<string,string>    m_Variables;
  map<string,string>    m_CommandLineVars;
  const fileinfo       *m_MakeDir;
  refptr<rule>          m_pCurrentRule;
  refptr<fileinfoarray> m_pCurrentItems;
  refptr<fileinfoarray> m_pCurrentDeps;
  fileinfo*             m_FirstTarget;
  fileinfoarray         m_IncludedMakefiles;
  refptr<fileinfoarray> m_pIncludeDirs;
  string                m_IncludeDirs;

  bool                  m_DoubleColonRule;
  bool                  m_AutoDepsDirty;
  bool                  m_ForceAutoDepRescan;
  set<string>           m_SkipHeaders;        // Headers to skip
  vector<string>        m_PercentHeaders;  // Percent specification of headers to skip
  bool                  m_SkipHeadersInitialized;    // true when previous 2 variables are initialized

  bool                  m_RebuildAll; /* true when to rebuild all targets of this makefile */

  set<string>           m_UsedEnvVars;     // Array containing a list of variables that are taken from the environment (This is used for rebuild checking)
  set<string>           m_EnvVarsToIgnore; // List of env vars that do not have to be taken into account for rebuild checking
  set<string>           m_Exports;         // Array containing a list of exported variables in the makefile (This is used for rebuild checking)
  vector< pair< string, refptr<fileinfoarray> > > m_vPath;
#ifdef WIN32
  char                 *m_pEnv;            // New environment in case the makefile exports variables
#else
  char                **m_pEnv;            // New environment in case the makefile exports variables
#endif
  size_t                m_EnvLen;          // Current length of m_pEnv

  autodeps_t m_AutoDeps;
  set< const fileinfo* > m_Targets; // List of targets that are build by this makefile

  vector<string> m_FilesToRemoveAtEnd;

  static mh_time_t m_sBuildTime;
private:
  string ResolveExpression(const string &InExpr,string &Rest) const;

public:
#ifdef _DEBUG
  deque<fileinfo*> m_TargetStack;   /* Used to detect circular dependencies */
#endif

  mhmakefileparser(const map<string,string> &CommandLineVars)
    :  m_CommandLineVars(CommandLineVars)
      ,m_AutoDepsDirty(false)
      ,m_ForceAutoDepRescan(false)
      ,m_SkipHeadersInitialized(false)
      ,m_RebuildAll(false)
      ,m_EnvMd5_32(0)
      ,m_pEnv(NULL)
      ,m_FirstTarget(NULL)
      ,m_RuleThatIsBuild(NULL)
      ,m_AutoDepFileLoaded(NULL)
      #ifdef _DEBUG
      ,m_ImplicitSearch(0)
      #endif
  {
    if (!m_FunctionsInitialised) InitFuncs();
    SetVariable("MAKE_VERSION",MHMAKEVER);
    SetVariable(OBJEXTVAR,OBJEXT);
    SetVariable(EXEEXTVAR,EXEEXT);
    m_EnvVarsToIgnore.insert(WC_REVISION);
    m_EnvVarsToIgnore.insert(WC_URL);
    m_EnvVarsToIgnore.insert(MAKE);
  }

  /* Needed if you only want to use the searchcommand and execommand functions */
  mhmakefileparser(const fileinfo *pMakeDir) :
    m_MakeDir(pMakeDir)
  , m_AutoDepsDirty(false)
  , m_pEnv(NULL)
  {}

  static void SetNrParallelBuilds(int NrParallelBuilds)
  {
    sm_CommandQueue.SetNrParallelBuilds(NrParallelBuilds);
  }

  bool CompareEnv() const;
  uint32 CreateEnvMd5_32() const;
  string GetFromEnv(const string &Var, bool *pDefined=NULL) const;
  void CreateUSED_ENVVARS();

  void SetExport(const string & Var, const string & Val);
  void SetvPath(const string & Pattern, const string & Path);

  void CheckEnv(void);

  #ifdef _DEBUG
  int ImplicitSearch() const
  {
    return m_ImplicitSearch;
  }
  #endif

  void SetAutoDepsDirty()
  {
    m_AutoDepsDirty=true;
  }

  void ClearAutoDepsDirty()
  {
    m_AutoDepsDirty=false;
  }
  bool IsAutoDepsDirty() const
  {
    return m_AutoDepsDirty;
  }

  void SetRebuildAll()
  {
    m_RebuildAll=true;
    SetAutoDepsDirty();  /* This is to be sure that all new calculated md5 strings are saved. */
  }

  void SetVariable(const string &Var, const string &Val)
  {
    m_Variables[Var]=Val;
  }
  void DeleteVariable(const string &Var)
  {
    m_Variables.erase(Var);
  }
  bool GetVariable(const string &Var, string &Val)
  {
    map<string,string>::const_iterator pIt=m_Variables.find(Var);
    if (pIt==m_Variables.end())
    {
      return false;
    }
    else
    {
      Val = pIt->second;
      return true;
    }
  }

  void EnableAutoDepRescan(void)
  {
    m_ForceAutoDepRescan=true;
    SetAutoDepsDirty();
  }
  bool ForceAutoDepRescan(void)
  {
    return m_ForceAutoDepRescan;
  }
  void SetRuleThatIsBuild(fileinfo *pTarget)
  {
    m_RuleThatIsBuild=pTarget;
  }
  void ClearRuleThatIsBuild()
  {
    m_RuleThatIsBuild=NULL;
  }
  void GetAutoDepsIfNeeded(fileinfo *pTarget, const fileinfo *pFirstDep);
  void UpdateAutomaticDependencies(fileinfo *pTarget);
  void UpdateNoRuleAutomaticDependencies(fileinfo *pTarget);
  const refptr<fileinfoarray> GetIncludeDirs() const;
  void GetAutoDeps(const fileinfo *pFirstDep, deps_t &Autodeps);
  void SaveAutoDepsFile();
  void LoadAutoDepsFile(fileinfo *pDepFile);
  bool SkipHeaderFile(const string &FileName);
  void InitEnv() const;

  virtual ~mhmakefileparser() /* virtual to be sure the correct destructor is called when we delete with a pointer to this class which in reality is a pointer to a derived class */
  {
    SaveAutoDepsFile();
    /* remove generated temporary batch files */
    vector<string>::const_iterator It=m_FilesToRemoveAtEnd.begin();
    while (It!=m_FilesToRemoveAtEnd.end())
    {
      remove(It->c_str());
      It++;
    }
#ifndef WIN32
    char **pEnv=m_pEnv;
    if (pEnv)
      while (*pEnv) free(*pEnv++);
#endif
    free(m_pEnv);
  }

  virtual int parse()
  {
    throw("Please derive if you want to execute yyparse.");
  }

  int ParseFile(const fileinfo *pFileInfo,const fileinfo *pMakeDir=NULL);
  int ParseString(const string &StringIn);

  /* Functions to handle variables */
  bool IsDefined(const string &Var) const;
  bool IsEqual(const string &EqualExpr) const;
  bool IsExprTrue(const string &EqualExpr) const;
  string ExpandExpression(const string &Expr) const;
  string ExpandMacro(const string &Expr) const;
  string ExpandVar(const string &Var) const;

  void PrintVariables(bool Expand=false) const;

  /* Functions for macro functions */
  static funcdef m_FunctionsDef[];
  static map<string,function_f> m_Functions;
  static bool m_FunctionsInitialised;
  static void InitFuncs(void);
  string f_filter(const string &) const;
  string f_call(const string &) const;
  string f_if(const string &) const;
  string f_findstring(const string &) const;
  string f_firstword(const string &) const;
  string f_wildcard(const string &) const;
  string f_subst(const string &) const;
  string f_patsubst(const string &) const;
  string f_concat(const string &) const;
  string f_basename(const string & Arg) const;
  string f_notdir(const string & Arg) const;
  string f_dir(const string & Arg) const;
  string f_shell(const string & Arg) const;
  string f_relpath(const string & Arg) const;
  string f_realpath(const string & Arg) const;
  string f_abspath(const string & Arg) const;
  string f_toupper(const string & Arg) const;
  string f_tolower(const string & Arg) const;
  string f_exist(const string & Arg) const;
  string f_filesindirs(const string & Arg) const;
  string f_fullname(const string & Arg) const;
  string f_addprefix(const string & Arg) const;
  string f_addsuffix(const string & Arg) const;
  string f_filterout(const string & Arg) const;
  string f_word(const string & Arg) const;
  string f_lastword(const string & Arg) const;
  string f_words(const string & Arg) const;
  string f_strip(const string & Arg) const;
  string f_which(const string & Arg) const;
  string f_foreach(const string & Arg) const;
  string f_eval(const string & Arg) const;
  string f_sort(const string & Arg) const;
  string f_error(const string & Arg) const;
  string f_info(const string & Arg) const;
  string f_warning(const string & Arg) const;

  fileinfo* GetFirstTarget() const
  {
    return m_FirstTarget;
  }

  const fileinfo *GetMakeDir() const
  {
    return m_MakeDir;
  }

  mh_time_t GetDate(void) const
  {
    return m_Date;
  }

  void UpdateDate(mh_time_t Date)
  {
    if (Date.IsNewer(m_Date))
      m_Date=Date;
  }

  void AddTarget(const fileinfo* pTarget)
  {
    m_Targets.insert(pTarget);
  }
  /* Starts building the target,
    returns 0 when target build is not finished,
    returns the date of the target when target is build, especially important for phony rules, since this will be the youngest date of all dependencies */
  mh_time_t StartBuildTarget(fileinfo* pTarget, bool bCheckTargetDir=true);
  /* Waits for the target being build, returns the date of the target. Not needed to be cald when StartBuildTarget returned a value different from zero */
  mh_time_t WaitBuildTarget(fileinfo* pTarget);

  fileinfo* SearchvPath(const fileinfo* pTarget);  /* Search for the target using the vPath */

  /* Use the following command when you want to wait for the target is built */
  mh_time_t BuildTarget(fileinfo* pTarget, bool bCheckTargetDir=true)
  {
    mh_time_t TargetDate=StartBuildTarget(pTarget,bCheckTargetDir);
    if (!TargetDate.IsDateValid())
      TargetDate=WaitBuildTarget(pTarget);
    return TargetDate;
  }

  void BuildDependencies(const refptr<rule> &pRule, fileinfo* pTarget, mh_time_t TargetDate, mh_time_t &YoungestDate, bool &MakeTarget);

  void BuildIncludedMakefiles();

  void AddIncludedMakefile(fileinfo* pMakeInfo)
  {
    UpdateDate(pMakeInfo->GetDate());
    m_IncludedMakefiles.push_back(pMakeInfo);
  }
  fileinfoarray &GetIncludedMakefiles()
  {
    return m_IncludedMakefiles;
  }

  void IncludeAfterBuild(const string &IncludeFile)
  {
    m_ToBeIncludeAfterBuild.push_back(IncludeFile);
  }
  void ParseBuildedIncludeFiles();

  void AddMakefileToMakefilesToLoad(const string &Makefile)
  {
    m_MakefilesToLoad.push_back(Makefile);
  }
  vector<string>& GetMakefilesToLoad()
  {
    return m_MakefilesToLoad;
  }
  void AddRule();

  mh_pid_t ExecuteCommand(string Command, bool &IgnoreError, string *pOutput=NULL);
  mh_pid_t ExecuteCommand(string Command, string *pOutput=NULL)
  {
    bool IgnoreError;
    return ExecuteCommand(Command, IgnoreError, pOutput);
  }
  string GetFullCommand(const string &Command);

  static void InitBuildTime();

  void SplitToItems(const string &String, vector<fileinfo*> &Items) const;
  mh_pid_t DeleteFiles(const string &Params) const;
  mh_pid_t CopyFiles(const string &Params) const;
  mh_pid_t TouchFiles(const string &Params) const;
  mh_pid_t EchoCommand(const string &Params) const;
  mh_pid_t MakeDirsCommand(const string &Params) const;
  string SearchCommand(const string &Command, const string &Extension="") const;
  const string &GetPythonExe() const;
  int SearchPath(const char *szCommand, const char *pExt, size_t Len, char *szFullCommand,char **pFilePart) const;
  mh_pid_t OsExeCommand(const string &Command, const string &Params, bool IgnoreError, string *pOutput) const;

  string GetFileNameLineNo(void) const;
};

class mhmakeparserbase
{
protected:
  mhmakefileparser *m_pMakefile;
  mhmakeFlexLexer  *m_ptheLexer;
public:
  mhmakeparserbase(mhmakefileparser *pMakefile, mhmakeFlexLexer *pLexer)
    : m_pMakefile(pMakefile), m_ptheLexer(pLexer)
  {
  }
  int yylex (YYSTYPE* yylval, yy::location* yylloc)
  {
    return m_ptheLexer->yylex(yylval,yylloc);
  }
  string &GetInputFilename(void) const
  {
    return m_ptheLexer->GetInputFilename();
  }
  mhmakefileparser *getMakefile()
  {
    return m_pMakefile;
  }

};

#endif

