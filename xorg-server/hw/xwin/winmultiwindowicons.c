/*
 *Copyright (C) 1994-2000 The XFree86 Project, Inc. All Rights Reserved.
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
 * Authors:	Earle F. Philhower, III
 */

#ifdef HAVE_XWIN_CONFIG_H
#include <xwin-config.h>
#endif
#include "win.h"
#include "dixevents.h"
#include "winmultiwindowclass.h"
#include "winprefs.h"

#include "propertyst.h"
#include "windowstr.h"

/*
 * Prototypes for local functions
 */

static void

winScaleXBitmapToWindows(int iconSize, int effBPP,
                         PixmapPtr pixmap, unsigned char *image);

/*
 * Scale an X icon bitmap into a Windoze icon bitmap
 */

static void
winScaleXBitmapToWindows(int iconSize,
                         int effBPP, PixmapPtr pixmap, unsigned char *image)
{
    int row, column, effXBPP, effXDepth;
    unsigned char *outPtr;
    char *iconData = 0;
    int stride, xStride;
    float factX, factY;
    int posX, posY;
    unsigned char *ptr;
    unsigned int zero;
    unsigned int color;

    effXBPP = BitsPerPixel(pixmap->drawable.depth);
    effXDepth = pixmap->drawable.depth;

    if (pixmap->drawable.bitsPerPixel == 15)
        effXBPP = 16;

    if (pixmap->drawable.depth == 15)
        effXDepth = 16;

    /* Need 16-bit aligned rows for DDBitmaps */
    stride = ((iconSize * effBPP + 15) & (~15)) / 8;
    xStride = PixmapBytePad(pixmap->drawable.width, pixmap->drawable.depth);
    if (stride == 0 || xStride == 0) {
        ErrorF("winScaleXBitmapToWindows - stride or xStride is zero.  "
               "Bailing.\n");
        return;
    }

    /* Allocate memory for icon data */
    iconData = malloc(xStride * pixmap->drawable.height);
    if (!iconData) {
        ErrorF("winScaleXBitmapToWindows - malloc failed for iconData.  "
               "Bailing.\n");
        return;
    }

    /* Get icon data */
    miGetImage((DrawablePtr) &(pixmap->drawable), 0, 0,
               pixmap->drawable.width, pixmap->drawable.height,
               ZPixmap, 0xffffffff, iconData);

    /* Keep aspect ratio */
    factX = ((float) pixmap->drawable.width) / ((float) iconSize);
    factY = ((float) pixmap->drawable.height) / ((float) iconSize);
    if (factX > factY)
        factY = factX;
    else
        factX = factY;

    /* Out-of-bounds, fill icon with zero */
    zero = 0;

    for (row = 0; row < iconSize; row++) {
        outPtr = image + stride * row;
        for (column = 0; column < iconSize; column++) {
            posX = factX * column;
            posY = factY * row;

            ptr = (unsigned char *) iconData + posY * xStride;
            if (effXBPP == 1) {
                ptr += posX / 8;

                /* Out of X icon bounds, leave space blank */
                if (posX >= pixmap->drawable.width
                    || posY >= pixmap->drawable.height)
                    ptr = (unsigned char *) &zero;

                if ((*ptr) & (1 << (posX & 7)))
                    switch (effBPP) {
                    case 32:
                        *(outPtr++) = 0;
                    case 24:
                        *(outPtr++) = 0;
                    case 16:
                        *(outPtr++) = 0;
                    case 8:
                        *(outPtr++) = 0;
                        break;
                    case 1:
                        outPtr[column / 8] &= ~(1 << (7 - (column & 7)));
                        break;
                    }
                else
                    switch (effBPP) {
                    case 32:
                        *(outPtr++) = 255;
                        *(outPtr++) = 255;
                        *(outPtr++) = 255;
                        *(outPtr++) = 0;
                        break;
                    case 24:
                        *(outPtr++) = 255;
                    case 16:
                        *(outPtr++) = 255;
                    case 8:
                        *(outPtr++) = 255;
                        break;
                    case 1:
                        outPtr[column / 8] |= (1 << (7 - (column & 7)));
                        break;
                    }
            }
            else if (effXDepth == 24 || effXDepth == 32) {
                ptr += posX * (effXBPP / 8);

                /* Out of X icon bounds, leave space blank */
                if (posX >= pixmap->drawable.width
                    || posY >= pixmap->drawable.height)
                    ptr = (unsigned char *) &zero;
                color = (((*ptr) << 16)
                         + ((*(ptr + 1)) << 8)
                         + ((*(ptr + 2)) << 0));
                switch (effBPP) {
                case 32:
                    *(outPtr++) = *(ptr++);     /* b */
                    *(outPtr++) = *(ptr++);     /* g */
                    *(outPtr++) = *(ptr++);     /* r */
                    *(outPtr++) = (effXDepth == 32) ? *(ptr++) : 0x0;   /* alpha */
                    break;
                case 24:
                    *(outPtr++) = *(ptr++);
                    *(outPtr++) = *(ptr++);
                    *(outPtr++) = *(ptr++);
                    break;
                case 16:
                    color = ((((*ptr) >> 2) << 10)
                             + (((*(ptr + 1)) >> 2) << 5)
                             + (((*(ptr + 2)) >> 2)));
                    *(outPtr++) = (color >> 8);
                    *(outPtr++) = (color & 255);
                    break;
                case 8:
                    color = (((*ptr))) + (((*(ptr + 1)))) + (((*(ptr + 2))));
                    color /= 3;
                    *(outPtr++) = color;
                    break;
                case 1:
                    if (color)
                        outPtr[column / 8] |= (1 << (7 - (column & 7)));
                    else
                        outPtr[column / 8] &= ~(1 << (7 - (column & 7)));
                }
            }
            else if (effXDepth == 16) {
                ptr += posX * (effXBPP / 8);

                /* Out of X icon bounds, leave space blank */
                if (posX >= pixmap->drawable.width
                    || posY >= pixmap->drawable.height)
                    ptr = (unsigned char *) &zero;
                color = ((*ptr) << 8) + (*(ptr + 1));
                switch (effBPP) {
                case 32:
                    *(outPtr++) = (color & 31) << 2;
                    *(outPtr++) = ((color >> 5) & 31) << 2;
                    *(outPtr++) = ((color >> 10) & 31) << 2;
                    *(outPtr++) = 0;    /* resvd */
                    break;
                case 24:
                    *(outPtr++) = (color & 31) << 2;
                    *(outPtr++) = ((color >> 5) & 31) << 2;
                    *(outPtr++) = ((color >> 10) & 31) << 2;
                    break;
                case 16:
                    *(outPtr++) = *(ptr++);
                    *(outPtr++) = *(ptr++);
                    break;
                case 8:
                    *(outPtr++) = (((color & 31)
                                    + ((color >> 5) & 31)
                                    + ((color >> 10) & 31)) / 3) << 2;
                    break;
                case 1:
                    if (color)
                        outPtr[column / 8] |= (1 << (7 - (column & 7)));
                    else
                        outPtr[column / 8] &= ~(1 << (7 - (column & 7)));
                    break;
                }               /* end switch(effbpp) */
            }                   /* end if effxbpp==16) */
        }                       /* end for column */
    }                           /* end for row */
    free(iconData);
}

static HICON
NetWMToWinIconAlpha(uint32_t * icon)
{
    int width = icon[0];
    int height = icon[1];
    uint32_t *pixels = &icon[2];
    HICON result;
    HDC hdc = GetDC(NULL);
    uint32_t *DIB_pixels;
    ICONINFO ii = { TRUE };
    BITMAPV4HEADER bmh = { sizeof(bmh) };

    /* Define an ARGB pixel format used for Color+Alpha icons */
    bmh.bV4Width = width;
    bmh.bV4Height = -height;    /* Invert the image */
    bmh.bV4Planes = 1;
    bmh.bV4BitCount = 32;
    bmh.bV4V4Compression = BI_BITFIELDS;
    bmh.bV4AlphaMask = 0xFF000000;
    bmh.bV4RedMask = 0x00FF0000;
    bmh.bV4GreenMask = 0x0000FF00;
    bmh.bV4BlueMask = 0x000000FF;

    ii.hbmColor = CreateDIBSection(hdc, (BITMAPINFO *) & bmh,
                                   DIB_RGB_COLORS, (void **) &DIB_pixels, NULL,
                                   0);
    ReleaseDC(NULL, hdc);
    ii.hbmMask = CreateBitmap(width, height, 1, 1, NULL);
    memcpy(DIB_pixels, pixels, height * width * 4);

    /* CreateIconIndirect() traditionally required DDBitmaps */
    /* Systems from WinXP accept 32-bit ARGB DIBitmaps with full 8-bit alpha support */
    /* The icon is created with a DIB + empty DDB mask (an MS example does the same) */
    result = CreateIconIndirect(&ii);

    DeleteObject(ii.hbmColor);
    DeleteObject(ii.hbmMask);

    winDebug("NetWMToWinIconAlpha - %d x %d = %p\n", icon[0], icon[1], result);
    return result;
}

static HICON
NetWMToWinIconThreshold(uint32_t * icon)
{
    int width = icon[0];
    int height = icon[1];
    uint32_t *pixels = &icon[2];
    int row, col;
    HICON result;
    ICONINFO ii = { TRUE };

    HDC hdc = GetDC(NULL);
    HDC xorDC = CreateCompatibleDC(hdc);
    HDC andDC = CreateCompatibleDC(hdc);

    ii.hbmColor = CreateCompatibleBitmap(hdc, width, height);
    ii.hbmMask = CreateCompatibleBitmap(hdc, width, height);
    ReleaseDC(NULL, hdc);
    SelectObject(xorDC, ii.hbmColor);
    SelectObject(andDC, ii.hbmMask);

    for (row = 0; row < height; row++) {
        for (col = 0; col < width; col++) {
            if ((*pixels & 0xFF000000) > 31 << 24) {    /* 31 alpha threshold, i.e. opaque above, transparent below */
                SetPixelV(xorDC, col, row,
                          RGB(((char *) pixels)[2], ((char *) pixels)[1],
                              ((char *) pixels)[0]));
                SetPixelV(andDC, col, row, RGB(0, 0, 0));       /* black mask */
            }
            else {
                SetPixelV(xorDC, col, row, RGB(0, 0, 0));
                SetPixelV(andDC, col, row, RGB(255, 255, 255)); /* white mask */
            }
            pixels++;
        }
    }
    DeleteDC(xorDC);
    DeleteDC(andDC);

    result = CreateIconIndirect(&ii);

    DeleteObject(ii.hbmColor);
    DeleteObject(ii.hbmMask);

    winDebug("NetWMToWinIconThreshold - %d x %d = %p\n", icon[0], icon[1],
             result);
    return result;
}

static HICON
NetWMToWinIcon(int bpp, uint32_t * icon)
{
    static Bool hasIconAlphaChannel = FALSE;
    static BOOL versionChecked = FALSE;

    if (!versionChecked) {
        OSVERSIONINFOEX osvi = { 0 };
        ULONGLONG dwlConditionMask = 0;

        osvi.dwOSVersionInfoSize = sizeof(osvi);
        osvi.dwMajorVersion = 5;
        osvi.dwMinorVersion = 1;

        /* Windows versions later than XP have icon alpha channel suport, 2000 does not */
        VER_SET_CONDITION(dwlConditionMask, VER_MAJORVERSION,
                          VER_GREATER_EQUAL);
        VER_SET_CONDITION(dwlConditionMask, VER_MINORVERSION,
                          VER_GREATER_EQUAL);
        hasIconAlphaChannel =
            VerifyVersionInfo(&osvi, VER_MAJORVERSION | VER_MINORVERSION,
                              dwlConditionMask);
        versionChecked = TRUE;

        ErrorF("OS has icon alpha channel support: %s\n",
               hasIconAlphaChannel ? "yes" : "no");
    }

    if (hasIconAlphaChannel && (bpp == 32))
        return NetWMToWinIconAlpha(icon);
    else
        return NetWMToWinIconThreshold(icon);
}

static pointer
GetWindowProp(WindowPtr pWin, Atom name, long int *size_return)
{
    struct _Window *pwin;
    struct _Property *prop;

    if (!pWin || !name) {
        ErrorF("GetWindowProp - pWin or name was NULL\n");
        return 0;
    }
    pwin = (struct _Window *) pWin;
    if (!pwin->optional)
        return NULL;
    for (prop = (struct _Property *) pwin->optional->userProps;
         prop; prop = prop->next) {
        if (prop->propertyName == name) {
            *size_return = prop->size;
            return prop->data;
        }
    }
    return NULL;
}

/*
 * Attempt to create a custom icon from the WM_HINTS bitmaps
 */

HICON
winXIconToHICON(WindowPtr pWin, int iconSize)
{
    unsigned char *mask, *image, *imageMask;
    unsigned char *dst, *src;
    PixmapPtr iconPtr;
    PixmapPtr maskPtr;
    int planes, bpp, effBPP, stride, maskStride, i;
    int biggest_size = 0;
    HDC hDC;
    ICONINFO ii;
    WinXWMHints hints;
    HICON hIcon = NULL;
    uint32_t *biggest_icon = NULL;

    /* Try to get _NET_WM_ICON icons first */
    static Atom _XA_NET_WM_ICON;
    static int generation;
    uint32_t *icon, *icon_data = NULL;
    long int size = 0;

    hDC = GetDC(GetDesktopWindow());
    planes = GetDeviceCaps(hDC, PLANES);
    bpp = GetDeviceCaps(hDC, BITSPIXEL);
    ReleaseDC(GetDesktopWindow(), hDC);

    if (generation != serverGeneration) {
        generation = serverGeneration;
        _XA_NET_WM_ICON = MakeAtom("_NET_WM_ICON", 12, TRUE);
    }

    if (_XA_NET_WM_ICON)
        icon_data = GetWindowProp(pWin, _XA_NET_WM_ICON, &size);
    if (icon_data) {
        for (icon = icon_data;
             icon < &icon_data[size] && *icon;
             icon = &icon[icon[0] * icon[1] + 2]) {
            if (icon[0] == iconSize && icon[1] == iconSize)
                return NetWMToWinIcon(bpp, icon);
            /* Find the biggest icon and let Windows scale the size */
            else if (biggest_size < icon[0]) {
                biggest_icon = icon;
                biggest_size = icon[0];
            }
        }
        if (biggest_icon)
            return NetWMToWinIcon(bpp, biggest_icon);
    }
    winDebug("winXIconToHICON - pWin %x: no suitable NetIcon\n", (int) pWin,
             iconSize);

    winMultiWindowGetWMHints(pWin, &hints);
    if (!hints.icon_pixmap)
        return NULL;

    dixLookupResourceByType((pointer) &iconPtr, hints.icon_pixmap, RT_PIXMAP,
                            NullClient, DixUnknownAccess);

    if (!iconPtr)
        return NULL;

    /* 15 BPP is really 16BPP as far as we care */
    if (bpp == 15)
        effBPP = 16;
    else
        effBPP = bpp;

    /* Need 16-bit aligned rows for DDBitmaps */
    stride = ((iconSize * effBPP + 15) & (~15)) / 8;

    /* Mask is 1-bit deep */
    maskStride = ((iconSize * 1 + 15) & (~15)) / 8;

    image = malloc(stride * iconSize);
    imageMask = malloc(stride * iconSize);
    /* Default to a completely black mask */
    mask = calloc(maskStride, iconSize);

    winScaleXBitmapToWindows(iconSize, effBPP, iconPtr, image);
    dixLookupResourceByType((pointer) &maskPtr, hints.icon_mask, RT_PIXMAP,
                            NullClient, DixUnknownAccess);

    if (maskPtr) {
        winScaleXBitmapToWindows(iconSize, 1, maskPtr, mask);

        winScaleXBitmapToWindows(iconSize, effBPP, maskPtr, imageMask);

        /* Now we need to set all bits of the icon which are not masked */
        /* on to 0 because Color is really an XOR, not an OR function */
        dst = image;
        src = imageMask;

        for (i = 0; i < (stride * iconSize); i++)
            if ((*(src++)))
                *(dst++) = 0;
            else
                dst++;
    }

    ii.fIcon = TRUE;
    ii.xHotspot = 0;            /* ignored */
    ii.yHotspot = 0;            /* ignored */

    /* Create Win32 mask from pixmap shape */
    ii.hbmMask = CreateBitmap(iconSize, iconSize, planes, 1, mask);

    /* Create Win32 bitmap from pixmap */
    ii.hbmColor = CreateBitmap(iconSize, iconSize, planes, bpp, image);

    /* Merge Win32 mask and bitmap into icon */
    hIcon = CreateIconIndirect(&ii);

    /* Release Win32 mask and bitmap */
    DeleteObject(ii.hbmMask);
    DeleteObject(ii.hbmColor);

    /* Free X mask and bitmap */
    free(mask);
    free(image);
    free(imageMask);

    return hIcon;
}

/*
 * Change the Windows window icon 
 */

#ifdef XWIN_MULTIWINDOW
void
winUpdateIcon(Window id)
{
    WindowPtr pWin;
    HICON hIcon, hIconSmall = NULL, hIconOld;

    dixLookupResourceByType((pointer) &pWin, id, RT_WINDOW, NullClient,
                            DixUnknownAccess);
    if (pWin) {
        winWindowPriv(pWin);
        if (pWinPriv->hWnd) {
            hIcon = winOverrideIcon((unsigned long) pWin);
            if (!hIcon) {
                hIcon = winXIconToHICON(pWin, GetSystemMetrics(SM_CXICON));
                if (!hIcon) {
                    hIcon = g_hIconX;
                    hIconSmall = g_hSmallIconX;
                }
                else {
                    /* Leave undefined if not found */
                    hIconSmall =
                        winXIconToHICON(pWin, GetSystemMetrics(SM_CXSMICON));
                }
            }

            /* Set the large icon */
            hIconOld = (HICON) SendMessage(pWinPriv->hWnd,
                                           WM_SETICON, ICON_BIG,
                                           (LPARAM) hIcon);

            /* Delete the icon if its not the default */
            winDestroyIcon(hIconOld);

            /* Same for the small icon */
            hIconOld = (HICON) SendMessage(pWinPriv->hWnd,
                                           WM_SETICON, ICON_SMALL,
                                           (LPARAM) hIconSmall);
            winDestroyIcon(hIconOld);
        }
    }
}

void
winInitGlobalIcons(void)
{
    int sm_cx = GetSystemMetrics(SM_CXICON);
    int sm_cxsm = GetSystemMetrics(SM_CXSMICON);

    /* Load default X icon in case it's not ready yet */
    if (!g_hIconX) {
        g_hIconX = winOverrideDefaultIcon(sm_cx);
        g_hSmallIconX = winOverrideDefaultIcon(sm_cxsm);
    }

    if (!g_hIconX) {
        g_hIconX = (HICON) LoadImage(g_hInstance,
                                     MAKEINTRESOURCE(IDI_XWIN),
                                     IMAGE_ICON,
                                     GetSystemMetrics(SM_CXICON),
                                     GetSystemMetrics(SM_CYICON), 0);
        g_hSmallIconX = (HICON) LoadImage(g_hInstance,
                                          MAKEINTRESOURCE(IDI_XWIN),
                                          IMAGE_ICON,
                                          GetSystemMetrics(SM_CXSMICON),
                                          GetSystemMetrics(SM_CYSMICON),
                                          LR_DEFAULTSIZE);
    }
}

void
winSelectIcons(WindowPtr pWin, HICON * pIcon, HICON * pSmallIcon)
{
    HICON hIcon, hSmallIcon;

    winInitGlobalIcons();

    /* Try and get the icon from WM_HINTS */
    hIcon = winXIconToHICON(pWin, GetSystemMetrics(SM_CXICON));
    hSmallIcon = winXIconToHICON(pWin, GetSystemMetrics(SM_CXSMICON));

    /* If we got the small, but not the large one swap them */
    if (!hIcon && hSmallIcon) {
        hIcon = hSmallIcon;
        hSmallIcon = NULL;
    }

    /* Use default X icon if no icon loaded from WM_HINTS */
    if (!hIcon) {
        hIcon = g_hIconX;
        hSmallIcon = g_hSmallIconX;
    }

    if (pIcon)
        *pIcon = hIcon;
    else
        winDestroyIcon(hIcon);

    if (pSmallIcon)
        *pSmallIcon = hSmallIcon;
    else
        winDestroyIcon(hSmallIcon);
}

void
winDestroyIcon(HICON hIcon)
{
    /* Delete the icon if its not one of the application defaults or an override */
    if (hIcon &&
        hIcon != g_hIconX &&
        hIcon != g_hSmallIconX && !winIconIsOverride((unsigned long) hIcon))
        DestroyIcon(hIcon);
}
#endif
