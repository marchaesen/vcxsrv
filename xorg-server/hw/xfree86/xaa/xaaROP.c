#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include <X11/X.h>
#include "misc.h"
#include "xf86.h"
#include "xf86_OSproc.h"

#include "scrnintstr.h"
#include "xf86str.h"
#include "xaarop.h"
#include "xaa.h"
#include "xaalocal.h"

int XAACopyROP[16] = {
    ROP_0,                      /* GXclear */
    ROP_DSa,                    /* GXand */
    ROP_SDna,                   /* GXandReverse */
    ROP_S,                      /* GXcopy */
    ROP_DSna,                   /* GXandInverted */
    ROP_D,                      /* GXnoop */
    ROP_DSx,                    /* GXxor */
    ROP_DSo,                    /* GXor */
    ROP_DSon,                   /* GXnor */
    ROP_DSxn,                   /* GXequiv */
    ROP_Dn,                     /* GXinvert */
    ROP_SDno,                   /* GXorReverse */
    ROP_Sn,                     /* GXcopyInverted */
    ROP_DSno,                   /* GXorInverted */
    ROP_DSan,                   /* GXnand */
    ROP_1                       /* GXset */
};

int XAACopyROP_PM[16] = {
    ROP_0,                      /* not used */
    ROP_DSPnoa,
    ROP_DPSnaon,
    ROP_DPSDxax,
    ROP_DPSana,
    ROP_D,                      /* not used */
    ROP_DPSax,
    ROP_DPSao,
    ROP_DPSaon,
    ROP_DPSaxn,
    ROP_Dn,                     /* not used */
    ROP_DPSanan,
    ROP_PSDPxox,                /* is that correct ? */
    ROP_DPSnao,
    ROP_DSPnoan,
    ROP_1                       /* not used */
};

int XAAPatternROP[16] = {
    ROP_0,
    ROP_DPa,
    ROP_PDna,
    ROP_P,
    ROP_DPna,
    ROP_D,
    ROP_DPx,
    ROP_DPo,
    ROP_DPon,
    ROP_PDxn,
    ROP_Dn,
    ROP_PDno,
    ROP_Pn,
    ROP_DPno,
    ROP_DPan,
    ROP_1
};

int XAAPatternROP_PM[16] = {
    ROP_DPna,
    ROP_DPSnoa,
    ROP_DSPnaon,
    ROP_DSPDxax,
    ROP_DPSana,
    ROP_D,
    ROP_DPSax,
    ROP_DPSao,
    ROP_DPSaon,
    ROP_DPSaxn,
    ROP_DPx,
    ROP_DPSanan,
    ROP_SPDSxox,                /* is that correct ? */
    ROP_DSPnao,
    ROP_DPSnoan,
    ROP_DPo
};

int
XAAGetCopyROP(int i)
{
    return XAACopyROP[i];
}

int
XAAGetCopyROP_PM(int i)
{
    return XAACopyROP_PM[i];
}

int
XAAGetPatternROP(int i)
{
    return XAAPatternROP[i];
}

int
XAAGetPatternROP_PM(int i)
{
    return XAAPatternROP_PM[i];
}

int
XAAHelpPatternROP(ScrnInfoPtr pScrn, int *fg, int *bg, int pm, int *rop)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCRNINFOPTR(pScrn);
    int ret = 0;

    pm &= infoRec->FullPlanemasks[pScrn->depth - 1];

    if (pm == infoRec->FullPlanemasks[pScrn->depth - 1]) {
        if (!NO_SRC_ROP(*rop))
            ret |= ROP_PAT;
        *rop = XAAPatternROP[*rop];
    }
    else {
        switch (*rop) {
        case GXnoop:
            break;
        case GXset:
        case GXclear:
        case GXinvert:
            ret |= ROP_PAT;
            *fg = pm;
            if (*bg != -1)
                *bg = pm;
            break;
        default:
            ret |= ROP_PAT | ROP_SRC;
            break;
        }
        *rop = XAAPatternROP_PM[*rop];
    }

    return ret;
}

int
XAAHelpSolidROP(ScrnInfoPtr pScrn, int *fg, int pm, int *rop)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCRNINFOPTR(pScrn);
    int ret = 0;

    pm &= infoRec->FullPlanemasks[pScrn->depth - 1];

    if (pm == infoRec->FullPlanemasks[pScrn->depth - 1]) {
        if (!NO_SRC_ROP(*rop))
            ret |= ROP_PAT;
        *rop = XAAPatternROP[*rop];
    }
    else {
        switch (*rop) {
        case GXnoop:
            break;
        case GXset:
        case GXclear:
        case GXinvert:
            ret |= ROP_PAT;
            *fg = pm;
            break;
        default:
            ret |= ROP_PAT | ROP_SRC;
            break;
        }
        *rop = XAAPatternROP_PM[*rop];
    }

    return ret;
}
