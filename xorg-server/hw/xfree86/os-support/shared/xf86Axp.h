#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#ifndef _XF86_AXP_H_
#define _XF86_AXP_H_

typedef enum {
    SYS_NONE,
    TSUNAMI,
    LCA,
    APECS,
    T2,
    T2_GAMMA,
    CIA,
    MCPCIA,
    JENSEN,
    POLARIS,
    PYXIS,
    PYXIS_CIA,
    IRONGATE
} axpDevice;

typedef struct {
    axpDevice id;
    unsigned long hae_thresh;
    unsigned long hae_mask;
    unsigned long size;
} axpParams;

extern axpParams xf86AXPParams[];

#endif
