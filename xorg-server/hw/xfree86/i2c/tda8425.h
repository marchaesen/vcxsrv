#ifndef __TDA8425_H__
#define __TDA8425_H__

#include "xf86i2c.h"

typedef struct {
    I2CDevRec d;

    int mux;
    int stereo;
    int v_left;
    int v_right;
    int bass;
    int treble;
    int src_sel;
    Bool mute;
} TDA8425Rec, *TDA8425Ptr;

#define TDA8425_ADDR_1   0x82

/* the third parameter is meant to force detection of tda8425.
   This is because tda8425 is write-only and complete implementation
   of I2C protocol is not always available. Besides address there is no good
   way to autodetect it so we have to _know_ it is there anyway */

#define xf86_Detect_tda8425	Detect_tda8425
extern _X_EXPORT TDA8425Ptr Detect_tda8425(I2CBusPtr b, I2CSlaveAddr addr,
                                           Bool force);
#define xf86_tda8425_init	tda8425_init
extern _X_EXPORT Bool tda8425_init(TDA8425Ptr t);

#define xf86_tda8425_setaudio	tda8425_setaudio
extern _X_EXPORT void tda8425_setaudio(TDA8425Ptr t);

#define xf86_tda8425_mute	tda8425_mute
extern _X_EXPORT void tda8425_mute(TDA8425Ptr t, Bool mute);

#define TDA8425SymbolsList  \
		"Detect_tda8425", \
		"tda8425_init", \
		"tda8425_setaudio", \
		"tda8425_mute"

#endif
