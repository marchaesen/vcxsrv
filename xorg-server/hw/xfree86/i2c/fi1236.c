#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "xf86.h"
#include "xf86i2c.h"
#include "fi1236.h"
#include "tda9885.h"
#include "i2c_def.h"

#define NUM_TUNERS    8

const FI1236_parameters tuner_parms[NUM_TUNERS] = {
    /* 0 - FI1236 */
    {733, 884, 12820, 2516, 7220, 0xA2, 0x94, 0x34, 0x8e},
    /* !!!based on documentation - it should be:
       {733, 16*55.25, 16*801.25, 16*160, 16*454, 0xA0, 0x90, 0x30, 0x8e}, */

    /* 1 - FI1216 */
    {623, 16 * 48.75, 16 * 855.25, 16 * 170, 16 * 450, 0xA0, 0x90, 0x30, 0x8e},
    /* 2 - TEMIC FN5AL */
    {623, 16 * 45.75, 16 * 855.25, 16 * 169, 16 * 454, 0xA0, 0x90, 0x30, 0x8e},
    /* 3 - MT2032.. */
    {733, 768, 13760, 0, 0, 0, 0, 0, 0},
    /* 4 - FI1246 */
    {623, 16 * 45.75, 16 * 855.25, 16 * 170, 16 * 450, 0xA0, 0x90, 0x30, 0x8e},
    /* 5 - FI1256 */
    {623, 16 * 49.75, 16 * 863.25, 16 * 170, 16 * 450, 0xA0, 0x90, 0x30, 0x8e},
    /* 6 - FI1236W */
    /*{ 733, 884, 12820, 2516, 7220, 0x1, 0x2, 0x4, 0x8e }, */
    {732, 16 * 55.25, 16 * 801.25, 16 * 160, 16 * 442, 0x1, 0x2, 0x4, 0x8e},
    /* 7 - FM1216ME */
    {623, 16 * 48.25, 16 * 863.25, 16 * 158.00, 16 * 442.00, 0x1, 0x2, 0x4, 0x8e}
};

FI1236Ptr
Detect_FI1236(I2CBusPtr b, I2CSlaveAddr addr)
{
    FI1236Ptr f;
    I2CByte a;

    f = calloc(1, sizeof(FI1236Rec));
    if (f == NULL)
        return NULL;
    f->d.DevName = strdup("FI12xx Tuner");
    f->d.SlaveAddr = addr;
    f->d.pI2CBus = b;
    f->d.NextDev = NULL;
    f->d.StartTimeout = b->StartTimeout;
    f->d.BitTimeout = b->BitTimeout;
    f->d.AcknTimeout = b->AcknTimeout;
    f->d.ByteTimeout = b->ByteTimeout;
    f->type = TUNER_TYPE_FI1236;
    f->afc_timer_installed = FALSE;
    f->last_afc_hint = TUNER_OFF;
    f->video_if = 45.7812;

    if (!I2C_WriteRead(&(f->d), NULL, 0, &a, 1)) {
        free(f);
        return NULL;
    }
    FI1236_set_tuner_type(f, TUNER_TYPE_FI1236);
    if (!I2CDevInit(&(f->d))) {
        free(f);
        return NULL;
    }
    return f;
}

static void
MT2032_dump_parameters(FI1236Ptr f, MT2032_parameters * m)
{
    xf86DrvMsg(f->d.pI2CBus->scrnIndex, X_INFO,
               "MT2032: input f_rf=%g f_if1=%g f_if2=%g f_ref=%g f_ifbw=%g f_step=%g\n",
               m->f_rf, m->f_if1, m->f_if2, m->f_ref, m->f_ifbw, m->f_step);

    xf86DrvMsg(f->d.pI2CBus->scrnIndex, X_INFO,
               "MT2032: computed f_lo1=%g f_lo2=%g LO1I=%d LO2I=%d SEL=%d STEP=%d NUM=%d\n",
               m->f_lo1, m->f_lo2, m->LO1I, m->LO2I, m->SEL, m->STEP, m->NUM);
}

static void
MT2032_getid(FI1236Ptr f)
{
    CARD8 out[4];
    CARD8 in;

    in = 0x11;
    I2C_WriteRead(&(f->d), (I2CByte *) & in, 1, out, 4);
    xf86DrvMsg(f->d.pI2CBus->scrnIndex, X_INFO,
               "MT2032: Company code 0x%02x%02x, part code 0x%02x, revision code 0x%02x\n",
               out[0], out[1], out[2], out[3]);

}

/* might be buggy */
#if 0
static void
MT2032_shutdown(FI1236Ptr f)
{
    CARD8 data[10];

    data[0] = 0x00;             /* start with register 0x00 */
    data[1] = 0x1A;
    data[2] = 0x44;
    data[3] = 0x20;

    I2C_WriteRead(&(f->d), (I2CByte *) data, 4, NULL, 0);

    data[0] = 0x05;             /* now start with register 0x05 */
    data[1] = 0xD7;
    data[2] = 0x14;
    data[3] = 0x05;
    I2C_WriteRead(&(f->d), (I2CByte *) data, 4, NULL, 0);

    data[0] = 0x0B;             /* now start with register 0x05 */
    data[1] = 0x8F;
    data[2] = 0x07;
    data[3] = 0x43;
    I2C_WriteRead(&(f->d), (I2CByte *) data, 4, NULL, 0);

    usleep(15000);
}
#endif

static void MT2032_dump_status(FI1236Ptr f);

static void
MT2032_init(FI1236Ptr f)
{
    CARD8 data[10];
    CARD8 value;
    CARD8 xogc = 0x00;

    MT2032_getid(f);

    data[0] = 0x02;             /* start with register 0x02 */
    data[1] = 0xFF;
    data[2] = 0x0F;
    data[3] = 0x1F;

    I2C_WriteRead(&(f->d), (I2CByte *) data, 4, NULL, 0);

    data[0] = 0x06;             /* now start with register 0x06 */
    data[1] = 0xE4;
    data[2] = 0x8F;
    data[3] = 0xC3;
    data[4] = 0x4E;
    data[5] = 0xEC;
    I2C_WriteRead(&(f->d), (I2CByte *) data, 6, NULL, 0);

    data[0] = 0x0d;             /* now start with register 0x0d */
    data[1] = 0x32;
    I2C_WriteRead(&(f->d), (I2CByte *) data, 2, NULL, 0);

    while (1) {
        usleep(15000);          /* wait 15 milliseconds */

        data[0] = 0x0e;         /* register number 7, status */
        value = 0xFF;
        if (!I2C_WriteRead(&(f->d), (I2CByte *) data, 1, &value, 1))
            xf86DrvMsg(f->d.pI2CBus->scrnIndex, X_INFO,
                       "MT2032: failed to read XOK\n");
        xf86DrvMsg(f->d.pI2CBus->scrnIndex, X_INFO,
                   "MT2032: XOK=%d\n", value & 0x01);
        if (value & 1)
            break;

        data[0] = 0x07;
        if (!I2C_WriteRead(&(f->d), (I2CByte *) data, 1, &value, 1))
            xf86DrvMsg(f->d.pI2CBus->scrnIndex, X_INFO,
                       "MT2032: failed to read XOGC\n");

        xogc = value & 0x7;
        if (xogc == 4)
            break;              /* XOGC has reached 4.. stop */
        xogc--;
        xf86DrvMsg(f->d.pI2CBus->scrnIndex, X_INFO,
                   "MT2032: try XOGC=%d\n", xogc);
        usleep(15000);
        data[0] = 0x07;         /* register number 7, control byte 2 */
        data[1] = 0x08 | xogc;
        I2C_WriteRead(&(f->d), (I2CByte *) data, 2, NULL, 0);
    }
    f->xogc = xogc;
    /* wait before continuing */
    usleep(15000);              /* wait 50 milliseconds */
    MT2032_dump_status(f);
}

static int
MT2032_no_spur_in_band(MT2032_parameters * m)
{
    int n_max, n1, n2;
    double f_test;

    n_max = 5;
    n1 = 1;
    while (1) {
        n2 = -n1;
        f_test = n1 * (m->f_lo1 - m->f_lo2);
        while (1) {
            n2--;
            f_test = f_test - m->f_lo2;
            xf86DrvMsg(0, X_INFO,
                       "testing f_test=%g n1=%d n2=%d f_lo1=%g f_lo2=%g f_if2=%g\n",
                       f_test, n1, n2, m->f_lo1, m->f_lo2, m->f_if2);
            xf86DrvMsg(0, X_INFO, "d_f=%g f_ifbw=%g\n",
                       fabs(fabs(f_test) - m->f_if2), m->f_ifbw);
            if ((fabs(fabs(f_test) - m->f_if2) * 2.0) <= m->f_ifbw)
                return 0;
            if (n2 <= -n_max)
                break;
            /* this line in the manual is bogus. I say it is faster
               and more correct to go over all harmonics.. */
#if 0
            if (f_test < (m->f_lo2 - m->f_if2 - m->f_ifbw))
                break;
#endif
        }
        n1++;
        if (n1 >= n_max)
            return 1;
    }

}

static void
MT2032_calculate_register_settings(MT2032_parameters * m, double f_rf,
                                   double f_if1, double f_if2, double f_ref,
                                   double f_ifbw, double f_step)
{
    int n;

    m->f_rf = f_rf;
    m->f_if1 = f_if1;
    m->f_if2 = f_if2;
    m->f_ref = f_ref;
    m->f_ifbw = f_ifbw;
    m->f_step = f_step;

    m->f_lo1 = f_rf + f_if1;
    m->LO1I = lrint(m->f_lo1 / f_ref);
    m->f_lo1 = f_ref * m->LO1I;

    m->f_lo2 = m->f_lo1 - f_rf - f_if2;

    /* check for spurs */
    n = 1;
    while (n < 3) {
        if (MT2032_no_spur_in_band(m))
            break;

        if (m->f_lo1 < (f_rf + f_if1))
            m->LO1I += n;
        else
            m->LO1I -= n;

        m->f_lo1 = m->LO1I * f_ref;
        m->f_lo2 = m->f_lo1 - f_rf - f_if2;
        n++;
    }
    /* xf86DrvMsg(0, X_INFO, "MT2032: n=%d\n", n); */
    /* select VCO */

    /* m->f_lo1>1100.0 */
    if (m->f_lo1 < 1370.0)
        m->SEL = 4;
    else if (m->f_lo1 < 1530.0)
        m->SEL = 3;
    else if (m->f_lo1 < 1720.0)
        m->SEL = 2;
    else if (m->f_lo1 < 1890.0)
        m->SEL = 1;
    else                        /* m->f_lo1 < 1958.0 */
        m->SEL = 0;

    /* calculate the rest of the registers */
    m->LO2I = floor(m->f_lo2 / f_ref);
    m->STEP = floor(3780.0 * f_step / f_ref);
    m->NUM = floor(3780.0 * (m->f_lo2 / f_ref - m->LO2I));
    m->NUM = m->STEP * lrint((1.0 * m->NUM) / (1.0 * m->STEP));
}

static int
MT2032_wait_for_lock(FI1236Ptr f)
{
    int n;
    CARD8 data[10];
    CARD8 value;

    n = 12;
    while (1) {
        data[0] = 0x0e;         /* register number 7, status */
        I2C_WriteRead(&(f->d), (I2CByte *) data, 1, &value, 1);
        /* xf86DrvMsg(f->d.pI2CBus->scrnIndex, X_INFO,
           "MT2032: LO1LK=%d LO2LK=%d\n",
           (value & 0x04)>>2, (value & 0x02)>>1); */
        if ((value & 6) == 6)
            break;
        usleep(1500);
        n--;
        if (n < 0)
            break;
    }
    if (n < 0) {
        xf86DrvMsg(f->d.pI2CBus->scrnIndex, X_INFO,
                   "MT2032: failed to set frequency\n");
        return 0;
    }
    return 1;
}

static void
MT2032_implement_settings(FI1236Ptr f, MT2032_parameters * m)
{
    CARD8 data[10];
    CARD8 value;

    data[0] = 0x00;             /* start with register 0x00 */
    data[1] = (m->LO1I >> 3) - 1;
    data[2] = (m->SEL << 4) | (m->LO1I & 0x7);
    data[3] = 0x86;
    I2C_WriteRead(&(f->d), (I2CByte *) data, 4, NULL, 0);

    data[0] = 0x05;             /* start with register 0x05 */
    data[1] = ((m->LO2I & 0x7) << 5) | ((m->LO2I >> 3) - 1);
    if (m->f_rf < 400.0)
        data[2] = 0xe4;
    else
        data[2] = 0xf4;
    I2C_WriteRead(&(f->d), (I2CByte *) data, 3, NULL, 0);

    data[0] = 0x07;             /* register number 7, control byte 2 */
    I2C_WriteRead(&(f->d), (I2CByte *) data, 1, &value, 1);
    xf86DrvMsg(f->d.pI2CBus->scrnIndex, X_INFO,
               "MT2032: using XOGC=%d\n", (value & 0x07));
    data[1] = 8 | (value & 0x7);
    I2C_WriteRead(&(f->d), (I2CByte *) data, 2, NULL, 0);

    data[0] = 0x0b;             /* start with register 0x0b */
    data[1] = m->NUM & 0xff;
    data[2] = (1 << 7) | ((m->NUM >> 8) & 0x0f);
    I2C_WriteRead(&(f->d), (I2CByte *) data, 3, NULL, 0);

    MT2032_wait_for_lock(f);
}

static void
MT2032_optimize_VCO(FI1236Ptr f, MT2032_parameters * m)
{
    CARD8 data[10];
    CARD8 value;
    CARD8 TAD1;

    data[0] = 0x0f;             /* register number 7, status */
    I2C_WriteRead(&(f->d), (I2CByte *) data, 1, &value, 1);
    TAD1 = value & 0x07;
    xf86DrvMsg(f->d.pI2CBus->scrnIndex, X_INFO,
               "MT2032: TAD1=%d SEL=%d\n", TAD1, m->SEL);
    if (TAD1 < 2)
        return;
    if (TAD1 == 2) {
        if (m->SEL == 0)
            return;
        m->SEL--;
    }
    else {
        if (m->SEL >= 4)
            return;
        m->SEL++;
    }
    data[0] = 0x01;             /* start with register 1 */
    data[1] = (m->SEL << 4) | (m->LO1I & 0x7);
    I2C_WriteRead(&(f->d), (I2CByte *) data, 2, NULL, 0);

}

static int
FI1236_get_afc_hint(FI1236Ptr f)
{
    CARD8 out;
    CARD8 AFC;

    if ((f->type == TUNER_TYPE_FM1216ME) || (f->type == TUNER_TYPE_FI1236W)) {
        TDA9885Ptr t = (TDA9885Ptr) f->afc_source;

        if (t == NULL)
            return TUNER_OFF;

        tda9885_getstatus(t);
        tda9885_dumpstatus(t);
        AFC = t->afc_status & 0x0f;

        xf86DrvMsg(f->d.pI2CBus->scrnIndex, X_INFO,
                   "AFC: FI1236_get_afc_hint: %i\n", AFC);
        if (AFC == 0)
            return TUNER_TUNED;
        else if (AFC <= 0x07)
            return TUNER_JUST_BELOW;
        else if (AFC < 0x0f)
            return TUNER_JUST_ABOVE;
        else if (AFC == 0x0f)
            return TUNER_TUNED;
    }
    else {
        I2C_WriteRead(&(f->d), NULL, 0, &out, 1);
        AFC = out & 0x7;
        xf86DrvMsg(f->d.pI2CBus->scrnIndex, X_INFO,
                   "AFC: FI1236_get_afc_hint: %i\n", AFC);
        if (AFC == 2)
            return TUNER_TUNED;
        if (AFC == 3)
            return TUNER_JUST_BELOW;
        if (AFC == 1)
            return TUNER_JUST_ABOVE;
        return TUNER_OFF;
    }
    return TUNER_OFF;
}

static int
MT2032_get_afc_hint(FI1236Ptr f)
{
    CARD8 in;
    CARD8 out[2];
    CARD8 AFC;

    in = 0x0e;
    I2C_WriteRead(&(f->d), (I2CByte *) & in, 1, out, 2);
    AFC = (out[0] >> 4) & 0x7;
#if 0
    xf86DrvMsg(f->d.pI2CBus->scrnIndex, X_INFO, "AFC=%d TAD1=%d TAD2=%d\n",
               AFC, out[1] & 0x7, (out[1] >> 4) & 0x07);
#endif
    if (AFC == 2)
        return TUNER_TUNED;
    if (AFC == 3)
        return TUNER_JUST_BELOW;
    if (AFC == 1)
        return TUNER_JUST_ABOVE;
    return TUNER_OFF;
}

/* this function is for external use only */
int
TUNER_get_afc_hint(FI1236Ptr f)
{
    if (f->afc_timer_installed)
        return TUNER_STILL_TUNING;
    return f->last_afc_hint;
}

static void
MT2032_dump_status(FI1236Ptr f)
{
    CARD8 in;
    CARD8 out[2];
    CARD8 AFC;
    CARD8 LDONrb;
    CARD8 LO1LK, LO2LK, XOK;
    CARD8 TAD2, TAD1;

    in = 0x0e;
    I2C_WriteRead(&(f->d), (I2CByte *) & in, 1, out, 2);
    XOK = out[0] & 1;
    LO1LK = (out[0] >> 2) & 1;
    LO2LK = (out[0] >> 1) & 1;
    LDONrb = (out[0] >> 3) & 1;

    AFC = (out[0] >> 4) & 0x7;

    TAD1 = (out[1] & 0x7);
    TAD2 = (out[1] >> 4) & 0x7;

    xf86DrvMsg(f->d.pI2CBus->scrnIndex, X_INFO,
               "MT2032: status: XOK=%d LO1LK=%d LO2LK=%d LDONrb=%d AFC=%d TAD1=%d TAD2=%d\n",
               XOK, LO1LK, LO2LK, LDONrb, AFC, TAD1, TAD2);
    xf86DrvMsg(f->d.pI2CBus->scrnIndex, X_INFO,
               "MT2032: status: OSCILLATOR:%s PLL1:%s PLL2:%s\n",
               XOK ? "ok" : "off",
               LO1LK ? "locked" : "off", LO2LK ? "locked" : "off");

}

static void
MT2032_tune(FI1236Ptr f, double freq, double step)
{
    MT2032_parameters m;
    CARD8 data[10];
    int i;

    /* NTSC IF is 44mhz.. but 733/16=45.8125 and all TDAXXXX docs mention
       45.75, 39, 58.75 and 30. */
#if 0
    MT2032_calculate_register_settings(&m, freq, 1090.0, 45.125, 5.25, 6.0,
                                       step);
    MT2032_calculate_register_settings(&m, freq, 1090.0, 45.74, 5.25, 6.0,
                                       step);
#endif
    MT2032_calculate_register_settings(&m, freq, 1090.0, f->video_if, 5.25, 3.0,
                                       step);
    MT2032_dump_parameters(f, &m);
    MT2032_implement_settings(f, &m);
    /* MT2032_dump_parameters(f, &m); */
    for (i = 0; i < 3; i++) {
        MT2032_optimize_VCO(f, &m);
        if (MT2032_wait_for_lock(f)) {
            data[0] = 0x02;     /* LO Gain control register 0x02 */
            data[1] = 0x20;
            I2C_WriteRead(&(f->d), (I2CByte *) data, 2, NULL, 0);
            return;
        }
        data[0] = 0x07;
        data[1] = 0x88 | f->xogc;
        I2C_WriteRead(&(f->d), (I2CByte *) data, 2, NULL, 0);
        usleep(15000);
        data[1] = 0x08 | f->xogc;
        I2C_WriteRead(&(f->d), (I2CByte *) data, 2, NULL, 0);
    }
    xf86DrvMsg(f->d.pI2CBus->scrnIndex, X_INFO,
               "MT2032: failed to set frequency\n");
}

void
FI1236_set_tuner_type(FI1236Ptr f, int type)
{
    f->type = type;
    if (type >= NUM_TUNERS)
        type = NUM_TUNERS - 1;
    if (type < 0)
        type = 0;
    memcpy(&(f->parm), &(tuner_parms[type]), sizeof(FI1236_parameters));
    f->original_frequency = f->parm.min_freq;
    f->afc_delta = 0;
    if (type == TUNER_TYPE_MT2032) {
        MT2032_init(f);
        return;
    }
}

static CARD32
AFC_TimerCallback(OsTimerPtr timer, CARD32 time, pointer data)
{
    FI1236Ptr f = (FI1236Ptr) data;

    if (FI1236_AFC(f))
        return 150;
    else {
        f->afc_timer_installed = FALSE;
        f->afc_count = 0;
        return 0;
    }
}

void
FI1236_tune(FI1236Ptr f, CARD32 frequency)
{
    CARD16 divider;
    CARD8 data;

    if (frequency < f->parm.min_freq)
        frequency = f->parm.min_freq;
    if (frequency > f->parm.max_freq)
        frequency = f->parm.max_freq;

    divider = (f->parm.fcar + (CARD16) frequency) & 0x7fff;
    f->tuner_data.div1 = (CARD8) ((divider >> 8) & 0x7f);
    f->tuner_data.div2 = (CARD8) (divider & 0xff);
    f->tuner_data.control = f->parm.control;

    if (frequency < f->parm.threshold1)
        f->tuner_data.band = f->parm.band_low;
    else if (frequency < f->parm.threshold2)
        f->tuner_data.band = f->parm.band_mid;
    else
        f->tuner_data.band = f->parm.band_high;

    xf86DrvMsg(f->d.pI2CBus->scrnIndex, X_INFO,
               "Setting tuner band to %d\n", f->tuner_data.band);

    xf86DrvMsg(f->d.pI2CBus->scrnIndex, X_INFO,
               "Setting tuner frequency to %d\n", (int) frequency);

    if ((f->type == TUNER_TYPE_FM1216ME) || (f->type == TUNER_TYPE_FI1236W)) {
        f->tuner_data.aux = 0x20;
        I2C_WriteRead(&(f->d), (I2CByte *) & (f->tuner_data), 5, NULL, 0);
        I2C_WriteRead(&(f->d), NULL, 0, &data, 1);
        xf86DrvMsg(f->d.pI2CBus->scrnIndex, X_INFO, "Tuner status %x\n", data);
    }
    else
        I2C_WriteRead(&(f->d), (I2CByte *) & (f->tuner_data), 4, NULL, 0);
}

void
TUNER_set_frequency(FI1236Ptr f, CARD32 frequency)
{
    if (frequency < f->parm.min_freq)
        frequency = f->parm.min_freq;
    if (frequency > f->parm.max_freq)
        frequency = f->parm.max_freq;

    f->afc_delta = 0;
    f->original_frequency = frequency;

    if (f->type == TUNER_TYPE_MT2032)
        MT2032_tune(f, (1.0 * frequency) / 16.0, 0.0625);
    else
        FI1236_tune(f, frequency);

    if (!f->afc_timer_installed) {
        f->afc_timer_installed = TRUE;
/*      RegisterBlockAndWakeupHandlers(FI1236_BlockHandler, AFCWakeup, f); */
        TimerSet(NULL, 0, 300, AFC_TimerCallback, f);
    }

}

int
FI1236_AFC(FI1236Ptr f)
{
#if 0
    xf86DrvMsg(f->d.pI2CBus->scrnIndex, X_INFO,
               "AFC: f=%p f->count=%d f->original_frequency=%d f->afc_delta=%d\n",
               f, f->afc_count, f->original_frequency, f->afc_delta);
#endif
    f->afc_count++;
    if (f->type == TUNER_TYPE_MT2032) {
        f->last_afc_hint = MT2032_get_afc_hint(f);
        xf86DrvMsg(f->d.pI2CBus->scrnIndex, X_INFO,
                   "AFC: afc_hint=%d\n", f->last_afc_hint);
        if (f->last_afc_hint == TUNER_TUNED)
            return 0;
        if (f->afc_count > 3)
            f->last_afc_hint = TUNER_OFF;
        if (f->last_afc_hint == TUNER_OFF)
            f->afc_delta = 0;
        else
            f->afc_delta += f->last_afc_hint;

        xf86DrvMsg(f->d.pI2CBus->scrnIndex, X_INFO,
                   "AFC: Setting tuner frequency to %g\n",
                   (0.5 * (2 * f->original_frequency + f->afc_delta)) / 16.0);
        MT2032_tune(f,
                    (1.0 * f->original_frequency + 0.5 * f->afc_delta) / 16.0,
                    0.03125);
        if (f->last_afc_hint == TUNER_OFF)
            return 0;
        return 1;               /* call me again */
    }
    else {
        f->last_afc_hint = FI1236_get_afc_hint(f);
        if (f->last_afc_hint == TUNER_TUNED) {
            xf86DrvMsg(f->d.pI2CBus->scrnIndex, X_INFO, "AFC: TUNER_TUNNED\n");
            return 0;
        }
        if (f->afc_count > 3)
            f->last_afc_hint = TUNER_OFF;

        if (f->last_afc_hint == TUNER_OFF)
            f->afc_delta = 0;
        else
            f->afc_delta += f->last_afc_hint;

        xf86DrvMsg(f->d.pI2CBus->scrnIndex, X_INFO,
                   "AFC: Setting tuner frequency to %g\n",
                   (0.5 * (2 * f->original_frequency + f->afc_delta)) / 16.0);
        FI1236_tune(f, f->original_frequency + f->afc_delta);
        if (f->last_afc_hint == TUNER_OFF)
            return 0;
        return 1;               /* call me again */
    }
    return 0;                   /* done */
}

void
fi1236_dump_status(FI1236Ptr f)
{
    if (f->type == TUNER_TYPE_MT2032)
        MT2032_dump_status(f);
}
