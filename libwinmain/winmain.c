#include <windows.h>

#include <stdio.h>
#include <fcntl.h>
#include <io.h>

#define MAX_CONSOLE_LINES 500

int main(int argc, char *argv[], char *penv[]);

/* Ignore control c and control break signals */
static BOOL WINAPI IgnoreControlC(DWORD dwCtrlType)
{
  if (dwCtrlType==CTRL_C_EVENT || dwCtrlType==CTRL_BREAK_EVENT)
    return TRUE;
  return FALSE;
}

static void CreateConsole(void)
{
  static int ConsoleCreated=0;
  if (!ConsoleCreated)
  {
    int hConHandle;
    HANDLE lStdHandle;
    CONSOLE_SCREEN_BUFFER_INFO coninfo;

    FILE *fp;
    ConsoleCreated=1;
    if (!AttachConsole(ATTACH_PARENT_PROCESS))
      AllocConsole();
    else
      SetConsoleCtrlHandler(IgnoreControlC,TRUE);     // Only allow control C interrupt the program when a console was created


      // set the screen buffer to be big enough to let us scroll text
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &coninfo);
    if (coninfo.dwSize.Y < MAX_CONSOLE_LINES)
    {
      coninfo.dwSize.Y = MAX_CONSOLE_LINES;
      SetConsoleScreenBufferSize(GetStdHandle(STD_OUTPUT_HANDLE), coninfo.dwSize);
    }

    // redirect unbuffered STDOUT to the console
    lStdHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    hConHandle = _open_osfhandle((intptr_t)lStdHandle, _O_TEXT);
    if (hConHandle==-1) // error occured, so return
      return;
    fp = _fdopen( hConHandle, "w" );
    *stdout = *fp;
    setvbuf( stdout, NULL, _IONBF, 0 );

    // redirect unbuffered STDIN to the console
    lStdHandle = GetStdHandle(STD_INPUT_HANDLE);
    hConHandle = _open_osfhandle((intptr_t)lStdHandle, _O_TEXT);
    fp = _fdopen( hConHandle, "r" );
    *stdin = *fp;
    setvbuf( stdin, NULL, _IONBF, 0 );

    // redirect unbuffered STDERR to the console
    lStdHandle = GetStdHandle(STD_ERROR_HANDLE);
    hConHandle = _open_osfhandle((intptr_t)lStdHandle, _O_TEXT);
    fp = _fdopen( hConHandle, "w" );
    *stderr = *fp;
    setvbuf( stderr, NULL, _IONBF, 0 );
  }
}

int __stdcall WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
  int argc=1;
  char ProgramName[255];
  #define MAXNRARGS 100
  char *argv[MAXNRARGS];
  char *pTmp;
  char *pProgramName;
  char **penv;
  int Ret;
  LPTCH pEnvStrings;

  hInstance=hInstance;
  nCmdShow=nCmdShow;
  hPrevInstance=hPrevInstance;

  if (!strncmp(lpCmdLine,"-console",8))
  {
    CreateConsole();
    lpCmdLine+=9;
  }
  else
  {
    SetConsoleCtrlHandler(IgnoreControlC,TRUE);
  }

  GetModuleFileName(NULL,ProgramName,255);
  pTmp=strrchr(ProgramName,'\\');
  if (pTmp)
    pProgramName=pTmp+1;
  else
    pProgramName=ProgramName;
  pTmp=strrchr(pProgramName,'.');
  if (pTmp)
    *pTmp=0;
  argv[0]=pProgramName;

  pTmp=lpCmdLine;
  while (*pTmp && argc<MAXNRARGS-1)
  {
    char *pEnd;
    if (*pTmp=='"')
    {
      pTmp++;
      pEnd=strchr(pTmp,'"');
    }
    else if (*pTmp!=' ')
    {
      pEnd=strchr(pTmp,' ');
    }
    else
    {
      pTmp++;
      continue;
    }
    if (pEnd)
    {
      *pEnd=0;
      argv[argc++]=pTmp;
      pTmp=pEnd+1;
    }
    else
    {
      argv[argc++]=pTmp;
      break;
    }
  }
  
  {
    LPTCH pTmp=GetEnvironmentStrings();
    int NrEnv=0;
    pEnvStrings=pTmp;
    penv=malloc(sizeof(*penv));
    while (*pTmp)
    {
      penv=realloc(penv,(NrEnv+2)*sizeof(*penv));
      penv[NrEnv++]=pTmp;
      putenv(pTmp);
      while (*pTmp) pTmp++;
      pTmp++;
    }
    penv[NrEnv]=NULL;
  }

  Ret = main(argc,argv,penv);
  free(penv);
  FreeEnvironmentStrings(pEnvStrings);
  return Ret;
}
