#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include "xf86.h"
#include "xf86i2c.h"
#include "tda9850.h"
#include "i2c_def.h"

#define TDA9850(a,b)	{ \
		data[0]=a; \
		data[1]=b; \
		I2C_WriteRead(&(t->d), data, 2, NULL, 0); \
		}

TDA9850Ptr
Detect_tda9850(I2CBusPtr b, I2CSlaveAddr addr)
{
    TDA9850Ptr t;
    I2CByte a;

    t = calloc(1, sizeof(TDA9850Rec));
    if (t == NULL)
        return NULL;
    switch (addr) {
    case TDA9850_ADDR_1:
        t->d.DevName = "TDA9850 BTSC Stereo+SAP Audio Processor";
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
tda9850_init(TDA9850Ptr t)
{
    t->stereo = 1;
    t->sap = 0;
    t->mute = TRUE;
    t->sap_mute = TRUE;
    tda9850_setaudio(t);
    return TRUE;
}

void
tda9850_setaudio(TDA9850Ptr t)
{
    CARD8 data[2];

    if (t->mux == 2) {
        TDA9850(0x04, 0x0F);
        TDA9850(0x05, 0x0F);
        TDA9850(0x06, 0x58);
        TDA9850(0x07, 0x07);
        TDA9850(0x08, 0x00);
        TDA9850(0x09, 0x00);
        TDA9850(0x0A, 0x03);
    }
    else {
        TDA9850(0x04, 0x07);
        TDA9850(0x05, 0x07);
        TDA9850(0x06, 0x58);
        TDA9850(0x07, 0x07);
        TDA9850(0x08, 0x10);
        TDA9850(0x09, 0x10);
        TDA9850(0x0A, 0x03);
    }

    TDA9850(0x06,
            (t->stereo << 6) | (t->sap << 7) | (t->mute ? 0x8 : 0) | (t->
                                                                      sap_mute ?
                                                                      0x10 :
                                                                      0x0));
}

void
tda9850_mute(TDA9850Ptr t, Bool mute)
{
    CARD8 data[2];

    xf86DrvMsg(t->d.pI2CBus->scrnIndex, X_INFO, "tda9850_mute %s\n",
               mute ? "on" : "off");
    t->mute = mute;

    TDA9850(0x06,
            (t->stereo << 6) | (t->sap << 7) | (t->mute ? 0x8 : 0x0) | (t->
                                                                        sap_mute
                                                                        ? 0x10 :
                                                                        0x0));
}

void
tda9850_sap_mute(TDA9850Ptr t, Bool sap_mute)
{
    CARD8 data[2];

    xf86DrvMsg(t->d.pI2CBus->scrnIndex, X_INFO, "tda9850_sap_mute %s\n",
               sap_mute ? "on" : "off");
    t->sap_mute = sap_mute;

    TDA9850(0x06,
            (t->stereo << 6) | (t->sap << 7) | (t->mute ? 0x8 : 0x0) | (t->
                                                                        sap_mute
                                                                        ? 0x10 :
                                                                        0x0));
}

CARD16
tda9850_getstatus(TDA9850Ptr t)
{
    CARD16 status;

    I2C_WriteRead(&(t->d), NULL, 0, (I2CByte *) &status, 2);
    return status;
}
