
#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#ifndef XVMODPROC_H
#define XVMODPROC_H
#include "xvmcext.h"

extern DevPrivateKey (*XvGetScreenKeyProc) (void);
extern unsigned long (*XvGetRTPortProc) (void);
extern int (*XvScreenInitProc) (ScreenPtr);
extern int (*XvMCScreenInitProc) (ScreenPtr, int, XvMCAdaptorPtr);

extern void XvRegister(void);

#endif /* XVMODPROC_H */
