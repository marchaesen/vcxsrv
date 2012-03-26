
#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include <X11/X.h>
#include "os.h"
#include "xf86.h"
#include "xf86Priv.h"
#include "shared/xf86Axp.h"
#include <sys/param.h>
#include "xf86_OSlib.h"
#include <stdio.h>
#include <sys/sysctl.h>

axpDevice bsdGetAXP(void);

/*
 * BSD does a very nice job providing system information to
 * user space programs. Unfortunately it doesn't provide all
 * the information required. Therefore we just obtain the
 * system type and look up the rest from a list we maintain
 * ourselves.
 */

typedef struct {
    char *name;
    int type;
} _AXP;

static _AXP axpList[] = {
    {"apecs", APECS},
    {"pyxis", PYXIS},
    {"cia", CIA},
    {"irongate", IRONGATE},
    {"lca", LCA},
    {"t2", T2},
    {"tsunami", TSUNAMI},
    {NULL, SYS_NONE}
};

axpDevice
bsdGetAXP(void)
{
    int i;
    char sysname[64];
    size_t len = sizeof(sysname);

#ifdef __OpenBSD__
    int mib[3];
    int error;

    mib[0] = CTL_MACHDEP;
    mib[1] = CPU_CHIPSET;
    mib[2] = CPU_CHIPSET_TYPE;

    if ((error = sysctl(mib, 3, &sysname, &len, NULL, 0)) < 0)
#else
    if ((sysctlbyname("hw.chipset.type", &sysname, &len, 0, 0)) < 0)
#endif
        FatalError("bsdGetAXP: can't find machine type\n");
#ifdef DEBUG
    xf86Msg(X_INFO, "AXP is a: %s\n", sysname);
#endif
    for (i = 0;; i++) {
        if (axpList[i].name == NULL)
            return SYS_NONE;
        if (!strcmp(sysname, axpList[i].name))
            return axpList[i].type;
    }
}
