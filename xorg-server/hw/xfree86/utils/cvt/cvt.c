/*
 * Copyright 2005-2006 Luc Verhaegen.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

/* Standalone VESA CVT standard timing modelines generator. */

#include "xf86.h"
#include "xf86Modes.h"

/* FatalError implementation used by the server code we built in */
void
FatalError(const char *f, ...)
{
    va_list args;

    va_start(args, f);
    vfprintf(stderr, f, args);
    va_end(args);
    exit(1);
}

/* xnfalloc implementation used by the server code we built in */
void *
XNFalloc(unsigned long n)
{
    void *r;

    r = malloc(n);
    if (!r) {
        perror("malloc failed");
        exit(1);
    }
    return r;
}

/* xnfcalloc implementation used by the server code we built in */
void *
XNFcallocarray(size_t nmemb, size_t size)
{
    void *r;

    r = calloc(nmemb, size);
    if (!r) {
        perror("calloc failed");
        exit(1);
    }
    return r;
}

/*
 * Quickly check wether this is a CVT standard mode.
 */
static Bool
CVTCheckStandard(int HDisplay, int VDisplay, float VRefresh, Bool Reduced,
                 Bool Verbose)
{
    Bool IsCVT = TRUE;

    if ((!(VDisplay % 3) && ((VDisplay * 4 / 3) == HDisplay)) ||
        (!(VDisplay % 9) && ((VDisplay * 16 / 9) == HDisplay)) ||
        (!(VDisplay % 10) && ((VDisplay * 16 / 10) == HDisplay)) ||
        (!(VDisplay % 4) && ((VDisplay * 5 / 4) == HDisplay)) ||
        (!(VDisplay % 9) && ((VDisplay * 15 / 9) == HDisplay)));
    else {
        if (Verbose)
            fprintf(stderr, "Warning: Aspect Ratio is not CVT standard.\n");
        IsCVT = FALSE;
    }

    if ((VRefresh != 50.0) && (VRefresh != 60.0) &&
        (VRefresh != 75.0) && (VRefresh != 85.0)) {
        if (Verbose)
            fprintf(stderr, "Warning: Refresh Rate is not CVT standard "
                    "(50, 60, 75 or 85Hz).\n");
        IsCVT = FALSE;
    }

    return IsCVT;
}

/*
 * I'm not documenting --interlaced for obvious reasons, even though I did
 * implement it. I also can't deny having looked at gtf here.
 */
static void
PrintUsage(char *Name)
{
    fprintf(stderr, "\n");
    fprintf(stderr, "usage: %s [-v|--verbose] [-r|--reduced] X Y [refresh]\n",
            Name);
    fprintf(stderr, "\n");
    fprintf(stderr, " -v|--verbose : Warn about CVT standard adherance.\n");
    fprintf(stderr, " -r|--reduced : Create a mode with reduced blanking "
            "(default: normal blanking).\n");
    fprintf(stderr, "            X : Desired horizontal resolution "
            "(multiple of 8, required).\n");
    fprintf(stderr,
            "            Y : Desired vertical resolution (required).\n");
    fprintf(stderr,
            "      refresh : Desired refresh rate (default: 60.0Hz).\n");
    fprintf(stderr, "\n");

    fprintf(stderr, "Calculates VESA CVT (Coordinated Video Timing) modelines"
            " for use with X.\n");
}

/*
 *
 */
static void
PrintComment(DisplayModeRec * Mode, Bool CVT, Bool Reduced)
{
    printf("# %dx%d %.2f Hz ", Mode->HDisplay, Mode->VDisplay, Mode->VRefresh);

    if (CVT) {
        printf("(CVT %.2fM",
               ((float) Mode->HDisplay * Mode->VDisplay) / 1000000.0);

        if (!(Mode->VDisplay % 3) &&
            ((Mode->VDisplay * 4 / 3) == Mode->HDisplay))
            printf("3");
        else if (!(Mode->VDisplay % 9) &&
                 ((Mode->VDisplay * 16 / 9) == Mode->HDisplay))
            printf("9");
        else if (!(Mode->VDisplay % 10) &&
                 ((Mode->VDisplay * 16 / 10) == Mode->HDisplay))
            printf("A");
        else if (!(Mode->VDisplay % 4) &&
                 ((Mode->VDisplay * 5 / 4) == Mode->HDisplay))
            printf("4");
        else if (!(Mode->VDisplay % 9) &&
                 ((Mode->VDisplay * 15 / 9) == Mode->HDisplay))
            printf("9");

        if (Reduced)
            printf("-R");

        printf(") ");
    }
    else
        printf("(CVT) ");

    printf("hsync: %.2f kHz; ", Mode->HSync);
    printf("pclk: %.2f MHz", ((float) Mode->Clock) / 1000.0);

    printf("\n");
}

/*
 * Originally grabbed from xf86Mode.c.
 *
 * Ignoring the actual Mode->name, as the user will want something solid
 * to grab hold of.
 */
static void
PrintModeline(DisplayModePtr Mode, int HDisplay, int VDisplay, float VRefresh,
              Bool Reduced)
{
    if (Reduced)
        printf("Modeline \"%dx%dR\"  ", HDisplay, VDisplay);
    else
        printf("Modeline \"%dx%d_%.2f\"  ", HDisplay, VDisplay, VRefresh);

    printf("%6.2f  %i %i %i %i  %i %i %i %i", Mode->Clock / 1000.,
           Mode->HDisplay, Mode->HSyncStart, Mode->HSyncEnd, Mode->HTotal,
           Mode->VDisplay, Mode->VSyncStart, Mode->VSyncEnd, Mode->VTotal);

    if (Mode->Flags & V_INTERLACE)
        printf(" interlace");
    if (Mode->Flags & V_PHSYNC)
        printf(" +hsync");
    if (Mode->Flags & V_NHSYNC)
        printf(" -hsync");
    if (Mode->Flags & V_PVSYNC)
        printf(" +vsync");
    if (Mode->Flags & V_NVSYNC)
        printf(" -vsync");

    printf("\n");
}

/*
 *
 */
int
main(int argc, char *argv[])
{
    DisplayModeRec *Mode;
    int HDisplay = 0, VDisplay = 0;
    float VRefresh = 0.0;
    Bool Reduced = FALSE, Verbose = FALSE, IsCVT;
    Bool Interlaced = FALSE;
    int n;

    if ((argc < 3) || (argc > 7)) {
        PrintUsage(argv[0]);
        return 1;
    }

    /* This doesn't filter out bad flags properly. Bad flags get passed down
     * to atoi/atof, which then return 0, so that these variables can get
     * filled next time round. So this is just a cosmetic problem.
     */
    for (n = 1; n < argc; n++) {
        if (!strcmp(argv[n], "-r") || !strcmp(argv[n], "--reduced"))
            Reduced = TRUE;
        else if (!strcmp(argv[n], "-i") || !strcmp(argv[n], "--interlaced"))
            Interlaced = TRUE;
        else if (!strcmp(argv[n], "-v") || !strcmp(argv[n], "--verbose"))
            Verbose = TRUE;
        else if (!strcmp(argv[n], "-h") || !strcmp(argv[n], "--help")) {
            PrintUsage(argv[0]);
            return 0;
        }
        else if (!HDisplay) {
            HDisplay = atoi(argv[n]);
            if (!HDisplay) {
                PrintUsage(argv[0]);
                return 1;
            }
        }
        else if (!VDisplay) {
            VDisplay = atoi(argv[n]);
            if (!VDisplay) {
                PrintUsage(argv[0]);
                return 1;
            }
        }
        else if (!VRefresh) {
            VRefresh = atof(argv[n]);
            if (!VRefresh) {
                PrintUsage(argv[0]);
                return 1;
            }
        }
        else {
            PrintUsage(argv[0]);
            return 1;
        }
    }

    if (!HDisplay || !VDisplay) {
        PrintUsage(argv[0]);
        return 0;
    }

    /* Default to 60.0Hz */
    if (!VRefresh)
        VRefresh = 60.0;

    /* Horizontal timing is always a multiple of 8: round up. */
    if (HDisplay & 0x07) {
        HDisplay &= ~0x07;
        HDisplay += 8;
    }

    if (Reduced) {
        if ((VRefresh / 60.0) != floor(VRefresh / 60.0)) {
            fprintf(stderr,
                    "\nERROR: Multiple of 60Hz refresh rate required for "
                    " reduced blanking.\n");
            PrintUsage(argv[0]);
            return 0;
        }
    }

    IsCVT = CVTCheckStandard(HDisplay, VDisplay, VRefresh, Reduced, Verbose);

    Mode = xf86CVTMode(HDisplay, VDisplay, VRefresh, Reduced, Interlaced);

    PrintComment(Mode, IsCVT, Reduced);
    PrintModeline(Mode, HDisplay, VDisplay, VRefresh, Reduced);

    return 0;
}
