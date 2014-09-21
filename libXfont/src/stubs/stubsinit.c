#include "stubs.h"

#if defined(NO_WEAK_SYMBOLS) && defined(PIC)

#ifdef WIN32
#include <X11/Xwindows.h>
#define DLOPEN_SELF() GetModuleHandle(NULL)
#define DLSYM(h,f) GetProcAddress(h,f)
#else
#include <dlfcn.h>
#define DLOPEN_SELF() dlopen(NULL, RTLD_LOCAL)
#define DLSYM(h,f) dlsym(h, f)
#endif

int (*__client_auth_generation)(ClientPtr) = NULL;
Bool (*__ClientSignal)(ClientPtr) = NULL;
void (*__DeleteFontClientID)(Font) = NULL;
void (*__VErrorF)(const char *, va_list) = NULL;
FontPtr (*__find_old_font)(FSID) = NULL;
FontResolutionPtr (*__GetClientResolutions)(int *) = NULL;
int (*__GetDefaultPointSize)(void) = NULL;
Font (*__GetNewFontClientID)(void) = NULL;
unsigned long (*__GetTimeInMillis)(void) = NULL;
int (*__init_fs_handlers)(FontPathElementPtr, BlockHandlerProcPtr) = NULL;
int (*__RegisterFPEFunctions)(NameCheckFunc, InitFpeFunc, FreeFpeFunc,
     ResetFpeFunc, OpenFontFunc, CloseFontFunc, ListFontsFunc,
     StartLfwiFunc, NextLfwiFunc, WakeupFpeFunc, ClientDiedFunc,
     LoadGlyphsFunc, StartLaFunc, NextLaFunc, SetPathFunc) = NULL;
void (*__remove_fs_handlers)(FontPathElementPtr, BlockHandlerProcPtr, Bool) = NULL;
void **__ptr_serverClient = NULL;
int (*__set_font_authorizations)(char **, int *, ClientPtr) = NULL;
int (*__StoreFontClientFont)(FontPtr, Font) = NULL;
Atom (*__MakeAtom)(const char *, unsigned, int) = NULL;
int (*__ValidAtom)(Atom) = NULL;
char *(*__NameForAtom)(Atom) = NULL;
unsigned long *__ptr_serverGeneration = NULL;
void (*__register_fpe_functions)(void) = NULL;

#define INIT_SYMBOL(sym) \
  if (!__##sym) \
   __##sym = (typeof(__##sym)) DLSYM(handle, #sym)
#define INIT_DATA(sym) \
  if (!__ptr_##sym) \
   __ptr_##sym = (typeof(__ptr_##sym)) DLSYM(handle, #sym)

int
_font_init_stubs (void)
{
  static int inited = FALSE;
  static void *handle = NULL;

  if (inited)
    return inited;
  if (!handle)
    handle = DLOPEN_SELF();

  INIT_SYMBOL(client_auth_generation);
  INIT_SYMBOL(ClientSignal);
  INIT_SYMBOL(DeleteFontClientID);
  INIT_SYMBOL(VErrorF);
  INIT_SYMBOL(find_old_font);
  INIT_SYMBOL(GetClientResolutions);
  INIT_SYMBOL(GetDefaultPointSize);
  INIT_SYMBOL(GetNewFontClientID);
  INIT_SYMBOL(GetTimeInMillis);
  INIT_SYMBOL(init_fs_handlers);
  INIT_SYMBOL(RegisterFPEFunctions);
  INIT_SYMBOL(remove_fs_handlers);
  INIT_SYMBOL(set_font_authorizations);
  INIT_SYMBOL(StoreFontClientFont);
  INIT_SYMBOL(MakeAtom);
  INIT_SYMBOL(ValidAtom);
  INIT_SYMBOL(NameForAtom);
  INIT_SYMBOL(register_fpe_functions);
  INIT_DATA(serverClient);
  INIT_DATA(serverGeneration);

  inited = TRUE;
  return inited;
}

#endif /* NO_WEAK_SYMBOLS && PIC */
