#ifndef __FI1236_H__
#define __FI1236_H__

#include "xf86i2c.h"

/* why someone has defined NUM someplace else is beyoung me.. */
#undef NUM

typedef struct {
    CARD32 fcar;                /* 16 * fcar_Mhz */
    CARD32 min_freq;            /* 16 * min_freq_Mhz */
    CARD32 max_freq;            /* 16 * max_freq_Mhz */

    CARD32 threshold1;          /* 16 * Value_Mhz */
    CARD32 threshold2;          /* 16 * Value_Mhz */

    CARD8 band_low;
    CARD8 band_mid;
    CARD8 band_high;
    CARD8 control;
} FI1236_parameters;

typedef struct {
    /* what we want */
    /* all frequencies are in Mhz */
    double f_rf;                /* frequency to tune to */
    double f_if1;               /* first intermediate frequency */
    double f_if2;               /* second intermediate frequency */
    double f_ref;               /* reference frequency */
    double f_ifbw;              /* bandwidth */
    double f_step;              /* step */

    /* what we compute */
    double f_lo1;
    double f_lo2;
    int LO1I;
    int LO2I;
    int SEL;
    int STEP;
    int NUM;
} MT2032_parameters;

typedef struct {
    I2CDevRec d;
    int type;

    void *afc_source;           /* The AFC source may be another chip like TDA988x */

    int afc_delta;
    CARD32 original_frequency;
    Bool afc_timer_installed;
    int afc_count;
    int last_afc_hint;

    double video_if;
    FI1236_parameters parm;
    int xogc;                   /* for MT2032 */

    struct {
        CARD8 div1;
        CARD8 div2;
        CARD8 control;
        CARD8 band;
        CARD8 aux;              /* this is for MK3 tuners */
    } tuner_data;
} FI1236Rec, *FI1236Ptr;

#define TUNER_TYPE_FI1236              0
#define TUNER_TYPE_FI1216              1
#define TUNER_TYPE_TEMIC_FN5AL         2
#define TUNER_TYPE_MT2032	       3
#define TUNER_TYPE_FI1246              4
#define TUNER_TYPE_FI1256              5
#define TUNER_TYPE_FI1236W             6
#define TUNER_TYPE_FM1216ME            7

#define FI1236_ADDR(a)        ((a)->d.SlaveAddr)

#define FI1236_ADDR_1	     0xC6
#define FI1236_ADDR_2        0xC0

#define TUNER_TUNED   0
#define TUNER_JUST_BELOW 1
#define TUNER_JUST_ABOVE -1
#define TUNER_OFF      4
#define TUNER_STILL_TUNING      5

void FI1236_tune(FI1236Ptr f, CARD32 frequency);

#define FI1236SymbolsList  \
		"Detect_FI1236", \
		"FI1236_set_tuner_type", \
		"TUNER_set_frequency"

#define xf86_Detect_FI1236		Detect_FI1236
extern _X_EXPORT FI1236Ptr Detect_FI1236(I2CBusPtr b, I2CSlaveAddr addr);

#define xf86_FI1236_set_tuner_type	FI1236_set_tuner_type
extern _X_EXPORT void FI1236_set_tuner_type(FI1236Ptr f, int type);

#define xf86_TUNER_set_frequency	TUNER_set_frequency
extern _X_EXPORT void TUNER_set_frequency(FI1236Ptr f, CARD32 frequency);

#define xf86_FI1236_AFC			FI1236_AFC
extern _X_EXPORT int FI1236_AFC(FI1236Ptr f);

#define xf86_TUNER_get_afc_hint		TUNER_get_afc_hint
extern _X_EXPORT int TUNER_get_afc_hint(FI1236Ptr f);

#define xf86_fi1236_dump_status		fi1236_dump_status
extern _X_EXPORT void fi1236_dump_status(FI1236Ptr f);

#endif
