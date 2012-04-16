#ifndef __BT829_H__
#define __BT829_H__

#include "xf86i2c.h"

typedef struct {
    int tunertype;              /* Must be set before init */
    /* Private variables */
    I2CDevRec d;

    CARD8 brightness;
    CARD8 ccmode;
    CARD8 code;
    CARD16 contrast;
    CARD8 format;
    int height;
    CARD8 hue;
    CARD8 len;
    CARD8 mux;
    CARD8 out_en;
    CARD8 p_io;
    CARD16 sat_u;
    CARD16 sat_v;
    CARD8 vbien;
    CARD8 vbifmt;
    int width;

    CARD16 hdelay;
    CARD16 hscale;
    CARD16 vactive;
    CARD16 vdelay;
    CARD16 vscale;

    CARD16 htotal;
    CARD8 id;
    CARD8 svideo_mux;
} BT829Rec, *BT829Ptr;

#define xf86_bt829_Detect	bt829_Detect
extern _X_EXPORT BT829Ptr bt829_Detect(I2CBusPtr b, I2CSlaveAddr addr);

/* ATI card specific initialization */
#define BT829_ATI_ADDR_1	0x8A
#define BT829_ATI_ADDR_2	0x88

#define xf86_bt829_ATIInit	bt829_ATIInit
extern _X_EXPORT int bt829_ATIInit(BT829Ptr bt);

#define BT829_NTSC		1       /* NTSC-M */
#define BT829_NTSC_JAPAN	2       /* NTSC-Japan */
#define BT829_PAL		3       /* PAL-B,D,G,H,I */
#define BT829_PAL_M		4       /* PAL-M */
#define BT829_PAL_N		5       /* PAL-N */
#define BT829_SECAM		6       /* SECAM */
#define BT829_PAL_N_COMB	7       /* PAL-N combination */

#define xf86_bt829_SetFormat	bt829_SetFormat
extern _X_EXPORT int bt829_SetFormat(BT829Ptr bt, CARD8 format);

#define BT829_MUX2	1       /* ATI -> composite video */
#define BT829_MUX0	2       /* ATI -> tv tuner */
#define BT829_MUX1	3       /* ATI -> s-video */

#define xf86_bt829_SetMux	bt829_SetMux
extern _X_EXPORT int bt829_SetMux(BT829Ptr bt, CARD8 mux);

#define xf86_bt829_SetCaptSize		bt829_SetCaptSize
extern _X_EXPORT int bt829_SetCaptSize(BT829Ptr bt, int width, int height);

#define xf86_bt829_SetBrightness	bt829_SetBrightness
extern _X_EXPORT void bt829_SetBrightness(BT829Ptr bt, int brightness);

#define xf86_bt829_SetContrast		bt829_SetContrast
extern _X_EXPORT void bt829_SetContrast(BT829Ptr bt, int contrast);

#define xf86_bt829_SetSaturation	bt829_SetSaturation
extern _X_EXPORT void bt829_SetSaturation(BT829Ptr bt, int saturation);

#define xf86_bt829_SetTint		bt829_SetTint
extern _X_EXPORT void bt829_SetTint(BT829Ptr bt, int hue);      /* Hue */

#define xf86_bt829_SetOUT_EN		bt829_SetOUT_EN
extern _X_EXPORT void bt829_SetOUT_EN(BT829Ptr bt, BOOL out_en);        /* VPOLE register */

#define xf86_bt829_SetP_IO		bt829_SetP_IO
extern _X_EXPORT void bt829_SetP_IO(BT829Ptr bt, CARD8 p_io);   /* P_IO register */

extern _X_EXPORT int bt829_SetCC(BT829Ptr bt);

#define BT829SymbolsList   \
		"bt829_Detect", \
		"bt829_ATIInit", \
		"bt829_SetFormat", \
		"bt829_SetMux", \
		"bt829_SetBrightness", \
		"bt829_SetContrast", \
		"bt829_SetSaturation", \
		"bt829_SetTint", \
		"bt829_SetCaptSize", \
		"bt829_SetOUT_EN", \
		"bt829_SetP_IO"

#endif
