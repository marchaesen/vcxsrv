#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include "xf86_os_support.h"
#include "xf86_OSlib.h"

void
xf86OSInitVidMem(VidMemInfoPtr pVidMem)
{
    pVidMem->initialised = TRUE;
    return;
}
