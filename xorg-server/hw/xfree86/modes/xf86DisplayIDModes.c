/*
 * Copyright 2009 Red Hat, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software")
 * to deal in the software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * them Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTIBILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES, OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT, OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *	Adam Jackson <ajax@redhat.com>
 */

#include "xorg-config.h"
#include "xf86.h"
#include "xf86str.h"
#include "edid.h"
#include "xf86DDC.h"

typedef void (*did_proc) (int scrnIndex, unsigned char *data, void *closure);

#define DID_PRODUCT_ID		    0x00
#define DID_DISPLAY_PARAMETERS	    0x01
#define DID_COLOR_INFO		    0x02
#define DID_TIMING_1_DETAILED	    0x03
#define DID_TIMING_2_DETAILED	    0x04
#define DID_TIMING_3_SHORT	    0x05
#define DID_TIMING_4_DMT	    0x06
#define DID_TIMING_VESA		    0x07
#define DID_TIMING_CEA		    0x08
#define DID_TIMING_RANGE_LIMITS	    0x09
#define DID_PRODUCT_SERIAL	    0x0A
#define DID_ASCII_STRING	    0x0B
#define DID_DISPLAY_DEVICE	    0x0C
#define DID_POWER_SEQUENCING	    0x0D
#define DID_TRANSFER_INFO	    0x0E
#define DID_DISPLAY_INTERFACE	    0x0F
#define DID_STEREO		    0x10
#define DID_VENDOR		    0x7F

#define extract_le16(x, i) ((x[i+1] << 8) + (x[i]))
#define extract_le24(x, i) ((x[i+2] << 16) + (x[i+1] << 8) + (x[i]))

static DisplayModePtr
modeCalloc(void)
{
    return calloc(1, sizeof(DisplayModeRec));
}

/*
 * How awesome is it to have two detailed timing formats, neither of which
 * are compatible with the format in EDID?  So awesome.
 */

static void
didDetailedTiming1(int i, unsigned char *x, MonPtr mon)
{
    DisplayModePtr m = modeCalloc();

    if (!m)
        return;

    m->Clock = extract_le24(x, 0);

    m->HDisplay = extract_le16(x, 4);
    m->HSyncStart = m->HDisplay + (extract_le16(x, 8) & 0x7f);
    m->HSyncEnd = m->HSyncStart + extract_le16(x, 10);
    m->HTotal = m->HDisplay + extract_le16(x, 6);
    m->Flags |= (x[9] & 0x80) ? V_PHSYNC : V_NHSYNC;

    m->VDisplay = extract_le16(x, 12);
    m->VSyncStart = m->VDisplay + (extract_le16(x, 16) & 0x7f);
    m->VSyncEnd = m->VSyncStart + extract_le16(x, 18);
    m->VTotal = m->VDisplay + extract_le16(x, 14);
    m->Flags |= (x[17] & 0x80) ? V_PVSYNC : V_NVSYNC;

    m->type = M_T_DRIVER;
    if (x[3] & 0x80)
        m->type |= M_T_PREFERRED;

    /* XXX double check handling of this */
    if (x[3] & 0x10)
        m->Flags |= V_INTERLACE;

    mon->Modes = xf86ModesAdd(mon->Modes, m);
}

/* XXX no sync bits.  what to do? */
static void
didDetailedTiming2(int i, unsigned char *x, MonPtr mon)
{
    DisplayModePtr mode = modeCalloc();

    if (!mode)
        return;

    mode->Clock = extract_le24(x, 0);

    /* horiz sizes are in character cells, not pixels, hence * 8 */
    mode->HDisplay = ((extract_le16(x, 4) & 0x01ff) + 1) * 8;
    mode->HSyncStart = mode->HDisplay + (((x[6] & 0xf0) >> 4) + 1) * 8;
    mode->HSyncEnd = mode->HSyncStart + ((x[6] & 0x0f) + 1) * 8;
    mode->HTotal = mode->HDisplay + ((x[5] >> 1) + 1) * 8;

    mode->VDisplay = extract_le16(x, 7) & 0x07ff;
    mode->VSyncStart = mode->VDisplay + (x[10] >> 4) + 1;
    mode->VSyncEnd = mode->VSyncStart + (x[10] & 0x0f) + 1;
    mode->VTotal = mode->VDisplay + x[9];

    mode->status = M_T_DRIVER;
    if (x[3] & 0x80)
        mode->status |= M_T_PREFERRED;

    /* XXX double check handling of this */
    if (x[3] & 0x10)
        mode->Flags |= V_INTERLACE;

    mon->Modes = xf86ModesAdd(mon->Modes, mode);
}

static void
didShortTiming(int i, unsigned char *x, MonPtr mon)
{
    DisplayModePtr m;
    int w, h, r;

    w = (x[1] + 1) * 8;
    switch (x[0] & 0x0f) {
    case 0:
        h = w;
        break;
    case 1:
        h = (w * 4) / 5;
        break;
    case 2:
        h = (w * 3) / 4;
        break;
    case 3:
        h = (w * 9) / 15;
        break;
    case 4:
        h = (w * 9) / 16;
        break;
    case 5:
        h = (w * 10) / 16;
        break;
    default:
        return;
    }
    r = (x[2] & 0x7f) + 1;

    m = xf86CVTMode(w, h, r, ! !(x[0] & 0x10), ! !(x[2] & 0x80));

    m->type = M_T_DRIVER;
    if (x[0] & 0x80)
        m->type |= M_T_PREFERRED;

    mon->Modes = xf86ModesAdd(mon->Modes, m);
}

static void
didDMTTiming(int i, unsigned char *x, void *closure)
{
    MonPtr mon = closure;

    mon->Modes = xf86ModesAdd(mon->Modes, xf86DuplicateMode(DMTModes + *x));
}

#define RB 1
#define INT 2
static const struct did_dmt {
    short w, h, r, f;
} did_dmt[] = {
    /* byte 3 */
    {640, 350, 85, 0},
    {640, 400, 85, 0},
    {720, 400, 85, 0},
    {640, 480, 60, 0},
    {640, 480, 72, 0},
    {640, 480, 75, 0},
    {640, 480, 85, 0},
    {800, 600, 56, 0},
        /* byte 4 */
    {800, 600, 60, 0},
    {800, 600, 72, 0},
    {800, 600, 75, 0},
    {800, 600, 85, 0},
    {800, 600, 120, RB},
    {848, 480, 60, 0},
    {1024, 768, 43, INT},
    {1024, 768, 60, 0},
        /* byte 5 */
    {1024, 768, 70, 0},
    {1024, 768, 75, 0},
    {1024, 768, 85, 0},
    {1024, 768, 120, RB},
    {1152, 864, 75, 0},
    {1280, 768, 60, RB},
    {1280, 768, 60, 0},
    {1280, 768, 75, 0},
        /* byte 6 */
    {1280, 768, 85, 0},
    {1280, 768, 120, RB},
    {1280, 800, 60, RB},
    {1280, 800, 60, 0},
    {1280, 800, 75, 0},
    {1280, 800, 85, 0},
    {1280, 800, 120, RB},
    {1280, 960, 60, 0},
        /* byte 7 */
    {1280, 960, 85, 0},
    {1280, 960, 120, RB},
    {1280, 1024, 60, 0},
    {1280, 1024, 75, 0},
    {1280, 1024, 85, 0},
    {1280, 1024, 120, RB},
    {1360, 768, 60, 0},
    {1360, 768, 120, RB},
        /* byte 8 */
    {1400, 1050, 60, RB},
    {1400, 1050, 60, 0},
    {1400, 1050, 75, 0},
    {1400, 1050, 85, 0},
    {1400, 1050, 120, RB},
    {1440, 900, 60, RB},
    {1440, 900, 60, 0},
    {1440, 900, 75, 0},
        /* byte 9 */
    {1440, 900, 85, 0},
    {1440, 900, 120, RB},
    {1600, 1200, 60, 0},
    {1600, 1200, 65, 0},
    {1600, 1200, 70, 0},
    {1600, 1200, 75, 0},
    {1600, 1200, 85, 0},
    {1600, 1200, 120, RB},
        /* byte a */
    {1680, 1050, 60, RB},
    {1680, 1050, 60, 0},
    {1680, 1050, 75, 0},
    {1680, 1050, 85, 0},
    {1680, 1050, 120, RB},
    {1792, 1344, 60, 0},
    {1792, 1344, 75, 0},
    {1792, 1344, 120, RB},
        /* byte b */
    {1856, 1392, 60, 0},
    {1856, 1392, 75, 0},
    {1856, 1392, 120, RB},
    {1920, 1200, 60, RB},
    {1920, 1200, 60, 0},
    {1920, 1200, 75, 0},
    {1920, 1200, 85, 0},
    {1920, 1200, 120, RB},
        /* byte c */
    {1920, 1440, 60, 0},
    {1920, 1440, 75, 0},
    {1920, 1440, 120, RB},
    {2560, 1600, 60, RB},
    {2560, 1600, 60, 0},
    {2560, 1600, 75, 0},
    {2560, 1600, 85, 0},
    {2560, 1600, 120, RB},
};

static void
didVesaTiming(int scrn, unsigned char *x, MonPtr mon)
{
    int i, j;

    x += 3;

    for (i = 0; i < 10; i++)
        for (j = 0; j < 8; j++)
            if (x[i] & (1 << j)) {
                const struct did_dmt *d = &(did_dmt[i * 8 + j]);

                if (d->f == INT)
                    continue;
                mon->Modes = xf86ModesAdd(mon->Modes,
                                          FindDMTMode(d->w, d->h, d->r,
                                                      d->f == RB));
            }

}

static void
handleDisplayIDBlock(int scrnIndex, unsigned char *x, void *closure)
{
    MonPtr mon = closure;

    switch (x[0]) {
    case DID_DISPLAY_PARAMETERS:
        /* w/h are in decimillimeters */
        mon->widthmm = (extract_le16(x, 3) + 5) / 10;
        mon->heightmm = (extract_le16(x, 5) + 5) / 10;
        /* XXX pixel count, feature flags, gamma, aspect, color depth */
        break;

    case DID_TIMING_RANGE_LIMITS:
    {
        int n;

        mon->maxPixClock = max(mon->maxPixClock, extract_le24(x, 6) * 10);

        n = mon->nHsync++;
        if (n < MAX_HSYNC) {
            mon->hsync[n].lo = x[9];
            mon->hsync[n].hi = x[10];
        }
        else {
            n = MAX_HSYNC;
        }
        n = mon->nVrefresh++;
        if (n < MAX_VREFRESH) {
            mon->vrefresh[n].lo = x[13];
            mon->vrefresh[n].hi = x[14];
        }
        else {
            n = MAX_VREFRESH;
        }
        break;
    }

    case DID_TIMING_1_DETAILED:
    {
        int i;

        for (i = 0; i < x[2]; i += 20)
            didDetailedTiming1(scrnIndex, x + i + 3, mon);
        break;
    }

    case DID_TIMING_2_DETAILED:
    {
        int i;

        for (i = 0; i < x[2]; i += 11)
            didDetailedTiming2(scrnIndex, x + i + 3, mon);
        break;
    }

    case DID_TIMING_3_SHORT:
    {
        int i;

        for (i = 0; i < x[2]; i += 3)
            didShortTiming(scrnIndex, x + i + 3, mon);
        break;
    }

    case DID_TIMING_4_DMT:
    {
        int i;

        for (i = 0; i < x[2]; i++)
            didDMTTiming(scrnIndex, x + i + 3, mon);
        break;
    }

    case DID_TIMING_VESA:
        didVesaTiming(scrnIndex, x, mon);
        break;

        /* XXX pixel format, ar, orientation, subpixel, dot pitch, bit depth */
    case DID_DISPLAY_DEVICE:

        /* XXX interface, links, color encoding, ss, drm */
    case DID_DISPLAY_INTERFACE:

        /* XXX stereo */
    case DID_STEREO:

        /* nothing interesting in these */
    case DID_COLOR_INFO:
    case DID_PRODUCT_SERIAL:
    case DID_ASCII_STRING:
    case DID_POWER_SEQUENCING:
    case DID_TRANSFER_INFO:
    case DID_VENDOR:
        break;

        /* warn about anything else */
    default:
        xf86DrvMsg(scrnIndex, X_WARNING,
                   "Unknown DisplayID block type %hx\n", x[0]);
        break;
    }
}

static void
forEachDisplayIDBlock(int scrnIndex, unsigned char *did, did_proc proc,
                      void *closure)
{
    int num_extensions = did[3];
    int section_size = did[1];
    unsigned char *block;

    do {
        if ((did[0] & 0xf0) != 0x10)    /* not 1.x, abort */
            return;
        /* XXX also, checksum */

        block = did + 4;

        while (section_size > 0) {
            int block_size = (block[2] + 2);

            proc(scrnIndex, block, closure);

            section_size -= block_size;
            block += block_size;
        }

        did += (did[1] + 5);
    } while (num_extensions--);
}

/*
 * Fill out MonPtr with xf86MonPtr information.
 */
void
xf86DisplayIDMonitorSet(int scrnIndex, MonPtr mon, xf86MonPtr DDC)
{
    if (!mon || !DDC)
        return;

    mon->DDC = DDC;

    forEachDisplayIDBlock(scrnIndex, DDC->rawData, handleDisplayIDBlock, mon);
}
