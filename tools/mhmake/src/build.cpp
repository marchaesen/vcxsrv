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

#include "mhmakefileparser.h"
#include "rule.h"
#include "util.h"

/*****************************************************************************/
int mhmakefileparser::SearchPath(const char *szCommand, const char *pExt, size_t Len, char *szFullCommand,char **pFilePart) const
{
  static vector< fileinfo* > vSearchPath;

  string Command(szCommand);
  if (pExt)
    Command+=pExt;
  vector< fileinfo* >::iterator It;
  vector< fileinfo* >::iterator ItEnd;

  fileinfo* pCommandFile=GetFileInfo(Command,m_MakeDir);
  if (pCommandFile->Exists())
  {
    goto found;
  }
  pCommandFile->InvalidateDate(); // It could be created in the makefile later
  if (!vSearchPath.size())
  {
     char *pPath=getenv(PATH);
     if (!pPath)
       return 0;
     char *Path=strdup(pPath); // To be able to use strtok
     char *pTok=strtok(Path,OSPATHENVSEPSTR);
     while (pTok)
     {
       vSearchPath.push_back(GetFileInfo(pTok,m_MakeDir));
       pTok=strtok(NULL,OSPATHENVSEPSTR);
     }
     free(Path);
  }
  It=vSearchPath.begin();
  ItEnd=vSearchPath.end();
  while (It!=ItEnd)
  {
    pCommandFile=GetFileInfo(Command,*It);
    if (pCommandFile->Exists())
      goto found;
    It++;
  }

  return 0;

found:
  string FullCommand=pCommandFile->GetFullFileName();
  size_t CommandLen=FullCommand.size();
  if (CommandLen>Len-1)
  {
    throw string("Command to long: ") + FullCommand;
  }
  strcpy(szFullCommand,FullCommand.c_str());
  return 1;
}

/*****************************************************************************/
string mhmakefileparser::SearchCommand(const string &Command, const string &Extension) const
{
  char FullCommand[MAX_PATH]="";
  unsigned long Size=sizeof(FullCommand);
  const char *pExt;
  if (Extension.empty())
    pExt=NULL;
  else
    pExt=Extension.c_str();
  if (SearchPath(UnquoteFileName(Command).c_str(),pExt,MAX_PATH,FullCommand,NULL))
    return FullCommand;
#ifdef WIN32
  /* See if we have a path for python.exe in the registry */
  HKEY hKey;
  string RegEntry=Command;
  if (pExt)
  {
    RegEntry+=Extension;
  }
  string KeyName=string("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\")+RegEntry;
  if (ERROR_SUCCESS!=RegOpenKey(HKEY_LOCAL_MACHINE,KeyName.c_str(),&hKey))
    return FullCommand;

  RegQueryValueEx(hKey,NULL,NULL,NULL,(LPBYTE)FullCommand,&Size);

  RegCloseKey(hKey);
#endif

  return FullCommand;
}

/*****************************************************************************/
/* Deletes a complete directory or files with wildcard. It can be assumed that the Directory passed does exist */
static bool DeleteDir(const string &Dir,const string WildSearch="*",bool bRemoveDir=true)
{
  bool Error=false;
  string Pattern=Dir+OSPATHSEP+WildSearch;
#ifdef WIN32
  WIN32_FIND_DATA FindData;
  mh_pid_t hFind=FindFirstFile(Pattern.c_str(),&FindData);
  if (hFind==INVALID_HANDLE_VALUE)
  {
    return Error;
  }

  do
  {
    /* Only handle items which are not . and .. */
    if (FindData.cFileName[0]!='.' || (FindData.cFileName[1] && (FindData.cFileName[1]!='.' || FindData.cFileName[2])) )
    {
      string FileName=Dir+OSPATHSEP+FindData.cFileName;
      if (FindData.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)
      {
        Error = DeleteDir(FileName);
      }
      else
      {
        Error = (-1==remove(FileName.c_str()));
      }
    }
  } while (FindNextFile(hFind,&FindData));

  FindClose(hFind);

  if (bRemoveDir)
    Error = (0==RemoveDirectory(Dir.c_str()));
#else
  glob_t Res;
  if (glob (Pattern.c_str(), GLOB_ERR|GLOB_NOSORT|GLOB_MARK, NULL, &Res))
    return Error;

  for (int i=0; i<Res.gl_pathc; i++)
  {
    int Len=strlen(Res.gl_pathv[i])-1;
    if (Res.gl_pathv[i][Len]=='/')
    {
      Res.gl_pathv[i][Len]=0;
      Error = DeleteDir(Res.gl_pathv[i]);
    }
    else
    {
      Error = (-1==remove(Res.gl_pathv[i]));
    }
  }

  globfree(&Res);

  if (bRemoveDir)
    Error = (-1==remove(Dir.c_str()));
#endif

  return Error;
}

/*****************************************************************************/
mh_pid_t mhmakefileparser::DeleteFiles(const string &Params) const
{
  bool IgnoreError=false;
  vector<fileinfo*> Files;

  // First check if the first parameter is -e meaning don't give an error when file does not exist
  if (Params[1]=='-')
  {
    if (Params[2]=='e')
    {
      IgnoreError=true;
      SplitToItems(Params.substr(4),Files);
    }
    else
    {
      cerr << "Invalid option "<<Params[1]<<" in del statement\n";
      return (mh_pid_t)-1;
    }
  }
  else
  {
    SplitToItems(Params,Files);
  }

  vector<fileinfo*>::const_iterator It=Files.begin();
  while (It!=Files.end())
  {
    fileinfo* pFile=*It++;
    string DirSearch="*";
    bool bRemoveDir=true;

    /* Now check if there is a wildcard */
    if (pFile->GetFullFileName().find('*')!=string::npos)
    {
      DirSearch=pFile->GetName();
      pFile=pFile->GetDir();
      bRemoveDir=false;
    }

    pFile->InvalidateDate();
    if (IgnoreError && !pFile->Exists() && !pFile->IsDir())
    {
      continue;
    }

    const string &FileName=pFile->GetFullFileName();
    if (pFile->IsDir())
    {
      if (DeleteDir(FileName,DirSearch,bRemoveDir) && !IgnoreError)
      {
        cerr << "Error deleting "<<FileName<<endl;
        return (mh_pid_t)-1;
      }
    }
    else
    {
      if (-1==remove(FileName.c_str()) && !IgnoreError)
      {
        cerr << "Error deleting "<<FileName<<endl;
        return (mh_pid_t)-1;
      }
    }
  }
  return (mh_pid_t)0;
}

/*****************************************************************************/
/* pDest can be a directory or a file */
static bool CopyFile(fileinfo* pSrc, fileinfo* pDest)
{
  if (pDest->IsDir())
  {
    pDest=GetFileInfo(pSrc->GetName(),pDest);
  }
  string SrcFileName=pSrc->GetFullFileName();
  string DestFileName=pDest->GetFullFileName();

  /* Now copy the file */
  FILE *pSrcFile=fopen(SrcFileName.c_str(),"rb");
  if (!pSrcFile)
  {
    cerr << "copy: error opening file "<<SrcFileName<<endl;
    return false;
  }
  FILE *pDestFile=fopen(DestFileName.c_str(),"wb");
  if (!pDestFile)
  {
    cerr << "copy: error creating file "<<DestFileName<<endl;
    return false;
  }
  char Buf[4096];
  size_t Ret;

  while ( (Ret=fread(Buf,1,sizeof(Buf),pSrcFile)) > 0)
  {
    fwrite(Buf,1,Ret,pDestFile);
  }
  fclose(pSrcFile);
  fclose(pDestFile);
  pDest->InvalidateDate();
  return true;
}

/*****************************************************************************/
/* Copies a complete directory to a destination (currenlty not recursive */
static bool CopyDir(fileinfo* pDir, fileinfo* pDest,const string WildSearch="*")
{
  bool Error=true;
  string Pattern=pDir->GetFullFileName()+OSPATHSEP+WildSearch;
#ifdef WIN32
  WIN32_FIND_DATA FindData;
  mh_pid_t hFind=FindFirstFile(Pattern.c_str(),&FindData);
  if (hFind==INVALID_HANDLE_VALUE)
  {
    return false;
  }

  do
  {
    /* Only handle items which are not . and .. */
    if (FindData.cFileName[0]!='.' || (FindData.cFileName[1] && (FindData.cFileName[1]!='.' || FindData.cFileName[2])) )
    {
      if (FindData.dwFileAttributes&FILE_ATTRIBUTE_HIDDEN)
        continue;
      fileinfo* pSrc=GetFileInfo(FindData.cFileName,pDir);

      if (pSrc->IsDir())
      {
        fileinfo* pNewDest=GetFileInfo(FindData.cFileName,pDest);
        if (!pNewDest->IsDir())
        {
          if (pNewDest->Exists())
          {
            cerr << pNewDest->GetFullFileName() << " exists and is not a directory.\n";
            Error = false;
            goto exit;
          }
          if (!CreateDirectory(pNewDest->GetFullFileName().c_str(),NULL))
          {
            cerr << "Error creating directory " << pNewDest->GetFullFileName() << endl;
            Error = false;
            goto exit;
          }
          pNewDest->InvalidateDate();
        }
        Error = CopyDir(pSrc,pNewDest);
        if (!Error) goto exit;
      }
      else
      {
        Error = CopyFile(pSrc,pDest);
        if (!Error) goto exit;
      }
    }
  } while (FindNextFile(hFind,&FindData));

exit:
  FindClose(hFind);
#else
  glob_t Res;
  if (glob (Pattern.c_str(), GLOB_ERR|GLOB_NOSORT|GLOB_MARK, NULL, &Res))
    return Error;

  for (int i=0; i<Res.gl_pathc; i++)
  {
    fileinfo* pSrc=GetFileInfo(Res.gl_pathv[i],pDir);
    if (pSrc->IsDir())
    {
      *(strrchr(Res.gl_pathv[i],'/'))='\0';
      const char *SrcDirName=strrchr(Res.gl_pathv[i],'/')+1;

      if (SrcDirName[0]=='.')
        continue;

      fileinfo* pNewDest=GetFileInfo(SrcDirName,pDest);
      if (!pNewDest->IsDir())
      {
        if (pNewDest->Exists())
        {
          cerr << pNewDest->GetQuotedFullFileName() << " exists and is not a directory.\n";
          Error = false;
          goto exit;
        }
        struct stat statbuf;
        if ( (-1==stat(pDest->GetFullFileName().c_str(),&statbuf)) ||
             (-1==mkdir(pNewDest->GetFullFileName().c_str(),statbuf.st_mode))
           )
        {
          cerr << "Error creating directory " << pNewDest->GetQuotedFullFileName() << endl;
          Error = false;
          goto exit;
        }
        pNewDest->InvalidateDate();
      }
      Error = CopyDir(pSrc,pNewDest);
      if (!Error) goto exit;
    }
    else
    {
      Error = CopyFile(GetFileInfo(Res.gl_pathv[i],pDir),pDest);
      if (!Error) goto exit;
    }
  }

exit:
  globfree(&Res);
#endif

  return Error;
}

/*****************************************************************************/
mh_pid_t mhmakefileparser::MakeDirsCommand(const string &Params) const
{
  vector<fileinfo*> Dirs;

  SplitToItems(Params,Dirs);

  size_t NrDirs=Dirs.size();
  for (size_t i=0; i<NrDirs; i++)
  {
    fileinfo* pDir=Dirs[i];
    if (pDir->GetName()=="-p")
      continue;
    if (!MakeDirs(pDir))
      return (mh_pid_t)-1;
  }
  return (mh_pid_t)0;
}

/*****************************************************************************/
static string RemoveQuotes(const string &StrIn)
{
  char FirstChar=StrIn[0];
  string Ret=StrIn;
  if (FirstChar=='"')
  {
    int Len=Ret.length()-1;
    if (Ret[Len]=='"')
      Ret=Ret.substr(1,Len-1);
  }
  else if (FirstChar=='\'')
  {
    int Len=Ret.length()-1;
    if (Ret[Len]=='\'')
      Ret=Ret.substr(1,Len-1);
  }
  return Ret;
}
mh_pid_t mhmakefileparser::EchoCommand(const string &ParamsIn) const
{
  string Params=f_strip(ParamsIn);
    // Find the first > character
  size_t Pos=Params.find_first_of('>');
  if (Pos==string::npos)
  {
    // Just echo it
    cout << RemoveQuotes(Params) << endl;
  }
  else
  {
    FILE *pfFile;
     /* Extra the filename */
    string Filename;
    if (Params[Pos+1]=='>')
    {
      NextItem(Params.substr(Pos+2).c_str(),Filename);
      fileinfo* pFile=GetFileInfo(Filename,m_MakeDir);
        // Open file in append
      pfFile=fopen(pFile->GetFullFileName().c_str(),"a");
    }
    else
    {
      NextItem(Params.substr(Pos+1).c_str(),Filename);
      fileinfo* pFile=GetFileInfo(Filename,m_MakeDir);
      pfFile=fopen(pFile->GetFullFileName().c_str(),"w");
    }
    if (!pfFile)
    {
      cerr << "Error opening file "<<Filename<<endl;
      return (mh_pid_t)-1;
    }
    string EchoStr=RemoveQuotes(Params.substr(0,Pos))+"\n";
    if (EchoStr.length()!=fwrite(EchoStr.c_str(),1,EchoStr.length(),pfFile))
    {
      cerr << "Error writing file "<<Filename<<endl;
      return (mh_pid_t)-1;
    }
    fclose(pfFile);
  }
  return (mh_pid_t)0;
}

/*****************************************************************************/
mh_pid_t mhmakefileparser::CopyFiles(const string &Params) const
{
  vector<fileinfo*> Files;

  SplitToItems(Params,Files);

  size_t NrSrcs=Files.size()-1;

  if (NrSrcs<1)
  {
    cerr << "Wrong number of arguments in copy: "<<Params<<endl;
    return (mh_pid_t)-1;
  }

  fileinfo* pDest=Files[NrSrcs];
  if (NrSrcs>1 && !pDest->IsDir())
  {
    cerr << "copy: Destination must be a directory when more then one source : "<<Params<<endl;
    return (mh_pid_t)-1;
  }

  for (size_t i=0; i<NrSrcs; i++)
  {
    fileinfo* pSrc=Files[i];

    string SrcFileName=pSrc->GetFullFileName();

    if (pSrc->IsDir())
    {
      SrcFileName+=OSPATHSEPSTR "*";
      pSrc=GetFileInfo(SrcFileName,m_MakeDir);
    }

    //cerr << "copy "<<pSrc->GetFullFileName()<<" "<<pDest->GetFullFileName()<<endl;
    /* Now check if there is a wildcard */
    if (SrcFileName.find('*')!=string::npos)
    {
      if (!CopyDir(pSrc->GetDir(), pDest, pSrc->GetName()))
      {
        cerr << "copy: Error copying directory: " << Params << endl;
        return (mh_pid_t)-1;
      }
    }
    else
    {
      if (!CopyFile(pSrc,pDest))
      {
        cerr << "copy: Error copying file: " << Params << endl;
        return (mh_pid_t)-1;
      }
    }
  }

  return (mh_pid_t)0;

}

/*****************************************************************************/
mh_pid_t mhmakefileparser::TouchFiles(const string &Params) const
{
  vector<fileinfo*> Files;

  SplitToItems(Params,Files);

  vector<fileinfo*>::const_iterator It=Files.begin();
  while (It!=Files.end())
  {
    fileinfo* pFile=*It++;
    const string &FileName=pFile->GetFullFileName();

    /* Since this can be part of a list of commands for a certain rule, and it is possible that the file
     * was generated by one on the previous commands, we first need the invalidate the date so that the
     * existance checking is done again */
    pFile->InvalidateDate();

    if (pFile->IsDir())
    {
      cerr << "touch: Cannot touch a directory: " << FileName << endl;
      return (mh_pid_t)-1;
    }
    if (pFile->Exists())
    {
      int fd;
      char c;
      int status = 0;
      struct stat st;
      int Ret;
      int saved_errno = 0;

      fd = open (FileName.c_str(), O_RDWR);
      if (fd<0)
      {
        st.st_size=0;
      }
      else
      {
        if (fstat (fd, &st) < 0)
        {
          cerr << "touch: Cannot stat file " << FileName << endl;
          return (mh_pid_t)-1;
        }
      }

      if (st.st_size == 0)
      {
        FILE *pFile;
        if (fd>=0 && close(fd) < 0)
        {
          cerr << "touch: Error closing file " << FileName << endl;
          return (mh_pid_t)-1;
        }
        /*Re-Create an empty file */
        pFile=fopen(FileName.c_str(),"wb");
        if (!pFile)
        {
          cerr << "touch: Cannot create file: " << FileName << endl;
          return (mh_pid_t)-1;
        }
        fclose(pFile);
      }
      else
      {
        Ret=read (fd, &c, sizeof(c));
        if (Ret!=sizeof(c) && Ret!=EOF)
        {
          cerr << "touch: Cannot read file " << FileName << ": "<<Ret<<endl;
          return (mh_pid_t)-1;
        }
        if (lseek (fd, (off_t) 0, SEEK_SET) < 0)
        {
          cerr << "touch: Error changing file pointer " << FileName << endl;
          return (mh_pid_t)-1;
        }
        if (write (fd, &c, sizeof c) != sizeof(c))
        {
          cerr << "touch: Error writing file " << FileName << endl;
          return (mh_pid_t)-1;
        }
        if (close (fd) < 0)
        {
          cerr << "touch: Error closing file " << FileName << endl;
          return (mh_pid_t)-1;
        }
      }
    }
    else
    {
      /* Create an empty file */
      FILE *pFile=fopen(FileName.c_str(),"wb");
      if (!pFile)
      {
        cerr << "touch: Cannot create file: " << FileName << endl;
        return (mh_pid_t)-1;
      }
      fclose(pFile);
    }
    pFile->InvalidateDate();
  }
  return (mh_pid_t)0;
}

/*****************************************************************************/
const string &mhmakefileparser::GetPythonExe() const
{
  static string PythonExe;
  if (PythonExe.empty())
  {
    string FullCommand=SearchCommand(PYTHONEXE);
    if (!FullCommand.empty())
    {
      PythonExe=QuoteFileName(FullCommand)+" ";
    }
    else
    {
      cerr<<"python executable not found in path and registry.\n";
      exit(1);
    }
  }
  return PythonExe;
}

/*****************************************************************************/
static const string &GetComspec()
{
  static string Comspec;
  if (Comspec.empty())
  {
    const char *pComspec=getenv(COMSPEC);
    if (pComspec)
    {
      Comspec=pComspec;
      #ifdef WIN32
      Comspec+=" /c ";
      #else
      Comspec+=" -c \"";
      #endif
    }
    else
    {
      #ifdef WIN32
      Comspec="cmd.exe /c ";
      #else
      Comspec="sh -c \"";
      #endif
    }
  }
  return Comspec;
}

/*****************************************************************************/
static string GetExecutableFromCommand(const string &FullCommand)
{
  string Ret;
  FILE *pFile=fopen(FullCommand.c_str(),"r");
  if (!pFile)
    return Ret;
  char Line[MAX_PATH];
  if (fgets(Line,MAX_PATH,pFile) && Line[0]=='#' && Line[1]=='!')
  {
    Ret=Line+2;
    Ret.resize(Ret.length()-1);
    Ret=QuoteFileName(Ret)+" ";
  }
  fclose(pFile);
  return Ret;
}

/*****************************************************************************/
string mhmakefileparser::GetFullCommand(const string &CommandIn)
{
  map<string,string>::iterator pFound=m_CommandCache.find(CommandIn);
  string Command=CommandIn;
  if (pFound==m_CommandCache.end())
  {
    bool Found=false;
    // Not found in the stack, search in the environment path
    // Check if an extension is specified
    const char *pBeg=Command.c_str();
    const char *pEnd=pBeg+Command.length()-1;
    bool HasExt=false;
    while (pEnd>pBeg && *pEnd!='\\' && *pEnd!='/')
    {
      if (*pEnd=='.')
      {
        HasExt=true;
        break;
      }
      pEnd--;
    }
    if (HasExt)
    {
      string FullCommand=SearchCommand(Command);
      if (!FullCommand.empty())
      {
        Found=true;
        Command=QuoteFileName(FullCommand);
      }
    }
    else
    {
      /* First check for special internal commands */
      if (CommandIn=="del")
      {
        m_CommandCache[CommandIn]="del";
        return Command;
      }
      // Try with different extensions
      string FullCommand=SearchCommand(Command,EXEEXT);
      if (!FullCommand.empty())
      {
        Found=true;
        Command=QuoteFileName(FullCommand);
      }
      else
      {
        FullCommand=SearchCommand(Command,".py");
        if (!FullCommand.empty())
        {
          Found=true;
          Command=GetPythonExe()+QuoteFileName(FullCommand);
        }
        else
        {
          // also search without extension, if found look inside the file if we find an executable to use
          FullCommand=SearchCommand(Command);
          if (!FullCommand.empty())
          {
            Found=true;
            Command=GetExecutableFromCommand(FullCommand)+QuoteFileName(FullCommand);
          }
        }
      }
    }
    if (!Found)
    {
      Command=GetComspec()+QuoteFileName(Command);
    }
    m_CommandCache[CommandIn]=Command;
    return Command;
  }
  return pFound->second;
}

static void CommandSep(const string &Command, unsigned &EndPos, unsigned &NextBegin)
{
  while (1)
  {
    EndPos=Command.find('&',NextBegin);
    if (EndPos==(unsigned)string::npos || !EndPos)
    {
      // When there is only one command return that command, so we run %comspec% commands
      // always via a temporary batch file. This is to avoid problems when quotes and pipe
      // characters in parameters when using cmd /c
      return;
    }
    EndPos--;
    char C=Command[EndPos];
    NextBegin=EndPos+2;
    if (strchr("|<>",C))
    {
      continue;
    }
    if (NextBegin<Command.length())
    {
      C=Command[NextBegin];
      if (strchr("|<>",C))
      {
        continue;
      }
      while (strchr(" \t|<>",Command[NextBegin])) NextBegin++;
    }
    while (strchr(" \t|<>",Command[EndPos])) EndPos--;
    break;
  };

}

mh_pid_t mhmakefileparser::OsExeCommand(const string &Command, const string &Params, bool IgnoreError, string *pOutput) const
{
  string FullCommandLine;
  string ComSpec=GetComspec();

  if (Command.substr(0,ComSpec.size())==ComSpec)
  {
    static char Filename[MAX_PATH];
    static char *pFilenameOffset;

    if (pFilenameOffset==NULL)
    {
      #ifdef WIN32
      const char *pDir=getenv("TEMP");
      if (!pDir)
        pDir=getenv("TMP");
      if (!pDir)
        pDir=m_MakeDir->GetFullFileName().c_str();
      sprintf(Filename, "%s\\tmp%d_", pDir, GetCurrentProcessId());
      #else
      sprintf(Filename, "/tmp/tmp%d_", getpid());
      #endif
      pFilenameOffset=Filename+strlen(Filename);
    }

    string tmpCommand=Command.substr(ComSpec.size(),Command.size());
    FullCommandLine=ComSpec;

    string ComspecCommandLine=tmpCommand+Params;
    unsigned NextBegin=0;
    unsigned EndPos=0;

    CommandSep(ComspecCommandLine,EndPos,NextBegin);

      // We have multiple commands so create an temporary batch file
    FILE *pFile;
    while (1)
    {
      #ifdef WIN32
      sprintf(pFilenameOffset,"%d.bat",rand());
      #else
      sprintf(pFilenameOffset,"%d.sh",rand());
      #endif
      pFile=fopen(Filename,"r");
      if (!pFile)
        break;
      fclose(pFile);
    }
    pFile=fopen(Filename,"w");
    #ifdef WIN32
    fprintf(pFile,"@echo off\n");
    #endif
    unsigned PrevPos=0;
    while (EndPos!=(unsigned)string::npos)
    {
      string SubCommand=ComspecCommandLine.substr(PrevPos,EndPos-PrevPos+1);
      fprintf(pFile,"%s\n",SubCommand.c_str());
      PrevPos=NextBegin;
      CommandSep(ComspecCommandLine,EndPos,NextBegin);
    }
    string SubCommand=ComspecCommandLine.substr(PrevPos);
    fprintf(pFile,"%s\n",SubCommand.c_str());
    fclose(pFile);
    #ifndef WIN32
    // make the file executable
    chmod(Filename, S_IRWXU);
    #endif
    FullCommandLine+=QuoteFileName(Filename);
    ((mhmakefileparser*)this)->m_FilesToRemoveAtEnd.push_back(string(Filename));
  }
  else
  {
    FullCommandLine=Command+Params;
  }
#ifdef WIN32
  STARTUPINFO StartupInfo;
  memset(&StartupInfo,0,sizeof(StartupInfo));
  StartupInfo.cb=sizeof(STARTUPINFO);
  PROCESS_INFORMATION ProcessInfo;

  char *pFullCommand=new char[FullCommandLine.length()+1];
  strcpy(pFullCommand,FullCommandLine.c_str());

  if (pOutput || g_Quiet)
  {
    mh_pid_t hChildStdinRd;
    mh_pid_t hChildStdinWr;
    mh_pid_t hChildStdoutRd;
    mh_pid_t hChildStdoutWr;
    mh_pid_t hChildStdinWrDup;
    mh_pid_t hChildStdoutRdDup;
    SECURITY_ATTRIBUTES saAttr;
    BOOL fSuccess;

    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = NULL;

    if (!CreatePipe(&hChildStdinRd, &hChildStdinWr, &saAttr, 0))
      return (mh_pid_t)-1;

        /* Create new output read handle and the input write handle. Set
    * the inheritance properties to FALSE. Otherwise, the child inherits
    * the these handles; resulting in non-closeable handles to the pipes
        * being created. */
    fSuccess = DuplicateHandle(GetCurrentProcess(), hChildStdinWr,
                               GetCurrentProcess(), &hChildStdinWrDup, 0,
                               FALSE, DUPLICATE_SAME_ACCESS);
    if (!fSuccess) return (mh_pid_t)-1;
    /* Close the inheritable version of ChildStdin that we're using. */
    CloseHandle(hChildStdinWr);

    if (!CreatePipe(&hChildStdoutRd, &hChildStdoutWr, &saAttr, 0))
      return (mh_pid_t)-1;

    fSuccess = DuplicateHandle(GetCurrentProcess(), hChildStdoutRd,
                               GetCurrentProcess(), &hChildStdoutRdDup, 0,
                               FALSE, DUPLICATE_SAME_ACCESS);
    if (!fSuccess) return (mh_pid_t)-1;
    CloseHandle(hChildStdoutRd);

    int hStdIn = _open_osfhandle((intptr_t)hChildStdinWrDup, _O_WRONLY|_O_TEXT);
    FILE *pStdIn = _fdopen(hStdIn, "w");
    int hStdOut = _open_osfhandle((intptr_t)hChildStdoutRdDup, _O_RDONLY|_O_TEXT);
    FILE *pStdOut = _fdopen(hStdOut, "r");

    StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    StartupInfo.hStdInput = hChildStdinRd;
    StartupInfo.hStdOutput = hChildStdoutWr;
    StartupInfo.hStdError = hChildStdoutWr;

    if (!CreateProcess(NULL,pFullCommand,NULL,NULL,TRUE,CREATE_NO_WINDOW,m_pEnv,m_MakeDir->GetFullFileName().c_str(),&StartupInfo,&ProcessInfo))
    {
      delete[] pFullCommand;
      string ErrorMessage=string("Error starting command: ") + FullCommandLine + " : " + stringify(GetLastError());
      if (IgnoreError)
        cerr << ErrorMessage << endl;
      else
        throw ErrorMessage;
    }
    delete[] pFullCommand;
    if (!CloseHandle(hChildStdinRd)) return (mh_pid_t)-1;
    if (!CloseHandle(hChildStdoutWr)) return (mh_pid_t)-1;

    CloseHandle(ProcessInfo.hThread);
    char Buf[256];
    size_t Nbr;
    while ( (Nbr=fread(Buf,1,sizeof(Buf)-1,pStdOut)) > 0)
    {
      if (pOutput)
      {
        Buf[Nbr]=0;
        *pOutput+=Buf;
      }
    }
    WaitForSingleObject(ProcessInfo.hProcess,INFINITE);
    fclose(pStdIn);
    fclose(pStdOut);

    DWORD ExitCode=0;
    if (!GetExitCodeProcess(ProcessInfo.hProcess,&ExitCode) || ExitCode)
    {
      if (IgnoreError)
      {
        cerr << "Error running command: "<<Command<<", but ignoring error\n";
        return (mh_pid_t)0; // Ignore error
      }
      else
        return (mh_pid_t)-1;
    }
    CloseHandle(ProcessInfo.hProcess);
    return (mh_pid_t)0;
  }
  else
  {
    if (!CreateProcess(NULL,pFullCommand,NULL,NULL,TRUE,0,m_pEnv,m_MakeDir->GetFullFileName().c_str(),&StartupInfo,&ProcessInfo))
    {
      delete[] pFullCommand;
      string ErrorMessage=string("Error starting command: ") + Command + " : " + stringify(GetLastError());
      if (IgnoreError)
        cerr << ErrorMessage << endl;
      else
        throw ErrorMessage;
    }
    delete[] pFullCommand;
    CloseHandle(ProcessInfo.hThread);
    return ProcessInfo.hProcess;
  }
#else
  if (pOutput || g_Quiet)
  {
    int pipeto[2];      /* pipe to feed the exec'ed program input */
    int pipefrom[2];    /* pipe to get the exec'ed program output */

    if ( (-1==pipe(pipeto)) || (-1==pipe(pipefrom)) )
    {
      if (IgnoreError)
      {
        cerr << "Error creating pipe for : "<<Command<<", but ignoring error\n";
        return (mh_pid_t)0; // Ignore error
      }
      else
        return (mh_pid_t)-1;
    }

    pid_t ID=vfork();
    if (ID==-1)
    {
      if (IgnoreError)
      {
        cerr << "Error forking when try to run command: "<<Command<<", but ignoring error\n";
        return (mh_pid_t)0; // Ignore error
      }
      else
        return (mh_pid_t)-1;
    }
    else if (ID==0)
    {
      int argc;
      const char **pargv;

      dup2( pipeto[0], STDIN_FILENO );
      dup2( pipefrom[1], STDOUT_FILENO  );
      /* close unnecessary pipe descriptors for a clean environment */
      close( pipeto[0] );
      close( pipeto[1] );
      close( pipefrom[0] );
      close( pipefrom[1] );

      poptParseArgvString(FullCommandLine.c_str(), &argc, &pargv);
      if (-1==chdir(m_MakeDir->GetFullFileName().c_str()))
        throw string("Error changing current directory to ")+m_MakeDir->GetFullFileName();
      if (m_pEnv)
        execve(pargv[0],(char *const*)pargv,m_pEnv);
      else
        execv(pargv[0],(char *const*)pargv);
      free(pargv);
      _exit (EXIT_FAILURE);
    }
    else
    {
        /* Close unused pipe ends. This is especially important for the
      * pipefrom[1] write descriptor, otherwise readFromPipe will never
        * get an EOF. */
      close( pipeto[0] );
      close( pipefrom[1] );

      pid_t ID2=vfork();
      if (ID2==-1)
      {
        if (IgnoreError)
        {
          cerr << "Error forking when try to run command: "<<Command<<", but ignoring error\n";
          return (mh_pid_t)0; // Ignore error
        }
        else
          return (mh_pid_t)-1;
      }
      else if (ID2==0)
      {
        /* Close pipe write descriptor, or we will never know when the
        * writer process closes its end of the pipe and stops feeding the
        * exec'ed program. */
        close( pipeto[1] );
        char Buf[256];
        int Nbr;
        while ( (Nbr=read(pipefrom[0],Buf,sizeof(Buf)-1)) > 0)
        {
          if (pOutput)
          {
            Buf[Nbr]=0;
            *pOutput+=Buf;
          }
        }
        close( pipefrom[0]);
        _exit (EXIT_FAILURE);
      }
      else
      {
        /* close unused pipe end */
        close( pipefrom[0] );
        close( pipeto[1] );

        int Status;
        waitpid(ID2,&Status,0); // Wait until the reading of the output is finished
      }

      int Status;
      int Ret=waitpid(ID,&Status,0);
      if (Ret!=ID || Status)
      {
        if (IgnoreError)
        {
          cerr << "Error running command: "<<Command<<", but ignoring error\n";
          return (mh_pid_t)0; // Ignore error
        }
        else
          return (mh_pid_t)-1;
      }
    }
  }
  else
  {   // No pOutput
    pid_t ID=fork();
    if (ID==-1)
    {
      if (IgnoreError)
      {
        cerr << "Error forking when try to run command: "<<Command<<", but ignoring error\n";
        return (mh_pid_t)0; // Ignore error
      }
      else
        return (mh_pid_t)-1;
    }
    else if (ID==0)
    {
      int argc;
      const char **pargv;

      poptParseArgvString(FullCommandLine.c_str(), &argc, &pargv);
      if (-1==chdir(m_MakeDir->GetFullFileName().c_str()))
        throw string("Error changing current directory to ")+m_MakeDir->GetFullFileName();
      if (m_pEnv)
        execve(pargv[0],(char *const*)pargv,m_pEnv);
      else
        execv(pargv[0],(char *const*)pargv);
      free(pargv);
      _exit (EXIT_FAILURE);
    }
    else
    {
      return (mh_pid_t)ID;
    }
  }

  return (mh_pid_t)0;
#endif
}

///////////////////////////////////////////////////////////////////////////////
static bool NeedsShell(const string &Params)
{
  unsigned i;
  int Detect=1;
  for (i=0; i<Params.size(); i++)
  {
    char Char=Params[i];
    if (Char=='"')
    {
      Detect=1-Detect;
    }
    else if (Detect)
    {
      if (strchr("<>|&\n[*",Char))
      {
        break;
      }
    }
  }
  return i!=Params.size();
}

///////////////////////////////////////////////////////////////////////////////
mh_pid_t mhmakefileparser::ExecuteCommand(string Command, bool &IgnoreError, string *pOutput)
{
  bool Echo=true;
  IgnoreError=false;
  while (Command.length())
  {
    if (Command[0]=='@')
    {
      Echo=false;
      Command=Command.substr(1);
      continue;
    }
    if (Command[0]=='-')
    {
      IgnoreError=true;
      Command=Command.substr(1);
      continue;
    }
    break;
  }
  if (!Command.length())
    return (mh_pid_t)0;
  string InCommand=Command;
  if (g_Quiet)
    Echo=false;

  const char *pCom=Command.c_str();
  int StartCommandPos;
  int EndCommandPos;
  int BeginParamPos;
  if (*pCom=='"')
  {
    StartCommandPos=1;
    EndCommandPos=1;
    while (pCom[EndCommandPos]!='"') EndCommandPos++;
  }
  else
  {
    StartCommandPos=0;
    EndCommandPos=0;
  }
  while (!strchr(" \t",pCom[EndCommandPos])) EndCommandPos++;
  BeginParamPos=EndCommandPos;
  string Params=Command.substr(BeginParamPos);
  Command=Command.substr(StartCommandPos,EndCommandPos-StartCommandPos);

  // If we have special characters in the params we always call the command via the shell
  if (!NeedsShell(Params))
  {
    if (Command!="del" && Command!="touch" && Command!="copy" && Command!="echo" && Command!="mkdir")
      Command=GetFullCommand(Command);
  }
  else
  {
    if (Command!="echo" || strchr(Params.c_str(),'|'))  // the EchoCommand(Params) does not implement piping, only redirecting (>)
    {
      string ComSpec=GetComspec();
      Command=ComSpec+Command;
    }
  }
  if (Echo
      #ifdef _DEBUG
      || g_DoNotExecute
      #endif
      )
  {
    #ifdef _DEBUG
    if (g_PrintAdditionalInfo)
      cout << Command << Params << endl;
    else
      cout << InCommand << endl;
    #endif
  }

  /* first we check special internal commands */
  #ifdef _DEBUG
  if (pOutput || !g_DoNotExecute)
  {
  #endif
    if (Command=="del")
    {
      return DeleteFiles(Params);
    }
    else if (Command=="touch")
    {
      return TouchFiles(Params);
    }
    else if (Command=="copy")
    {
      return CopyFiles(Params);
    }
    else if (Command=="echo")
    {
      return EchoCommand(Params);
    }
    else if (Command=="mkdir")
    {
      return MakeDirsCommand(Params);
    }

    return OsExeCommand(Command,Params,IgnoreError,pOutput);
  #ifdef _DEBUG
  }
  #endif
  return (mh_pid_t)0; /* No Error */
}

///////////////////////////////////////////////////////////////////////////////
void mhmakefileparser::BuildDependencies(const refptr<rule> &pRule, fileinfo* pTarget, mh_time_t TargetDate, mh_time_t &YoungestDate, bool &MakeTarget)
{

  vector<fileinfo*> &Deps=pTarget->GetDeps();
  vector<fileinfo*>::iterator pDepIt=Deps.begin();
  while (pDepIt!=Deps.end())
  {
    StartBuildTarget(*pDepIt);
    pDepIt++;
  }
  pDepIt=Deps.begin();
  while (pDepIt!=Deps.end())
  {
    mh_time_t DepDate=WaitBuildTarget(*pDepIt);
    #ifdef _DEBUG
    if (!DepDate.DoesExist()&&!(*pDepIt)->IsPhony())
    {
      if (!(*pDepIt)->GetDir()->GetDate().DoesExist())
        cout << "Building of "<<(*pDepIt)->GetFullFileName()<<" failed because directory does not exist (rule defined to create it?)\n";
      else
        cout << "Does not known how to create "<<(*pDepIt)->GetFullFileName()<<".\n";
    }
    #endif
    if (DepDate.IsNewer(YoungestDate))
      YoungestDate=DepDate;
    if (DepDate.IsNewer(TargetDate))
    {
      #ifdef _DEBUG
      if (pRule&&g_pPrintDependencyCheck && DepDate.IsExistingFile() && TargetDate.IsExistingFile())
        cout<<"Going to build "<<pTarget->GetQuotedFullFileName()<<" because "<<(*pDepIt)->GetQuotedFullFileName()<<" is more recent\n";
      #endif
      MakeTarget=true;
    }
    pDepIt++;
  }
}

///////////////////////////////////////////////////////////////////////////////
mh_time_t mhmakefileparser::StartBuildTarget(fileinfo* pTarget,bool bCheckTargetDir)
{
  #ifdef _DEBUG
  if (g_CheckCircularDeps)
  {
    deque<fileinfo*>::const_iterator pFind=find(m_TargetStack.begin(),m_TargetStack.end(),pTarget);
    if (pFind!=m_TargetStack.end())
    {
      cout << "Circular dependency detected.\n"<<pTarget->GetQuotedFullFileName()<<" depending on itself.\n";
      cout << "Dependency stack:\n";
      deque<fileinfo*>::const_iterator pIt=m_TargetStack.begin();
      while (pIt!=m_TargetStack.end())
      {
        cout << "  " << (*pIt)->GetQuotedFullFileName() << endl;
        pIt++;
      }
    }
    if (!pTarget->IsBuildStarted()) m_TargetStack.push_back(pTarget);
  }
  #endif

  #ifdef _DEBUG
  static int Indent;
  #endif

  if (g_StopCompiling)
  {
    throw string("Compilation Interrupted by user.");
  }

  if (pTarget->IsBuild())
  {
    #ifdef _DEBUG
    if (g_pPrintDependencyCheck)
    {
      for (int i=0; i<Indent; i++)
        cout<<"  ";
      cout<<"  Already build "<<pTarget->GetQuotedFullFileName()<<" : "<<pTarget->GetDate()<<endl;
    }
    #endif
    return pTarget->GetDate();
  }
  if (pTarget->IsBuilding())
    return mh_time_t();  // pTarget is still building, so we have to wait

  #ifdef _DEBUG
  if (g_GenProjectTree)
    cout << pTarget->GetQuotedFullFileName() << endl;

  Indent++;
  if (g_pPrintDependencyCheck)
  {
    for (int i=0; i<Indent; i++)
      cout<<"  ";
    cout<<"Building dependencies of "<<pTarget->GetQuotedFullFileName()<<endl;
  }
  #endif

  pTarget->SetBuild();

  /* Optimisation: do not build target when target dir does not exist,
     but first build the target dir, in case there exists a rule for it*/
  refptr<rule> pRule=pTarget->GetRule();

  if (!pRule && bCheckTargetDir)
  {
    fileinfo *pTargetDir=pTarget->GetDir();
    mh_time_t TargetDirDate=BuildTarget(pTargetDir,false);

    if (!pTargetDir->Exists())
    {
      #ifdef _DEBUG
      Indent--;
      if (g_CheckCircularDeps)
      {
        m_TargetStack.pop_back();
      }
      #endif
      return TargetDirDate;
    }
  }

  mh_time_t TargetDate=pTarget->GetDate();
  mh_time_t YoungestDate=TargetDate;
  bool MakeTarget=!TargetDate.DoesExist(); // When the target does not exist, always build when a rule is found. This also makes sure the autodepscan will be done in case the target does not exist yet

  if (!pRule || !pRule->GetCommands().size())
  {
    implicitruledep_t Result;

    IMPLICITRULE::SearchImplicitRule(pTarget,Result);

    implicitruledep_t::iterator ResultIt=Result.begin();
    while (ResultIt!=Result.end())
    {
      if (ResultIt->first.empty())
      {
        pRule=ResultIt->second;
        pTarget->SetRule(pRule);
        #ifdef _DEBUG
        if (g_PrintAdditionalInfo)
        {
          cout<<"Found implicit rule for "<<pTarget->GetQuotedFullFileName()<<endl;
          pRule->PrintCommands(pTarget);
        }
        #endif
        break;
      }
      else if (!IMPLICITRULE::PushRule(ResultIt->second))
      {
        /* We have to check all the implicitrule dependencies, only when they all can be build,
           the implicit rule matches */
        #ifdef _DEBUG
        fileinfo *pYoungestTarget;
        m_ImplicitSearch++;
        #endif
        mh_time_t ThisYoungestDate=YoungestDate;
        vector<fileinfo*>::iterator ImplicitRuleDepsIt=ResultIt->first.begin();
        while (ImplicitRuleDepsIt!=ResultIt->first.end())
        {
          fileinfo *pNewTarget=*ImplicitRuleDepsIt;
          mh_time_t DepDate=BuildTarget(pNewTarget);
          if (!DepDate.DoesExist())
          {
            pNewTarget=ResultIt->second->GetMakefile()->SearchvPath(pNewTarget);
            if (pNewTarget!=NULL)
            {
              *ImplicitRuleDepsIt=pNewTarget;  /* The file was elsewere so we have to update the dependencies */
              DepDate=pNewTarget->GetDate();
            }
          }
          if (!DepDate.DoesExist())
            break;
          if (DepDate.IsNewer(ThisYoungestDate))
          {
            ThisYoungestDate=DepDate;
            #ifdef _DEBUG
            pYoungestTarget=pNewTarget;
            #endif
          }
          ImplicitRuleDepsIt++;
        }

        IMPLICITRULE::PopRule(ResultIt->second);
        #ifdef _DEBUG
        m_ImplicitSearch--;
        #endif
        if (ImplicitRuleDepsIt==ResultIt->first.end()) // All deps exists
        {
          YoungestDate=ThisYoungestDate;
          pRule=ResultIt->second;
          pTarget->InsertDeps(ResultIt->first);
          pTarget->SetRule(pRule);  /* This is an implicit rule so do not add the target */
          #ifdef _DEBUG
          if (g_PrintAdditionalInfo)
          {
            cout<<"Found implicit rule for "<<pTarget->GetQuotedFullFileName()<<". Dependents:\n";
            vector<fileinfo*>::iterator ImplicitRuleDepsIt=ResultIt->first.begin();
            while (ImplicitRuleDepsIt!=ResultIt->first.end())
            {
              cout<<"  "<<(*ImplicitRuleDepsIt)->GetQuotedFullFileName()<<endl;
              ImplicitRuleDepsIt++;
            }
            pRule->PrintCommands(pTarget);
          }
          #endif
          if (ThisYoungestDate.IsNewer(TargetDate))
          {
            #ifdef _DEBUG
            if (pRule,g_pPrintDependencyCheck && ThisYoungestDate.IsExistingFile() && TargetDate.IsExistingFile())
              cout<<"Going to build "<<pTarget->GetQuotedFullFileName()<<" because "<<pYoungestTarget->GetQuotedFullFileName()<<" is more recent\n";
            #endif
            MakeTarget=true;
          }
          break;
        }
      }
      #ifdef _DEBUG
      else if (g_PrintAdditionalInfo)
      {
        cout << "Stopping implicit search of "<<pTarget->GetFullFileName()<<" because of implicit rule recursion detected.\n";
      }
      #endif
      ResultIt++;
    }
    if (pRule)
    {
      #ifdef _DEBUG
      pTarget->SetBuilding();
      pTarget->SetRule(pRule);
      pTarget->ClearBuilding();
      #else
      pTarget->SetRule(pRule);
      #endif
    }
  }

  mhmakefileparser *pMakefile=NULL;
  if (pRule)
  {
    pMakefile=pRule->GetMakefile();
    if (pMakefile->ForceAutoDepRescan()||MakeTarget==true)
      pMakefile->UpdateAutomaticDependencies(pTarget);
  }
  else if (pTarget->GetAutoDepsMakefile())
    pTarget->GetAutoDepsMakefile()->UpdateNoRuleAutomaticDependencies(pTarget);

  BuildDependencies(pRule,pTarget,TargetDate,YoungestDate,MakeTarget);

  if (pRule)
  {
    #ifdef _DEBUG
    if (g_pPrintDependencyCheck)
    {
      for (int i=0; i<Indent; i++)
        cout<<"  ";
      cout<<"Building "<<pTarget->GetQuotedFullFileName()<<endl;
    }
    #endif
    if (!MakeTarget)
    {
      if (!TargetDate.DoesExist() || ( (g_RebuildAll || pMakefile->m_RebuildAll) && TargetDate.IsOlder(m_sBuildTime)))
      {
        #ifdef _DEBUG
        if (g_pPrintDependencyCheck)
        {
          if (!TargetDate.DoesExist())
          {
            if (!m_ImplicitSearch && !pTarget->IsPhony())
              cout<<"Building "<<pTarget->GetQuotedFullFileName()<<" because it does not exist yet\n";
          }
          else if (TargetDate.IsOlder(m_sBuildTime))
          {
            cout<<"Building "<<pTarget->GetQuotedFullFileName()<<" because need to rebuild all (-a)\n";
          }
        }
        #endif
        MakeTarget=true;
      }
    }

    // Now execute the commands
    vector<string> &Commands=pRule->GetCommands();

    if (!MakeTarget)
    {
      vector<string>::iterator CommandIt=Commands.begin();

      md5_context ctx;
      md5_starts( &ctx );
      while (CommandIt!=Commands.end())
      {
        pMakefile->SetRuleThatIsBuild(pTarget); // Make sure that the command expension is correct
        string Command=pMakefile->ExpandExpression(*CommandIt);
        pMakefile->ClearRuleThatIsBuild();  /* Make sure that further expansion is not taking this rule into account.*/
        md5_update( &ctx, (uint8 *)Command.c_str(), (unsigned long)Command.size());
        CommandIt++;
      }

      uint32 Md5_32=md5_finish32( &ctx);
      if (!pTarget->CompareMd5_32(Md5_32))
      {
        if (TargetDate.IsNewerOrSame(m_sBuildTime) || TargetDate.IsDir())
        {
          // Only rebuild if it is not yet rebuild in this current run. This may happen for implicit rules that have multiple targets (implicit rules that build more then one target at the same time
          pTarget->SetCommandsMd5_32(Md5_32);
          pMakefile->AddTarget(pTarget);
          pMakefile->SetAutoDepsDirty();  /* We need to update the autodeps file if the md5 has been changed */
        }
        else
        {
          #ifdef _DEBUG
          if (!g_GenProjectTree)
            cout << "Md5 is different for " << pTarget->GetQuotedFullFileName() << " Old:"<<hex<<pTarget->GetCommandsMd5_32()<<", New: "<<Md5_32<<". Commandline must have been changed so recompiling\n";
          #endif

          #ifdef _DEBUG
          Indent--;
          if (g_CheckCircularDeps)
          {
            m_TargetStack.pop_back();
          }
          #endif

          MakeTarget=true;
        }
      }
    }
    if (MakeTarget)
    {
      // Queue for execution
      if (sm_CommandQueue.QueueTarget(pTarget))
        return mh_time_t();
      mh_time_t NewDate=pTarget->GetDate();
      if (NewDate.IsNewer(YoungestDate))
        YoungestDate=NewDate;
    }
  }

  #ifdef _DEBUG
  if (g_pPrintDependencyCheck)
  {
    for (int i=0; i<Indent; i++)
      cout<<"  ";
    cout<<"Building "<<pTarget->GetQuotedFullFileName()<<" finished : "<< YoungestDate << endl;
  }
  Indent--;
  if (g_CheckCircularDeps)
  {
    m_TargetStack.pop_back();
  }

  if (!m_ImplicitSearch && !pTarget->Exists() && !pTarget->IsPhony() && !g_DoNotExecute && !g_GenProjectTree)
  {
    // This is only a warning for phony messages
    cout<<"Warning: don't know how to make "<<pTarget->GetQuotedFullFileName()<<"\nMake the rule a phony rule to avoid this warning (but only do it when it is really phony).\n";;
  }
  #endif
  pTarget->SetDate(YoungestDate); /* This is especially needed for phony targets in between real targets */
  return YoungestDate;
}

///////////////////////////////////////////////////////////////////////////////
mh_time_t mhmakefileparser::WaitBuildTarget(fileinfo* pTarget)
{
  return sm_CommandQueue.WaitForTarget(pTarget);
}

///////////////////////////////////////////////////////////////////////////////
void mhmakefileparser::BuildIncludedMakefiles()
{
  vector<fileinfo*>::iterator MakefileIt=m_IncludedMakefiles.begin();
  while (MakefileIt!=m_IncludedMakefiles.end())
  {
    #ifdef _DEBUG
    if (g_PrintAdditionalInfo)
      cout<<"Building include file "<<(*MakefileIt)->GetQuotedFullFileName()<<endl;
    #endif
    BuildTarget(*MakefileIt);
    MakefileIt++;
  }
}
