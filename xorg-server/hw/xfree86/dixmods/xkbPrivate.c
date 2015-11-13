
#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include <stdio.h>
#include <X11/X.h>
#include "windowstr.h"
#define XKBSRV_NEED_FILE_FUNCS
#include <xkbsrv.h>

#include "dixgrabs.h"
#include "os.h"
#include "xf86.h"

int
XkbDDXPrivate(DeviceIntPtr dev, KeyCode key, XkbAction *act)
{
    XkbAnyAction *xf86act = &(act->any);
    char msgbuf[XkbAnyActionDataSize + 1];

    if (xf86act->type == XkbSA_XFree86Private) {
        memcpy(msgbuf, xf86act->data, XkbAnyActionDataSize);
        msgbuf[XkbAnyActionDataSize] = '\0';
        if (strcasecmp(msgbuf, "-vmode") == 0)
            xf86ProcessActionEvent(ACTION_PREV_MODE, NULL);
        else if (strcasecmp(msgbuf, "+vmode") == 0)
            xf86ProcessActionEvent(ACTION_NEXT_MODE, NULL);
        else if (strcasecmp(msgbuf, "prgrbs") == 0) {
            DeviceIntPtr tmp;

            xf86Msg(X_INFO, "Printing all currently active device grabs:\n");
            for (tmp = inputInfo.devices; tmp; tmp = tmp->next)
                if (tmp->deviceGrab.grab)
                    PrintDeviceGrabInfo(tmp);
            xf86Msg(X_INFO, "End list of active device grabs\n");

            PrintPassiveGrabs();
        }
        else if (strcasecmp(msgbuf, "ungrab") == 0)
            UngrabAllDevices(FALSE);
        else if (strcasecmp(msgbuf, "clsgrb") == 0)
            UngrabAllDevices(TRUE);
        else if (strcasecmp(msgbuf, "prwins") == 0)
            PrintWindowTree();
    }

    return 0;
}
