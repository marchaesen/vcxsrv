#ifndef __ASSERT_H__
#define __ASSERT_H__

#include <stdio.h>

#ifdef __cplusplus
extern "C"
#endif
__declspec(dllimport) void __stdcall DebugBreak(void);

static __inline void __assert(int Cond)
{
#ifdef _DEBUG
  if (!Cond)
  {
    printf("assertion occured.\n");
    DebugBreak();
    while (1);
  }
#endif
}

#define assert(Cond) __assert((int)(Cond))

#endif
 
