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

#ifndef __FILEINFO_H
#define __FILEINFO_H

#include "curdir.h"
#include "rule.h"
#include "md5.h"

#ifdef WIN32
#define OSPATHSEP '\\'
#define OSPATHSEPSTR "\\"
#define OSPATHENVSEP      ';'
#define OSPATHENVSEPSTR   ";"
#else
#define OSPATHSEP '/'
#define OSPATHSEPSTR "/"
#define OSPATHENVSEP      ':'
#define OSPATHENVSEPSTR   ":"
#endif

extern bool g_DumpOnError;
extern bool g_PrintVarsAndRules;
extern bool g_DoNotExecute;
extern bool g_GenProjectTree;
extern bool g_Quiet;
extern bool g_RebuildAll;
extern bool g_PrintAdditionalInfo;
extern bool g_pPrintDependencyCheck;
extern bool g_CheckCircularDeps;
extern bool g_ForceAutoDepRescan;
extern bool g_PrintLexYacc;
extern bool g_Clean;
extern bool g_StopCompiling;
extern bool g_PrintMultipleDefinedRules;

extern const string g_EmptyString;
extern const string g_SpaceString;
extern const string g_QuoteString;

string QuoteFileName(const string &Filename);
string UnquoteFileName(const string &Filename);

template<typename T>
inline string stringify(const T& x)
{
  ostringstream o;
  o << x;
  return o.str();
}

#ifdef WIN32
typedef unsigned _int64 mh_basetime_t;
#else
typedef unsigned long mh_basetime_t;
#endif

#define TIMESAFETY 3
class mh_time
{
  enum
  {
    DATENOTVALID=0,
    NOTEXISTTIME=1,
    DIRTIME     =2+TIMESAFETY
  };
  mh_basetime_t m_Time;

  bool operator < (const mh_time &Src);
public:
  mh_time(){m_Time=DATENOTVALID;}
  mh_time(time_t Time) : m_Time((mh_basetime_t)Time) {}
  mh_time(mh_basetime_t Time) : m_Time(Time) {}
  mh_time(const mh_time &Time) : m_Time(Time.m_Time) {}

  void SetDir(void)
  {
    m_Time=DIRTIME;
  }
  bool IsDir(void) const
  {
    return m_Time==DIRTIME;
  }
  void SetNotExist(void)
  {
    m_Time=NOTEXISTTIME;
  }
  bool IsExistingFile(void) const
  {
    return m_Time>DIRTIME;
  }
  bool DoesExist(void) const
  {
    return m_Time!=NOTEXISTTIME;
  }
  void Invalidate(void)
  {
    m_Time=DATENOTVALID;
  }
  bool IsDateValid(void) const
  {
    return m_Time!=DATENOTVALID;
  }
  friend ostream& operator<<(ostream& out,const mh_time &Src);

  mh_time &operator = (const mh_time &Src) { m_Time=Src.m_Time; return *this;}

  bool IsOlder(const mh_time &Src) const { return m_Time<Src.m_Time-TIMESAFETY; }

  bool IsNewer(const mh_time &Src) const { return m_Time>Src.m_Time+TIMESAFETY; }
  bool IsNewerOrSame(const mh_time &Src) const { return m_Time>=Src.m_Time-TIMESAFETY; }
};
typedef mh_time mh_time_t;

inline ostream& operator<<(ostream& out,const mh_time &Src)
{
  out << hex << (unsigned long)Src.m_Time;
  return out;
}

class fileinfo
{
  string m_AbsFileName;
  bool m_IsPhony;
  int m_BuildStatus;  /* Bit 0 means the target built is started, Bit 1 means the target is still building */
  refptr<rule> m_pRule;
  mhmakefileparser *m_pAutoDepsMakefile;
  vector<fileinfo*> m_Deps;
  mh_time_t m_Date;
  uint32 m_CommandsMd5_32;  // 32-bit Md5 checksum of commands to build this target

  fileinfo(const fileinfo &Src);
  fileinfo(void);

  void init(const string &AbsFileName,uint32 Md5_32)
  {
    m_IsPhony=false;
    m_pAutoDepsMakefile=NULL;
    m_BuildStatus=0;
    m_AbsFileName=UnquoteFileName(AbsFileName);
    InvalidateDate();
    m_CommandsMd5_32=Md5_32;
    #ifdef _DEBUG
    if (g_PrintAdditionalInfo)
      cout << "Initialising Md5 of "<<GetQuotedFullFileName().c_str()<<" to 0x"<<hex<<Md5_32<<endl;
    #endif
  }
  fileinfo &operator = (const fileinfo &Src);  // Do not allow copying
public:
  fileinfo(const string &AbsFileName,uint32 Md5_32)
  {
    init(AbsFileName,Md5_32);
  }
  fileinfo(const string &AbsFileName)
  {
    init(AbsFileName,0);
  }
  /* The following constructor is only used for name comparisons, and should only be used for that */
  fileinfo(int Dummy,const string &AbsFileName)
  {
    m_AbsFileName=UnquoteFileName(AbsFileName);
  }

  fileinfo(const char *szFile)
  {
    init(szFile,0);
  }
  fileinfo(const char *szFile,uint32 Md5_32)
  {
    init(szFile,Md5_32);
  }
  const string &GetFullFileName(void) const
  {
    return m_AbsFileName;
  }
  string GetQuotedFullFileName(void) const
  {
    return QuoteFileName(m_AbsFileName);
  }
#ifdef WIN32
#define MINPATHLENGTH 3 // The smallest dir name in windows is 3, e.g. "c:\"
#else
#define MINPATHLENGTH 1 // The smallest dir name in linux is 1, e.g. "/"
#endif
  void SetFullFileName(const string &strAbsName)
  {
    m_AbsFileName=UnquoteFileName(strAbsName);
    // If the last char is path sep strip it, except for the smallest dir name
    if (m_AbsFileName.length()>MINPATHLENGTH && m_AbsFileName[m_AbsFileName.length()-1]==OSPATHSEP)
      m_AbsFileName.resize(m_AbsFileName.length()-1);
  }

  fileinfo* GetDir(void) const;
  string GetName() const;
  bool IsDir() const;

  string GetErrorMessageDuplicateRule(const refptr<rule> &pRule);

  void SetRule(refptr<rule> &pRule)
  {
    #if defined(_DEBUG) && defined(_MSC_VER)
    if (m_pRule && m_pRule->GetCommands().size() && !IsBuilding()) {
      DebugBreak();
    }
    #endif
    m_pRule=pRule;
  }

  void SetRuleIfNotExist(refptr<rule> &pRule)
  {
    if (pRule)
    {
      if (!m_pRule)
      {
        SetRule(pRule);
        pRule->AddTarget(this);
      }
      #ifdef _DEBUG
      else
      {
        if (*m_pRule!=*pRule)
        {
          throw(GetErrorMessageDuplicateRule(pRule));
        }
        else if (g_PrintMultipleDefinedRules)
        {
          cerr<<GetErrorMessageDuplicateRule(pRule);
        }
      }
      #endif
    }
  }

  refptr<rule> GetRule(void) const
  {
    return m_pRule;
  }
  void AddDep(fileinfo *pDep)
  {
    if (&*pDep==this)
    {
      #ifdef _DEBUG
      cout << GetQuotedFullFileName()<<" is directly dependent on itself\n";
      #endif
      return;
    }
    m_Deps.push_back(pDep);
  }
  void AddDeps(vector<fileinfo*> &Deps);

  void InsertDeps(vector<fileinfo*> &Deps)
  {
    #ifdef _DEBUG
    vector<fileinfo*> NewDeps;
    vector<fileinfo*>::const_iterator It=Deps.begin();
    vector<fileinfo*>::const_iterator ItEnd=Deps.end();
    while (It!=ItEnd)
    {
      if (&**It==this)
      {
        #ifdef _DEBUG
        cout << GetQuotedFullFileName()<<" is directly dependent on itself\n";
        #endif
      }
      else
        NewDeps.push_back(*It);
      It++;
    }
    if (NewDeps.size())
      m_Deps.insert(m_Deps.begin(),NewDeps.begin(),NewDeps.end());
    #else
      m_Deps.insert(m_Deps.begin(),Deps.begin(),Deps.end());
    #endif
  }
  vector<fileinfo*> &GetDeps(void)
  {
    return m_Deps;
  }
  string GetPrerequisits(void) const;
  void SetAutoDepsScan(mhmakefileparser *pMakefile)
  {
    #ifdef _DEBUG
    if (m_pAutoDepsMakefile)
      throw(string(".AUTODEPS can only defined ones for rule ")+GetFullFileName());
    #endif
    m_pAutoDepsMakefile=pMakefile;
  }
  mhmakefileparser *GetAutoDepsMakefile(void) const
  {
    return m_pAutoDepsMakefile;
  }
  void SetPhony(void)
  {
    m_IsPhony=true;
    m_Date.SetNotExist(); // This will sure that this target will always be build (even if a corresponding file exists)
  }
  bool IsPhony(void) const
  {
    return m_IsPhony;
  }
  mh_time_t realGetDate(void) const;
  void SetDateToNow(void);

  void SetDate(mh_time_t Date)
  {
    m_Date=Date;
  }

  bool IsDateValid() const
  {
    return m_Date.IsDateValid();
  }
  void InvalidateDate(void)
  {
    m_Date.Invalidate();
  }

  mh_time_t GetDate(void) const
  {
    if (m_Date.IsDateValid())
      return m_Date;
    else
      return realGetDate();
  }
  void SetNotExist(void)
  {  // this is used to make sure that this item is rebuild, even if it really exists
    m_Date.SetNotExist();
  }
  bool Exists(void) const
  {
    return GetDate().DoesExist();
  }
  bool IsBuildStarted(void) const
  {
    return (m_BuildStatus&1)==1;
  }
  bool IsBuild(void) const
  {
    return m_BuildStatus==1;
  }
  void SetBuild(void)
  {
    m_BuildStatus=1;
  }
  bool IsBuilding(void) const
  {
    return (m_BuildStatus&2)==2;
  }
  void SetBuilding(bool Others=true)
  {
    m_BuildStatus|=2;
    /* Check if there are targets build by the rule attached to this target, if so set them also to building */
    if (Others && m_pRule)
    {
      m_pRule->SetTargetsIsBuilding(this);
    }
  }
  void ClearBuilding(void)
  {
    m_BuildStatus&=~2;
  }
  bool IsAutoDepExtention(void) const;

  void SetCommandsMd5_32(uint32 Md5_32)
  {
    #ifdef _DEBUG
    if (g_PrintAdditionalInfo)
      cout << "Setting Md5 of "<<GetQuotedFullFileName()<<" to 0x"<<hex<<Md5_32<<endl;
    #endif
    m_CommandsMd5_32=Md5_32;
  }
#ifdef _DEBUG
  uint32 GetCommandsMd5_32(void) const
  {
    return m_CommandsMd5_32;
  }
#endif
  bool CompareMd5_32(uint32 Md5_32) const
  {
    return m_CommandsMd5_32==Md5_32;
  }
  void WriteMd5_32(FILE *pFile) const
  {
    fwrite(&m_CommandsMd5_32,sizeof(m_CommandsMd5_32),1,pFile);
  }
};

struct less_fileinfo : public binary_function <const fileinfo*, const fileinfo*, bool>
{
  bool operator()(const fileinfo *_Left, const fileinfo *_Right) const
  {
    return less<string>().operator ()(_Left->GetFullFileName(),_Right->GetFullFileName());
  }
};

fileinfo *GetFileInfo(const string &szName,const fileinfo* pRelDir);

class fileinfos : public set<fileinfo*,less_fileinfo>
{
  public:
    ~fileinfos();
};

extern fileinfos g_FileInfos;

inline fileinfo *GetAbsFileInfo(const string &strAbsName)
{
  static fileinfo SearchFileInfo("");
  SearchFileInfo.SetFullFileName(strAbsName);
  /* Using find is just an optimalisation, you could use insert immediately */
  fileinfos::const_iterator pFind=g_FileInfos.find(&SearchFileInfo);
  if (pFind==g_FileInfos.end())
  {
    pair <fileinfos::iterator, bool> pPair=g_FileInfos.insert(new fileinfo(SearchFileInfo.GetFullFileName()));
    return *(pPair.first);
  }
  else
    return *pFind;
}

inline fileinfo *GetFileInfo(const char *szName,const fileinfo* pRelDir)
{
  return GetFileInfo(string(szName),pRelDir);
}

string &NormalizePathName(string &Name);
void PrintFileInfos();

#endif

