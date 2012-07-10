
#ifndef __DGAPROC_H
#define __DGAPROC_H

#include <X11/Xproto.h>
#include "pixmap.h"

#define DGA_CONCURRENT_ACCESS	0x00000001
#define DGA_FILL_RECT		0x00000002
#define DGA_BLIT_RECT		0x00000004
#define DGA_BLIT_RECT_TRANS	0x00000008
#define DGA_PIXMAP_AVAILABLE	0x00000010

#define DGA_INTERLACED		0x00010000
#define DGA_DOUBLESCAN		0x00020000

#define DGA_FLIP_IMMEDIATE	0x00000001
#define DGA_FLIP_RETRACE	0x00000002

#define DGA_COMPLETED		0x00000000
#define DGA_PENDING		0x00000001

#define DGA_NEED_ROOT		0x00000001

typedef struct {
    int num;                    /* A unique identifier for the mode (num > 0) */
    char *name;                 /* name of mode given in the XF86Config */
    int VSync_num;
    int VSync_den;
    int flags;                  /* DGA_CONCURRENT_ACCESS, etc... */
    int imageWidth;             /* linear accessible portion (pixels) */
    int imageHeight;
    int pixmapWidth;            /* Xlib accessible portion (pixels) */
    int pixmapHeight;           /* both fields ignored if no concurrent access */
    int bytesPerScanline;
    int byteOrder;              /* MSBFirst, LSBFirst */
    int depth;
    int bitsPerPixel;
    unsigned long red_mask;
    unsigned long green_mask;
    unsigned long blue_mask;
    short visualClass;
    int viewportWidth;
    int viewportHeight;
    int xViewportStep;          /* viewport position granularity */
    int yViewportStep;
    int maxViewportX;           /* max viewport origin */
    int maxViewportY;
    int viewportFlags;          /* types of page flipping possible */
    int offset;
    int reserved1;
    int reserved2;
} XDGAModeRec, *XDGAModePtr;

/* DDX interface */

extern _X_EXPORT int
 DGASetMode(int Index, int num, XDGAModePtr mode, PixmapPtr *pPix);

extern _X_EXPORT void
 DGASetInputMode(int Index, Bool keyboard, Bool mouse);

extern _X_EXPORT void
 DGASelectInput(int Index, ClientPtr client, long mask);

extern _X_EXPORT Bool DGAAvailable(int Index);
extern _X_EXPORT Bool DGAScreenAvailable(ScreenPtr pScreen);
extern _X_EXPORT Bool DGAActive(int Index);
extern _X_EXPORT void DGAShutdown(void);
extern _X_EXPORT void DGAInstallCmap(ColormapPtr cmap);
extern _X_EXPORT int DGAGetViewportStatus(int Index);
extern _X_EXPORT int DGASync(int Index);

extern _X_EXPORT int
 DGAFillRect(int Index, int x, int y, int w, int h, unsigned long color);

extern _X_EXPORT int
 DGABlitRect(int Index, int srcx, int srcy, int w, int h, int dstx, int dsty);

extern _X_EXPORT int

DGABlitTransRect(int Index,
                 int srcx, int srcy,
                 int w, int h, int dstx, int dsty, unsigned long color);

extern _X_EXPORT int
 DGASetViewport(int Index, int x, int y, int mode);

extern _X_EXPORT int DGAGetModes(int Index);
extern _X_EXPORT int DGAGetOldDGAMode(int Index);

extern _X_EXPORT int DGAGetModeInfo(int Index, XDGAModePtr mode, int num);

extern _X_EXPORT Bool DGAVTSwitch(void);
extern _X_EXPORT Bool DGAStealButtonEvent(DeviceIntPtr dev, int Index,
                                          int button, int is_down);
extern _X_EXPORT Bool DGAStealMotionEvent(DeviceIntPtr dev, int Index, int dx,
                                          int dy);
extern _X_EXPORT Bool DGAStealKeyEvent(DeviceIntPtr dev, int Index,
                                       int key_code, int is_down);

extern _X_EXPORT Bool DGAOpenFramebuffer(int Index, char **name,
                                         unsigned char **mem, int *size,
                                         int *offset, int *flags);
extern _X_EXPORT void DGACloseFramebuffer(int Index);
extern _X_EXPORT Bool DGAChangePixmapMode(int Index, int *x, int *y, int mode);
extern _X_EXPORT int DGACreateColormap(int Index, ClientPtr client, int id,
                                       int mode, int alloc);

#endif                          /* __DGAPROC_H */
