#ifndef __ASSERT_H__
#define __ASSERT_H__

#include <stdio.h>

#ifdef __cplusplus
extern "C"
#endif
__declspec(dllimport) void __stdcall DebugBreak(void);

static __inline void __assert(intptr_t Cond)
{
#ifdef _DEBUG
  if (!Cond)
  {
    fprintf(stderr, "assertion occured.\n");
    DebugBreak();
    while (1);
  }
#endif
}

#ifdef _DEBUG
#define assert(Cond) __assert((intptr_t)(Cond))
#else
#define assert(Cond) __assert(0)
#endif

#endif
 
