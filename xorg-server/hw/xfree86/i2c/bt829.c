/* TODO: clean up/fix CC code */

#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "xf86.h"
#include "xf86i2c.h"
#include "bt829.h"
#include "i2c_def.h"

/* Changing the following settings (especially VCROP) may */
/* require modifying code that calls this driver.         */
#define HCROP	0               /* amount to crop from the left and right edges */
#define VCROP	0               /* amount to crop from the top and bottom edges */

#define BTVERSION	(bt->id>>4)

#define H(X)		( ((X)>>8) & 0xFF )
#define L(X)		( (X) & 0xFF )

#define LIMIT(X,A,B)	(((X)<(A)) ? (A) : ((X)>(B)) ? (B) : (X) )

/* Bt829 family chip ID's */
#define BT815	0x02
#define BT817	0x06
#define BT819	0x07
#define BT827	0x0C
#define BT829	0x0E

/* Bt829 registers */
#define STATUS		0x00    /* Device Status */
#define IFORM		0x01    /* Input Format */
#define TDEC		0x02    /* Temporal Decimation */
#define CROP		0x03    /* MSB Cropping */
#define VDELAY_LO	0x04    /* Vertical Delay */
#define VACTIVE_LO	0x05    /* Vertical Active */
#define HDELAY_LO	0x06    /* Horizontal Delay */
#define HACTIVE_LO	0x07    /* Horizontal Active */
#define HSCALE_HI	0x08    /* Horizontal Scaling */
#define HSCALE_LO	0x09    /* Horizontal Scaling */
#define BRIGHT		0x0A    /* Brightness Control */
#define CONTROL		0x0B    /* Miscellaneous Control */
#define CONTRAST_LO	0x0C    /* Luma Gain (Contrast) */
#define SAT_U_LO	0x0D    /* Chroma (U) Gain (Saturation) */
#define SAT_V_LO	0x0E    /* Chroma (V) Gain (Saturation) */
#define HUE		0x0F    /* Hue Control */
#define SCLOOP		0x10    /* SC Loop Control */
#define WC_UP		0x11    /* White Crush Up Count */
#define OFORM		0x12    /* Output Format */
#define VSCALE_HI	0x13    /* Vertical Scaling */
#define VSCALE_LO	0x14    /* Vertical Scaling */
#define TEST		0x15    /* Test Control */
#define VPOLE		0x16    /* Video Timing Polarity */
#define IDCODE		0x17    /* ID Code */
#define ADELAY		0x18    /* AGC Delay */
#define BDELAY		0x19    /* Burst Gate Delay */
#define ADC		0x1A    /* ADC Interface */
#define VTC		0x1B    /* Video Timing Control */
#define CC_STATUS	0x1C    /* Extended Data Services/Closed Capt Status */
#define CC_DATA		0x1D    /* Extended Data Services/Closed Capt Data */
#define WC_DN		0x1E    /* White Crush Down Count */
#define SRESET		0x1F    /* Software Reset */
#define P_IO		0x3F    /* Programmable I/O */

static CARD8
btread(BT829Ptr bt, CARD8 reg)
{
    CARD8 v;

    I2C_WriteRead(&(bt->d), &reg, 1, &v, 1);

    return v;
}

static void
btwrite(BT829Ptr bt, CARD8 reg, CARD8 val)
{
    CARD8 data[2];

    data[0] = reg;
    data[1] = val;
    I2C_WriteRead(&(bt->d), data, 2, NULL, 0);
}

/*
 * Register access
 */
static void
btwrite_status(BT829Ptr bt)
{                               /* STATUS */
    btwrite(bt, STATUS, 0x00);  /* clear */
}

static void
btwrite_iform(BT829Ptr bt)
{                               /* IFORM */
    int xtsel;

    switch (bt->format) {
    case BT829_NTSC:
    case BT829_NTSC_JAPAN:
    case BT829_PAL_M:
    case BT829_PAL_N_COMB:     /* gatos says xtsel = 2 */
        xtsel = 1;
        break;
    case BT829_PAL:
    case BT829_PAL_N:
    case BT829_SECAM:
        xtsel = 2;
        break;
    default:                   /* shouldn't get here */
        xtsel = 3;              /* hardware default */
        break;
    }

    btwrite(bt, IFORM, (bt->mux << 5) | (xtsel << 3) | bt->format);
}

static void
btwrite_tdec(BT829Ptr bt)
{                               /* TDEC */
    /* use default */
}

static void
btwrite_crop(BT829Ptr bt)
{                               /* CROP */
    btwrite(bt, CROP, (H(bt->vdelay) << 6) | (H(bt->vactive) << 4) |
            (H(bt->hdelay) << 2) | H(bt->width));
}

static void
btwrite_vdelay_lo(BT829Ptr bt)
{                               /* VDELAY_LO */
    btwrite(bt, VDELAY_LO, L(bt->vdelay));
}

static void
btwrite_vactive_lo(BT829Ptr bt)
{                               /* VACTIVE_LO */
    btwrite(bt, VACTIVE_LO, L(bt->vactive));
}

static void
btwrite_hdelay_lo(BT829Ptr bt)
{                               /* HDELAY_LO */
    btwrite(bt, HDELAY_LO, L(bt->hdelay));
}

static void
btwrite_hactive_lo(BT829Ptr bt)
{                               /* HACTIVE_LO */
    btwrite(bt, HACTIVE_LO, L(bt->width));
}

static void
btwrite_hscale_hi(BT829Ptr bt)
{                               /* HSCALE_HI */
    btwrite(bt, HSCALE_HI, H(bt->hscale));
}

static void
btwrite_hscale_lo(BT829Ptr bt)
{                               /* HSCALE_LO */
    btwrite(bt, HSCALE_LO, L(bt->hscale));
}

static void
btwrite_bright(BT829Ptr bt)
{                               /* BRIGHT */
    btwrite(bt, BRIGHT, bt->brightness);
}

static void
btwrite_control(BT829Ptr bt)
{                               /* CONTROL */
    int ldec;

    /* The data sheet says ldec should always be 0 for SECAM */
    /* but the picture quality is better with ldec = 1       */
    ldec = (bt->width > 360);   /* gatos says 384 */

    btwrite(bt, CONTROL, ((bt->mux == bt->svideo_mux) ? 0xC0 : 0x00) |  /* LNOTCH and COMP */
            (ldec << 5) | (H(bt->contrast) << 2) | (H(bt->sat_u) << 1) | H(bt->
                                                                           sat_v));
}

static void
btwrite_contrast_lo(BT829Ptr bt)
{                               /* CONTRAST_LO */
    btwrite(bt, CONTRAST_LO, L(bt->contrast));
}

static void
btwrite_sat_u_lo(BT829Ptr bt)
{                               /* SAT_U_LO */
    btwrite(bt, SAT_U_LO, L(bt->sat_u));
}

static void
btwrite_sat_v_lo(BT829Ptr bt)
{                               /* SAT_V_LO */
    btwrite(bt, SAT_V_LO, L(bt->sat_v));
}

static void
btwrite_hue(BT829Ptr bt)
{                               /* HUE */
    btwrite(bt, HUE, bt->hue);
}

static void
btwrite_scloop(BT829Ptr bt)
{                               /* SCLOOP */
    if (BTVERSION >= BT827) {
        btwrite(bt, SCLOOP, (bt->format == BT829_SECAM) ? 0x10 : 0x00   /* QCIF or AUTO */
            );
    }
}

static void
btwrite_wc_up(BT829Ptr bt)
{                               /* WC_UP */
    if (BTVERSION >= BT827) {
        /* use default */
    }
}

static void
btwrite_oform(BT829Ptr bt)
{                               /* OFORM */
    btwrite(bt, OFORM, (bt->code << 3) | (bt->len << 2) | 0x02  /* RANGE = 0, CORE = 0, VBI_FRAME = 0, OES = 2 (default) */
        );
}

static void
btwrite_vscale_hi(BT829Ptr bt)
{                               /* VSCALE_HI */
    btwrite(bt, VSCALE_HI, H(bt->vscale) | 0x60 /* YCOMB = 0, COMB = 1, INT = 1 (default) */
        );
}

static void
btwrite_vscale_lo(BT829Ptr bt)
{                               /* VSCALE_LO */
    btwrite(bt, VSCALE_LO, L(bt->vscale));
}

/* TEST should not be written to */

static void
btwrite_vpole(BT829Ptr bt)
{                               /* VPOLE */
    btwrite(bt, VPOLE, (bt->out_en << 7));
}

/* IDCODE is read only */

static void
btwrite_adelay(BT829Ptr bt)
{                               /* ADELAY */
    switch (bt->format) {
    case BT829_NTSC:
    case BT829_NTSC_JAPAN:
    case BT829_PAL_M:
        btwrite(bt, ADELAY, 104);
        break;
    case BT829_PAL:
    case BT829_PAL_N:
    case BT829_SECAM:
    case BT829_PAL_N_COMB:
        btwrite(bt, ADELAY, 127);
        break;
    default:                   /* shouldn't get here */
        btwrite(bt, ADELAY, 104);       /* hardware default */
        break;
    }
}

static void
btwrite_bdelay(BT829Ptr bt)
{                               /* BDELAY */
    switch (bt->format) {
    case BT829_NTSC:
    case BT829_NTSC_JAPAN:
    case BT829_PAL_M:
        btwrite(bt, BDELAY, 93);
        break;
    case BT829_PAL:
    case BT829_PAL_N:
    case BT829_PAL_N_COMB:
        btwrite(bt, BDELAY, 114);
        break;
    case BT829_SECAM:
        btwrite(bt, BDELAY, 160);
        break;
    default:                   /* shouldn't get here */
        btwrite(bt, BDELAY, 93);        /* hardware default */
        break;
    }
}

static void
btwrite_adc(BT829Ptr bt)
{                               /* ADC */
    btwrite(bt, ADC, bt->mux == bt->svideo_mux ? 0x80 : 0x82);  /* CSLEEP = 0 or 1 */
}

static void
btwrite_vtc(BT829Ptr bt)
{                               /* VTC */
    int vfilt = 0;              /* hardware default */

    if (BTVERSION > BT827) {    /* gatos says >= BT827 */
        switch (bt->format) {
        case BT829_NTSC:
        case BT829_NTSC_JAPAN:
        case BT829_PAL_M:
        case BT829_PAL_N_COMB: /* gatos groups with BT829_PAL */
            if (bt->width <= 360)
                vfilt = 1;      /* gatos says <= 240 */
            if (bt->width <= 180)
                vfilt = 2;      /* gatos says <= 120 */
            if (bt->width <= 90)
                vfilt = 3;      /* gatos says <= 60 */
            break;
        case BT829_PAL:
        case BT829_PAL_N:
        case BT829_SECAM:
            if (bt->width <= 384)
                vfilt = 1;
            if (bt->width <= 192)
                vfilt = 2;
            if (bt->width <= 96)
                vfilt = 3;
            break;
        default:               /* shouldn't get here */
            break;              /* use hardware default */
        }
        btwrite(bt, VTC, (bt->vbien << 4) | (bt->vbifmt << 3) | vfilt);
    }
}

static void
btwrite_cc_status(BT829Ptr bt)
{                               /* CC_STATUS *//* FIXME: ATI specific */
    if (BTVERSION >= BT827) {
        if (bt->ccmode == 0)
            btwrite(bt, CC_STATUS, 0x00);
        /* 0x40 is activate to set the CCVALID line. Not required yet */
        else
            btwrite(bt, CC_STATUS, (bt->ccmode << 4) | 0x40);
    }
}

/* CC_DATA is read only */

static void
btwrite_wc_dn(BT829Ptr bt)
{                               /* WC_DN */
    if (BTVERSION >= BT827) {
        /* use default */
    }
}

static void
bt_reset(BT829Ptr bt)
{                               /* SRESET */
    btwrite(bt, SRESET, 0x0);   /* Reset all registers */
}

static void
btwrite_p_io(BT829Ptr bt)
{                               /* P_IO */
    if (BTVERSION >= BT827) {
        btwrite(bt, P_IO, bt->p_io);
    }
}

/*
 * Deal with dependencies
 */
static void
propagate_changes(BT829Ptr bt)
{
    CARD16 hdelay, unscaled_hdelay, vdelay, hscale, vscale;
    int htotal, vactive;

    switch (bt->format) {
    case BT829_NTSC:
    case BT829_NTSC_JAPAN:
    case BT829_PAL_M:
        vdelay = 22;
        htotal = 754;
        vactive = 480;
        unscaled_hdelay = 135;
        break;
    case BT829_PAL:
    case BT829_PAL_N:
        vdelay = (bt->tunertype == 5) ? 34 : 22;
        htotal = 922;
        vactive = 576;
        unscaled_hdelay = 186;
        break;
    case BT829_SECAM:
        vdelay = 34;
        htotal = 922;
        vactive = 576;
        unscaled_hdelay = 186;
        break;
    case BT829_PAL_N_COMB:
        vdelay = (bt->tunertype == 5) ? 34 : 22;        /* windows says 22 */
        htotal = 754;           /* gatos and windows say 922 */
        vactive = 576;
        unscaled_hdelay = 135;  /* gatos and windows say 186 */
        break;
    default:                   /* shouldn't get here */
        vdelay = 22;            /* hardware default */
        htotal = 754;
        vactive = 480;          /* hardware default */
        unscaled_hdelay = 135;
        break;
    }

    bt->htotal = htotal;        /* Used for error checking in bt829_SetCaptSize */

    hscale = 4096 * htotal / (bt->width + 2 * HCROP) - 4096;
    hdelay =
        (HCROP + (bt->width + 2 * HCROP) * unscaled_hdelay / htotal) & 0x3FE;

    vactive = vactive - 2 * VCROP;
    vdelay = vdelay + VCROP;
    vscale = (0x10000 - (512 * vactive / bt->height - 512)) & 0x1FFF;

    if ((hdelay != bt->hdelay) || (vdelay != bt->vdelay) ||
        (vactive != bt->vactive) || (hscale != bt->hscale) ||
        (vscale != bt->vscale)) {
        bt->hdelay = hdelay;
        bt->vdelay = vdelay;
        bt->vactive = vactive;
        bt->hscale = hscale;
        bt->vscale = vscale;
        btwrite_crop(bt);
        btwrite_vdelay_lo(bt);
        btwrite_vactive_lo(bt);
        btwrite_hdelay_lo(bt);
        btwrite_hscale_hi(bt);
        btwrite_hscale_lo(bt);
        btwrite_control(bt);
        btwrite_vscale_hi(bt);
        btwrite_vscale_lo(bt);
    }
}

static void
write_all(BT829Ptr bt)
{
    bt_reset(bt);
    propagate_changes(bt);      /* ensure consistency */
    btwrite_iform(bt);
    btwrite_tdec(bt);
    btwrite_crop(bt);
    btwrite_vdelay_lo(bt);
    btwrite_vactive_lo(bt);
    btwrite_hdelay_lo(bt);
    btwrite_hactive_lo(bt);
    btwrite_hscale_hi(bt);
    btwrite_hscale_lo(bt);
    btwrite_bright(bt);
    btwrite_control(bt);
    btwrite_contrast_lo(bt);
    btwrite_sat_u_lo(bt);
    btwrite_sat_v_lo(bt);
    btwrite_hue(bt);
    btwrite_scloop(bt);
    btwrite_wc_up(bt);
    btwrite_oform(bt);
    btwrite_vscale_hi(bt);
    btwrite_vscale_lo(bt);
    btwrite_vpole(bt);
    btwrite_adelay(bt);
    btwrite_bdelay(bt);
    btwrite_adc(bt);
    btwrite_vtc(bt);
    /*  btwrite_cc_status(bt); *//* FIXME: CC code needs cleaning */
    btwrite_wc_dn(bt);
    btwrite_p_io(bt);
}

/*
 * Public functions
 */
BT829Ptr
bt829_Detect(I2CBusPtr b, I2CSlaveAddr addr)
{
    BT829Ptr bt;
    I2CByte a;
    char *devname;

    bt = calloc(1, sizeof(BT829Rec));
    if (bt == NULL)
        return NULL;
    bt->d.DevName = strdup("BT829 video decoder");
    bt->d.SlaveAddr = addr;
    bt->d.pI2CBus = b;
    bt->d.NextDev = NULL;
    bt->d.StartTimeout = b->StartTimeout;
    bt->d.BitTimeout = b->BitTimeout;
    bt->d.AcknTimeout = b->AcknTimeout;
    bt->d.ByteTimeout = b->ByteTimeout;

    if (!I2C_WriteRead(&(bt->d), NULL, 0, &a, 1)) {
        free(bt);
        return NULL;
    }

    bt->id = btread(bt, IDCODE);

    free((void *) bt->d.DevName);
    bt->d.DevName = devname = calloc(200, sizeof(char));
    switch (BTVERSION) {
    case BT815:
        sprintf(devname, "bt815a video decoder, revision %d",
                bt->id & 0xf);
        break;
    case BT817:
        sprintf(devname, "bt817a video decoder, revision %d",
                bt->id & 0xf);
        break;
    case BT819:
        sprintf(devname, "bt819a video decoder, revision %d",
                bt->id & 0xf);
        break;
    case BT827:
        sprintf(devname, "bt827a/b video decoder, revision %d",
                bt->id & 0xf);
        break;
    case BT829:
        sprintf(devname, "bt829a/b video decoder, revision %d",
                bt->id & 0xf);
        break;
    default:
        sprintf(devname,
                "bt8xx/unknown video decoder version %d, revision %d",
                bt->id >> 4, bt->id & 0xf);
        break;
    }

    /* set default parameters */
    if (!I2CDevInit(&(bt->d))) {
        free(bt);
        return NULL;
    }

    bt->tunertype = 1;

    bt->brightness = 0;         /* hardware default */
    bt->ccmode = 0;
    bt->code = 0;               /* hardware default */
    bt->contrast = 216;         /* hardware default */
    bt->format = BT829_NTSC;
    bt->height = 480;           /* hardware default for vactive */
    bt->hue = 0;                /* hardware default */
    bt->len = 1;                /* hardware default */
    bt->mux = BT829_MUX0;       /* hardware default */
    bt->out_en = 0;             /* hardware default */
    bt->p_io = 0;               /* hardware default */
    bt->sat_u = 254;            /* hardware default */
    bt->sat_v = 180;            /* hardware default */
    bt->vbien = 0;              /* hardware default */
    bt->vbifmt = 0;             /* hardware default */
    bt->width = 640;            /* hardware default for hactive */

    bt->hdelay = 120;           /* hardware default */
    bt->hscale = 684;           /* hardware default */
    bt->vactive = 480;          /* hardware default */
    bt->vdelay = 22;            /* hardware default */
    bt->vscale = 0;             /* hardware default */

    bt->htotal = 754;           /* NTSC */
    bt->svideo_mux = 0;         /* no s-video */

    return bt;
}

int
bt829_ATIInit(BT829Ptr bt)
{
    bt->code = 1;
    bt->len = 0;
    bt->vbien = 1;
    bt->vbifmt = 1;
    bt->svideo_mux = BT829_MUX1;

    write_all(bt);

    return 0;
}

int
bt829_SetFormat(BT829Ptr bt, CARD8 format)
{
    if ((format < 1) || (format > 7))
        return -1;
    if ((BTVERSION <= BT819) && (format != BT829_NTSC) && (format != BT829_PAL))
        return -1;
    if (format == bt->format)
        return 0;
    bt->format = format;
    propagate_changes(bt);
    btwrite_iform(bt);
    btwrite_scloop(bt);
    btwrite_adelay(bt);
    btwrite_bdelay(bt);
    btwrite_vtc(bt);
    return 0;
}

int
bt829_SetMux(BT829Ptr bt, CARD8 mux)
{
    if ((mux < 1) || (mux > 3))
        return -1;
    if (mux == bt->mux)
        return 0;
    bt->mux = mux;
    /* propagate_changes(bt); *//* no dependencies */
    btwrite_iform(bt);
    btwrite_control(bt);
    btwrite_adc(bt);
    return 0;
}

void
bt829_SetBrightness(BT829Ptr bt, int brightness)
{
    brightness = LIMIT(brightness, -1000, 999); /* ensure -128 <= brightness <= 127 below */
    brightness = (128 * brightness) / 1000;
    if (brightness == bt->brightness)
        return;
    bt->brightness = brightness;
    /* propagate_changes(bt); *//* no dependencies */
    btwrite_bright(bt);
}

void
bt829_SetContrast(BT829Ptr bt, int contrast)
{
    contrast = LIMIT(contrast, -1000, 1000);
    contrast = (216 * (contrast + 1000)) / 1000;
    if (contrast == bt->contrast)
        return;
    bt->contrast = contrast;
    /* propagate_changes(bt); *//* no dependencies */
    btwrite_control(bt);
    btwrite_contrast_lo(bt);
}

void
bt829_SetSaturation(BT829Ptr bt, int saturation)
{
    CARD16 sat_u, sat_v;

    saturation = LIMIT(saturation, -1000, 1000);
    sat_u = (254 * (saturation + 1000)) / 1000;
    sat_v = (180 * (saturation + 1000)) / 1000;
    if ((sat_u == bt->sat_u) && (sat_v == bt->sat_v))
        return;
    bt->sat_u = sat_u;
    bt->sat_v = sat_v;
    /* propagate_changes(bt); *//* no dependencies */
    btwrite_control(bt);
    btwrite_sat_u_lo(bt);
    btwrite_sat_v_lo(bt);
}

void
bt829_SetTint(BT829Ptr bt, int hue)
{
    hue = LIMIT(hue, -1000, 999);       /* ensure -128 <= hue <= 127 below */
    hue = (128 * hue) / 1000;
    if (hue == bt->hue)
        return;
    bt->hue = hue;
    /* propagate_changes(bt); *//* no dependencies */
    btwrite_hue(bt);
}

int
bt829_SetCaptSize(BT829Ptr bt, int width, int height)
{
    if ((width > bt->htotal - 2 * HCROP) ||
        (16 * width < bt->htotal - 32 * HCROP))
        return -1;
    if ((height > bt->vactive) || (16 * height < bt->vactive))
        return -1;
    if ((width == bt->width) && (height == bt->height))
        return 0;
    bt->width = width;
    bt->height = height;
    propagate_changes(bt);
    btwrite_crop(bt);
    btwrite_hactive_lo(bt);
    btwrite_control(bt);
    btwrite_vtc(bt);
    return 0;
}

int
bt829_SetCC(BT829Ptr bt)
{                               /* FIXME: should take ccmode as a parameter */
    if (BTVERSION < BT827)
        return -1;              /* can't do it */
    /* propagate_changes(bt); *//* no dependencies */
    btwrite_cc_status(bt);
    /* we write to STATUS to reset the CCVALID flag */
    if (bt->ccmode != 0)
        btwrite_status(bt);
    return 0;
}

void
bt829_SetOUT_EN(BT829Ptr bt, BOOL out_en)
{
    out_en = (out_en != 0);
    if (out_en == bt->out_en)
        return;
    bt->out_en = out_en;
    /* propagate_changes(bt); *//* no dependencies */
    btwrite_vpole(bt);
}

void
bt829_SetP_IO(BT829Ptr bt, CARD8 p_io)
{
    if (p_io == bt->p_io)
        return;
    bt->p_io = p_io;
    /* propagate_changes(bt); *//* no dependencies */
    btwrite_p_io(bt);
}

#define BTREAD(R)	btread(bt,(R))

#if 0

void
bt829_getCCdata(BT829Ptr bt, struct CCdata *data)
{
    CARD8 status;

    data->num_valid = 0;
    /* wait for buffer to be half full (means 8/16 bytes)
     * either 4 (one of CC/EDS) or 2 (both CC/EDS) frames */
    if (!(BTREAD(STATUS) & 0x04))
        return;                 /* could comment this line */
    for (; data->num_valid < CC_FIFO_SIZE; data->num_valid++) {
        status = BTREAD(CC_STATUS);
        if (!(status & 0x04))
            break;
        data->data[data->num_valid] = BTREAD(CC_DATA) & 0x7f;
        /* stripped high bit (parity) */
        data->status[data->num_valid] = (CCS_EDS * ((status & 0x02) >> 1)) |
            (CCS_HIGH * (status & 0x01)) |
            (CCS_OVER * ((status & 0x08) >> 3)) |
            (CCS_PAR * ((status & 0x80) >> 7));
    }
    btwrite(bt, STATUS, 0x00);  /* Reset CCVALID status bit */
    return;
}

#endif

/* ------------------------------------------------------------------------ */
/* Debug and report routines */

#define DUMPREG(REG)   \
  xf86DrvMsg(bt->d.pI2CBus->scrnIndex,X_INFO," %-12s (0x%02X) = 0x%02X\n", \
              #REG,REG,BTREAD(REG))

/*static void bt829_dumpregs(BT829Ptr bt)
{
  DUMPREG(STATUS);
  DUMPREG(IFORM);
  DUMPREG(TDEC);
  DUMPREG(CROP);
  DUMPREG(VDELAY_LO);
  DUMPREG(VACTIVE_LO);
  DUMPREG(HDELAY_LO);
  DUMPREG(HACTIVE_LO);
  DUMPREG(HSCALE_HI);
  DUMPREG(HSCALE_LO);
  DUMPREG(BRIGHT);
  DUMPREG(CONTROL);
  DUMPREG(CONTRAST_LO);
  DUMPREG(SAT_U_LO);
  DUMPREG(SAT_V_LO);
  DUMPREG(HUE);
  if (BTVERSION >= BT827) {
    DUMPREG(SCLOOP);
    DUMPREG(WC_UP) ; }
  DUMPREG(OFORM);
  DUMPREG(VSCALE_HI);
  DUMPREG(VSCALE_LO);
  DUMPREG(TEST);
  DUMPREG(VPOLE);
  DUMPREG(IDCODE);
  DUMPREG(ADELAY);
  DUMPREG(BDELAY);
  DUMPREG(ADC);
  if (BTVERSION >= BT827) {
    DUMPREG(VTC);
    DUMPREG(CC_STATUS);
    DUMPREG(CC_DATA);
    DUMPREG(WC_DN);
    DUMPREG(P_IO) ; }
}*/
