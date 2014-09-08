#include <stdio.h>
#include <X11/fonts/fntfilst.h>
#include <X11/fonts/font.h>

#ifndef True
#define True (-1)
#endif
#ifndef False
#define False (0)
#endif

/* this probably works for Mach-O too, but probably not for PE */
#if (defined(__APPLE__) || defined(__ELF__)) && defined(__GNUC__) && (__GNUC__ >= 3)
#define weak __attribute__((weak))
#else
#define weak
#ifndef __SUNPRO_C /* Sun compilers use #pragma weak in .c files instead */
#define NO_WEAK_SYMBOLS
#endif
#endif

#if defined(NO_WEAK_SYMBOLS) && defined(PIC)
#include <stdarg.h>
extern int _font_init_stubs(void);
#define OVERRIDE_DATA(sym) \
    _font_init_stubs(); \
    if (__ptr_##sym && __ptr_##sym != &sym) \
      sym = *__ptr_##sym
#define OVERRIDE_SYMBOL(sym,...) \
    _font_init_stubs(); \
    if (__##sym && __##sym != sym) \
      return (*__##sym)(__VA_ARGS__)
#define OVERRIDE_VA_SYMBOL(sym,f) \
    va_list _args; \
    _font_init_stubs(); \
    va_start(_args, f); \
    if (__##sym) \
      (*__##sym)(f, _args); \
    va_end(_args)

int (*__client_auth_generation)(ClientPtr);
Bool (*__ClientSignal)(ClientPtr);
void (*__DeleteFontClientID)(Font);
void (*__VErrorF)(const char *, va_list);
FontPtr (*__find_old_font)(FSID);
FontResolutionPtr (*__GetClientResolutions)(int *);
int (*__GetDefaultPointSize)(void);
Font (*__GetNewFontClientID)(void);
unsigned long (*__GetTimeInMillis)(void);
int (*__init_fs_handlers)(FontPathElementPtr, BlockHandlerProcPtr);
int (*__RegisterFPEFunctions)(NameCheckFunc, InitFpeFunc, FreeFpeFunc,
     ResetFpeFunc, OpenFontFunc, CloseFontFunc, ListFontsFunc,
     StartLfwiFunc, NextLfwiFunc, WakeupFpeFunc, ClientDiedFunc,
     LoadGlyphsFunc, StartLaFunc, NextLaFunc, SetPathFunc);
void (*__remove_fs_handlers)(FontPathElementPtr, BlockHandlerProcPtr, Bool);
void **__ptr_serverClient;
int (*__set_font_authorizations)(char **, int *, ClientPtr);
int (*__StoreFontClientFont)(FontPtr, Font);
Atom (*__MakeAtom)(const char *, unsigned, int);
int (*__ValidAtom)(Atom);
char *(*__NameForAtom)(Atom);
unsigned long *__ptr_serverGeneration;
void (*__register_fpe_functions)(void);
#else /* NO_WEAK_SYMBOLS && PIC */
#define OVERRIDE_DATA(sym)
#define OVERRIDE_SYMBOL(sym,...)
#define OVERRIDE_VA_SYMBOL(sym,f)
#endif

/* This is really just a hack for now... __APPLE__ really should be using
 * the weak symbols route above, but it's causing an as-yet unresolved issue,
 * so we're instead building with flat_namespace.
 */
#ifdef __APPLE__
#undef weak
#define weak
#endif

extern FontPtr find_old_font ( FSID id );
extern int set_font_authorizations ( char **authorizations,
				     int *authlen,
				     ClientPtr client );

extern unsigned long GetTimeInMillis (void);

extern void ErrorF(const char *format, ...);

/* end of file */
