
#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include "xvmcext.h"

extern DevPrivateKey (*XvGetScreenKeyProc) (void);
extern unsigned long (*XvGetRTPortProc) (void);
extern int (*XvScreenInitProc) (ScreenPtr);
extern int (*XvMCScreenInitProc) (ScreenPtr, int, XvMCAdaptorPtr);

extern void XvRegister(void);
