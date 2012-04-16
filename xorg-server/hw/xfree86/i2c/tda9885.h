#ifndef __TDA9885_H__
#define __TDA9885_H__

#include "xf86i2c.h"

typedef struct {
    I2CDevRec d;

    /* write-only parameters */
    /* B DATA */
    CARD8 sound_trap;
    CARD8 auto_mute_fm;
    CARD8 carrier_mode;
    CARD8 modulation;
    CARD8 forced_mute_audio;
    CARD8 port1;
    CARD8 port2;
    /* C DATA */
    CARD8 top_adjustment;
    CARD8 deemphasis;
    CARD8 audio_gain;
    /* E DATA */
    CARD8 standard_sound_carrier;
    CARD8 standard_video_if;
    CARD8 minimum_gain;
    CARD8 gating;
    CARD8 vif_agc;
    /* read-only values */

    CARD8 after_reset;
    CARD8 afc_status;
    CARD8 vif_level;
    CARD8 afc_win;
    CARD8 fm_carrier;
} TDA9885Rec, *TDA9885Ptr;

#define TDA9885_ADDR_1   0x86
#define TDA9885_ADDR_2   0x84
#define TDA9885_ADDR_3   0x96
#define TDA9885_ADDR_4   0x94

#define xf86_Detect_tda9885		Detect_tda9885
extern _X_EXPORT TDA9885Ptr Detect_tda9885(I2CBusPtr b, I2CSlaveAddr addr);

#define xf86_tda9885_init		tda9885_init
extern _X_EXPORT Bool tda9885_init(TDA9885Ptr t);

#define xf86_tda9885_setparameters	tda9885_setparameters
extern _X_EXPORT void tda9885_setparameters(TDA9885Ptr t);

#define xf86_tda9885_getstatus		tda9885_getstatus
extern _X_EXPORT void tda9885_getstatus(TDA9885Ptr t);

#define xf86_tda9885_dumpstatus		tda9885_dumpstatus
extern _X_EXPORT void tda9885_dumpstatus(TDA9885Ptr t);

#define TDA9885SymbolsList  \
		"Detect_tda9885", \
		"tda9885_init", \
		"tda9885_setaudio", \
		"tda9885_mute"

#endif
