#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include <string.h>
#include <unistd.h>

#include "xf86.h"
#include "xf86i2c.h"
#include "msp3430.h"
#include "i2c_def.h"

#define CONTROL         0x00
#define WR_DEM          0x10
#define RD_DEM          0x11
#define WR_DSP          0x12
#define RD_DSP          0x13

void InitMSP34xxG(MSP3430Ptr m);
void InitMSP34x5D(MSP3430Ptr m);
void CheckModeMSP34x5D(MSP3430Ptr m);
static const char *MSP_getProductName(CARD16 product_id);
void mpause(int milliseconds);

#define __MSPDEBUG__	0

#if __MSPDEBUG__ > 3

void MSPBeep(MSP3430Ptr m, CARD8 freq);

#define __MSPBEEP MSPBeep(m,0x14);

#else

#define __MSPBEEP
#endif

static void
SetMSP3430Control(MSP3430Ptr m, CARD8 RegAddress, CARD8 RegValueHigh,
                  CARD8 RegValueLow)
{
    I2CByte data[3];

    data[0] = RegAddress;
    data[1] = RegValueHigh;
    data[2] = RegValueLow;

    I2C_WriteRead(&(m->d), data, 3, NULL, 0);
}

static void
SetMSP3430Data(MSP3430Ptr m, CARD8 RegAddress, CARD8 RegSubAddressHigh,
               CARD8 RegSubAddressLow, CARD8 RegValueHigh, CARD8 RegValueLow)
{
    I2CByte data[5];

#ifdef MSP_DEBUG
    if (!m->registers_present[RegSubAddressLow]) {
        xf86DrvMsg(m->d.pI2CBus->scrnIndex, X_ERROR,
                   "Attempt to access non-existent register in MSP34xxX: 0x%02x 0x%02x 0x%02x <- 0x%02x 0x%02x\n",
                   RegAddress, RegSubAddressHigh, RegSubAddressLow,
                   RegValueHigh, RegValueLow);
    }
#endif

    data[0] = RegAddress;
    data[1] = RegSubAddressHigh;
    data[2] = RegSubAddressLow;
    data[3] = RegValueHigh;
    data[4] = RegValueLow;

    I2C_WriteRead(&(m->d), data, 5, NULL, 0);
}

static void
GetMSP3430Data(MSP3430Ptr m, CARD8 RegAddress, CARD8 RegSubAddressHigh,
               CARD8 RegSubAddressLow, CARD8 *RegValueHigh, CARD8 *RegValueLow)
{
    I2CByte send[3];
    I2CByte receive[2];

    send[0] = RegAddress;
    send[1] = RegSubAddressHigh;
    send[2] = RegSubAddressLow;

    I2C_WriteRead(&(m->d), send, 3, receive, 2);

    *RegValueHigh = receive[0];
    *RegValueLow = receive[1];
}

#if __MSPDEBUG__ > 2
static void
MSP3430DumpStatus(MSP3430Ptr m)
{
    CARD8 status_hi, status_lo;
    CARD8 subaddr, data[2];

    GetMSP3430Data(m, RD_DEM, 0x02, 0x00, &status_hi, &status_lo);
    xf86DrvMsg(m->d.pI2CBus->scrnIndex, X_INFO,
               "MSP34xx: SAP(8)=%d mono/NICAM(7)=%d stereo=%d %s O_1=%d O_0=%d 2nd car=%d 1st car=%d\n",
               status_hi & 1, (status_lo >> 7) & 1, (status_lo >> 6) & 1,
               (status_lo >> 5) ? ((status_hi >> 1) & 1 ? "bad NICAM reception"
                                   : "NICAM") : ((status_hi >> 1) & 1 ? "bogus"
                                                 : "ANALOG FM/AM"),
               (status_lo >> 4) & 1, (status_lo >> 3) & 1,
               !((status_lo >> 2) & 1), !((status_lo >> 1) & 1));

    GetMSP3430Data(m, RD_DEM, 0x00, 0x7E, &status_hi, &status_lo);
    xf86DrvMsg(m->d.pI2CBus->scrnIndex, X_INFO,
               "MSP34xx: standard result=0x%02x%02x\n", status_hi, status_lo);
    subaddr = 0x0;
    I2C_WriteRead(&(m->d), &subaddr, 1, data, 2);
    xf86DrvMsg(m->d.pI2CBus->scrnIndex, X_INFO, "MSP34xx: control=0x%02x%02x\n",
               data[1], data[0]);
}
#endif

/* wrapper */
void
InitMSP3430(MSP3430Ptr m)
{
#if __MSPDEBUG__ > 1
    xf86DrvMsg(m->d.pI2CBus->scrnIndex, X_INFO,
               "InitMSP3430(m->connector=%d, m->standard=%d, m->chip_family=%d)\n",
               m->connector, m->standard, m->chip_family);
#endif
    switch (m->chip_family) {
    case MSPFAMILY_34x0G:
        InitMSP34xxG(m);
        break;
    case MSPFAMILY_34x5G:
        InitMSP34xxG(m);
        break;
    case MSPFAMILY_34x5D:
        InitMSP34x5D(m);
        break;
    }
}

/*-----------------------------------------------------------------
| common functions for all MSP34xx chips 
|----------------------------------------------------------------*/

MSP3430Ptr
DetectMSP3430(I2CBusPtr b, I2CSlaveAddr addr)
{
    MSP3430Ptr m;
    I2CByte a;
    CARD8 hardware_version, major_revision, product_code, rom_version;
    Bool supported;

    m = calloc(1, sizeof(MSP3430Rec));
    if (m == NULL)
        return NULL;
    m->d.DevName = strdup("MSP34xx");
    m->d.SlaveAddr = addr;
    m->d.pI2CBus = b;
    m->d.NextDev = NULL;
    m->d.StartTimeout = b->StartTimeout;
    m->d.BitTimeout = b->BitTimeout;
    m->d.AcknTimeout = b->AcknTimeout;
    m->d.ByteTimeout = b->ByteTimeout;

    if (!I2C_WriteRead(&(m->d), NULL, 0, &a, 1)) {
        free(m->d.DevName);
        free(m);
        return NULL;
    }

    m->standard = MSP3430_NTSC;
    m->connector = MSP3430_CONNECTOR_1;
    m->mode = MSPMODE_STEREO_A; /*stereo or chanel A if avail. */
    m->c_format = MSPFORMAT_UNKNOWN;
    m->c_standard = MSPSTANDARD_UNKNOWN;
    m->c_matrix = m->c_fmmatrix = m->c_source = 0;
    m->volume = 0;
    m->recheck = FALSE;

    GetMSP3430Data(m, RD_DSP, 0x00, 0x1E, &hardware_version, &major_revision);
    GetMSP3430Data(m, RD_DSP, 0x00, 0x1F, &product_code, &rom_version);
    m->hardware_version = hardware_version;
    m->major_revision = major_revision;
    m->product_code = product_code;
    m->rom_version = rom_version;

    m->chip_id = ((major_revision << 8) | product_code);

    supported = FALSE;
    switch (major_revision) {
    case 4:                    /* 34xxD */
        switch (product_code) {
        case 0x05:             /* 3405D */
        case 0x0A:             /* 3410D */
        case 0x0F:             /* 3415D */
            m->chip_family = MSPFAMILY_34x5D;
            m->recheck = TRUE;
            supported = TRUE;
            break;
        default:
            m->chip_family = MSPFAMILY_34x0D;
        }
        break;
    case 7:                    /* 34xxG */
        switch (product_code) {
        case 0x00:
        case 0x0A:
        case 0x1E:
        case 0x28:
        case 0x32:
            m->chip_family = MSPFAMILY_34x0G;
            supported = TRUE;
            break;
        case 0x0f:
        case 0x19:
        case 0x2d:
        case 0x37:
        case 0x41:
            m->chip_family = MSPFAMILY_34x5G;
            supported = TRUE;
#ifdef MSP_DEBUG
            memset(m->registers_present, 0, 256);
#define A(num) m->registers_present[(num)]=1;
#define B(num1, num2) memset(&(m->registers_present[num1]), 1, num2-num1);
            A(0x20)
                A(0x30)
                A(0x40)
                A(0x00)
                B(0x01, 0x08)
                B(0x0B, 0x0E)
                A(0x10)
                B(0x12, 0x14)
                A(0x16)
                A(0x29)
#undef B
#undef A
#endif
                break;
        default:
            m->chip_family = MSPFAMILY_UNKNOWN;
        }
        break;
    default:
        m->chip_family = MSPFAMILY_UNKNOWN;
    }

    xf86DrvMsg(m->d.pI2CBus->scrnIndex, X_INFO,
               "Found %s%s, rom version 0x%02x, chip_id=0x%04x\n",
               MSP_getProductName(m->chip_id),
               supported ? "" : " (unsupported)", rom_version, m->chip_id);

    if (!supported) {
        free(m->d.DevName);
        free(m);
        return NULL;
    }
    if (!I2CDevInit(&(m->d))) {
        free(m->d.DevName);
        free(m);
        return NULL;
    }

    return m;
}

void
ResetMSP3430(MSP3430Ptr m)
{
    /* Reset the MSP3430 */
    SetMSP3430Control(m, 0x00, 0x80, 0x00);
    /* Set it back to normal operation */
    SetMSP3430Control(m, 0x00, 0x00, 0x00);

    m->c_format = MSPFORMAT_UNKNOWN;
    m->c_standard = MSPSTANDARD_UNKNOWN;
    m->c_matrix = m->c_fmmatrix = m->c_source = 0;
    m->volume = 0;
}

void
MSP3430SetVolume(MSP3430Ptr m, CARD8 value)
{
    CARD8 result;

#if 0
    CARD8 old_volume;

    GetMSP3430Data(m, RD_DSP, 0x00, 0x00, &old_volume, &result);
    xf86DrvMsg(m->d.pI2CBus->scrnIndex, X_INFO, "MSP3430 result 0x%02x\n",
               result);
#endif
    /* save an extra Get call */
    result = 0;

    SetMSP3430Data(m, WR_DSP, 0x00, 0x00, value, result);

    SetMSP3430Data(m, WR_DSP, 0x00, 0x07, value, 0);
    m->volume = value;

#if __MSPDEBUG__ > 2
    MSP3430DumpStatus(m);
    __MSPBEEP GetMSP3430Data(m, RD_DSP, 0x00, 0x00, &old_volume, &result);
    xf86DrvMsg(m->d.pI2CBus->scrnIndex, X_INFO, "MSP3430 volume 0x%02x\n",
               value);
#endif
}

void
MSP3430SetSAP(MSP3430Ptr m, int mode)
{
    xf86DrvMsg(m->d.pI2CBus->scrnIndex, X_INFO,
               "Put actual code to change SAP here\n");

    SetMSP3430Data(m, WR_DSP, 0x00, 0x08, mode & 0xff, 0x20);
}

#if 0
void
MSP3430SetSource(MSP3430Ptr m, CARD8 value)
{
    /* Write to DSP, register 0x0008, (loudspeaker channel source/matrix) */
    /* This sets the source to the TV tuner, for stereo operation */
    SetMSP3430Data(m, WR_DSP, 0x00, 0x08, value, 0x20);
}
#endif

static const char *
MSP_getProductName(CARD16 product_id)
{
    switch (product_id) {
    case 0x0400:
        return "MSP3400D";
    case 0x040a:
        return "MSP3410D";
    case 0x0405:
        return "MSP3405D";
    case 0x040f:
        return "MSP3415D";
    case 0x0700:
        return "MSP3400G";
    case 0x070a:
        return "MSP3410G";
    case 0x071e:
        return "MSP3430G";
    case 0x0728:
        return "MSP3440G";
    case 0x0732:
        return "MSP3450G";
    case 0x070f:
        return "MSP3415G";
    case 0x0719:
        return "MSP3425G";
    case 0x072d:
        return "MSP3445G";
    case 0x0737:
        return "MSP3455G";
    case 0x0741:
        return "MSP3465G";
    }
    return "MSP - unknown type";
}

#if __MSPDEBUG__ > 2
/*puts beep in MSP output
    freq = 0x01 - 16Hz ... 0x40 - 1kHz ... 0xff - 4kHz  
*/
void
MSPBeep(MSP3430Ptr m, CARD8 freq)
{
    SetMSP3430Data(m, WR_DSP, 0x00, freq, 0x7f, 0x40);
    mpause(100);
    SetMSP3430Data(m, WR_DSP, 0x00, 0x14, 0x00, 0x00);
}
#endif

void
mpause(int milliseconds)
{
    int i, m;

    m = milliseconds / 20;
    for (i = 0; i < m; i++)
        usleep(20000);
}

/*-----------------------------------------------------------------
| specific functions for all MSP34xxG chips 
|----------------------------------------------------------------*/

void
InitMSP34xxG(MSP3430Ptr m)
{

#if __MSPDEBUG__ > 1
    xf86DrvMsg(m->d.pI2CBus->scrnIndex, X_INFO,
               "InitMSP34xxG(m->connector=%d, m->standard=%d, m->chip_family=%d)\n",
               m->connector, m->standard, m->chip_family);
#endif
    /* Reset MSP3430 */
    SetMSP3430Control(m, 0x00, 0x80, 0x00);
    /* Set it back to normal operation */
    SetMSP3430Control(m, 0x00, 0x00, 0x00);

    /*set MODUS register */
    /* bits: 0 - automatic sound detection */
    /*       1 - enable STATUS change */
    /*       12 - detect 6.5 Mhz carrier as D/K1, D/K2 or D/K NICAM  (does not seem to work ) */
    /*       13 - detect 4.5 Mhz carrier as BTSC */
    if ((m->standard & 0xff) == MSP3430_PAL) {
        SetMSP3430Data(m, WR_DEM, 0x00, 0x30, 0x30, 0x03 | 0x08);       /* make O_ pins tristate */
        /* PAL standard */
        SetMSP3430Data(m, WR_DEM, 0x00, 0x20, 0x00, 0x01);      /* possibly wrong */
    }
    else {
        SetMSP3430Data(m, WR_DEM, 0x00, 0x30, 0x20, 0x03 | 0x08);
        /* standard selection is M-BTSC-Stereo */
        SetMSP3430Data(m, WR_DEM, 0x00, 0x20, 0x00, 0x20);
    }

    switch (m->connector) {
    case MSP3430_CONNECTOR_1:
        SetMSP3430Data(m, WR_DSP, 0x00, 0x08, 0x03, 0x20);
        break;
    case MSP3430_CONNECTOR_2:
        /* this has not been checked yet.. could be bogus */
        /* SCART Input Prescale: 0 dB gain */
        SetMSP3430Data(m, WR_DSP, 0x00, 0x0d, 0x19, 0x00);
        SetMSP3430Data(m, WR_DSP, 0x00, 0x08, 0x02, 0x20);
        break;
    case MSP3430_CONNECTOR_3:
    default:
        /* SCART Input Prescale: 0 dB gain */
        SetMSP3430Data(m, WR_DSP, 0x00, 0x0d, 0x19, 0x00);

        SetMSP3430Data(m, WR_DSP, 0x00, 0x08, 0x02, 0x20);
        break;
    }

    switch (m->standard) {
    case MSP3430_PAL:
        SetMSP3430Data(m, WR_DSP, 0x00, 0x0e, 0x24, 0x03);
        SetMSP3430Data(m, WR_DSP, 0x00, 0x10, 0x00, 0x5a);
        SetMSP3430Data(m, WR_DEM, 0x00, 0x20, 0x00, 0x03);
        /* Set volume to FAST_MUTE. */
        SetMSP3430Data(m, WR_DSP, 0x00, 0x00, 0xFF, 0x00);
        break;
    case MSP3430_PAL_DK1:
        SetMSP3430Data(m, WR_DSP, 0x00, 0x0e, 0x24, 0x03);
        SetMSP3430Data(m, WR_DSP, 0x00, 0x10, 0x00, 0x5a);
        SetMSP3430Data(m, WR_DEM, 0x00, 0x20, 0x00, 0x04);
        /* Set volume to FAST_MUTE. */
        SetMSP3430Data(m, WR_DSP, 0x00, 0x00, 0xFF, 0x00);
        break;
    case MSP3430_SECAM:        /* is this right ? */
    case MSP3430_NTSC:
        /* Write to DSP, register 0x000E, (prescale FM/FM matrix) */
        SetMSP3430Data(m, WR_DSP, 0x00, 0x0e, 0x24, 0x03);

        /* Set volume to FAST_MUTE. */
        SetMSP3430Data(m, WR_DSP, 0x00, 0x00, 0xFF, 0x00);
        break;
    }

}

/*-----------------------------------------------------------------
| specific functions for all MSP34x5D chips 
|----------------------------------------------------------------*/

void
InitMSP34x5D(MSP3430Ptr m)
{
    int count;
    CARD8 high, low;
    CARD16 result, standard;
    CARD16 peak;

    if (m->c_format == MSPFORMAT_UNKNOWN)
        ResetMSP3430(m);
    else {
        /*mute volume */
        SetMSP3430Data(m, WR_DSP, 0x00, 0x00, 0x00, 0x00);
    }

    switch (m->connector) {
    case MSP3430_CONNECTOR_2:
    case MSP3430_CONNECTOR_3:
        if (m->c_format != MSPFORMAT_SCART) {
            /* SCART Input Prescale: 0 dB gain */
            SetMSP3430Data(m, WR_DSP, 0x00, 0x0d, 0x19, 0x00);
            /* this has not been checked yet.. could be bogus */
            m->c_format = MSPFORMAT_SCART;      /*stereo */
        }
        break;
    case MSP3430_CONNECTOR_1:
    default:

        switch (m->standard & 0x00ff) {
        case MSP3430_PAL:
            switch (m->standard) {
            case MSP3430_PAL_DK1:
                standard = MSPSTANDARD_FM_DK1;
                break;
/*			case MSP3430_PAL_DK2:
			    standard=MSPSTANDARD_FM_DK2;
			    break;
			case MSP3430_PAL_BG:
			may be FM stereo (Germany) or FM NICAM (Scandinavia,spain)
			    standard=MSPSTANDARD_AUTO;
			    break;
*/
            default:
                standard = MSPSTANDARD_AUTO;
            }
            break;
        case MSP3430_SECAM:
            standard = MSPSTANDARD_AUTO;
        case MSP3430_NTSC:
            /* Only MSP34x5 supported format - Korean NTSC-M */
            standard = MSPSTANDARD_FM_M;
        default:
            standard = MSPSTANDARD_AUTO;
        }

        /*no NICAM support in MSP3410D - force to autodetect */
        if ((m->chip_id == 0x405) && (standard >= MSPSTANDARD_NICAM_BG))
            standard = MSPSTANDARD_AUTO;

        if (m->c_standard != standard) {

            SetMSP3430Data(m, WR_DEM, 0x00, 0x20, standard >> 8,
                           standard & 0xFF);
            if (standard == MSPSTANDARD_AUTO) {
                count = 50;     /* time shouldn't exceed 1s, just in case */
                do {
                    usleep(20000);
                    GetMSP3430Data(m, RD_DEM, 0x00, 0x7e, &high, &low);
                    result = (high << 8) | low;
                    --count;
                } while (result > 0x07ff && count > 0);

                if ((result > MSPSTANDARD_AUTO))
                    standard = result;
                else
                    standard = MSPSTANDARD_UNKNOWN;
#if __MSPDEBUG__ > 1
                xf86DrvMsg(m->d.pI2CBus->scrnIndex, X_INFO,
                           "Detected audio standard: %d\n", result);
#endif
                /* result = MSPSTANDARD_NICAM_L can be one of:
                   SECAM_L - MSPSTANDARD_NICAM_L
                   D/K1 - MSPSTANDARD_FM_DK1
                   D/K2 - MSPSTANDARD_FM_DK2
                   D/K-NICAM - MSPSTANDARD_NICAM_DK */
                if (standard == MSPSTANDARD_NICAM_L) {
                    if ((m->standard & 0x00ff) == MSP3430_PAL) {
                        /* force PAL D/K  */
                        standard = MSPSTANDARD_FM_DK1;
                        SetMSP3430Data(m, WR_DEM, 0x00, 0x20, standard >> 8,
                                       standard & 0xFF);
#if __MSPDEBUG__ > 1
                        xf86DrvMsg(m->d.pI2CBus->scrnIndex, X_INFO,
                                   "Detected 6.5MHz carrier - forced to D/K1 !!!\n");
#endif
                    }
                }
            }
            m->c_standard = standard;
        }                       /*end - standard changed */
        else {
            if (standard < MSPSTANDARD_NICAM_BG) {
                /* get old value of ident. mode register */
                GetMSP3430Data(m, RD_DSP, 0x00, 0x15, &high, &low);
                /* reset Ident-Filter */
                SetMSP3430Data(m, WR_DSP, 0x00, 0x14, 0x00, 0x3F);
                /* put back old value to ident. mode register */
                SetMSP3430Data(m, WR_DSP, 0x00, 0x14, 0x00, low);
            }
        }

        if (standard <= MSPSTANDARD_AUTO) {
            m->c_format = MSPFORMAT_1xFM;
        }
        else if (standard < MSPSTANDARD_NICAM_BG) {
            /* set FM prescale */
            SetMSP3430Data(m, WR_DSP, 0x00, 0x0e, 0x30, 0);
            /* set FM deemphasis */
            SetMSP3430Data(m, WR_DSP, 0x00, 0x0f,
                           ((standard == MSPSTANDARD_FM_M) ? 0 : 1), 0);

            /* check if FM2 carrier is present */
            /*turn off FM DC Notch */
            SetMSP3430Data(m, WR_DSP, 0x00, 0x17, 0x00, 0x3f);
            /*matrix source for Quasi-Peak Detector - stereo: ch2->L ch1->R */
            SetMSP3430Data(m, WR_DSP, 0x00, 0x0c, 0x00, 0x20);

            mpause(250);
            GetMSP3430Data(m, RD_DSP, 0x00, 0x1A, &high, &low);
            peak = (high << 8) | low;
#if __MSPDEBUG__ > 1
            xf86DrvMsg(m->d.pI2CBus->scrnIndex, X_INFO,
                       "Second carrier Quasi-Peak detection: %d\n", peak);
#endif
            /*turn on FM DC Notch */
            SetMSP3430Data(m, WR_DSP, 0x00, 0x17, 0x00, 0x00);

            if (peak < 5) {
                /* if second carrier not detected - only mono from first carrier */
                m->c_format = MSPFORMAT_1xFM;
            }
            else {
                m->c_format = MSPFORMAT_2xFM;
                /*start of FM identification process - FM_WAIT
                   wait at least 0.5s - used 1s - gives beter resolution */
                mpause(1000);
            }
        }
        else {
            if (standard == MSPSTANDARD_NICAM_L) {
                m->c_format = MSPFORMAT_NICAM_AM;
                /* set AM prescale */
                SetMSP3430Data(m, WR_DSP, 0x00, 0x0e, 0x7C, 0);
            }
            else {
                m->c_format = MSPFORMAT_NICAM_FM;
                /* set FM prescale */
                SetMSP3430Data(m, WR_DSP, 0x00, 0x0e, 0x30, 0);
            }
            /* set FM deemphasis */
            SetMSP3430Data(m, WR_DSP, 0x00, 0x0f, 0x00, 0);
            /* set NICAM prescale to 0dB */
            SetMSP3430Data(m, WR_DSP, 0x00, 0x10, 0x20, 0);
        }

        break;
    }                           /*end - case conector */

    CheckModeMSP34x5D(m);

    /* Set volume to FAST_MUTE. */
    /*SetMSP3430Data(m, WR_DSP, 0x00, 0x00, 0xFF, 0x00); */
    /*set volume */
    MSP3430SetVolume(m, m->volume);

__MSPBEEP}                      /* EnableMSP34x5D ()... */

void
CheckModeMSP34x5D(MSP3430Ptr m)
{
    const char stereo_on = 25;
    const char stereo_off = 20;
    const char dual_on = -stereo_on;
    const char dual_off = -stereo_off;
    char detect;
    CARD8 matrix, fmmatrix, source, high, low;

    fmmatrix = 0;               /*no matrix */
    source = 0;
    /*FM*/ switch (m->c_format) {
    case MSPFORMAT_NICAM_FM:
    case MSPFORMAT_NICAM_AM:
    case MSPFORMAT_SCART:
        source = ((m->c_format == MSPFORMAT_SCART) ? 2 : 1);
        switch (m->mode) {
        case MSPMODE_MONO:
            matrix = 0x30;
            /*MONO*/ break;
        case MSPMODE_A:
            matrix = 0x00;
            /*A*/ break;
        case MSPMODE_B:
            matrix = 0x10;
            /*B*/ break;
        default:
            matrix = 0x20;
            /*STEREO*/ break;
        }
        break;
    default:
    case MSPFORMAT_1xFM:
        matrix = 0x00;
        /*A*/ break;
    case MSPFORMAT_2xFM:
        switch (m->mode) {
        case MSPMODE_MONO:
            matrix = 0x30;
            /*MONO*/ break;
        case MSPMODE_STEREO:
            matrix = 0x20;
            /*STEREO*/ fmmatrix = ((m->c_standard == MSPSTANDARD_FM_M) ? 2 : 1);
            break;
        case MSPMODE_AB:
            matrix = 0x20;
            /*STEREO*/ break;
        case MSPMODE_A:
            matrix = 0x00;
            /*A*/ break;
        case MSPMODE_B:
            matrix = 0x10;
            /*B*/ break;
        default:
            /*FM_IDENT_CHECK */
            GetMSP3430Data(m, RD_DSP, 0x00, 0x18, &high, &low);
            detect = (char) high;
#if __MSPDEBUG__ > 1
            xf86DrvMsg(m->d.pI2CBus->scrnIndex, X_INFO,
                       "Stereo Detection Register: %d\n", detect);
#endif
            if (detect >=
                ((m->c_mode == MSPMODE_STEREO) ? stereo_off : stereo_on)) {
                m->c_mode = MSPMODE_STEREO;
                matrix = 0x20;
                /*STEREO*/
                    fmmatrix = ((m->c_standard == MSPSTANDARD_FM_M) ? 2 : 1);
            }
            else if (detect <= ((m->c_mode == MSPMODE_AB) ? dual_off : dual_on)) {
                m->c_mode = MSPMODE_AB;
                switch (m->mode) {
                case MSPMODE_STEREO_AB:
                    matrix = 0x20;
                    break;
                case MSPMODE_STEREO_B:
                    matrix = 0x10;
                    break;
                default:
                case MSPMODE_A:
                    matrix = 0x00;
                    break;
                }
            }
            else {
                m->c_mode = MSPMODE_MONO;
                matrix = 0x30;
            /*MONO*/}
            break;
        }                       /* end - case mode */
        break;
    }

    if (m->c_fmmatrix != fmmatrix) {
        GetMSP3430Data(m, RD_DSP, 0x00, 0x0e, &high, &low);
        SetMSP3430Data(m, WR_DSP, 0x00, 0x0e, high, fmmatrix);
        m->c_fmmatrix = fmmatrix;
    }

    if ((m->c_matrix != matrix) || (m->c_source != source)) {
        /*set chanel source and matrix for loudspeaker */
        SetMSP3430Data(m, WR_DSP, 0x00, 0x08, source, matrix);

        m->c_matrix = matrix;
        m->c_source = source;
    }

    if (((m->c_format) & 0xF0) == MSPFORMAT_NICAM)
        SetMSP3430Data(m, WR_DEM, 0x00, 0x21, 0, 1);

#if __MSPDEBUG__ > 0
    char *msg;

    switch (matrix) {
    case 0x30:
        /*MONO*/ msg = "MONO";
        break;
    case 0x00:
        /*LEFT*/ msg = "MONO/CHANNEL_1";
        break;
    case 0x10:
        /*RIGHT*/ msg = "MONO/CHANNEL_2";
        break;
    case 0x20:
        /*LEFT*/ msg = "STEREO";
        break;
    default:
        msg = "unknown";
        break;
    }
    xf86DrvMsg(m->d.pI2CBus->scrnIndex, X_INFO, "Audio mode set to: %s\n", msg);
#endif
}
