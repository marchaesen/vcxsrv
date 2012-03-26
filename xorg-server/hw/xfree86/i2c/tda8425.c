#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include "xf86.h"
#include "xf86i2c.h"
#include "tda8425.h"
#include "i2c_def.h"

#define TDA8425(a,b)	{ \
		data[0]=a; \
		data[1]=b; \
		I2C_WriteRead(&(t->d), data, 2, NULL, 0); \
		}

TDA8425Ptr
Detect_tda8425(I2CBusPtr b, I2CSlaveAddr addr, Bool force)
{
    TDA8425Ptr t;

    t = calloc(1, sizeof(TDA8425Rec));
    if (t == NULL)
        return NULL;
    t->d.DevName = "TDA8425 BTSC Stereo Audio Processor";
    t->d.SlaveAddr = addr;
    t->d.pI2CBus = b;
    t->d.NextDev = NULL;
    t->d.StartTimeout = b->StartTimeout;
    t->d.BitTimeout = b->BitTimeout;
    t->d.AcknTimeout = b->AcknTimeout;
    t->d.ByteTimeout = b->ByteTimeout;

    if (!force && !I2CProbeAddress(b, addr)) {
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
tda8425_init(TDA8425Ptr t)
{
    t->stereo = 3;              /* 3 = Spacial 2 = Linear 1 = Pseudo 0 = Forced mono */
    t->v_left = 0xFF;           /* FF - C0 */
    t->v_right = 0xFF;          /* FF - C0 */
    t->bass = 0xF6;             /* 0xFF - 0xF0 */
    t->treble = 0xF6;           /* 0xFF - 0xF0 */
    t->src_sel = 3;             /* 3 - stereo */
    t->mute = TRUE;
    t->mux = 0;                 /* 0 - source one, 1 -source 2 */

    tda8425_setaudio(t);
    return TRUE;
}

void
tda8425_setaudio(TDA8425Ptr t)
{
    I2CByte data[2];

    TDA8425(0x00, t->v_left);
    TDA8425(0x01, t->v_right);
    TDA8425(0x02, t->bass);
    TDA8425(0x03, t->treble);
    TDA8425(0x08,
            0xC0 | (t->mute ? 0x20 : 0x0) | (t->stereo << 3) | (t->
                                                                src_sel << 1) |
            t->mux);
}

void
tda8425_mute(TDA8425Ptr t, Bool mute)
{
    t->mute = mute;
    tda8425_setaudio(t);
}
