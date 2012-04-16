
#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include <X11/X.h>
#include "misc.h"
#include "scrnintstr.h"
#include "gc.h"
#include <X11/extensions/Xv.h>
#include <X11/extensions/Xvproto.h>
#include "xvdix.h"
#include "xvmodproc.h"

void
XvRegister(void)
{
    XvScreenInitProc = XvScreenInit;
    XvGetScreenKeyProc = XvGetScreenKey;
    XvGetRTPortProc = XvGetRTPort;
    XvMCScreenInitProc = XvMCScreenInit;
}
