/*
 *Copyright (C) 1994-2002 The XFree86 Project, Inc. All Rights Reserved.
 *
 *Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 *"Software"), to deal in the Software without restriction, including
 *without limitation the rights to use, copy, modify, merge, publish,
 *distribute, sublicense, and/or sell copies of the Software, and to
 *permit persons to whom the Software is furnished to do so, subject to
 *the following conditions:
 *
 *The above copyright notice and this permission notice shall be
 *included in all copies or substantial portions of the Software.
 *
 *THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 *NONINFRINGEMENT. IN NO EVENT SHALL THE XFREE86 PROJECT BE LIABLE FOR
 *ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 *CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 *WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 *Except as contained in this notice, the name of the XFree86 Project
 *shall not be used in advertising or otherwise to promote the sale, use
 *or other dealings in this Software without prior written authorization
 *from the XFree86 Project.
 *
 * 	Authors:	Alan Hourihane <alanh@fairlite.demon.co.uk>
 */

/*
 * Raster operations used by Windows translated to X's 16 rop codes...
 */
#ifdef HAVE_XWIN_CONFIG_H
#include <xwin-config.h>
#endif
#include "win.h"

void
 ROP16(HDC hdc, int rop);

int g_copyROP[16] = { 0xFF0062, /* GXclear            - 0 */
    0x8800C6,                   /* GXand              - S & D */
    0x440328,                   /* GXandReverse       - S & !D */
    0xCC0020,                   /* GXcopy             - S */
    0x220326,                   /* GXandInverted      - !S & D */
    0xAA0029,                   /* GXnoop             - D */
    0x660046,                   /* GXxor              - S ^ D */
    0xEE0086,                   /* GXor               - S | D */
    0x1100A6,                   /* GXnor              - !S & !D */
    0x990126,                   /* GXequiv            - !S ^ D */
    0x550009,                   /* GXinvert           - !D */
    0xDD0228,                   /* GXorReverse        - S | !D */
    0x330008,                   /* GXcopyInverted     - !S */
    0xBB0226,                   /* GXorInverted       - !S | D */
    0x7700C6,                   /* GXnand             - !S | !D */
    0x000042                    /* GXset              - 1 */
};

int g_patternROP[16] = { 0xFF0062,      /* GXclear            - 0 */
    0xA000C9,                   /* GXand              - P & D */
    0xF50225,                   /* GXandReverse       - P & !D */
    0xF00021,                   /* GXcopy             - P */
    0x5F00E9,                   /* GXandInverted      - !P & D */
    0xAA0029,                   /* GXnoop             - D */
    0xA50065,                   /* GXxor              - P ^ D */
    0xA000C9,                   /* GXor               - P | D */
    0x5F00E9,                   /* GXnor              - !P & !D */
    0x5A0049,                   /* GXequiv            - !P ^ D */
    0x550009,                   /* GXinvert           - !D */
    0x500325,                   /* GXorReverse        - P | !D */
    0x0F0001,                   /* GXcopyInverted     - !P */
    0x0A0329,                   /* GXorInverted       - !P | D */
    0x0500A9,                   /* GXnand             - !P | !D */
    0x000042                    /* GXset              - 1 */
};

void
ROP16(HDC hdc, int rop)
{
    switch (rop) {
    case GXclear:
        SetROP2(hdc, R2_BLACK);
        break;

    case GXand:
        SetROP2(hdc, R2_MASKPEN);
        break;

    case GXandReverse:
        SetROP2(hdc, R2_MASKPENNOT);
        break;

    case GXcopy:
        SetROP2(hdc, R2_COPYPEN);
        break;

    case GXnoop:
        SetROP2(hdc, R2_NOP);
        break;

    case GXxor:
        SetROP2(hdc, R2_XORPEN);
        break;

    case GXor:
        SetROP2(hdc, R2_MERGEPEN);
        break;

    case GXnor:
        SetROP2(hdc, R2_NOTMERGEPEN);
        break;

    case GXequiv:
        SetROP2(hdc, R2_NOTXORPEN);
        break;

    case GXinvert:
        SetROP2(hdc, R2_NOT);
        break;

    case GXorReverse:
        SetROP2(hdc, R2_MERGEPENNOT);
        break;

    case GXcopyInverted:
        SetROP2(hdc, R2_NOTCOPYPEN);
        break;

    case GXorInverted:
        SetROP2(hdc, R2_MERGENOTPEN);
        break;

    case GXnand:
        SetROP2(hdc, R2_NOTMASKPEN);
        break;

    case GXset:
        SetROP2(hdc, R2_WHITE);
        break;
    }
}
