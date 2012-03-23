#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include "xf86.h"
#include "xf86i2c.h"
#include "tda9885.h"
#include "i2c_def.h"

TDA9885Ptr
Detect_tda9885(I2CBusPtr b, I2CSlaveAddr addr)
{
    TDA9885Ptr t;
    I2CByte a;

    t = calloc(1, sizeof(TDA9885Rec));
    if (t == NULL)
        return NULL;
    switch (addr) {
    case TDA9885_ADDR_1:
    case TDA9885_ADDR_2:
    case TDA9885_ADDR_3:
    case TDA9885_ADDR_4:
        t->d.DevName = "TDA9885 Alignment-free IF-PLL";
        break;
    default:
        t->d.DevName = "Generic TDAxxxx";
        break;
    }
    t->d.SlaveAddr = addr;
    t->d.pI2CBus = b;
    t->d.NextDev = NULL;
    t->d.StartTimeout = b->StartTimeout;
    t->d.BitTimeout = b->BitTimeout;
    t->d.AcknTimeout = b->AcknTimeout;
    t->d.ByteTimeout = b->ByteTimeout;

    if (!I2C_WriteRead(&(t->d), NULL, 0, &a, 1)) {
        free(t);
        return NULL;
    }

    /* set default parameters */
    if (!I2CDevInit(&(t->d))) {
        free(t);
        return NULL;
    }

    return t;
}

Bool
tda9885_init(TDA9885Ptr t)
{
    t->forced_mute_audio = 1;
    return TRUE;
}

void
tda9885_getstatus(TDA9885Ptr t)
{
    CARD8 value;

    I2C_WriteRead(&(t->d), NULL, 0, &value, 1);
    t->after_reset = value & 1;
    t->afc_status = (value >> 1) & 0xf;
    t->fm_carrier = (value >> 5) & 1;
    t->vif_level = (value >> 6) & 1;
    t->afc_win = (value >> 7) & 1;
}

void
tda9885_setparameters(TDA9885Ptr t)
{
    CARD8 data[4];

    data[0] = 0;                /* start with subaddress 0 */
    data[1] = (t->sound_trap & 1) | ((t->auto_mute_fm & 1) << 1) | ((t->carrier_mode & 1) << 2) | ((t->modulation & 3) << 3) | ((t->forced_mute_audio & 1) << 5) | ((t->port1 & 1) << 6) | ((t->port2 & 1) << 7);       /* B data */
    data[2] = (t->top_adjustment & 0x1f) | ((t->deemphasis & 0x3) << 5) | ((t->audio_gain & 1) << 7);   /* C data */
    data[3] = (t->standard_sound_carrier & 0x3) | ((t->standard_video_if & 0x07) << 2) | ((t->minimum_gain & 0x01) << 5) | ((t->gating & 0x01) << 6) | ((t->vif_agc & 0x01) << 7);      /* E data */

    I2C_WriteRead(&(t->d), data, 4, NULL, 0);

    xf86DrvMsg(t->d.pI2CBus->scrnIndex, X_INFO,
               "TDA9885 setparam: B data: %x, C data: %x, E data: %x\n",
               data[1], data[2], data[3]);
}

void
tda9885_dumpstatus(TDA9885Ptr t)
{
    xf86DrvMsg(t->d.pI2CBus->scrnIndex, X_INFO,
               "TDA9885 status: after_reset=%d afc_status=%d (%3.1f kHz off) fm_carrier=%d vif_level=%d afc_win=%d %s\n",
               t->after_reset, t->afc_status,
               (t->afc_status <
                8) ? -12.5 - t->afc_status * 25.0 : -12.5 + (16 -
                                                             t->afc_status) *
               25.0, t->fm_carrier, t->vif_level, t->afc_win,
               t->afc_win ? "VCO in" : "VCO out");
}
