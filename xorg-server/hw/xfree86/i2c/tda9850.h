#ifndef __TDA9850_H__
#define __TDA9850_H__

#include "xf86i2c.h"

typedef struct {
    I2CDevRec d;

    int mux;
    int stereo;
    int sap;
    Bool mute;
    Bool sap_mute;
} TDA9850Rec, *TDA9850Ptr;

#define TDA9850_ADDR_1   0xB4

#define xf86_Detect_tda9850	Detect_tda9850
extern _X_EXPORT TDA9850Ptr Detect_tda9850(I2CBusPtr b, I2CSlaveAddr addr);

#define xf86_tda9850_init	tda9850_init
extern _X_EXPORT Bool tda9850_init(TDA9850Ptr t);

#define xf86_tda9850_setaudio	tda9850_setaudio
extern _X_EXPORT void tda9850_setaudio(TDA9850Ptr t);

#define xf86_tda9850_mute	tda9850_mute
extern _X_EXPORT void tda9850_mute(TDA9850Ptr t, Bool mute);

#define xf86_tda9850_sap_mute	tda9850_sap_mute
extern _X_EXPORT void tda9850_sap_mute(TDA9850Ptr t, Bool sap_mute);

#define xf86_tda9850_getstatus	tda9850_getstatus
extern _X_EXPORT CARD16 tda9850_getstatus(TDA9850Ptr t);

#define TDA9850SymbolsList  \
		"Detect_tda9850", \
		"tda9850_init", \
		"tda9850_setaudio", \
		"tda9850_mute", \
		"tda9850_sap_mute"

#endif
